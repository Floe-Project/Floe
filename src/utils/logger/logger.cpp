// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logger.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"

ErrorCodeOr<void> WriteFormattedLog(Writer writer,
                                    LogLevel level,
                                    WriteFormattedLogOptions options,
                                    String category,
                                    String message) {
    TRY(writer.WriteChar('['));
    bool needs_space = false;

    if (options.timestamp) {
        if (Exchange(needs_space, true)) TRY(writer.WriteChar(' '));
        TRY(writer.WriteChars(Timestamp()));
    }

    if (category.size) {
        if (Exchange(needs_space, true)) TRY(writer.WriteChar(' '));
        TRY(writer.WriteChars(category));
    }

    if (!(options.no_info_prefix && level == LogLevel::Info)) {
        if (Exchange(needs_space, true)) TRY(writer.WriteChar(' '));
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
        if (Exchange(needs_space, true)) TRY(writer.WriteChar(' '));
        if (auto const thread_name = ThreadName())
            TRY(writer.WriteChars(*thread_name));
        else
            TRY(writer.WriteChars(fmt::IntToString(CurrentThreadId(),
                                                   fmt::IntToStringOptions {
                                                       .base = fmt::IntToStringOptions::Base::Hexadecimal,
                                                   })));
    }

    TRY(writer.WriteChars("]"_s));
    if (message.size) TRY(writer.WriteChar(' '));
    TRY(writer.WriteChars(message));
    if (options.add_newline) TRY(writer.WriteChar('\n'));
    return k_success;
}

void Logger::TraceLn(CategoryString category, String message, SourceLocation loc) {
    DynamicArrayInline<char, 1000> buf;
    fmt::Append(buf, "trace: {}({}): {}", FromNullTerminated(loc.file), loc.line, loc.function);
    if (message.size) fmt::Append(buf, ": {}", message);

    LogFunction(category.str, buf, LogLevel::Debug, true);
}

void StdStreamLogger::LogFunction(String category, String str, LogLevel level, bool add_newline) {
    auto& mutex = StdStreamMutex(stream);
    mutex.Lock();
    DEFER { mutex.Unlock(); };

    auto log_config = config;
    log_config.add_newline = add_newline;
    auto _ = WriteFormattedLog(StdWriter(stream), level, log_config, category, str);
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

void FileLogger::LogFunction(String category, String str, LogLevel level, bool add_newline) {
    // Could just use a mutex here but this atomic method avoids the class having to have any sort of
    // destructor which is particularly beneficial at the moment trying to debug a crash at shutdown.
    //
    // NOTE: this is not perfect - this method for thread-safety means that logs could be printed out of
    // order.

    FileLogger::State ex = FileLogger::State::Uninitialised;
    if (state.CompareExchangeStrong(ex,
                                    FileLogger::State::Initialising,
                                    RmwMemoryOrder::Acquire,
                                    LoadMemoryOrder::Relaxed)) {
        ArenaAllocatorWithInlineStorage<1000> arena;
        auto outcome = FloeKnownDirectory(arena, FloeKnownDirectories::Logs);
        if (outcome.HasValue()) {
            auto path = DynamicArray<char>::FromOwnedSpan(outcome.Value(), arena);
            path::JoinAppend(path, "floe.log");
            dyn::Assign(filepath, String(path));
        } else {
            dyn::Clear(filepath);
            g_debug_log.DebugLn("file-logger"_cat, "failed to get logs known dir: {}", outcome.Error());
        }
        state.Store(FileLogger::State::Initialised, StoreMemoryOrder::Release);
    } else if (ex == FileLogger::State::Initialising) {
        do {
            SpinLoopPause();
        } while (state.Load(LoadMemoryOrder::Acquire) == FileLogger::State::Initialising);
    }

    ASSERT(state.Load(LoadMemoryOrder::Relaxed) == FileLogger::State::Initialised);

    auto file = ({
        auto outcome = OpenFile(filepath, FileMode::Append);
        if (outcome.HasError()) {
            g_debug_log.DebugLn(k_global_log_cat,
                                "failed to open log file {}: {}",
                                filepath,
                                outcome.Error());
            return;
        }
        outcome.ReleaseValue();
    });

    auto writer = file.Writer();
    auto o = WriteFormattedLog(writer,
                               level,
                               {
                                   .ansi_colors = false,
                                   .no_info_prefix = false,
                                   .timestamp = true,
                                   .add_newline = add_newline,
                               },
                               category,
                               str);
    if (o.HasError())
        g_debug_log.DebugLn(k_global_log_cat, "failed to write log file: {}, {}", filepath, o.Error());
}

FileLogger g_log_file {};

DefaultLogger g_log {};
