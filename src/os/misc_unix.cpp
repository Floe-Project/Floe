// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h> // strerror
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/debug/debug.hpp"

#include "time.h"

void* AllocatePages(usize bytes) {
    auto p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    TracyAlloc(p, bytes);
    if (p == MAP_FAILED) return nullptr;
    return p;
}

void FreePages(void* ptr, usize bytes) {
    TracyFree(ptr);
    munmap(ptr, bytes);
}

int CurrentProcessId() { return getpid(); }

void TryShrinkPages(void* ptr, usize old_size, usize new_size) {
    TracyFree(ptr);
    auto const page_size = GetSystemStats().page_size;
    auto const current_num_pages = old_size / page_size;
    auto const new_num_pages = (new_size == 0) ? 0 : ((new_size / page_size) + 1);
    if (current_num_pages != new_num_pages) {
        auto const num_pages = current_num_pages - new_num_pages;
        Span<u8> const unused_pages {(u8*)ptr + new_num_pages * page_size, page_size * num_pages};
        ASSERT(ContainsPointer(unused_pages, unused_pages.data));
        ASSERT(ContainsPointer(unused_pages, &Last(unused_pages)));

        auto result = munmap(unused_pages.data, unused_pages.size);
        ASSERT(result == 0, "munmap failed");
    }
    TracyAlloc(ptr, new_size);
}

void StdPrint(StdStream stream, String str) {
    auto const num_written = write(({
                                       int f = STDOUT_FILENO;
                                       switch (stream) {
                                           case StdStream::Out: f = STDOUT_FILENO; break;
                                           case StdStream::Err: f = STDERR_FILENO; break;
                                       }
                                       f;
                                   }),
                                   str.data,
                                   str.size);
    if (num_written < 0) {
        // error
    }
}

TimePoint TimePoint::Now() {
#if __APPLE__
    return TimePoint((s64)clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW));
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return TimePoint((s64)ts.tv_sec * (s64)1e+9 + (s64)ts.tv_nsec);
#endif
}

f64 operator-(TimePoint lhs, TimePoint rhs) {
    auto const nanoseconds = lhs.m_time - rhs.m_time;
    return (f64)nanoseconds / 1e+9;
}

TimePoint operator+(TimePoint t, f64 s) { return TimePoint(t.m_time + (s64)SecondsToNanoseconds(s)); }

static constexpr auto k_signals = Array {
    SIGABRT, // "abort", abnormal termination.
    SIGFPE, // floating point exception.
    SIGILL, // "illegal", invalid instruction.
    SIGSEGV, // "segmentation violation", invalid memory access.
    SIGBUS, // Bus error (bad memory access)
    SIGPIPE, // Broken pipe
    SIGTRAP, // Trace/breakpoint trap
};

bool g_signals_installed = false;
static struct sigaction g_previous_signal_actions[k_signals.size] {};

#if defined(__has_feature)
#if __has_feature(undefined_behavior_sanitizer)
#define HAS_UBSAN 1
#endif
#endif

#if HAS_UBSAN
constexpr bool k_ubsan = true;
#else
constexpr bool k_ubsan = false;
#endif

static void SignalHandler(int signal_num, siginfo_t* info, void* context) {
    static sig_atomic_t volatile first_call = 1;

    if (first_call) {
        first_call = 0;

#if IS_LINUX
        psiginfo(info, nullptr);
#else
        String message {};
        switch (signal_num) {
            case SIGILL:
                message = "Illegal Instruction";
                switch (info->si_code) {
                    case ILL_ILLOPC: message = "Illegal opcode"; break;
                    case ILL_ILLOPN: message = "Illegal operand"; break;
                    case ILL_ILLADR: message = "Illegal addressing mode"; break;
                    case ILL_ILLTRP: message = "Illegal trap"; break;
                    case ILL_PRVOPC: message = "Privileged opcode"; break;
                    case ILL_PRVREG: message = "Privileged register"; break;
                    case ILL_COPROC: message = "Coprocessor error"; break;
                    case ILL_BADSTK: message = "Internal stack error"; break;
                    default: break;
                }
                break;
            case SIGFPE:
                message = "Floating-point exception";
                switch (info->si_code) {
                    case FPE_INTDIV: message = "Integer divide by zero"; break;
                    case FPE_INTOVF: message = "Integer overflow"; break;
                    case FPE_FLTDIV: message = "Floating-point divide by zero"; break;
                    case FPE_FLTOVF: message = "Floating-point overflow"; break;
                    case FPE_FLTUND: message = "Floating-point underflow"; break;
                    case FPE_FLTRES: message = "Floating-point inexact result"; break;
                    case FPE_FLTINV: message = "Floating-point invalid operation"; break;
                    case FPE_FLTSUB: message = "Subscript out of range"; break;
                    default: break;
                }
                break;
            case SIGSEGV:
                message = "Invalid memory reference";
                switch (info->si_code) {
                    case SEGV_MAPERR: message = "Address not mapped to object"; break;
                    case SEGV_ACCERR: message = "Invalid permissions for mapped object"; break;
                    default: break;
                }
                break;
            case SIGPIPE: message = "Broken pipe"; break;
            case SIGBUS:
                message = "Bus error";
                switch (info->si_code) {
                    case BUS_ADRALN: message = "Invalid address alignment"; break;
                    case BUS_ADRERR: message = "Nonexistent physical address"; break;
                    case BUS_OBJERR: message = "Object-specific hardware error"; break;
                    default: break;
                }
                break;
            case SIGTRAP: message = "Trace/breakpoint"; break;
            case SIGABRT: message = "abort() called"; break;
        }
        ArenaAllocatorWithInlineStorage<200> scratch_arena;
        StdPrint(StdStream::Err,
                 fmt::Format(scratch_arena, "Received signal {} ({})\n", signal_num, message));
#endif

        {
            bool possibly_ubsan_error = false;
            if constexpr (k_arch == Arch::Aarch64) {
                if (signal_num == SIGTRAP && info && info->si_addr) {
                    uint32_t insn;
                    CopyMemory(&insn, info->si_addr, sizeof(insn));
                    if ((insn & 0xffe0001f) == 0xd4200000 && ((insn >> 13) & 255) == 'U')
                        possibly_ubsan_error = true;
                }
            } else if constexpr (k_arch == Arch::X86_64) {
                if (signal_num == SIGILL && info && info->si_code == ILL_ILLOPN && info->si_addr) {
                    auto* pc = reinterpret_cast<unsigned char const*>(info->si_addr);
                    if (pc[0] == 0x67 && pc[1] == 0x0f && pc[2] == 0xb9 && pc[3] == 0x40)
                        possibly_ubsan_error = true;
                }
            }
            if (signal_num == SIGTRAP && k_ubsan) possibly_ubsan_error = true;

            if (possibly_ubsan_error) DumpInfoAboutUBSanToStderr();
        }

        DumpCurrentStackTraceToStderr();

        for (auto [index, s] : Enumerate(k_signals)) {
            if (s == signal_num) {
                auto& prev_action = g_previous_signal_actions[index];

                enum class Type { HandlerFunction, HandlerWithInfoFunction };

                using HandlerFunction = TaggedUnion<
                    Type,
                    TypeAndTag<decltype(prev_action.sa_handler), Type::HandlerFunction>,
                    TypeAndTag<decltype(prev_action.sa_sigaction), Type::HandlerWithInfoFunction>>;

                auto const previous_handler_function = ({
                    HandlerFunction h {decltype(prev_action.sa_handler) {}};
                    if (prev_action.sa_flags & SA_SIGINFO)
                        h = prev_action.sa_sigaction;
                    else
                        h = prev_action.sa_handler;
                    h;
                });

                switch (previous_handler_function.tag) {
                    case Type::HandlerFunction: {
                        auto h = previous_handler_function.GetFromTag<Type::HandlerFunction>();
                        if (h == nullptr) {
                            StdPrint(StdStream::Err, "Previous signal handler is null\n");
                            _exit(EXIT_FAILURE);
                        } else if (*h == SIG_DFL) {
                            // If the previous is the default action, we set the default active again and
                            // raise it.
                            StdPrint(StdStream::Err, "Calling default signal handler\n");
                            signal(signal_num, SIG_DFL);
                            raise(signal_num);
                        } else if (*h == SIG_IGN) {
                            _exit(EXIT_FAILURE);
                            return;
                        } else if (*h == SIG_ERR) {
                            StdPrint(StdStream::Err, "Error in signal handler\n");
                            _exit(EXIT_FAILURE);
                            return;
                        }
                        break;
                    }
                    case Type::HandlerWithInfoFunction: {
                        auto h = previous_handler_function.GetFromTag<Type::HandlerWithInfoFunction>();
                        if (h == nullptr) {
                            StdPrint(StdStream::Err, "Previous info signal handler is null\n");
                            _exit(EXIT_FAILURE);
                        }
                        break;
                    }
                }

                if ((prev_action.sa_flags & SA_NODEFER) == 0) sigaddset(&(prev_action.sa_mask), signal_num);
                if ((prev_action.sa_flags & (int)SA_RESETHAND) != 0) prev_action.sa_handler = SIG_DFL;

                sigset_t oset;
                sigemptyset(&oset);
                pthread_sigmask(SIG_SETMASK, &(prev_action.sa_mask), &oset);

                switch (previous_handler_function.tag) {
                    case Type::HandlerFunction: {
                        StdPrint(StdStream::Err, "Calling previous signal handler\n");
                        auto h = previous_handler_function.GetFromTag<Type::HandlerFunction>();
                        (*h)(signal_num);
                        break;
                    }
                    case Type::HandlerWithInfoFunction: {
                        StdPrint(StdStream::Err, "Calling previous info signal handler\n");
                        auto h = previous_handler_function.GetFromTag<Type::HandlerWithInfoFunction>();
                        (*h)(signal_num, info, context);
                        break;
                    }
                }

                pthread_sigmask(SIG_SETMASK, &oset, nullptr);

                break;
            }
        }
    }

    // because of SA_RESETHAND, the signal handler is reset to the default handler
    raise(signal_num);
    _exit(EXIT_FAILURE);
}

void StartupCrashHandler() {
    if (g_signals_installed) return;
    g_signals_installed = true;

    for (auto [index, signal] : Enumerate(k_signals)) {
        struct sigaction action {};
        action.sa_flags =
            (int)(SA_SIGINFO | SA_NODEFER | SA_RESETHAND); // NOLINT(readability-redundant-casting)
        sigfillset(&action.sa_mask);
        sigdelset(&action.sa_mask, signal);
        action.sa_sigaction = &SignalHandler;

        int const r = sigaction(signal, &action, &g_previous_signal_actions[index]);
        if (r != 0)
            StdPrint(StdStream::Err,
                     fmt::FormatInline<200>("failed setting signal handler {}, errno({}) {}\n",
                                            signal,
                                            errno,
                                            strerror(errno)));
    }
}

void ShutdownCrashHandler() {
    if (!g_signals_installed) return;
    g_signals_installed = false;

    for (auto [index, signal] : Enumerate(k_signals)) {
        sigaction(signal, &g_previous_signal_actions[index], nullptr);
        g_previous_signal_actions[index] = {};
    }
}
