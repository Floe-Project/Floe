// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "sentry/sentry.hpp"

// Higher-level API building on top of BeginCrashDetection

namespace detail {

static void
WriteErrorToStderr(String crash_message, Optional<StacktraceStack> const& stacktrace, bool signal_safe) {
    auto writer = StdWriter(StdStream::Err);
    auto _ = fmt::FormatToWriter(writer,
                                 "\n" ANSI_COLOUR_SET_FOREGROUND_RED "{}" ANSI_COLOUR_RESET "\n",
                                 crash_message);
    if (stacktrace) {
        auto _ = WriteStacktrace(*stacktrace,
                                 writer,
                                 {
                                     .ansi_colours = true,
                                     .demangle = !signal_safe,
                                 });
    }
    auto _ = writer.WriteChar('\n');
}

static ErrorCodeOr<void> WriteCrashToFile(String crash_message, Optional<StacktraceStack> const& stacktrace) {
    auto const log_folder = LogFolder();
    if (!log_folder) {
        auto _ = StdPrint(StdStream::Err, "Log folder is not set, cannot write crash report\n");
        return ErrorCode {FilesystemError::PathDoesNotExist};
    }

    ArenaAllocatorWithInlineStorage<1000> arena {PageAllocator::Instance()};

    sentry::SentryOrFallback sentry {};
    return sentry::WriteCrashToFile(*sentry, stacktrace, *log_folder, crash_message, arena);
}

} // namespace detail

PUBLIC void CrashHookWriteToStdout(String message) {
    auto const stacktrace = CurrentStacktrace(IS_LINUX || IS_MACOS ? 6 : 3);
    detail::WriteErrorToStderr(message, stacktrace, true);
}

PUBLIC void CrashHookWriteCrashReport(String crash_message) {
    auto const stacktrace = CurrentStacktrace(IS_LINUX || IS_MACOS ? 6 : 3);
    auto _ = detail::WriteCrashToFile(crash_message, stacktrace);
    detail::WriteErrorToStderr(crash_message, stacktrace, true);
}

PUBLIC void PanicHook(char const* message_c_str, SourceLocation loc) {
    ArenaAllocatorWithInlineStorage<2000> arena {PageAllocator::Instance()};

    auto const stacktrace = CurrentStacktrace(2);
    auto const message = fmt::Format(arena, "{}\nAt {}", FromNullTerminated(message_c_str), loc);

    // stderr
    detail::WriteErrorToStderr(message, stacktrace, false);

    // sentry
    {
        sentry::SentryOrFallback sentry {};
        DynamicArray<char> response {arena};
        TRY_OR(sentry::SubmitCrash(*sentry, stacktrace, message, {}, arena), {
            g_log.Error(sentry::k_log_module, "Failed to submit panic to Sentry: {}, {}", error, response);
        });
    }
}
