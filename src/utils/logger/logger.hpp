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
    bool thread = false;
};

ErrorCodeOr<void> WriteFormattedLog(Writer writer,
                                    String module_name,
                                    LogLevel level,
                                    String message,
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

struct Logger {
    virtual ~Logger() = default;
    virtual void Log(LogModuleName module_name, LogLevel level, String str) = 0;

    using LogAllocator = ArenaAllocatorWithInlineStorage<2000>;

    // NOTE: you will have to use `using Logger::Log` in the derived class to bring this in
    template <typename... Args>
    void Log(LogModuleName module_name, LogLevel level, String format, Args const&... args) {
        static_assert(sizeof...(args) != 0);
        LogAllocator log_allocator {Malloc::Instance()};
        Log(module_name, level, fmt::Format(log_allocator, format, args...));
    }

    void
    Trace(LogModuleName module_name, String message = {}, SourceLocation loc = SourceLocation::Current());

    // A macro unfortunatly seems the best way to avoid repeating the same code while keep template
    // instantiations low (needed for fast compile times)
#define DECLARE_LOG_FUNCTION(level)                                                                          \
    template <typename... Args>                                                                              \
    void level(LogModuleName module_name, String format, Args const&... args) {                              \
        if constexpr (LogLevel::level == LogLevel::Debug && PRODUCTION_BUILD) return;                        \
        if constexpr (sizeof...(args) == 0) {                                                                \
            Log(module_name, LogLevel::level, format);                                                       \
        } else {                                                                                             \
            LogAllocator log_allocator {Malloc::Instance()};                                                 \
            Log(module_name, LogLevel::level, fmt::Format(log_allocator, format, args...));                  \
        }                                                                                                    \
    }

    DECLARE_LOG_FUNCTION(Debug)
    DECLARE_LOG_FUNCTION(Info)
    DECLARE_LOG_FUNCTION(Error)
    DECLARE_LOG_FUNCTION(Warning)
};

struct LineWriter {
    LineWriter(LogModuleName module_name, LogLevel level, Logger& sink)
        : module_name(module_name)
        , level(level)
        , sink(sink) {
        writer.Set<LineWriter>(*this, [](LineWriter& w, Span<u8 const> bytes) -> ErrorCodeOr<void> {
            w.Append(String {(char const*)bytes.data, bytes.size});
            return k_success;
        });
    }
    ~LineWriter() { Flush(); }

    void Flush() {
        if (buffer.size) {
            sink.Log(module_name, level, buffer);
            dyn::Clear(buffer);
        }
    }
    void Append(String data) {
        for (auto c : data)
            if (c == '\n')
                Flush();
            else
                dyn::Append(buffer, c);
    }

    LogModuleName module_name;
    LogLevel level;
    Logger& sink;
    DynamicArrayBounded<char, 250> buffer {};
    Writer writer {};
};

inline LineWriter ErrorWriter(Logger& logger) {
    return LineWriter {k_main_log_module, LogLevel::Error, logger};
}

struct StdStreamLogger final : Logger {
    StdStreamLogger(StdStream stream, WriteFormattedLogOptions config) : stream(stream), config(config) {}
    void Log(LogModuleName module_name, LogLevel level, String str) override;
    using Logger::Log; // Bring in the templated Log function
    StdStream const stream;
    WriteFormattedLogOptions const config;
};

// Debug log to STDOUT. The purpose of the log is to diagnose issues. Some tools like valgrind make it
// difficult to capture STDERR easily. Let's just keep things simple and use STDOUT for everything.
extern StdStreamLogger g_debug_log;

// STDOUT log for a CLI program. Info messages are printed verbatim, whereas warnings and erros have prefixes
// and colours
extern StdStreamLogger g_cli_out;

// Timestamped log file
struct FileLogger final : Logger {
    void Log(LogModuleName module_name, LogLevel level, String str) override;
    using Logger::Log;

    enum class State : u32 { Uninitialised, Initialising, Initialised };
    Atomic<State> state {State::Uninitialised};
    DynamicArrayBounded<char, 256> filepath;
};
extern FileLogger g_log_file;

struct TracyLogger final : Logger {
    void Log([[maybe_unused]] LogModuleName module_name, LogLevel, [[maybe_unused]] String str) override {
        if constexpr (k_tracy_enable) TracyMessageC(str.data, str.size, config.colour);
    }
    using Logger::Log;
};

struct BufferLogger final : Logger {
    BufferLogger(Allocator& a) : buffer(a) {}
    void Log(LogModuleName module_name, LogLevel level, String str) override {
        auto _ = WriteFormattedLog(dyn::WriterFor(buffer),
                                   module_name.str,
                                   level,
                                   str,
                                   {
                                       .ansi_colors = false,
                                       .no_info_prefix = true,
                                       .timestamp = false,
                                       .thread = false,
                                   });
    }

    DynamicArray<char> buffer;
};

struct DefaultLogger final : Logger {
    void Log(LogModuleName module_name, LogLevel level, String str) override {
        if constexpr (!PRODUCTION_BUILD) g_debug_log.Log(module_name, level, str);
        g_log_file.Log(module_name, level, str);
    }
    using Logger::Log;
};
extern DefaultLogger g_log;

#define DBG_PRINT_EXPR(x)     g_log.Debug({}, "{}: {} = {}", __FUNCTION__, #x, x)
#define DBG_PRINT_EXPR2(x, y) g_log.Debug({}, "{}: {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y)
#define DBG_PRINT_EXPR3(x, y, z)                                                                             \
    g_log.Debug({}, "{}: {} = {}, {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y, #z, z)
#define DBG_PRINT_STRUCT(x) g_log.Debug({}, "{}: {} = {}", __FUNCTION__, #x, fmt::DumpStruct(x))
