// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "tracy_wrapped.hpp"

// Sometimes don't want to depend on our usual string formatting because that code could be cause of the
// problem we're trying to debug.
struct InlineSprintfBuffer {
    InlineSprintfBuffer() { buffer[0] = 0; }

    void Append(char const* fmt, ...) __attribute__((__format__(__printf__, 2, 3))) {
        va_list args;
        va_start(args, fmt);
        auto const n = stbsp_vsnprintf(write_ptr, size_remaining, fmt, args);
        va_end(args);

        if (n < 0) return;

        auto const num_written = Min(size_remaining, n);
        write_ptr += num_written;
        size_remaining -= num_written;
    }

    String AsString() { return {buffer, ArraySize(buffer) - (usize)size_remaining}; }
    char const* CString() { return buffer; }

    char buffer[1024]; // always null-terminated
    int size_remaining = (int)ArraySize(buffer);
    char* write_ptr = buffer;
};

template <typename... Args>
[[noreturn]] PUBLIC void PanicF(SourceLocation loc, String format, Args const&... args) {
    DynamicArrayBounded<char, 1000> buffer {};
    fmt::Append(buffer, format, args...);
    Panic(dyn::NullTerminated(buffer), loc);
}

#define PANICF(format, ...) PanicF(SourceLocation::Current(), format, ##__VA_ARGS__)

enum class StacktraceError {
    NotInitialised,
};

ErrorCodeCategory const& StacktraceErrorCodeType();
inline ErrorCodeCategory const& ErrorCategoryForEnum(StacktraceError) { return StacktraceErrorCodeType(); }

struct StacktraceOptions {
    bool ansi_colours = false;
    bool demangle = true; // demangling is not signal-safe
};

using StacktraceStack = DynamicArrayBounded<uintptr, 32>;
Optional<StacktraceStack> CurrentStacktrace(int skip_frames = 1);
Optional<String> InitStacktraceState(Optional<String> current_binary_path); // returns error message if failed
void ShutdownStacktraceState();

struct FrameInfo {
    String function_name;
    String filename;
    int line;
};

MutableString CurrentStacktraceString(Allocator& a, StacktraceOptions options = {}, int skip_frames = 1);
MutableString StacktraceString(StacktraceStack const&, Allocator& a, StacktraceOptions options = {});
void CurrentStacktraceToCallback(FunctionRef<void(FrameInfo const&)> callback,
                                 StacktraceOptions options = {},
                                 int skip_frames = 1);
void StacktraceToCallback(StacktraceStack const&,
                          FunctionRef<void(FrameInfo const&)> callback,
                          StacktraceOptions options = {});
ErrorCodeOr<void> PrintCurrentStacktrace(StdStream stream, StacktraceOptions options, int skip_frames);
ErrorCodeOr<void> WriteStacktrace(StacktraceStack const&, Writer writer, StacktraceOptions options);
ErrorCodeOr<void> WriteCurrentStacktrace(Writer writer, StacktraceOptions options, int skip_frames);
ErrorCodeOr<void> WriteInfoForProgramCounter(uintptr_t pc, Writer writer, StacktraceOptions options);

struct TracyMessageConfig {
    String category;
    u32 colour;
    Optional<uintptr_t> object_id;
};

template <typename... Args>
PUBLIC void TracyMessageEx(TracyMessageConfig config, String format, Args const&... args) {
    if constexpr (!k_tracy_enable) return;

    DynamicArrayBounded<char, 5000> msg;
    dyn::Append(msg, '[');
    dyn::AppendSpan(msg, config.category);
    dyn::AppendSpan(msg, "] "_s);

    if (config.object_id) fmt::Append(msg, "{}: ", *config.object_id);

    if constexpr (sizeof...(args))
        fmt::Append(msg, format, args...);
    else
        dyn::AppendSpan(msg, format);
    TracyMessageC(msg.data, msg.size, config.colour);
}

#define ZoneScopedMessage(config, format, ...)                                                               \
    TracyMessageEx(config, format, ##__VA_ARGS__);                                                           \
    ZoneScopedN(format)

#define ZoneKeyNum(key, num)                                                                                 \
    do {                                                                                                     \
        if constexpr (k_tracy_enable) {                                                                      \
            const auto CONCAT(zone_key_num, __LINE__) = fmt::FormatInline<100>("{}: {}", key, num);          \
            ZoneText(CONCAT(zone_key_num, __LINE__).data, CONCAT(zone_key_num, __LINE__).size);              \
            (void)CONCAT(zone_key_num, __LINE__);                                                            \
        }                                                                                                    \
    } while (0)
