// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/threading.hpp"

enum class LogLevel { Debug, Info, Warning, Error };

struct WriteFormattedLogOptions {
    bool ansi_colors = false;
    bool no_info_prefix = false;
    bool timestamp = false;
    bool thread = false;
};

using MessageWriteFunction = FunctionRef<ErrorCodeOr<void>(Writer)>;

ErrorCodeOr<void> WriteFormattedLog(Writer writer,
                                    String module_name,
                                    LogLevel level,
                                    MessageWriteFunction write_message,
                                    WriteFormattedLogOptions options);

// Strongly-typed string so that it's not confused with strings and string formatting
struct LogModuleName {
    constexpr LogModuleName() = default;
    constexpr explicit LogModuleName(String str) : str(str) {
        if constexpr (!PRODUCTION_BUILD) {
            for (auto c : str)
                if (c == ' ' || c == '_' || IsUppercaseAscii(c))
                    throw "Log module names must be lowercase, use - instead of _ and not contain spaces";
        }
    }
    String str {};
};
constexpr LogModuleName operator""_log_module(char const* str, usize size) {
    return LogModuleName {String {str, size}};
}

// global infrastructure that is related to startup and shutdown
constexpr auto k_global_log_module = "🌍global"_log_module;

// main infrastructure related to the core of the application, the 'app' or 'instance' object
constexpr auto k_main_log_module = "🚀main"_log_module;

struct LogConfig {
    enum class Destination { Stderr, File };
    Atomic<Destination> destination = Destination::Stderr;
    Atomic<LogLevel> min_level_allowed = PRODUCTION_BUILD ? LogLevel::Info : LogLevel::Debug;
};

extern LogConfig g_log_config;

struct Logger {
    static void
    Log(LogModuleName module_name, LogLevel level, FunctionRef<ErrorCodeOr<void>(Writer)> write_message);

    template <typename... Args>
    void Log(LogModuleName module_name, LogLevel level, String format, Args const&... args) {
        Log(module_name, level, [&](Writer writer) { return fmt::FormatToWriter(writer, format, args...); });
    }

    static void
    Trace(LogModuleName module_name, String message = {}, SourceLocation loc = SourceLocation::Current());

    // A macro unfortunatly seems the best way to avoid repeating the same code while keep template
    // instantiations low (needed for fast compile times)
#define DECLARE_LOG_FUNCTION(level)                                                                          \
    template <typename... Args>                                                                              \
    void level(LogModuleName module_name, String format, Args const&... args) {                              \
        if constexpr (sizeof...(args) == 0) {                                                                \
            Log(module_name, LogLevel::level, format);                                                       \
        } else {                                                                                             \
            Log(module_name, LogLevel::level, [&](Writer writer) {                                           \
                return fmt::FormatToWriter(writer, format, args...);                                         \
            });                                                                                              \
        }                                                                                                    \
    }

    DECLARE_LOG_FUNCTION(Debug)
    DECLARE_LOG_FUNCTION(Info)
    DECLARE_LOG_FUNCTION(Error)
    DECLARE_LOG_FUNCTION(Warning)
};

extern Logger g_log;

#define DBG_PRINT_EXPR(x)     g_log.Debug({}, "{}: {} = {}", __FUNCTION__, #x, x)
#define DBG_PRINT_EXPR2(x, y) g_log.Debug({}, "{}: {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y)
#define DBG_PRINT_EXPR3(x, y, z)                                                                             \
    g_log.Debug({}, "{}: {} = {}, {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y, #z, z)
#define DBG_PRINT_STRUCT(x) g_log.Debug({}, "{}: {} = {}", __FUNCTION__, #x, fmt::DumpStruct(x))
