// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"
#include "utils/debug/tracy_wrapped.hpp"

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

using LogAllocator = ArenaAllocatorWithInlineStorage<2000>;

// Wraps a function that prints a string (to a file or stdout, for example) providing convenience
// functions for invoking it: different log levels, with or without newlines, log-level filtering.
struct Logger {
    virtual ~Logger() = default;
    virtual void LogFunction(String str, LogLevel level, bool add_newline) = 0;

    void Ln(LogLevel level, String str) { LogFunction(str, level, true); }
    template <typename... Args>
    void Ln(LogLevel level, String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), level, true);
    }

    void TraceLn(String message = {}, SourceLocation loc = SourceLocation::Current());

    void Debug(String str) {
        if constexpr (!PRODUCTION_BUILD) LogFunction(str, LogLevel::Debug, false);
    }
    void Info(String str) { LogFunction(str, LogLevel::Info, false); }
    void Warning(String str) { LogFunction(str, LogLevel::Warning, false); }
    void Error(String str) { LogFunction(str, LogLevel::Error, false); }

    void DebugLn(String str) {
        if constexpr (!PRODUCTION_BUILD) LogFunction(str, LogLevel::Debug, true);
    }
    void InfoLn(String str) { LogFunction(str, LogLevel::Info, true); }
    void WarningLn(String str) { LogFunction(str, LogLevel::Warning, true); }
    void ErrorLn(String str) { LogFunction(str, LogLevel::Error, true); }

    template <typename... Args>
    void DebugLn(String format, Args const&... args) {
        if constexpr (PRODUCTION_BUILD) return;
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Debug, true);
    }
    template <typename... Args>
    void InfoLn(String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Info, true);
    }
    template <typename... Args>
    void ErrorLn(String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Error, true);
    }
    template <typename... Args>
    void WarningLn(String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Warning, true);
    }

    template <typename... Args>
    void Debug(String format, Args const&... args) {
        if constexpr (PRODUCTION_BUILD) return;
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Debug, false);
    }
    template <typename... Args>
    void Info(String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Info, false);
    }
    template <typename... Args>
    void Error(String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Error, false);
    }
    template <typename... Args>
    void Warning(String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(fmt::Format(log_allocator, format, args...), LogLevel::Warning, false);
    }
};

struct StdoutLogger final : Logger {
    void LogFunction(String str, LogLevel level, bool add_newline) override;
};
extern StdoutLogger g_stdout_log;

// Logs to stdout but info messages don't have a prefix
struct CliOutLogger final : Logger {
    void LogFunction(String str, LogLevel level, bool add_newline) override;
};
extern CliOutLogger g_cli_out;

// Timestamped log file
struct FileLogger final : Logger {
    void LogFunction(String str, LogLevel level, bool add_newline) override;

    enum class State : u32 { Uninitialised, Initialising, Initialised };
    Atomic<State> state {State::Uninitialised};
    DynamicArrayInline<char, 256> filepath;
};
extern FileLogger g_log_file;

struct TracyLogger final : Logger {
    void LogFunction([[maybe_unused]] String str, LogLevel, bool) override {
        if constexpr (k_tracy_enable) TracyMessageC(str.data, str.size, config.colour);
    }
};

struct DefaultLogger final : Logger {
    void LogFunction(String str, LogLevel level, bool add_newline) override {
        if constexpr (!PRODUCTION_BUILD)
            g_stdout_log.LogFunction(str, level, add_newline);
        else
            g_log_file.LogFunction(str, level, add_newline);
    }
};

extern Logger& g_log;

#define DBG_PRINT_EXPR(x)     g_log.DebugLn("{}: {} = {}", __FUNCTION__, #x, x)
#define DBG_PRINT_EXPR2(x, y) g_log.DebugLn("{}: {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y)
#define DBG_PRINT_EXPR3(x, y, z)                                                                             \
    g_log.DebugLn("{}: {} = {}, {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y, #z, z)
#define DBG_PRINT_STRUCT(x) g_log.DebugLn("{}: {} = {}", __FUNCTION__, #x, fmt::DumpStruct(x))
