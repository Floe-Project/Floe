// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Contains a section of code from the LLVM project that is licenced differently, see below for full details.

#include "debug.hpp"

#include <backtrace.h>
#include <cxxabi.h>
#include <stdlib.h> // free

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "utils/logger/logger.hpp"

static void DefaultPanicHook(char const* message, SourceLocation loc) {
    constexpr StdStream k_panic_stream = StdStream::Err;
    InlineSprintfBuffer buffer;
    // we style the source location to look like the first item of a call stack and then print the stack
    // skipping one extra frame
    buffer.Append("\nPanic: " ANSI_COLOUR_SET_FOREGROUND_RED "%s" ANSI_COLOUR_RESET
                  "\n[0] " ANSI_COLOUR_SET_FOREGROUND_BLUE "%s" ANSI_COLOUR_RESET ":%d: %s\n",
                  message,
                  loc.file,
                  loc.line,
                  loc.function);
    auto _ = StdPrint(k_panic_stream, buffer.AsString());
    auto _ = PrintCurrentStacktrace(k_panic_stream, {.ansi_colours = true}, 4);
    auto _ = StdPrint(k_panic_stream, "\n");
}

Atomic<void (*)(char const* message, SourceLocation loc)> g_panic_hook = DefaultPanicHook;

void SetPanicHook(PanicHook hook) { g_panic_hook.Store(hook, StoreMemoryOrder::Release); }
PanicHook GetPanicHook() { return g_panic_hook.Load(LoadMemoryOrder::Acquire); }

thread_local bool g_in_signal_handler {};

static Atomic<bool> g_panic_occurred {};

bool PanicOccurred() { return g_panic_occurred.Load(LoadMemoryOrder::Acquire); }
void ResetPanic() { g_panic_occurred.Store(false, StoreMemoryOrder::Release); }

[[noreturn]] void Panic(char const* message, SourceLocation loc) {
    if (g_in_signal_handler) __builtin_abort();

    static thread_local u8 in_panic_hook {};

    switch (in_panic_hook) {
        case 0: {
            // First time we've panicked.
            ++in_panic_hook;
            g_panic_hook.Load(LoadMemoryOrder::Acquire)(message, loc);
            --in_panic_hook;

            g_panic_occurred.Store(true, StoreMemoryOrder::Release);
            throw PanicException();
        }
        default: {
            // We've panicked while in a panic hook. Hopefully we have a crash handler installed.
            __builtin_abort();
            break;
        }
    }
}

static void HandleUbsanError(String msg) {
    InlineSprintfBuffer buffer;
    buffer.Append("undefined behaviour: %.*s", (int)msg.size, msg.data);
    Panic(buffer.CString(), SourceLocation::Current());
}

#define INTERFACE extern "C" __attribute__((visibility("default")))

namespace ubsan {

// Code taken based LLVM's UBSan runtime implementation.
// https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/ubsan/ubsan_handlers.h
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) LLVM Project contributors
//
// Modified by Sam Windell to integrate with the rest of the codebase
// Copyright 2018-2024 Sam Windell
// =============================================================================================================

struct SourceLocation {
    char const* file;
    u32 line;
    u32 column;
};

using ValueHandle = uintptr_t;

struct TypeDescriptor {
    u16 kind;
    u16 info;
    char name[1];

    enum Kind { TkInteger = 0x0000, TkFloat = 0x0001, TkUnknown = 0xffff };
};

struct Value {
    TypeDescriptor const& type;
    ValueHandle val;
};

struct TypeMismatchData {
    SourceLocation loc;
    TypeDescriptor const& type;
    unsigned char log_alignment;
    unsigned char type_check_kind;
};

struct OverflowData {
    SourceLocation loc;
    TypeDescriptor const& type;
};

struct ShiftOutOfBoundsData {
    SourceLocation loc;
    TypeDescriptor const& lhs_type;
    TypeDescriptor const& rhs_type;
};

struct OutOfBoundsData {
    SourceLocation loc;
    TypeDescriptor const& array_type;
    TypeDescriptor const& index_type;
};

struct UnreachableData {
    SourceLocation loc;
};
struct VLABoundData {
    SourceLocation loc;
    TypeDescriptor const& type;
};

struct FloatCastOverflowDataV2 {
    SourceLocation loc;
    TypeDescriptor const& from_type;
    TypeDescriptor const& to_type;
};

struct InvalidBuiltinData {
    SourceLocation loc;
    unsigned char kind;
};

struct NonNullArgData {
    SourceLocation loc;
    SourceLocation attr_loc;
    int arg_index;
};

struct PointerOverflowData {
    SourceLocation loc;
};

struct DynamicTypeCacheMissData {
    SourceLocation loc;
    TypeDescriptor const& type;
    void* type_info;
    unsigned char type_check_kind;
};

} // namespace ubsan

// Full UBSan runtime
// NOLINTNEXTLINE
INTERFACE uintptr_t __ubsan_vptr_type_cache[128] = {};
INTERFACE void __ubsan_handle_dynamic_type_cache_miss([[maybe_unused]] ubsan::DynamicTypeCacheMissData* data,
                                                      [[maybe_unused]] ubsan::ValueHandle pointer,
                                                      [[maybe_unused]] ubsan::ValueHandle cache) {
    // I don't think this is necessarily a problem?
}
INTERFACE void __ubsan_handle_pointer_overflow([[maybe_unused]] ubsan::PointerOverflowData* data,
                                               [[maybe_unused]] ubsan::ValueHandle base,
                                               [[maybe_unused]] ubsan::ValueHandle result) {
    HandleUbsanError("pointer-overflow");
}
INTERFACE void __ubsan_handle_nonnull_arg([[maybe_unused]] ubsan::NonNullArgData* data) {
    HandleUbsanError("nonnull-arg: null was passed as an argument when it was explicitly marked as non-null");
}
INTERFACE void __ubsan_handle_float_cast_overflow([[maybe_unused]] ubsan::FloatCastOverflowDataV2* data,
                                                  [[maybe_unused]] ubsan::ValueHandle from) {
    HandleUbsanError("f32-cast-overflow");
}
INTERFACE void __ubsan_handle_invalid_builtin([[maybe_unused]] ubsan::InvalidBuiltinData* data) {
    HandleUbsanError("invalid-builtin");
}

INTERFACE void __ubsan_handle_add_overflow([[maybe_unused]] ubsan::OverflowData* data,
                                           [[maybe_unused]] ubsan::ValueHandle lhs,
                                           [[maybe_unused]] ubsan::ValueHandle rhs) {
    HandleUbsanError("add-overflow");
}
INTERFACE void __ubsan_handle_sub_overflow([[maybe_unused]] ubsan::OverflowData* data,
                                           [[maybe_unused]] ubsan::ValueHandle lhs,
                                           [[maybe_unused]] ubsan::ValueHandle rhs) {
    HandleUbsanError("sub-overflow");
}
INTERFACE void __ubsan_handle_mul_overflow([[maybe_unused]] ubsan::OverflowData* data,
                                           [[maybe_unused]] ubsan::ValueHandle lhs,
                                           [[maybe_unused]] ubsan::ValueHandle rhs) {
    HandleUbsanError("mul-overflow");
}
INTERFACE void __ubsan_handle_negate_overflow([[maybe_unused]] ubsan::OverflowData* data,
                                              [[maybe_unused]] ubsan::ValueHandle old_val) {
    HandleUbsanError("negate-overflow");
}
INTERFACE void __ubsan_handle_divrem_overflow([[maybe_unused]] ubsan::OverflowData* data,
                                              [[maybe_unused]] ubsan::ValueHandle lhs,
                                              [[maybe_unused]] ubsan::ValueHandle rhs) {
    HandleUbsanError("divrem-overflow");
}
INTERFACE void __ubsan_handle_type_mismatch_v1([[maybe_unused]] ubsan::TypeMismatchData* data,
                                               [[maybe_unused]] ubsan::ValueHandle pointer) {
    if (pointer == 0)
        HandleUbsanError("Null pointer access");
    else if (data->log_alignment != 0 && IsAligned((void*)pointer, data->log_alignment))
        HandleUbsanError("Unaligned memory access");
    else
        HandleUbsanError("Type mismatch: insufficient size");
}
INTERFACE void __ubsan_handle_out_of_bounds([[maybe_unused]] ubsan::OutOfBoundsData* data,
                                            [[maybe_unused]] ubsan::ValueHandle index) {
    HandleUbsanError("out-of-bounds");
}
INTERFACE void __ubsan_handle_shift_out_of_bounds([[maybe_unused]] ubsan::ShiftOutOfBoundsData* _data,
                                                  [[maybe_unused]] ubsan::ValueHandle lhs,
                                                  [[maybe_unused]] ubsan::ValueHandle rhs) {
    HandleUbsanError("shift-out-of-bounds");
}
INTERFACE void __ubsan_handle_builtin_unreachable([[maybe_unused]] void* _data) {
    HandleUbsanError("builtin-unreachable");
}
INTERFACE void __ubsan_handle_load_invalid_value([[maybe_unused]] void* _data, [[maybe_unused]] void* val) {
    HandleUbsanError("load-invalid-value");
}
INTERFACE void __ubsan_handle_alignment_assumption([[maybe_unused]] void* _data,
                                                   [[maybe_unused]] unsigned long ptr,
                                                   [[maybe_unused]] unsigned long align,
                                                   [[maybe_unused]] unsigned long offset) {
    HandleUbsanError("alignment-assumption");
}
INTERFACE void __ubsan_handle_missing_return([[maybe_unused]] void* _data) {
    HandleUbsanError("missing-return");
}

// Minimal UBSan runtime
#define MINIMAL_HANDLER_RECOVER(name, msg)                                                                   \
    INTERFACE void __ubsan_handle_##name##_minimal() { HandleUbsanError(msg); }

#define MINIMAL_HANDLER_NORECOVER(name, msg)                                                                 \
    INTERFACE void __ubsan_handle_##name##_minimal_abort() {                                                 \
        HandleUbsanError(msg);                                                                               \
        __builtin_abort();                                                                                   \
    }

#define MINIMAL_HANDLER(name, msg)                                                                           \
    MINIMAL_HANDLER_RECOVER(name, msg)                                                                       \
    MINIMAL_HANDLER_NORECOVER(name, msg)

MINIMAL_HANDLER(type_mismatch, "type-mismatch")
MINIMAL_HANDLER(alignment_assumption, "alignment-assumption")
MINIMAL_HANDLER(add_overflow, "add-overflow")
MINIMAL_HANDLER(sub_overflow, "sub-overflow")
MINIMAL_HANDLER(mul_overflow, "mul-overflow")
MINIMAL_HANDLER(negate_overflow, "negate-overflow")
MINIMAL_HANDLER(divrem_overflow, "divrem-overflow")
MINIMAL_HANDLER(shift_out_of_bounds, "shift-out-of-bounds")
MINIMAL_HANDLER(out_of_bounds, "out-of-bounds")
MINIMAL_HANDLER_RECOVER(builtin_unreachable, "builtin-unreachable")
MINIMAL_HANDLER_RECOVER(missing_return, "missing-return")
MINIMAL_HANDLER(vla_bound_not_positive, "vla-bound-not-positive")
MINIMAL_HANDLER(float_cast_overflow, "f32-cast-overflow")
MINIMAL_HANDLER(load_invalid_value, "load-invalid-value")
MINIMAL_HANDLER(invalid_builtin, "invalid-builtin")
MINIMAL_HANDLER(invalid_objc_cast, "invalid-objc-cast")
MINIMAL_HANDLER(function_type_mismatch, "function-type-mismatch")
MINIMAL_HANDLER(implicit_conversion, "implicit-conversion")
MINIMAL_HANDLER(nonnull_arg, "nonnull-arg")
MINIMAL_HANDLER(nonnull_return, "nonnull-return")
MINIMAL_HANDLER(nullability_arg, "nullability-arg")
MINIMAL_HANDLER(nullability_return, "nullability-return")
MINIMAL_HANDLER(pointer_overflow, "pointer-overflow")
MINIMAL_HANDLER(cfi_check_fail, "cfi-check-fail")

// End of LLVM-based code
// =============================================================================================================

void DumpInfoAboutUBSan(StdStream stream) {
    auto _ = StdPrint(stream, "Possibly undefined behaviour found with UBSan. UBSan checks include:\n");
    constexpr String k_ubsan_checks[] = {
        "  type-mismatch\n",       "  alignment-assumption\n",   "  add-overflow\n",
        "  sub-overflow\n",        "  mul-overflow\n",           "  negate-overflow\n",
        "  divrem-overflow\n",     "  shift-out-of-bounds\n",    "  out-of-bounds\n",
        "  builtin-unreachable\n", "  missing-return\n",         "  vla-bound-not-positive\n",
        "  f32-cast-overflow\n",   "  load-invalid-value\n",     "  invalid-builtin\n",
        "  invalid-objc-cast\n",   "  function-type-mismatch\n", "  implicit-conversion\n",
        "  nonnull-arg\n",         "  nonnull-return\n",         "  nullability-arg\n",
        "  nullability-return\n",  "  pointer-overflow\n",       "  cfi-check-fail\n",
    };
    for (auto check : k_ubsan_checks)
        auto _ = StdPrint(stream, check);
}

ErrorCodeCategory const& StacktraceErrorCodeType() {
    static constexpr ErrorCodeCategory k_cat = {
        .category_id = "ST",
        .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
            String str {};
            switch ((StacktraceError)code.code) {
                case StacktraceError::NotInitialised: str = "Not initialised"; break;
            }
            return writer.WriteChars(str);
        }};
    return k_cat;
}

struct BacktraceState {
    Optional<DynamicArrayBounded<char, 256>> failed_init_error {};
    backtrace_state* state = nullptr;
};

__attribute__((visibility("hidden"))) alignas(BacktraceState) static u8
    g_backtrace_state_storage[sizeof(BacktraceState)] {};
__attribute__((visibility("hidden"))) static Atomic<BacktraceState*> g_backtrace_state {};
__attribute__((visibility("hidden"))) static CountedInitFlag g_init {};
__attribute__((
    visibility("hidden"))) static MutableString g_current_binary_path {}; // includes null terminator

static Allocator& StateAllocator() { return PageAllocator::Instance(); }

Optional<String> InitStacktraceState(Optional<String> current_binary_path) {
    CountedInit(g_init, [current_binary_path] {
        if (current_binary_path) {
            ASSERT(current_binary_path->size);
            ASSERT(path::IsAbsolute(*current_binary_path));
            ASSERT(IsValidUtf8(*current_binary_path));
            g_current_binary_path =
                StateAllocator().AllocateExactSizeUninitialised<char>(current_binary_path->size + 1);
            usize pos = 0;
            WriteAndIncrement(pos, g_current_binary_path, *current_binary_path);
            WriteAndIncrement(pos, g_current_binary_path, '\0');
        } else {
            auto const o = CurrentBinaryPath(StateAllocator());
            if (o.HasError()) {
                auto state = PLACEMENT_NEW(g_backtrace_state_storage) BacktraceState;
                state->failed_init_error.Emplace();
                fmt::Assign(*state->failed_init_error,
                            "Stacktrace error: failed to get executable path: {}",
                            o.Error());
                g_backtrace_state.Store(state, StoreMemoryOrder::Release);
                return;
            }
            g_current_binary_path = o.Value();
            auto p = DynamicArray<char>::FromOwnedSpan(g_current_binary_path, StateAllocator());
            dyn::Append(p, '\0');
            g_current_binary_path = p.ToOwnedSpan();
        }

        auto state = PLACEMENT_NEW(g_backtrace_state_storage) BacktraceState;
        state->state = backtrace_create_state(
            g_current_binary_path.data, // filename must be a permanent, null-terminated buffer
            true,
            [](void* user_data, char const* msg, int errnum) {
                auto& self = *(BacktraceState*)user_data;
                self.failed_init_error.Emplace();
                if (errnum > 0)
                    fmt::Assign(*self.failed_init_error, "Stacktrace error ({}): {}", errnum, msg);
                else
                    fmt::Assign(*self.failed_init_error,
                                "Stacktrace error: no debug info is available ({}): {}",
                                errnum,
                                msg);
            },
            state);

        if constexpr (IS_WINDOWS) {
            // NOTE(Sam): Feb, 2024. libbacktrace initialises the state in the first call to one of its
            // functions. This is meant to be done in a thread-safe manner, but I'm finding it's not working
            // on Windows there is a crash due to reading null filename_fn. I've walked through the code and
            // can't obviously find the cause. This is a workaround to force the initialisation to happen on a
            // single thread.
            backtrace_pcinfo(
                state->state,
                (uintptr)__builtin_return_address(0),
                [](void*, uintptr_t, char const*, int, char const*) -> int { return 0; },
                [](void*, char const*, int) -> void {},
                nullptr);
        }

        g_backtrace_state.Store(state, StoreMemoryOrder::Release);
    });

    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (state->failed_init_error) {
        LogDebug(k_global_log_module, "Failed to initialise backtrace state: {}", *state->failed_init_error);
        return *state->failed_init_error;
    }
    return k_nullopt;
}

void ShutdownStacktraceState() {
    CountedDeinit(g_init, [] {
        if (auto state = g_backtrace_state.Exchange(nullptr, RmwMemoryOrder::AcquireRelease)) {
            if (g_current_binary_path.size) StateAllocator().Free(g_current_binary_path.ToByteSpan());
            state->~BacktraceState();
        }
    });
}

Optional<StacktraceStack> CurrentStacktrace(int skip_frames) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);

    if (!state || state->failed_init_error) return k_nullopt;

    StacktraceStack result;
    backtrace_simple(
        state->state,
        skip_frames,
        [](void* data, uintptr_t pc) -> int {
            auto& result = *(StacktraceStack*)data;
            dyn::Append(result, pc);
            return 0;
        },
        []([[maybe_unused]] void* data, [[maybe_unused]] char const* msg, [[maybe_unused]] int errnum) {
            // error callback. I think we just ignore errors; they will become known when the stacktrace is
            // printed
        },
        &result);

    return result;
}

struct StacktraceContext {
    StacktraceOptions options;
    Writer writer;
    u32 line_num = 1;
    ErrorCodeOr<void> return_value;
};

static int HandleStacktraceLine(void* data,
                                [[maybe_unused]] uintptr_t program_counter,
                                char const* filename,
                                int lineno,
                                char const* function) {
    auto& ctx = *(StacktraceContext*)data;

    String function_name = {};
    char* demangled_func = nullptr;
    DEFER { free(demangled_func); };
    if (function && ctx.options.demangle) {
        int status;
        demangled_func = abi::__cxa_demangle(function, nullptr, nullptr, &status);
        if (status == 0) function_name = FromNullTerminated(demangled_func);
    }
    if (!function_name.size) function_name = function ? FromNullTerminated(function) : ""_s;

    ctx.return_value = fmt::FormatToWriter(ctx.writer,
                                           "[{}] {}{}{}:{}: {}\n",
                                           ctx.line_num++,
                                           ctx.options.ansi_colours ? ANSI_COLOUR_SET_FOREGROUND_BLUE : ""_s,
                                           filename ? FromNullTerminated(filename) : "unknown-file"_s,
                                           ctx.options.ansi_colours ? ANSI_COLOUR_RESET : ""_s,
                                           lineno,
                                           function_name);
    return 0;
}

static void HandleStacktraceError(void* data, char const* message, [[maybe_unused]] int errnum) {
    auto& ctx = *(StacktraceContext*)data;

    auto _ = fmt::FormatToWriter(ctx.writer,
                                 "[{}] Stacktrace error: {}\n",
                                 ctx.line_num++,
                                 FromNullTerminated(message));
}

ErrorCodeOr<void> WriteStacktrace(StacktraceStack const& stack, Writer writer, StacktraceOptions options) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (!state) return ErrorCode {StacktraceError::NotInitialised};

    if (state->failed_init_error) return fmt::FormatToWriter(writer, "{}", *state->failed_init_error);

    StacktraceContext ctx {.options = options, .writer = writer};
    for (auto const pc : stack) {
        backtrace_pcinfo(state->state, pc, HandleStacktraceLine, HandleStacktraceError, &ctx);
        if (ctx.return_value.HasError()) return ctx.return_value;
    }

    return k_success;
}

ErrorCodeOr<void> WriteCurrentStacktrace(Writer writer, StacktraceOptions options, int skip_frames) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (!state) return ErrorCode {StacktraceError::NotInitialised};
    if (state->failed_init_error) return fmt::FormatToWriter(writer, "{}", *state->failed_init_error);

    StacktraceContext ctx {.options = options, .writer = writer};
    backtrace_full(state->state, skip_frames, HandleStacktraceLine, HandleStacktraceError, &ctx);
    return ctx.return_value;
}

ErrorCodeOr<void> WriteInfoForProgramCounter(uintptr_t pc, Writer writer, StacktraceOptions options) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (!state) return ErrorCode {StacktraceError::NotInitialised};
    if (state->failed_init_error) return fmt::FormatToWriter(writer, "{}", *state->failed_init_error);

    StacktraceContext ctx {.options = options, .writer = writer};
    backtrace_pcinfo(state->state, pc, HandleStacktraceLine, HandleStacktraceError, &ctx);
    return ctx.return_value;
}

MutableString StacktraceString(StacktraceStack const& stack, Allocator& a, StacktraceOptions options) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (!state) return a.Clone("Stacktrace error: not initialised"_s);
    if (state->failed_init_error) return a.Clone(*state->failed_init_error);

    DynamicArray<char> result {a};
    StacktraceContext ctx {.options = options, .writer = dyn::WriterFor(result)};
    for (auto const pc : stack)
        backtrace_pcinfo(state->state, pc, HandleStacktraceLine, HandleStacktraceError, &ctx);

    return result.ToOwnedSpan();
}

MutableString CurrentStacktraceString(Allocator& a, StacktraceOptions options, int skip_frames) {
    DynamicArray<char> result {a};
    auto _ = WriteCurrentStacktrace(dyn::WriterFor(result), options, skip_frames);
    return result.ToOwnedSpan();
}

void StacktraceToCallback(StacktraceStack const& stack,
                          FunctionRef<void(FrameInfo const&)> callback,
                          StacktraceOptions options) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (!state || state->failed_init_error) return;

    using CallbackType = decltype(callback);

    struct Context {
        CallbackType& callback;
        StacktraceOptions const& options;
    };
    Context context {callback, options};

    for (auto const pc : stack)
        backtrace_pcinfo(
            state->state,
            pc,
            [](void* data,
               [[maybe_unused]] uintptr_t program_counter,
               char const* filename,
               int lineno,
               char const* function) {
                auto& ctx = *(Context*)data;

                String function_name = {};
                char* demangled_func = nullptr;
                DEFER { free(demangled_func); };
                if (function && ctx.options.demangle) {
                    int status;
                    demangled_func = abi::__cxa_demangle(function, nullptr, nullptr, &status);
                    if (status == 0) function_name = FromNullTerminated(demangled_func);
                }
                if (!function_name.size) function_name = function ? FromNullTerminated(function) : ""_s;

                ctx.callback({function_name, filename ? FromNullTerminated(filename) : ""_s, lineno});

                return 0;
            },
            [](void*, char const*, int) {},
            &context);
}

void CurrentStacktraceToCallback(FunctionRef<void(FrameInfo const&)> callback,
                                 StacktraceOptions options,
                                 int skip_frames) {
    auto stack = CurrentStacktrace(skip_frames);
    if (stack) StacktraceToCallback(*stack, callback, options);
}

ErrorCodeOr<void> PrintCurrentStacktrace(StdStream stream, StacktraceOptions options, int skip_frames) {
    return WriteCurrentStacktrace(StdWriter(stream), options, skip_frames);
}
