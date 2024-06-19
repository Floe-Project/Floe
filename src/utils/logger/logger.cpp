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
                "Trace: {35}({3}), {}",
                path::Filename(FromNullTerminated(loc.file)),
                loc.line,
                loc.function);
    if (message.size) fmt::Append(buf, " | {}", message);

    LogFunction(buf, LogLevel::Info, true);
}

void StdoutLogger::LogFunction(String str, LogLevel level, bool add_newline) {
    StdPrint(StdStream::Out, LogPrefix(level, {.ansi_colors = true}));
    StdPrint(StdStream::Out, str);
    if (add_newline) StdPrint(StdStream::Out, "\n"_s);
}

StdoutLogger stdout_log;

void CliOutLogger::LogFunction(String str, LogLevel level, bool add_newline) {
    LogPrefixOptions options = {.ansi_colors = true};
    StdStream stream = StdStream::Out;
    if (level == LogLevel::Error)
        stream = StdStream::Err;
    else
        options.no_info_prefix = true;

    StdPrint(stream, LogPrefix(level, options));
    StdPrint(stream, str);
    if (add_newline) StdPrint(stream, "\n"_s);
}

CliOutLogger cli_out;

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

    bool first_message = false;

    // Could just use a mutex here but this atomic method avoids the class having to have any sort of
    // destructor which is particularly beneficial at the moment trying to debug a crash at shutdown.
    //
    // NOTE: this is not perfect - this method for thread-safety means that logs could be printed out of
    // order.

    FileLogger::State ex = FileLogger::State::Uninitialised;
    if (state.CompareExchangeStrong(ex, FileLogger::State::Initialising)) {
        ArenaAllocatorWithInlineStorage<1000> arena;
        auto outcome = KnownDirectoryWithSubdirectories(arena, KnownDirectories::Logs, Array {"Floe"_s});
        if (outcome.HasValue()) {
            auto path = DynamicArray<char>::FromOwnedSpan(outcome.Value(), arena);
            path::JoinAppend(path, "floe.log");
            filepath = String(path);
            first_message = true;
        } else {
            filepath = ""_s;
            DebugLn("failed to get logs known dir: {}", outcome.Error());
        }
        state.Store(FileLogger::State::Initialised);
    } else if (ex == FileLogger::State::Initialising) {
        do {
            SpinLoopPause();
        } while (state.Load() == FileLogger::State::Initialising);
    }

    ASSERT(state.Load() == FileLogger::State::Initialised);

    auto file = ({
        auto outcome = OpenFile(filepath, FileMode::Append);
        if (outcome.HasError()) {
            DebugLn("failed to open log file: {}", outcome.Error());
            return;
        }
        outcome.ReleaseValue();
    });

    auto try_writing = [&](Writer writer) -> ErrorCodeOr<void> {
        if (first_message) {
            TRY(writer.WriteChars("=======================\n"_s));

            auto const t = LocalTimeFromNanosecondsSinceEpoch(NanosecondsSinceEpoch());
            TRY(fmt::FormatToWriter(writer,
                                    "{} {} {} {} {}:{}:{}",
                                    t.DayName(),
                                    t.MonthName(),
                                    t.day_of_month,
                                    t.year,
                                    t.hour,
                                    t.minute,
                                    t.second));
            TRY(writer.WriteChar('\n'));

            TRY(fmt::FormatToWriter(writer,
                                    "Floe v{}.{}.{}\n",
                                    FLOE_MAJOR_VERSION,
                                    FLOE_MINOR_VERSION,
                                    FLOE_PATCH_VERSION));

            TRY(fmt::FormatToWriter(writer, "OS: {}\n", OperatingSystemName()));
        }

        TRY(writer.WriteChars(LogPrefix(level, {.ansi_colors = false, .no_info_prefix = false})));
        TRY(writer.WriteChars(str));
        if (add_newline) TRY(writer.WriteChar('\n'));
        return k_success;
    };

    auto writer = file.Writer();
    auto o = try_writing(writer);
    if (o.HasError()) DebugLn("failed to write log file: {}, {}", filepath, o.Error());
}

FileLogger g_log_file {};
