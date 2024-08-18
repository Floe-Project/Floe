// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logger.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "utils/debug/debug.hpp"

void Logger::TraceLn(String message, SourceLocation loc) {
    DynamicArrayInline<char, 1000> buf;
    fmt::Append(buf,
                "trace: {}({}): {}",
                path::Filename(FromNullTerminated(loc.file)),
                loc.line,
                loc.function);
    if (message.size) fmt::Append(buf, ": {}", message);

    LogFunction(buf, LogLevel::Debug, true);
}

void StdoutLogger::LogFunction(String str, LogLevel level, bool add_newline) {
    auto& mutex = StdStreamMutex(StdStream::Out);
    mutex.Lock();
    DEFER { mutex.Unlock(); };
    auto _ = StdPrint(StdStream::Out, LogPrefix(level, {.ansi_colors = true}));
    auto _ = StdPrint(StdStream::Out, str);
    if (add_newline) auto _ = StdPrint(StdStream::Out, "\n"_s);
}

StdoutLogger g_stdout_log;

void CliOutLogger::LogFunction(String str, LogLevel level, bool add_newline) {
    auto& mutex = StdStreamMutex(StdStream::Out);
    mutex.Lock();
    DEFER { mutex.Unlock(); };
    auto _ = StdPrint(StdStream::Out, LogPrefix(level, {.ansi_colors = true, .no_info_prefix = true}));
    auto _ = StdPrint(StdStream::Out, str);
    if (add_newline) auto _ = StdPrint(StdStream::Out, "\n"_s);
}

CliOutLogger g_cli_out;

void FileLogger::LogFunction(String str, LogLevel level, bool add_newline) {
    // TracyMessageEx(
    //     {
    //         .category = "floelogger",
    //         .colour = 0xffffff,
    //         .object_id = nullopt,
    //     },
    //     "{}: {}",
    //     ({
    //         String c;
    //         switch (level) {
    //             case LogLevel::Debug: c = "Debug: "_s; break;
    //             case LogLevel::Info: c = ""_s; break;
    //             case LogLevel::Warning: c = "Warning: "_s; break;
    //             case LogLevel::Error: c = "Error: "_s; break;
    //         }
    //         c;
    //     }),
    //     str);

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
            g_stdout_log.DebugLn("failed to get logs known dir: {}", outcome.Error());
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
            g_stdout_log.DebugLn("failed to open log file {}: {}", filepath, outcome.Error());
            return;
        }
        outcome.ReleaseValue();
    });

    auto try_writing = [&](Writer writer) -> ErrorCodeOr<void> {
        TRY(writer.WriteChars(Timestamp()));
        TRY(writer.WriteChar(' '));

        TRY(writer.WriteChars(LogPrefix(level, {.ansi_colors = false, .no_info_prefix = false})));
        TRY(writer.WriteChars(str));
        if (add_newline) TRY(writer.WriteChar('\n'));
        return k_success;
    };

    auto writer = file.Writer();
    auto o = try_writing(writer);
    if (o.HasError()) g_stdout_log.DebugLn("failed to write log file: {}, {}", filepath, o.Error());
}

FileLogger g_log_file {};

Logger& g_log = PRODUCTION_BUILD ? (Logger&)g_log_file : (Logger&)g_stdout_log;
