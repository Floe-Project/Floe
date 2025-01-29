// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logger.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"

ErrorCodeOr<void> WriteFormattedLog(Writer writer,
                                    String module_name,
                                    LogLevel level,
                                    MessageWriteFunction write_message,
                                    WriteFormattedLogOptions options) {
    bool needs_space = false;
    bool needs_open_bracket = true;

    auto const begin_prefix_item = [&]() -> ErrorCodeOr<void> {
        char buf[2];
        usize len = 0;
        if (Exchange(needs_open_bracket, false)) buf[len++] = '[';
        if (Exchange(needs_space, true)) buf[len++] = ' ';
        if (len) TRY(writer.WriteChars({buf, len}));
        return k_success;
    };

    if (options.timestamp) {
        TRY(begin_prefix_item());
        TRY(writer.WriteChars(Timestamp()));
    }

    if (module_name.size) {
        TRY(begin_prefix_item());
        TRY(writer.WriteChars(module_name));
    }

    if (!(options.no_info_prefix && level == LogLevel::Info)) {
        TRY(begin_prefix_item());
        TRY(writer.WriteChars(({
            String s;
            switch (level) {
                case LogLevel::Debug:
                    s = options.ansi_colors ? String(ANSI_COLOUR_FOREGROUND_BLUE("debug")) : "debug";
                    break;
                case LogLevel::Info: s = "info"; break;
                case LogLevel::Warning:
                    s = options.ansi_colors ? String(ANSI_COLOUR_FOREGROUND_YELLOW("warning")) : "warning";
                    break;
                case LogLevel::Error:
                    s = options.ansi_colors ? String(ANSI_COLOUR_FOREGROUND_RED("error")) : "error";
            }
            s;
        })));
    }

    if (options.thread) {
        TRY(begin_prefix_item());
        if (auto const thread_name = ThreadName())
            TRY(writer.WriteChars(*thread_name));
        else
            TRY(writer.WriteChars(fmt::IntToString(CurrentThreadId(),
                                                   fmt::IntToStringOptions {
                                                       .base = fmt::IntToStringOptions::Base::Hexadecimal,
                                                   })));
    }

    auto const prefix_was_written = !needs_open_bracket;

    if (prefix_was_written) TRY(writer.WriteChars("] "));
    TRY(write_message(writer));
    TRY(writer.WriteChar('\n'));
    return k_success;
}

void Trace(LogModuleName module_name, String message, SourceLocation loc) {
    Log(module_name, LogLevel::Debug, [&](Writer writer) -> ErrorCodeOr<void> {
        TRY(fmt::FormatToWriter(writer,
                                "trace: {}({}): {}",
                                FromNullTerminated(loc.file),
                                loc.line,
                                loc.function));
        if (message.size) TRY(fmt::FormatToWriter(writer, ": {}", message));
        return k_success;
    });
}

LogConfig g_log_config {};

void Log(LogModuleName module_name, LogLevel level, FunctionRef<ErrorCodeOr<void>(Writer)> write_message) {
    if (level < g_log_config.min_level_allowed.Load(LoadMemoryOrder::Relaxed)) return;

    static auto log_to_stderr = [](LogModuleName module_name,
                                   LogLevel level,
                                   FunctionRef<ErrorCodeOr<void>(Writer)> write_message) {
        constexpr WriteFormattedLogOptions k_config {
            .ansi_colors = true,
            .no_info_prefix = false,
            .timestamp = true,
            .thread = true,
        };
        auto& mutex = StdStreamMutex(StdStream::Err);
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        BufferedWriter<Kb(4)> buffered_writer {StdWriter(StdStream::Err)};
        DEFER { auto _ = buffered_writer.Flush(); };

        auto _ = WriteFormattedLog(buffered_writer.Writer(), module_name.str, level, write_message, k_config);
    };

    switch (g_log_config.destination.Load(LoadMemoryOrder::Relaxed)) {
        case LogConfig::Destination::Stderr: {
            log_to_stderr(module_name, level, write_message);
            break;
        }
        case LogConfig::Destination::File: {
            InitLogFolderIfNeeded();

            // TODO: Multiple processes might be trying to open and write to this file at the same time we
            // need to handle this case.
            auto file = ({
                constexpr String k_filename = "floe.log";
                constexpr usize k_buffer_size = 256;
                auto const log_folder = *LogFolder();
                if (log_folder.size + 1 + k_filename.size > k_buffer_size) {
                    log_to_stderr(k_global_log_module, LogLevel::Error, [](Writer writer) {
                        return writer.WriteChars("log file path too long"_s);
                    });
                    return;
                }

                auto outcome = OpenFile(path::JoinInline<k_buffer_size>(Array {*LogFolder(), k_filename}),
                                        FileMode::Append());
                if (outcome.HasError()) {
                    log_to_stderr(k_global_log_module,
                                  LogLevel::Error,
                                  [&outcome](Writer writer) -> ErrorCodeOr<void> {
                                      return fmt::FormatToWriter(writer,
                                                                 "failed to open log file: {}"_s,
                                                                 outcome.Error());
                                  });
                    return;
                }
                outcome.ReleaseValue();
            });

            BufferedWriter<Kb(4)> buffered_writer {file.Writer()};
            DEFER { auto _ = buffered_writer.Flush(); };

            auto o = WriteFormattedLog(buffered_writer.Writer(),
                                       module_name.str,
                                       level,
                                       write_message,
                                       {
                                           .ansi_colors = false,
                                           .no_info_prefix = false,
                                           .timestamp = true,
                                       });
            if (o.HasError()) {
                log_to_stderr(k_global_log_module, LogLevel::Error, [o](Writer writer) {
                    return fmt::FormatToWriter(writer, "failed to write log file: {}"_s, o.Error());
                });
            }

            break;
        }
    }
}
