// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/web.hpp"
#include "utils/debug/debug.hpp"
#include "utils/json/json_writer.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/final_binary_type.hpp"

namespace sentry {

struct Tag {
    Tag Clone(Allocator& arena) const {
        return Tag {
            .key = key.Clone(arena),
            .value = value.Clone(arena),
        };
    }

    String key;
    String value;
};

struct ErrorEvent {
    // NOTE: in Sentry, all events are 'errors' regardless of their level
    enum class Level {
        Fatal,
        Error,
        Warning,
        Info,
        Debug,
    };
    String LevelString() const {
        switch (level) {
            case Level::Fatal: return "fatal"_s;
            case Level::Error: return "error"_s;
            case Level::Warning: return "warning"_s;
            case Level::Info: return "info"_s;
            case Level::Debug: return "debug"_s;
        }
        PanicIfReached();
    }

    Level level;
    String message;
    Optional<StacktraceStack> stacktrace;
    Span<Tag const> tags;
};

struct DsnInfo {
    String dsn;
    String host;
    String project_id;
    String public_key;
};

static constexpr usize k_max_message_length = 8192;
static constexpr String k_release = "floe@" FLOE_VERSION_STRING;
static constexpr String k_environment = PRODUCTION_BUILD ? "production"_s : "development"_s;

struct Sentry {
    Optional<fmt::UuidArray> device_id {};
    DsnInfo dsn {};
    fmt::UuidArray session_id {};
    Atomic<u32> session_num_errors = 0;
    Atomic<s64> session_started_microsecs {};
    Atomic<u64> seed = {};
    FixedSizeAllocator<Kb(4)> arena {nullptr};
    Span<char> event_context_json {};
    Span<Tag> tags {};
};

// never destroyed, once set (InitGlobalSentry) it's good for the lifetime
extern Atomic<Sentry*> g_instance;

namespace detail {

static fmt::UuidArray Uuid(Atomic<u64>& seed) {
    u64 s = seed.Load(LoadMemoryOrder::Relaxed);
    auto result = fmt::Uuid(s);
    seed.Store(s, StoreMemoryOrder::Relaxed);
    return result;
}

} // namespace detail

// We only support the format: https://<public_key>@<host>/<project_id>
constexpr Optional<DsnInfo> ParseDsn(String dsn) {
    auto read_until = [](String& s, char c) {
        auto const i = Find(s, c);
        if (!i) return String {};
        auto const result = s.SubSpan(0, *i);
        s.RemovePrefix(*i + 1);
        return result;
    };

    DsnInfo result {.dsn = dsn};

    // Skip https://
    if (!StartsWithSpan(dsn, "https://"_s)) return k_nullopt;
    if (dsn.size < 8) return k_nullopt;
    dsn.RemovePrefix(8);

    // Get public key (everything before @)
    auto key = read_until(dsn, '@');
    if (key.size == 0) return k_nullopt;
    result.public_key = key;

    // Get host (everything before last /)
    auto last_slash = Find(dsn, '/');
    if (!last_slash) return k_nullopt;

    result.host = dsn.SubSpan(0, *last_slash);
    dsn.RemovePrefix(*last_slash + 1);

    // Remaining part is project_id
    if (dsn.size == 0) return k_nullopt;
    result.project_id = dsn;

    return result;
}

consteval DsnInfo ParseDsnOrThrow(String dsn) {
    auto o = ParseDsn(dsn);
    if (!o) throw "invalid DSN";
    return *o;
}

// not thread-safe, not signal-safe, inits g_instance
// adds device_id, OS info, CPU info
// dsn must be valid and static
Sentry* InitGlobalSentry(DsnInfo dsn, Span<Tag const> tag);

// threadsafe, signal-safe, doesn't include useful context info
// dsn must be valid and static
void InitBarebonesSentry(Sentry& sentry, DsnInfo dsn, Span<Tag const> tags);

// threadsafe, not signal-safe
PUBLIC ErrorCodeOr<void> SendSentryEnvelope(Sentry const& sentry,
                                            String envelope,
                                            Optional<Writer> response,
                                            ArenaAllocator& scratch_arena) {
    auto const envelope_url =
        fmt::Format(scratch_arena, "https://{}:443/api/{}/envelope/", sentry.dsn.host, sentry.dsn.project_id);

    g_log.Debug(k_main_log_module, "Posting to Sentry: {}", envelope);

    return HttpsPost(
        envelope_url,
        envelope,
        Array {
            "Content-Type: application/x-sentry-envelope"_s,
            fmt::Format(scratch_arena,
                        "X-Sentry-Auth: Sentry sentry_version=7, sentry_client=floe/{}, sentry_key={}",
                        FLOE_VERSION_STRING,
                        sentry.dsn.public_key),
            fmt::Format(scratch_arena, "Content-Length: {}", envelope.size),
            fmt::Format(scratch_arena,
                        "User-Agent: floe/{} ({})"_s,
                        FLOE_VERSION_STRING,
                        IS_WINDOWS ? "Windows"
                        : IS_LINUX ? "Linux"
                                   : "macOS"),
        },
        response);
}

// thread-safe, signal-safe
PUBLIC ErrorCodeOr<void> EnvelopeAddHeader(Sentry const& sentry, Writer writer) {
    json::WriteContext json_writer {
        .out = writer,
        .add_whitespace = false,
    };

    TRY(json::WriteObjectBegin(json_writer));
    TRY(json::WriteKeyValue(json_writer, "dsn", sentry.dsn.dsn));
    TRY(json::WriteKeyValue(json_writer, "sent_at", TimestampRfc3339UtcNow()));
    TRY(json::WriteObjectEnd(json_writer));
    TRY(writer.WriteChar('\n'));

    return k_success;
}

enum class SessionUpdateType { Start, EndedNormally, EndedCrashed };

// thread-safe, signal-safe
[[maybe_unused]] PUBLIC ErrorCodeOr<void> EnvelopeAddSessionUpdate(Sentry& sentry,
                                                                   Writer writer,
                                                                   SessionUpdateType session_update_type,
                                                                   Optional<u32> extra_num_errors = {}) {
    auto const now = MicrosecondsSinceEpoch();
    auto const timestamp = fmt::TimestampRfc3339Utc(UtcTimeFromMicrosecondsSinceEpoch(now));
    auto const init = session_update_type == SessionUpdateType::Start;

    switch (session_update_type) {
        case SessionUpdateType::Start:
            ASSERT(sentry.session_started_microsecs.Load(LoadMemoryOrder::Relaxed) == 0,
                   "session already started");
            sentry.session_id = detail::Uuid(sentry.seed);
            sentry.session_started_microsecs.Store(now, StoreMemoryOrder::Release);
            break;
        case SessionUpdateType::EndedNormally:
        case SessionUpdateType::EndedCrashed:
            ASSERT(sentry.session_started_microsecs.Load(LoadMemoryOrder::Relaxed) > 0,
                   "missing session start");
    }

    auto const num_errors = ({
        auto e = sentry.session_num_errors.Load(LoadMemoryOrder::Relaxed);
        if (extra_num_errors) e += *extra_num_errors;
        e;
    });

    json::WriteContext json_writer {
        .out = writer,
        .add_whitespace = false,
    };

    // Item header (session)
    json::ResetWriter(json_writer);
    TRY(json::WriteObjectBegin(json_writer));
    TRY(json::WriteKeyValue(json_writer, "type", "session"));
    TRY(json::WriteObjectEnd(json_writer));
    TRY(writer.WriteChar('\n'));

    // Item payload (session)
    json::ResetWriter(json_writer);
    TRY(json::WriteObjectBegin(json_writer));
    TRY(json::WriteKeyValue(json_writer, "sid", sentry.session_id));
    TRY(json::WriteKeyValue(json_writer, "status", ({
                                String s;
                                switch (session_update_type) {
                                    case SessionUpdateType::Start: s = "ok"; break;
                                    case SessionUpdateType::EndedNormally: s = "exited"; break;
                                    case SessionUpdateType::EndedCrashed: s = "crashed"; break;
                                }
                                s;
                            })));
    if (sentry.device_id) TRY(json::WriteKeyValue(json_writer, "did", *sentry.device_id));
    TRY(json::WriteKeyValue(json_writer, "init", init));
    TRY(json::WriteKeyValue(json_writer, "seq", init ? 0 : 1));
    TRY(json::WriteKeyValue(json_writer, "timestamp", timestamp));
    TRY(json::WriteKeyValue(json_writer, "started", ({
                                fmt::TimestampRfc3339UtcArray s;
                                if (init) {
                                    s = timestamp;
                                } else {
                                    s = fmt::TimestampRfc3339Utc(UtcTimeFromMicrosecondsSinceEpoch(
                                        sentry.session_started_microsecs.Load(LoadMemoryOrder::Acquire)));
                                }
                                s;
                            })));
    TRY(json::WriteKeyValue(json_writer, "errors", num_errors));
    {
        TRY(json::WriteKeyObjectBegin(json_writer, "attrs"));
        TRY(json::WriteKeyValue(json_writer, "release", k_release));
        TRY(json::WriteKeyValue(json_writer, "environment", k_environment));
        TRY(json::WriteKeyValue(json_writer, "user_agent", "floe/" FLOE_VERSION_STRING));
        TRY(json::WriteObjectEnd(json_writer));
    }
    TRY(json::WriteObjectEnd(json_writer));
    TRY(writer.WriteChar('\n'));

    if (session_update_type != SessionUpdateType::Start) {
        sentry.session_num_errors.Store(0, StoreMemoryOrder::Relaxed);
        sentry.session_started_microsecs.Store(0, StoreMemoryOrder::Relaxed);
    }

    return k_success;
}

// thread-safe, signal-safe
// NOTE (Jan 2025): all events are 'errors' in Sentry, there's no plain logging concept
[[maybe_unused]] PUBLIC ErrorCodeOr<void> EnvelopeAddEvent(Sentry& sentry, Writer writer, ErrorEvent event) {
    ASSERT(event.message.size <= k_max_message_length, "message too long");
    ASSERT(event.tags.size < 100, "too many tags");

    sentry.session_num_errors.FetchAdd(1, RmwMemoryOrder::Relaxed);

    json::WriteContext json_writer {
        .out = writer,
        .add_whitespace = false,
    };
    auto const timestamp = TimestampRfc3339UtcNow();
    auto const event_id = detail::Uuid(sentry.seed);

    // Item header (event)
    json::ResetWriter(json_writer);
    TRY(json::WriteObjectBegin(json_writer));
    TRY(json::WriteKeyValue(json_writer, "type", "event"));
    TRY(json::WriteKeyValue(json_writer, "event_id", event_id));
    TRY(json::WriteObjectEnd(json_writer));
    TRY(writer.WriteChar('\n'));

    // Item payload (event)
    json::ResetWriter(json_writer);
    TRY(json::WriteObjectBegin(json_writer));
    TRY(json::WriteKeyValue(json_writer, "event_id", event_id));
    TRY(json::WriteKeyValue(json_writer, "timestamp", timestamp));
    TRY(json::WriteKeyValue(json_writer, "platform", "native"));
    TRY(json::WriteKeyValue(json_writer, "level", event.LevelString()));
    TRY(json::WriteKeyValue(json_writer, "release", k_release));
    TRY(json::WriteKeyValue(json_writer, "environment", k_environment));

    // tags
    if (event.tags.size || sentry.tags.size) {
        TRY(json::WriteKeyObjectBegin(json_writer, "tags"));
        for (auto tags :
             Array {event.tags, sentry.tags, Array {Tag {"app_type", ToString(g_final_binary_type)}}}) {
            for (auto const& tag : tags) {
                if (!tag.key.size) continue;
                if (!tag.value.size) continue;
                if (tag.key.size >= 200) continue;
                if (tag.value.size >= 200) continue;
                TRY(json::WriteKeyValue(json_writer, tag.key, tag.value));
            }
        }
        TRY(json::WriteObjectEnd(json_writer));
    }

    // message
    {
        TRY(json::WriteKeyObjectBegin(json_writer, "message"));
        TRY(json::WriteKeyValue(json_writer, "formatted", event.message.SubSpan(0, k_max_message_length)));
        TRY(json::WriteObjectEnd(json_writer));
    }

    // stacktrace
    if (event.stacktrace && event.stacktrace->size) {
        TRY(json::WriteKeyObjectBegin(json_writer, "stacktrace"));
        TRY(json::WriteKeyArrayBegin(json_writer, "frames"));
        ErrorCodeOr<void> stacktrace_error = k_success;
        StacktraceToCallback(*event.stacktrace, [&](FrameInfo const& frame) {
            auto try_write = [&]() -> ErrorCodeOr<void> {
                TRY(json::WriteObjectBegin(json_writer));

                auto filename = TrimStartIfMatches(frame.filename, String {FLOE_PROJECT_ROOT_PATH});
                if (filename.size) {
                    TRY(json::WriteKeyValue(json_writer, "filename", filename));
                    TRY(json::WriteKeyValue(json_writer, "in_app", true));
                    TRY(json::WriteKeyValue(json_writer, "lineno", frame.line));
                }

                if (frame.function_name.size)
                    TRY(json::WriteKeyValue(json_writer, "function", frame.function_name));

                TRY(json::WriteObjectEnd(json_writer));
                return k_success;
            };
            if (!stacktrace_error.HasError()) stacktrace_error = try_write();
        });
        TRY(stacktrace_error);
        TRY(json::WriteArrayEnd(json_writer));
        TRY(json::WriteObjectEnd(json_writer));
    }

    // insert the common context
    {
        TRY(writer.WriteChar(','));
        TRY(writer.WriteChars(sentry.event_context_json));
    }

    TRY(json::WriteObjectEnd(json_writer));
    TRY(writer.WriteChar('\n'));

    return k_success;
}

constexpr auto k_crash_file_extension = ".floe-crash"_ca;

// thread-safe, signal-safe
PUBLIC ErrorCodeOr<void> WriteCrashToFile(Sentry& sentry,
                                          bool new_session,
                                          String folder,
                                          Optional<StacktraceStack> const& stacktrace,
                                          String message,
                                          ArenaAllocator& scratch_arena) {
    auto const timestamp = TimestampUtc();
    auto const filename = fmt::Format(scratch_arena, "crash_{}.{}", timestamp, k_crash_file_extension);
    auto path = path::Join(scratch_arena, Array {folder, filename});

    auto file = TRY(OpenFile(path, FileMode::Write));

    if (new_session) TRY(EnvelopeAddSessionUpdate(sentry, file.Writer(), SessionUpdateType::Start));
    TRY(EnvelopeAddEvent(sentry,
                         file.Writer(),
                         {
                             .level = ErrorEvent::Level::Error,
                             .message = message,
                             .stacktrace = stacktrace,
                         }));
    TRY(EnvelopeAddSessionUpdate(sentry, file.Writer(), SessionUpdateType::EndedCrashed));

    return k_success;
}

// PUBLIC ErrorCodeOr<void> ConsumeCrashFiles(String folder, ArenaAllocator& scratch_arena) {
//     constexpr auto k_wildcard = ConcatArrays("*"_ca, k_crash_file_extension);
//     auto it = TRY(dir_iterator::Create(scratch_arena, folder, {.wildcard = k_wildcard}));
//
// }

} // namespace sentry
