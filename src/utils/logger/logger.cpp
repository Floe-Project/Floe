// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logger.hpp"

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

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

[[clang::no_destroy]] StdoutLogger stdout_log;

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

[[clang::no_destroy]] CliOutLogger cli_out;
