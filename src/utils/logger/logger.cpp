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
    TRY(writer.WriteChar('['));
    bool needs_space = false;

    if (options.timestamp) {
        if (Exchange(needs_space, true)) TRY(writer.WriteChar(' '));
        TRY(writer.WriteChars(Timestamp()));
    }

    if (module_name.size) {
        if (Exchange(needs_space, true)) TRY(writer.WriteChar(' '));
        TRY(writer.WriteChars(module_name));
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
    TRY(writer.WriteChar('\n'));
    return k_success;
}

void Logger::Trace(LogModuleName module_name, String message, SourceLocation loc) {
    DynamicArrayInline<char, 1000> buf;
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
            g_debug_log.Debug("file-logger"_log_module, "failed to get logs known dir: {}", outcome.Error());
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
