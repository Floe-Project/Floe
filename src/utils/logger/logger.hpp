// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

enum class LogLevel { Debug, Info, Warning, Error };

constexpr String ToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "debug"_s;
        case LogLevel::Info: return "info"_s;
        case LogLevel::Warning: return "warning"_s;
        case LogLevel::Error: return "error"_s;
    }
    return "unknown"_s;
}

struct LogPrefixOptions {
    bool ansi_colors = false;
    bool no_info_prefix = false;
};

constexpr String LogPrefix(LogLevel level, LogPrefixOptions options = {}) {
    if (options.ansi_colors) {
        switch (level) {
            case LogLevel::Debug: return ANSI_COLOUR_FOREGROUND_BLUE("[debug] ");
            case LogLevel::Info: return options.no_info_prefix ? ""_s : "[info] ";
            case LogLevel::Warning: return ANSI_COLOUR_FOREGROUND_YELLOW("[warning] ");
            case LogLevel::Error: return ANSI_COLOUR_FOREGROUND_RED("[error] ");
        }
    } else {
        switch (level) {
            case LogLevel::Debug: return "[debug] ";
            case LogLevel::Info: return options.no_info_prefix ? ""_s : "[info] ";
            case LogLevel::Warning: return "[warning] ";
            case LogLevel::Error: return "[error] ";
        }
    }
    return "unknown: "_s;
}

using LogAllocator = ArenaAllocatorWithInlineStorage<1500>;

// Wraps a function that prints a string (to a file or stdout, for example) providing convenience
// functions for invoking it: different log levels, with or without newlines, log-level filtering.
struct Logger {
    virtual ~Logger() {}
    virtual void LogFunction(String str, LogLevel level, bool add_newline) = 0;

    void TraceLn(String message = {}, SourceLocation loc = SourceLocation::Current());

    void Debug(String str) {
        if (LogLevel::Debug < max_level_allowed) return;
        LogFunction(str, LogLevel::Info, false);
    }
    void Info(String str) {
        if (LogLevel::Info < max_level_allowed) return;
        LogFunction(str, LogLevel::Info, false);
    }
    void Warning(String str) {
        if (LogLevel::Warning < max_level_allowed) return;
        LogFunction(str, LogLevel::Info, false);
    }
    void Error(String str) {
        if (LogLevel::Error < max_level_allowed) return;
        LogFunction(str, LogLevel::Info, false);
    }

    void DebugLn(String str) {
        if (LogLevel::Debug < max_level_allowed) return;
        LogFunction(str, LogLevel::Info, true);
    }
    void InfoLn(String str) {
        if (LogLevel::Info < max_level_allowed) return;
        LogFunction(str, LogLevel::Info, true);
    }
    void WarningLn(String str) {
        if (LogLevel::Warning < max_level_allowed) return;
        LogFunction(str, LogLevel::Info, true);
    }
    void ErrorLn(String str) {
        if (LogLevel::Error < max_level_allowed) return;
        LogFunction(str, LogLevel::Info, true);
    }

    template <typename... Args>
    void DebugLn(String format, Args const&... args) {
        if (LogLevel::Debug < max_level_allowed) return;
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Debug, true);
    }
    template <typename... Args>
    void InfoLn(String format, Args const&... args) {
        if (LogLevel::Info < max_level_allowed) return;
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Info, true);
    }
    template <typename... Args>
    void ErrorLn(String format, Args const&... args) {
        if (LogLevel::Error < max_level_allowed) return;
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Error, true);
    }
    template <typename... Args>
    void WarningLn(String format, Args const&... args) {
        if (LogLevel::Warning < max_level_allowed) return;
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Warning, true);
    }

    template <typename... Args>
    void Debug(String format, Args const&... args) {
        if (LogLevel::Debug < max_level_allowed) return;
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Debug, false);
    }
    template <typename... Args>
    void Info(String format, Args const&... args) {
        if (LogLevel::Info < max_level_allowed) return;
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Info, false);
    }
    template <typename... Args>
    void Error(String format, Args const&... args) {
        if (LogLevel::Error < max_level_allowed) return;
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Error, false);
    }
    template <typename... Args>
    void Warning(String format, Args const&... args) {
        if (LogLevel::Warning < max_level_allowed) return;
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Warning, false);
    }

    LogLevel max_level_allowed = PRODUCTION_BUILD ? LogLevel::Info : LogLevel::Debug;
};

struct StdoutLogger : Logger {
    void LogFunction(String str, LogLevel level, bool add_newline) override;
};
extern StdoutLogger stdout_log;

// Errors are printed to stderr, the rest to stdout
struct CliOutLogger : Logger {
    void LogFunction(String str, LogLevel level, bool add_newline) override;
};
extern CliOutLogger cli_out;
