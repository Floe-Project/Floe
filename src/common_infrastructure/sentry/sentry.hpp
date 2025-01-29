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

#include "sentry_config.hpp"

namespace sentry {

constexpr auto k_log_module = "sentry"_log_module;

struct Tag {
    Tag Clone(Allocator& arena, CloneType) const {
        return Tag {
            .key = key.Clone(arena),
            .value = value.Clone(arena),
        };
    }

    String key;
    String value;
};

static_assert(Cloneable<Tag>);

struct ErrorEvent {
    // NOTE: in Sentry, all events are 'issues' regardless of their level
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

struct Error : ErrorEvent {
    ArenaAllocator arena {Malloc::Instance()};
};

struct DsnInfo {
    String dsn;
    String host;
    String project_id;
    String public_key;
};

struct Sentry {
    Optional<fmt::UuidArray> device_id {};
    DsnInfo dsn {};
    fmt::UuidArray session_id {};
    Atomic<u32> session_num_errors = 0;
    Atomic<s64> session_started_microsecs {};
    Atomic<u32> session_sequence = 0;
    Atomic<u64> seed = {};
    Atomic<bool> session_ended = false;
    FixedSizeAllocator<Kb(4)> arena {nullptr};
    Span<char> event_context_json {};
    Span<Tag> tags {};
};

namespace detail {

// NOTE: in Sentry, releases are created when an Event payload is sent with a release tag for the
// first time. We use an unchanging release tag for dev builds.
static constexpr String k_release = PRODUCTION_BUILD ? String {"floe@" FLOE_VERSION_STRING} : "floe@dev";
static constexpr usize k_max_message_length = 8192;
static constexpr String k_environment = PRODUCTION_BUILD ? "production"_s : "development"_s;
static constexpr String k_user_agent = PRODUCTION_BUILD ? String {"floe/" FLOE_VERSION_STRING} : "floe/dev";

constexpr auto k_error_file_extension = "floe-error"_ca;

static fmt::UuidArray Uuid(Atomic<u64>& seed) {
    u64 s = seed.FetchAdd(1, RmwMemoryOrder::Relaxed);
    auto result = fmt::Uuid(s);
    seed.Store(s, StoreMemoryOrder::Relaxed);
    return result;
}

static String UniqueErrorFilepath(String folder, Atomic<u64>& seed, Allocator& allocator) {
    auto s = seed.FetchAdd(1, RmwMemoryOrder::Relaxed);
    auto const filename = UniqueFilename("", ConcatArrays("."_ca, k_error_file_extension), s);
    seed.Store(s, StoreMemoryOrder::Relaxed);
    return path::Join(allocator, Array {folder, filename});
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
Sentry& InitGlobalSentry(DsnInfo dsn, Span<Tag const> tag);

// thread-safe, signal-safe, guaranteed to be valid if InitGlobalSentry has been called
Sentry* GlobalSentry();

// threadsafe, signal-safe, works just as well but doesn't include useful context info
void InitBarebonesSentry(Sentry& sentry);

// threadsafe, signal-safe
struct SentryOrFallback {
    SentryOrFallback() {
        sentry = GlobalSentry();
        if (!sentry) {
            // If the global version hasn't been initialized, we can still use a local version but it won't
            // have as much rich context associated with it.
            InitBarebonesSentry(fallback);
            sentry = &fallback;
        }
    }
    operator Sentry*() { return sentry; }
    Sentry* operator->() { return sentry; }
    Sentry* sentry;
    Sentry fallback;
};

// thread-safe (for Sentry), signal-safe
PUBLIC ErrorCodeOr<void> EnvelopeAddHeader(Sentry& sentry, Writer writer, bool include_sent_at) {
    json::WriteContext json_writer {
        .out = writer,
        .add_whitespace = false,
    };

    auto const event_id = detail::Uuid(sentry.seed);

    TRY(json::WriteObjectBegin(json_writer));
    if constexpr (k_online_reporting) TRY(json::WriteKeyValue(json_writer, "dsn", sentry.dsn.dsn));
    if (include_sent_at) TRY(json::WriteKeyValue(json_writer, "sent_at", TimestampRfc3339UtcNow()));
    TRY(json::WriteKeyValue(json_writer, "event_id", event_id));
    TRY(json::WriteObjectEnd(json_writer));
    TRY(writer.WriteChar('\n'));

    return k_success;
}

enum class SessionStatus {
    Ok,
    EndedNormally,
    Crashed,
};

// thread-safe (for Sentry), signal-safe
// https://develop.sentry.dev/sdk/telemetry/sessions/
// "Sessions are updated from events sent in. The most recent event holds the entire session state."
// "A session does not have to be started in order to crash. Just reporting a crash is sufficient."
[[maybe_unused]] PUBLIC ErrorCodeOr<void> EnvelopeAddSessionUpdate(Sentry& sentry,
                                                                   Writer writer,
                                                                   SessionStatus status,
                                                                   Optional<u32> extra_num_errors = {}) {
    switch (status) {
        case SessionStatus::Ok: break;
        case SessionStatus::EndedNormally:
        case SessionStatus::Crashed:
            // "A session can exist in two states: in progress or terminated. A terminated session must not
            // receive further updates. exited, crashed and abnormal are all terminal states. When a session
            // reaches this state the client must not report any more session updates or start a new session."
            if (sentry.session_ended.Exchange(true, RmwMemoryOrder::AcquireRelease)) return k_success;
            break;
    }

    auto const now = MicrosecondsSinceEpoch();
    auto const timestamp = fmt::TimestampRfc3339Utc(UtcTimeFromMicrosecondsSinceEpoch(now));

    s64 expected = 0;
    auto const init = sentry.session_started_microsecs.CompareExchangeStrong(expected,
                                                                             now,
                                                                             RmwMemoryOrder::AcquireRelease,
                                                                             LoadMemoryOrder::Acquire);
    auto const started = ({
        fmt::TimestampRfc3339UtcArray s;
        if (init)
            s = timestamp;
        else
            s = fmt::TimestampRfc3339Utc(UtcTimeFromMicrosecondsSinceEpoch(expected));
        s;
    });

    auto const num_errors = ({
        auto e = sentry.session_num_errors.Load(LoadMemoryOrder::Acquire);
        if (extra_num_errors) e += *extra_num_errors;
        // "It's important that this counter is also incremented when a session goes to crashed. (eg: the
        // crash itself is always an error as well)."
        if (status == SessionStatus::Crashed) e += 1;
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
                                switch (status) {
                                    case SessionStatus::Ok: s = "ok"; break;
                                    case SessionStatus::EndedNormally: s = "exited"; break;
                                    case SessionStatus::Crashed: s = "crashed"; break;
                                }
                                s;
                            })));
    if (sentry.device_id) TRY(json::WriteKeyValue(json_writer, "did", *sentry.device_id));
    TRY(json::WriteKeyValue(json_writer,
                            "seq",
                            sentry.session_sequence.FetchAdd(1, RmwMemoryOrder::AcquireRelease)));
    TRY(json::WriteKeyValue(json_writer, "timestamp", timestamp));
    TRY(json::WriteKeyValue(json_writer, "started", started));
    TRY(json::WriteKeyValue(json_writer, "init", init));
    TRY(json::WriteKeyValue(json_writer, "errors", num_errors));
    {
        TRY(json::WriteKeyObjectBegin(json_writer, "attrs"));
        TRY(json::WriteKeyValue(json_writer, "release", detail::k_release));
        TRY(json::WriteKeyValue(json_writer, "environment", detail::k_environment));
        TRY(json::WriteKeyValue(json_writer, "user_agent", detail::k_user_agent));
        TRY(json::WriteObjectEnd(json_writer));
    }
    TRY(json::WriteObjectEnd(json_writer));
    TRY(writer.WriteChar('\n'));

    return k_success;
}

// thread-safe (for Sentry), signal-safe if signal_safe is true
// NOTE (Jan 2025): all events are 'issues' in Sentry, there's no pure informational concept
[[maybe_unused]] PUBLIC ErrorCodeOr<void>
EnvelopeAddEvent(Sentry& sentry, Writer writer, ErrorEvent event, bool signal_safe) {
    ASSERT(event.message.size <= detail::k_max_message_length, "message too long");
    ASSERT(event.tags.size < 100, "too many tags");

    switch (event.level) {
        case ErrorEvent::Level::Fatal:
        case ErrorEvent::Level::Error:
            sentry.session_num_errors.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
            break;
        case ErrorEvent::Level::Warning:
        case ErrorEvent::Level::Info:
        case ErrorEvent::Level::Debug: break;
    }

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
    TRY(json::WriteKeyValue(json_writer, "release", detail::k_release));
    TRY(json::WriteKeyValue(json_writer, "environment", detail::k_environment));

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
        TRY(json::WriteKeyValue(json_writer,
                                "formatted",
                                event.message.SubSpan(0, detail::k_max_message_length)));
        TRY(json::WriteObjectEnd(json_writer));
    }

    // stacktrace
    if (event.stacktrace && event.stacktrace->size) {
        TRY(json::WriteKeyObjectBegin(json_writer, "stacktrace"));
        TRY(json::WriteKeyArrayBegin(json_writer, "frames"));
        ErrorCodeOr<void> stacktrace_error = k_success;
        StacktraceToCallback(
            *event.stacktrace,
            [&](FrameInfo const& frame) {
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
            },
            {
                .ansi_colours = false,
                .demangle = !signal_safe,
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

struct SubmissionOptions {
    bool write_to_file_if_needed = true;
    Optional<Writer> response = k_nullopt;
    RequestOptions request_options {};
};

// threadsafe (for Sentry), not signal-safe
// Blocks until the submission is complete. If the submission fails, it will write the envelope to a file if
// write_to_file_if_needed is true.
PUBLIC ErrorCodeOr<void> SubmitEnvelope(Sentry& sentry,
                                        String envelope_without_header,
                                        ArenaAllocator& scratch_arena,
                                        SubmissionOptions options) {
    if (envelope_without_header.size == 0) return k_success;

    DynamicArray<char> envelope {scratch_arena};
    envelope.Reserve(envelope_without_header.size + 200);
    auto envelope_writer = dyn::WriterFor(envelope);

    auto _ = EnvelopeAddHeader(sentry, envelope_writer, true);
    auto const online_envelope_header_size = envelope.size;
    dyn::AppendSpan(envelope, envelope_without_header);

    bool sent_online_successfully = false;
    ErrorCodeOr<void> result = k_success;

    if constexpr (k_online_reporting) {
        LogDebug(k_main_log_module, "Posting to Sentry: {}", envelope);

        auto const envelope_url = fmt::Format(scratch_arena,
                                              "https://{}:443/api/{}/envelope/",
                                              sentry.dsn.host,
                                              sentry.dsn.project_id);

        auto const o = HttpsPost(
            envelope_url,
            envelope,
            Array {
                "Content-Type: application/x-sentry-envelope"_s,
                fmt::Format(scratch_arena,
                            "X-Sentry-Auth: Sentry sentry_version=7, sentry_client={}, sentry_key={}",
                            detail::k_user_agent,
                            sentry.dsn.public_key),
                fmt::Format(scratch_arena, "Content-Length: {}", envelope.size),
                fmt::Format(scratch_arena,
                            "User-Agent: {} ({})"_s,
                            detail::k_user_agent,
                            IS_WINDOWS ? "Windows"
                            : IS_LINUX ? "Linux"
                                       : "macOS"),
            },
            options.response,
            options.request_options);

        // If there's an error other than just the internet being down, we want to capture that too.
        if (options.write_to_file_if_needed && o.HasError() && o.Error() != WebError::NetworkError) {
            auto _ = EnvelopeAddEvent(
                sentry,
                envelope_writer,
                {
                    .level = ErrorEvent::Level::Error,
                    .message = fmt::Format(scratch_arena, "Failed to send to Sentry: {}", o.Error()),
                },
                false);
        }

        if (o.Succeeded()) sent_online_successfully = true;

        result = o;
    }

    if (!sent_online_successfully && options.write_to_file_if_needed) {
        InitLogFolderIfNeeded();
        auto file = TRY(OpenFile(detail::UniqueErrorFilepath(*LogFolder(), sentry.seed, scratch_arena),
                                 FileMode::WriteNoOverwrite()));
        TRY(EnvelopeAddHeader(sentry, file.Writer(), false));

        // We have just written a header appropriate for the file, so we make sure don't write the header
        // appropriate for online submission by using SubSpan.
        TRY(file.Write(envelope.Items().SubSpan(online_envelope_header_size)));

        return k_success;
    }

    return result;
}

// thread-safe, signal-safe
PUBLIC ErrorCodeOr<void> WriteCrashToFile(Sentry& sentry,
                                          Optional<StacktraceStack> const& stacktrace,
                                          String folder,
                                          String message,
                                          Allocator& scratch_allocator) {
    auto file = TRY(OpenFile(detail::UniqueErrorFilepath(folder, sentry.seed, scratch_allocator),
                             FileMode::WriteNoOverwrite()));

    TRY(EnvelopeAddHeader(sentry, file.Writer(), false));
    TRY(EnvelopeAddEvent(sentry,
                         file.Writer(),
                         {
                             .level = ErrorEvent::Level::Fatal,
                             .message = message,
                             .stacktrace = stacktrace,
                         },
                         true));
    if constexpr (k_online_reporting)
        TRY(EnvelopeAddSessionUpdate(sentry, file.Writer(), SessionStatus::Crashed));

    return k_success;
}

// thread-safe, not signal-safe
PUBLIC ErrorCodeOr<void> SubmitCrash(Sentry& sentry,
                                     Optional<StacktraceStack> const& stacktrace,
                                     String message,
                                     ArenaAllocator& scratch_arena,
                                     SubmissionOptions options) {
    DynamicArray<char> envelope_without_header {scratch_arena};
    auto writer = dyn::WriterFor(envelope_without_header);

    TRY(EnvelopeAddEvent(sentry,
                         writer,
                         {
                             .level = ErrorEvent::Level::Fatal,
                             .message = message,
                             .stacktrace = stacktrace,
                         },
                         true));
    if constexpr (k_online_reporting) TRY(EnvelopeAddSessionUpdate(sentry, writer, SessionStatus::Crashed));
    TRY(SubmitEnvelope(sentry, envelope_without_header, scratch_arena, options));

    return k_success;
}

// thread-safe, not signal-safe
PUBLIC ErrorCodeOr<void> WriteErrorToFile(Sentry& sentry, ErrorEvent const& event) {
    PathArena path_arena {PageAllocator::Instance()};
    InitLogFolderIfNeeded();
    auto file = TRY(OpenFile(detail::UniqueErrorFilepath(*LogFolder(), sentry.seed, path_arena),
                             FileMode::WriteNoOverwrite()));

    TRY(EnvelopeAddHeader(sentry, file.Writer(), false));
    TRY(EnvelopeAddEvent(sentry, file.Writer(), event, false));
    return k_success;
}

PUBLIC ErrorCodeOr<void>
ConsumeAndSubmitErrorFiles(Sentry& sentry, String folder, ArenaAllocator& scratch_arena) {
    if constexpr (!k_online_reporting) return k_success;

    constexpr auto k_wildcard = ConcatArrays("*."_ca, detail::k_error_file_extension);
    auto const entries = TRY(FindEntriesInFolder(scratch_arena,
                                                 folder,
                                                 {
                                                     .options {
                                                         .wildcard = k_wildcard,
                                                     },
                                                     .recursive = false,
                                                     .only_file_type = FileType::File,
                                                 }));

    if (entries.size) {
        auto const temp_dir = TRY(TemporaryDirectoryOnSameFilesystemAs(folder, scratch_arena));
        DEFER { auto _ = Delete(temp_dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

        DynamicArray<char> full_path {scratch_arena};
        dyn::Assign(full_path, folder);
        dyn::Append(full_path, path::k_dir_separator);
        auto const full_path_len = full_path.size;
        full_path.Reserve(full_path.size + 40);

        DynamicArray<char> temp_full_path {scratch_arena};
        dyn::Assign(temp_full_path, temp_dir);
        dyn::Append(temp_full_path, path::k_dir_separator);
        auto const temp_full_path_len = temp_full_path.size;
        temp_full_path.Reserve(temp_full_path.size + 40);

        for (auto const entry : entries) {
            // construct the full path
            dyn::Resize(full_path, full_path_len);
            dyn::AppendSpan(full_path, entry.subpath);

            // construct the new temp path
            dyn::Resize(temp_full_path, temp_full_path_len);
            dyn::AppendSpan(temp_full_path, entry.subpath);

            // Move the file into the temporary directory, this will be atomic so that other processes don't
            // try and submit the same error report.
            if (auto const o = Rename(full_path, temp_full_path); o.HasError()) {
                if (o.Error() == FilesystemError::PathDoesNotExist) continue;
                LogError(k_main_log_module, "Couldn't move error file: {}", o.Error());
                continue;
            }

            // We now have exclusive access to the file, read it and try sending it to Sentry. If we fail, put
            // the file back where we found it, it can be tried again later.
            bool success = false;
            DEFER {
                if (!success) auto _ = Rename(temp_full_path, full_path);
            };

            auto envelope_without_header = TRY_OR(ReadEntireFile(temp_full_path, scratch_arena), {
                LogError(k_main_log_module, "Couldn't read error file: {}", error);
                continue;
            });

            // Remove the envelope header, SendSentryEnvelope will add another one with correct sent_at.
            // This is done by removing everything up to and including the first newline.
            auto const newline = Find(envelope_without_header, '\n');
            if (!newline) {
                success = true; // file is invalid, ignore it
                continue;
            }
            envelope_without_header.RemovePrefix(*newline + 1);

            DynamicArray<char> response {scratch_arena};

            TRY_OR(SubmitEnvelope(sentry,
                                  envelope_without_header,
                                  scratch_arena,
                                  {
                                      .write_to_file_if_needed = false,
                                      .response = dyn::WriterFor(response),
                                      .request_options =
                                          {
                                              .timeout_seconds = 5,
                                          },
                                  }),
                   {
                       LogError(k_main_log_module,
                                "Couldn't send error report to Sentry: {}. {}",
                                error,
                                response);
                       continue;
                   });

            success = true;
        }
    }

    return k_success;
}

} // namespace sentry
