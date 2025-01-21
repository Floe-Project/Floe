// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/web.hpp"
#include "utils/debug/debug.hpp"
#include "utils/json/json_writer.hpp"
#include "utils/logger/logger.hpp"

namespace sentry {

struct SentryDsn {
    String dsn;
    String host;
    String project_id;
    String public_key;
};

struct Tag {
    Tag Clone(ArenaAllocator& arena) const {
        return Tag {.key = key.Clone(arena), .value = value.Clone(arena)};
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

struct Sentry {
    static constexpr usize k_max_message_length = 8192;
    static constexpr String k_release = "floe@" FLOE_VERSION_STRING;
    static constexpr String k_environment = PRODUCTION_BUILD ? "production"_s : "development"_s;
    SentryDsn dsn {};
    Optional<fmt::UuidArray> device_id {};
    fmt::UuidArray session_id {};
    Atomic<u32> session_num_errors = 0;
    Atomic<s64> session_started_microsecs {};
    Atomic<u64> seed = RandomSeed();
    ArenaAllocator arena {PageAllocator::Instance()};
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

// not thread-safe, not signal-safe, inits g_instance
Sentry* InitGlobalSentry(String dsn, Span<Tag const> tag);

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
        TRY(json::WriteKeyValue(json_writer, "release", sentry.k_release));
        TRY(json::WriteKeyValue(json_writer, "environment", sentry.k_environment));
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
[[maybe_unused]] PUBLIC ErrorCodeOr<void> EnvelopeAddEvent(Sentry& sentry, Writer& writer, ErrorEvent event) {
    ASSERT(event.message.size <= Sentry::k_max_message_length, "message too long");
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
    TRY(json::WriteKeyValue(json_writer, "release", sentry.k_release));
    TRY(json::WriteKeyValue(json_writer, "environment", sentry.k_environment));

    // tags
    if (event.tags.size || sentry.tags.size) {
        TRY(json::WriteKeyObjectBegin(json_writer, "tags"));
        for (auto tags : Array {event.tags, sentry.tags}) {
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
        TRY(json::WriteKeyValue(json_writer,
                                "formatted",
                                event.message.SubSpan(0, sentry.k_max_message_length)));
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

} // namespace sentry
