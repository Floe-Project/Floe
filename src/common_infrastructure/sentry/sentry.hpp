// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
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

struct Sentry {
    struct Session {
        enum class Status : u32 {
            Ok,

            // session end statuses
            Exited,
            Crashed,
            Abnormal,
        };
        String StatusString() const {
            switch (status) {
                case Status::Ok: return "ok"_s;
                case Status::Exited: return "exited"_s;
                case Status::Crashed: return "crashed"_s;
                case Status::Abnormal: return "abnormal"_s;
            }
            PanicIfReached();
        }

        Status status;
    };

    struct Event {
        struct Tag {
            String key;
            String value;
        };
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

    static constexpr usize k_max_message_length = 8192;
    static constexpr String k_release = "floe@" FLOE_VERSION_STRING;
    static constexpr String k_environment = PRODUCTION_BUILD ? "production"_s : "development"_s;
    SentryDsn dsn;
    Optional<fmt::UuidArray> device_id;
    fmt::UuidArray session_id;
    u32 session_sequence = 0;
    u32 session_num_errors = 0;
    bool session_ended = false;
    fmt::TimestampRfc3339UtcArray session_started_at {};
    u64 seed = RandomSeed();
    DynamicArray<char> event_context_json {PageAllocator::Instance()};
};

namespace detail {

// We only support the format: https://<public_key>@<host>/<project_id>
static Optional<SentryDsn> ParseSentryDsn(String dsn) {
    auto read_until = [](String& s, char c) {
        auto const i = Find(s, c);
        if (!i) return String {};
        auto const result = s.SubSpan(0, *i);
        s.RemovePrefix(*i + 1);
        return result;
    };

    SentryDsn result {.dsn = dsn};

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

static Optional<fmt::UuidArray> DeviceId(u64& seed) {
    PathArena path_arena {PageAllocator::Instance()};
    auto const dir = KnownDirectory(path_arena, KnownDirectoryType::UserData, {.create = true});
    auto const path = path::JoinAppendResizeAllocation(path_arena, dir, Array {"device_id"_s});

    auto file = TRY_OR(OpenFile(path, FileMode::ReadWrite), {
        g_log.Error(k_main_log_module, "Failed to create device_id file: {}, {}", path, error);
        return k_nullopt;
    });

    TRY_OR(file.Lock(FileLockType::Exclusive), {
        g_log.Error(k_main_log_module, "Failed to lock device_id file: {}, {}", path, error);
        return k_nullopt;
    });
    DEFER { auto _ = file.Unlock(); };

    auto const size = TRY_OR(file.FileSize(), {
        g_log.Error(k_main_log_module, "Failed to get size of device_id file: {}, {}", path, error);
        return k_nullopt;
    });

    if (size == fmt::k_uuid_size) {
        fmt::UuidArray uuid {};
        TRY_OR(file.Read(uuid.data, uuid.size), {
            g_log.Error(k_main_log_module, "Failed to read device_id file: {}, {}", path, error);
            return k_nullopt;
        });

        bool valid = true;
        for (auto c : uuid) {
            if (!IsHexDigit(c)) {
                valid = false;
                break;
            }
        }
        if (valid) return uuid;
    }

    // File is invalid or empty, let's recreate it
    auto const uuid = fmt::Uuid(seed);

    TRY_OR(file.Seek(0, File::SeekOrigin::Start),
           { g_log.Error(k_main_log_module, "Failed to seek device_id file: {}, {}", path, error); });

    TRY_OR(file.Truncate(0),
           { g_log.Error(k_main_log_module, "Failed to truncate device_id file: {}, {}", path, error); });

    TRY_OR(file.Write(uuid),
           { g_log.Error(k_main_log_module, "Failed to write device_id file: {}, {}", path, error); });

    auto _ = file.Flush();

    return uuid;
}

} // namespace detail

PUBLIC bool InitSentry(Sentry& sentry, String dsn) {
    auto parsed_dsn = TRY_OPT_OR(detail::ParseSentryDsn(dsn), {
        g_log.Error(k_main_log_module, "Failed to parse Sentry DSN: {}", dsn);
        return false;
    });
    sentry.dsn = parsed_dsn;
    sentry.device_id = detail::DeviceId(sentry.seed);
    sentry.session_id = fmt::Uuid(sentry.seed);

    // this data is common to every event we want to send, so let's cache it as a JSON blob
    {
        json::WriteContext json_writer {
            .out = dyn::WriterFor(sentry.event_context_json),
            .add_whitespace = false,
        };

        auto _ = json::WriteObjectBegin(json_writer);

        // user
        if (sentry.device_id) {
            auto _ = json::WriteKeyObjectBegin(json_writer, "user");
            auto _ = json::WriteKeyValue(json_writer, "id", *sentry.device_id);
            auto _ = json::WriteObjectEnd(json_writer);
        }

        // contexts
        {
            auto _ = json::WriteKeyObjectBegin(json_writer, "contexts");

            // device
            {
                auto const system = GetSystemStats();
                auto _ = json::WriteKeyObjectBegin(json_writer, "device");
                auto _ = json::WriteKeyValue(json_writer, "name", "desktop");
                auto _ = json::WriteKeyValue(json_writer, "arch", system.Arch());
                auto _ = json::WriteKeyValue(json_writer, "cpu_description", system.cpu_name);
                auto _ = json::WriteKeyValue(json_writer, "processor_count", system.num_logical_cpus);
                auto _ = json::WriteKeyValue(json_writer, "processor_frequency", system.frequency_mhz);
                auto _ = json::WriteObjectEnd(json_writer);
            }

            // os
            {
                auto const os = GetOsInfo();
                auto _ = json::WriteKeyObjectBegin(json_writer, "os");
                auto _ = json::WriteKeyValue(json_writer, "name", os.name);
                if (os.version.size) auto _ = json::WriteKeyValue(json_writer, "version", os.version);
                if (os.build.size) auto _ = json::WriteKeyValue(json_writer, "build", os.build);
                if (os.kernel_version.size)
                    auto _ = json::WriteKeyValue(json_writer, "kernel_version", os.kernel_version);
                if (os.pretty_name.size)
                    auto _ = json::WriteKeyValue(json_writer, "pretty_name", os.pretty_name);
                if (os.distribution_name.size)
                    auto _ = json::WriteKeyValue(json_writer, "distribution_name", os.distribution_name);
                if (os.distribution_version.size)
                    auto _ =
                        json::WriteKeyValue(json_writer, "distribution_version", os.distribution_version);
                if (os.distribution_pretty_name.size)
                    auto _ = json::WriteKeyValue(json_writer,
                                                 "distribution_pretty_name",
                                                 os.distribution_pretty_name);
                auto _ = json::WriteObjectEnd(json_writer);
            }

            auto _ = json::WriteObjectEnd(json_writer);
        }
        auto _ = json::WriteObjectEnd(json_writer);

        // we want to be able to print the context directly into an existing JSON event so we remove the
        // object braces
        dyn::Pop(sentry.event_context_json);
        dyn::Remove(sentry.event_context_json, 0);
    }

    return true;
}

PUBLIC ErrorCodeOr<void> SendSentryEnvelope(Sentry& sentry,
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

PUBLIC ErrorCodeOr<void> EnvelopeAddHeader(Sentry& sentry, Writer writer) {
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

[[maybe_unused]] PUBLIC ErrorCodeOr<void>
EnvelopeAddSessionUpdate(Sentry& sentry, Writer writer, Sentry::Session session) {
    switch (session.status) {
        case Sentry::Session::Status::Exited:
        case Sentry::Session::Status::Crashed:
        case Sentry::Session::Status::Abnormal:
            ASSERT(!sentry.session_ended);
            sentry.session_ended = true;
            break;
        case Sentry::Session::Status::Ok: break;
    }

    json::WriteContext json_writer {
        .out = writer,
        .add_whitespace = false,
    };
    auto const timestamp = TimestampRfc3339UtcNow();
    auto const init = sentry.session_sequence == 0;
    if (init) sentry.session_started_at = timestamp;

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
    TRY(json::WriteKeyValue(json_writer, "status", session.StatusString()));
    if (sentry.device_id) TRY(json::WriteKeyValue(json_writer, "did", *sentry.device_id));
    TRY(json::WriteKeyValue(json_writer, "init", init));
    TRY(json::WriteKeyValue(json_writer, "seq", sentry.session_sequence++));
    TRY(json::WriteKeyValue(json_writer, "timestamp", timestamp));
    TRY(json::WriteKeyValue(json_writer, "started", sentry.session_started_at));
    TRY(json::WriteKeyValue(json_writer, "errors", sentry.session_num_errors));
    // attrs
    {
        TRY(json::WriteKeyObjectBegin(json_writer, "attrs"));
        TRY(json::WriteKeyValue(json_writer, "release", sentry.k_release));
        TRY(json::WriteKeyValue(json_writer, "environment", sentry.k_environment));
        TRY(json::WriteKeyValue(json_writer, "user_agent", "floe/" FLOE_VERSION_STRING));
        TRY(json::WriteObjectEnd(json_writer));
    }
    TRY(json::WriteObjectEnd(json_writer));
    TRY(writer.WriteChar('\n'));

    return k_success;
}

[[maybe_unused]] PUBLIC ErrorCodeOr<void>
EnvelopeAddEvent(Sentry& sentry, Writer& writer, Sentry::Event event) {
    switch (event.level) {
        case Sentry::Event::Level::Fatal:
        case Sentry::Event::Level::Error: sentry.session_num_errors++; break;
        case Sentry::Event::Level::Warning:
        case Sentry::Event::Level::Info:
        case Sentry::Event::Level::Debug: break;
    }

    json::WriteContext json_writer {
        .out = writer,
        .add_whitespace = false,
    };
    auto const timestamp = TimestampRfc3339UtcNow();
    auto const event_id = fmt::Uuid(sentry.seed);

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
    if (event.tags.size) {
        TRY(json::WriteKeyObjectBegin(json_writer, "tags"));
        for (auto const& tag : event.tags) {
            if (!tag.key.size) continue;
            if (!tag.value.size) continue;
            if (tag.key.size >= 200) continue;
            if (tag.value.size >= 200) continue;
            TRY(json::WriteKeyValue(json_writer, tag.key, tag.value));
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

    TRY(writer.WriteChar(','));
    TRY(writer.WriteChars(sentry.event_context_json));

    TRY(json::WriteObjectEnd(json_writer));
    TRY(writer.WriteChar('\n'));

    return k_success;
}

} // namespace sentry
