// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/threading.hpp"
#include "utils/debug/tracy_wrapped.hpp"

enum class LogLevel { Debug, Info, Warning, Error };

struct WriteFormattedLogOptions {
    bool ansi_colors = false;
    bool no_info_prefix = false;
    bool timestamp = false;
    bool add_newline = true;
};

ErrorCodeOr<void> WriteFormattedLog(Writer writer,
                                    LogLevel level,
                                    WriteFormattedLogOptions options,
                                    String category,
                                    String message);

using LogAllocator = ArenaAllocatorWithInlineStorage<2000>;

// TODO: rename to ModuleName? it's purpose is not really to categorise, but to identify the system
// Strongly-typed string so that it's not confused with strings and string formatting
struct CategoryString {
    constexpr CategoryString() = default;
    constexpr explicit CategoryString(String str) : str(str) {}
    String str {};
};
constexpr CategoryString operator""_cat(char const* str, usize size) {
    return CategoryString {String {str, size}};
}

// global infrastructure that is related to startup and shutdown
constexpr auto k_global_log_cat = "🌍global"_cat;

// main infrastructure related to the core of the application, the 'app' or 'instance' object
constexpr auto k_main_log_cat = "🚀main"_cat;

// Wraps a function that prints a string (to a file or stdout, for example) providing convenience
// functions for invoking it: different log levels, with or without newlines.
struct Logger {
    virtual ~Logger() = default;
    virtual void LogFunction(String category, String str, LogLevel level, bool add_newline) = 0;

    void Ln(LogLevel level, CategoryString category, String str) {
        LogFunction(category.str, str, level, true);
    }
    template <typename... Args>
    void Ln(LogLevel level, CategoryString category, String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(category.str, fmt::Format(log_allocator, format, args...), level, true);
    }

    void
    TraceLn(CategoryString category, String message = {}, SourceLocation loc = SourceLocation::Current());

    void Debug(CategoryString category, String str) {
        if constexpr (!PRODUCTION_BUILD) LogFunction(category.str, str, LogLevel::Debug, false);
    }
    void Info(CategoryString category, String str) { LogFunction(category.str, str, LogLevel::Info, false); }
    void Warning(CategoryString category, String str) {
        LogFunction(category.str, str, LogLevel::Warning, false);
    }
    void Error(CategoryString category, String str) {
        LogFunction(category.str, str, LogLevel::Error, false);
    }

    void DebugLn(CategoryString category, String str) {
        if constexpr (!PRODUCTION_BUILD) LogFunction(category.str, str, LogLevel::Debug, true);
    }
    void InfoLn(CategoryString category, String str) { LogFunction(category.str, str, LogLevel::Info, true); }
    void WarningLn(CategoryString category, String str) {
        LogFunction(category.str, str, LogLevel::Warning, true);
    }
    void ErrorLn(CategoryString category, String str) {
        LogFunction(category.str, str, LogLevel::Error, true);
    }

    template <typename... Args>
    void DebugLn(CategoryString category, String format, Args const&... args) {
        if constexpr (PRODUCTION_BUILD) return;
        LogAllocator log_allocator;
        LogFunction(category.str, fmt::Format(log_allocator, format, args...), LogLevel::Debug, true);
    }
    template <typename... Args>
    void InfoLn(CategoryString category, String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(category.str, fmt::Format(log_allocator, format, args...), LogLevel::Info, true);
    }
    template <typename... Args>
    void ErrorLn(CategoryString category, String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(category.str, fmt::Format(log_allocator, format, args...), LogLevel::Error, true);
    }
    template <typename... Args>
    void WarningLn(CategoryString category, String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(category.str, fmt::Format(log_allocator, format, args...), LogLevel::Warning, true);
    }

    template <typename... Args>
    void Debug(CategoryString category, String format, Args const&... args) {
        if constexpr (PRODUCTION_BUILD) return;
        LogAllocator log_allocator;
        LogFunction(category.str, fmt::Format(log_allocator, format, args...), LogLevel::Debug, false);
    }
    template <typename... Args>
    void Info(CategoryString category, String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(category.str, fmt::Format(log_allocator, format, args...), LogLevel::Info, false);
    }
    template <typename... Args>
    void Error(CategoryString category, String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(category.str, fmt::Format(log_allocator, format, args...), LogLevel::Error, false);
    }
    template <typename... Args>
    void Warning(CategoryString category, String format, Args const&... args) {
        LogAllocator log_allocator;
        LogFunction(category.str, fmt::Format(log_allocator, format, args...), LogLevel::Warning, false);
    }
};

struct StreamLogger final : Logger {
    StreamLogger(StdStream stream, WriteFormattedLogOptions config) : stream(stream), config(config) {}
    void LogFunction(String category, String str, LogLevel level, bool add_newline) override;
    StdStream const stream;
    WriteFormattedLogOptions const config;
};

extern StreamLogger g_stdout_log;
extern StreamLogger g_cli_out; // Logs to stdout but info messages don't have a prefix

// Timestamped log file
struct FileLogger final : Logger {
    void LogFunction(String category, String str, LogLevel level, bool add_newline) override;

    enum class State : u32 { Uninitialised, Initialising, Initialised };
    Atomic<State> state {State::Uninitialised};
    DynamicArrayInline<char, 256> filepath;
};
extern FileLogger g_log_file;

struct TracyLogger final : Logger {
    void LogFunction([[maybe_unused]] String category, [[maybe_unused]] String str, LogLevel, bool) override {
        if constexpr (k_tracy_enable) TracyMessageC(str.data, str.size, config.colour);
    }
};

struct DefaultLogger final : Logger {
    void LogFunction(String category, String str, LogLevel level, bool add_newline) override {
        if constexpr (!PRODUCTION_BUILD) g_stdout_log.LogFunction(category, str, level, add_newline);
        g_log_file.LogFunction(category, str, level, add_newline);
    }
};

extern DefaultLogger g_log;

#define DBG_PRINT_EXPR(x)     g_log.DebugLn("{}: {} = {}", __FUNCTION__, #x, x)
#define DBG_PRINT_EXPR2(x, y) g_log.DebugLn("{}: {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y)
#define DBG_PRINT_EXPR3(x, y, z)                                                                             \
    g_log.DebugLn("{}: {} = {}, {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y, #z, z)
#define DBG_PRINT_STRUCT(x) g_log.DebugLn("{}: {} = {}", __FUNCTION__, #x, fmt::DumpStruct(x))
