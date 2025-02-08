// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

enum class LogLevel { Debug, Info, Warning, Error };

struct WriteLogLineOptions {
    bool ansi_colors = false;
    bool no_info_prefix = false;
    bool timestamp = false;
    bool thread = false;
    bool newline = true;
};

struct LogRingBuffer {
    static constexpr usize k_buffer_size = 1 << 13; // must be a power of 2
    static constexpr usize k_max_message_size = LargestRepresentableValue<u8>();

    u32 Mask(u32 val) { return val & (buffer.size - 1); }

    void Write(String message) {
        // We allow indexes to grow continuously until they naturally wrap around. These are the requirements
        // to make this work:
        static_assert(IsPowerOfTwo(k_buffer_size));
        static_assert(UnsignedInt<decltype(write)>);
        static_assert(Same<decltype(write), decltype(read)>);
        // The maximum capacity can only be half the range of the index data types. (So 2^31-1 when using 32
        // bit unsigned integers)
        static_assert(k_buffer_size <= ((1 << ((sizeof(write) * 8) - 1)) - 1));

        if (message.size > k_max_message_size) [[unlikely]] {
            // we need to truncate the message, but not invalidate utf-8
            for (usize i = 0; i < message.size;) {
                auto const size = Utf8CodepointSize(message[i]);
                if (i + size > k_max_message_size) {
                    message = message.SubSpan(0, i);
                    break;
                }
                i += size;
            }
        }

        mutex.Lock();
        DEFER { mutex.Unlock(); };

        // if there's no room for this message, we remove the oldest messages until there is room
        while (true) {
            auto const used = (u32)write - (u32)read;
            ASSERT_HOT(used <= buffer.size);
            auto const remaining = buffer.size - used;
            // we need the extra byte for prefixing the message with its size
            if (remaining >= (message.size + 1)) break;
            auto const tail_message_size = buffer[Mask(read)];
            read += 1; // message size byte
            read += tail_message_size;
        }

        buffer[Mask(write++)] = (u8)message.size;
        for (auto c : message)
            buffer[Mask(write++)] = (u8)c;
    }

    void ReadToNullTerminatedStringList(DynamicArrayBounded<char, k_buffer_size>& out) {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        dyn::Resize(out, write - read);
        usize out_index = 0;

        auto pos = read;
        while (pos != write) {
            auto const message_size = (u8)buffer[Mask(pos++)];
            for (u8 i = 0; i < message_size; i++)
                out[out_index++] = (char)buffer[Mask(pos++)];
            out[out_index++] = '\0';
        }
    }

    void Reset() {
        mutex.Lock();
        DEFER { mutex.Unlock(); };
        write = 0;
        read = 0;
    }

    Array<u8, k_buffer_size> buffer;
    MutexThin mutex {};
    u16 write {};
    u16 read {};
};

enum class ModuleName {
    Global,
    Main,
    Package,
    Gui,
    ErrorReporting,
    Filesystem,
    SampleLibrary,
    Clap,
    SampleLibraryServer,
    Settings,
    Standalone,
};

constexpr String ModuleNameString(ModuleName module_name) {
    switch (module_name) {
        case ModuleName::Global: return "🌍glbl"_s;
        case ModuleName::Main: return "🚀main"_s;
        case ModuleName::Package: return "📦pkg";
        case ModuleName::Gui: return "🖥️gui";
        case ModuleName::ErrorReporting: return "⚠️report";
        case ModuleName::Filesystem: return "📁fs";
        case ModuleName::SampleLibrary: return "📚smpl-lib";
        case ModuleName::Clap: return "👏clap";
        case ModuleName::SampleLibraryServer: return "📚smpl-srv";
        case ModuleName::Settings: return "⚙️sett";
        case ModuleName::Standalone: return "🧍stand";
    }
}

using MessageWriteFunction = FunctionRef<ErrorCodeOr<void>(Writer)>;

ErrorCodeOr<void> WriteLogLine(Writer writer,
                               ModuleName module_name,
                               LogLevel level,
                               MessageWriteFunction write_message,
                               WriteLogLineOptions options);

struct LogConfig {
    enum class Destination { Stderr, File };
    Destination destination = Destination::Stderr;
    LogLevel min_level_allowed = PRODUCTION_BUILD ? LogLevel::Info : LogLevel::Debug;
};

ErrorCodeOr<void> CleanupOldLogFilesIfNeeded(ArenaAllocator& scratch_arena);

void Log(ModuleName module_name, LogLevel level, FunctionRef<ErrorCodeOr<void>(Writer)> write_message);

template <typename... Args>
void Log(ModuleName module_name, LogLevel level, String format, Args const&... args) {
    Log(module_name, level, [&](Writer writer) { return fmt::FormatToWriter(writer, format, args...); });
}

// thread-safe, not signal-safe
// Returns log message strings in the order they were written, each message is separated by a null terminator.
void GetLatestLogMessages(DynamicArrayBounded<char, LogRingBuffer::k_buffer_size>& out);

void InitLogger(LogConfig);
void ShutdownLogger();

// TODO: remove Trace
void Trace(ModuleName module_name, String message = {}, SourceLocation loc = SourceLocation::Current());

// A macro unfortunatly seems the best way to avoid repeating the same code while keep template
// instantiations low (needed for fast compile times)
#define DECLARE_LOG_FUNCTION(level)                                                                          \
    template <typename... Args>                                                                              \
    void Log##level(ModuleName module_name, String format, Args const&... args) {                            \
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

#define DBG_PRINT_EXPR(x)     LogDebug({}, "{}: {} = {}", __FUNCTION__, #x, x)
#define DBG_PRINT_EXPR2(x, y) LogDebug({}, "{}: {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y)
#define DBG_PRINT_EXPR3(x, y, z)                                                                             \
    LogDebug({}, "{}: {} = {}, {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y, #z, z)
#define DBG_PRINT_STRUCT(x) LogDebug({}, "{}: {} = {}", __FUNCTION__, #x, fmt::DumpStruct(x))
