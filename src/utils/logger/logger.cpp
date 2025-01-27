// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logger.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"

ErrorCodeOr<void> WriteFormattedLog(Writer writer,
                                    String module_name,
                                    LogLevel level,
                                    String message,
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
    TRY(writer.WriteChars(message));
    if (prefix_was_written || message.size) {
        if (!message.size || (message.size && message[message.size - 1] != '\n')) TRY(writer.WriteChar('\n'));
    }
    return k_success;
}

void Logger::Trace(LogModuleName module_name, String message, SourceLocation loc) {
    DynamicArrayBounded<char, 1000> buf;
    fmt::Append(buf, "trace: {}({}): {}", FromNullTerminated(loc.file), loc.line, loc.function);
    if (message.size) fmt::Append(buf, ": {}", message);

    Log(module_name, LogLevel::Debug, buf);
}

void StdStreamLogger::Log(LogModuleName module_name, LogLevel level, String str) {
    auto& mutex = StdStreamMutex(stream);
    mutex.Lock();
    DEFER { mutex.Unlock(); };

    auto _ = WriteFormattedLog(StdWriter(stream), module_name.str, level, str, config);
}

StdStreamLogger g_debug_log {
    StdStream::Out,
    WriteFormattedLogOptions {.ansi_colors = true,
                              .no_info_prefix = false,
                              .timestamp = true,
                              .thread = true},
};

StdStreamLogger g_cli_out {
    StdStream::Out,
    WriteFormattedLogOptions {.ansi_colors = true,
                              .no_info_prefix = true,
                              .timestamp = false,
                              .thread = false},
};

void FileLogger::Log(LogModuleName module_name, LogLevel level, String str) {
    CallOnce(init_flag, [this] {
        auto error_log = StdWriter(StdStream::Err);
        filepath = FloeKnownDirectory(path_allocator,
                                      FloeKnownDirectoryType::Logs,
                                      "floe.log"_s,
                                      {.create = true, .error_log = &error_log});
    });

    auto file = ({
        auto outcome = OpenFile(filepath, FileMode::Append);
        if (outcome.HasError()) {
            g_debug_log.Debug(k_global_log_module,
                              "failed to open log file {}: {}",
                              filepath,
                              outcome.Error());
            return;
        }
        outcome.ReleaseValue();
    });

    auto writer = file.Writer();
    auto o = WriteFormattedLog(writer,
                               module_name.str,
                               level,
                               str,
                               {
                                   .ansi_colors = false,
                                   .no_info_prefix = false,
                                   .timestamp = true,
                               });
    if (o.HasError())
        g_debug_log.Debug(k_global_log_module, "failed to write log file: {}, {}", filepath, o.Error());
}

FileLogger g_log_file {};

DefaultLogger g_log {};
