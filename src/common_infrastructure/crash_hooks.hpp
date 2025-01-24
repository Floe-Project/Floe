// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "sentry/sentry.hpp"

// Higher-level API building on top of BeginCrashDetection

static void
WriteErrorToStdout(String crash_message, Optional<StacktraceStack> const& stacktrace, bool signal_safe) {
    auto writer = StdWriter(StdStream::Out);
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

static sentry::Sentry* GetSentry(sentry::Sentry& fallback) {
    auto sentry = sentry::g_instance.Load(LoadMemoryOrder::Acquire);
    if (!sentry) {
        // we've crashed without there being rich context available, but we can still generate a barebones
        // crash report
        sentry::InitBarebonesSentry(fallback);
        sentry = &fallback;
    }
    return sentry;
}

static ErrorCodeOr<void> WriteErrorToFile(String crash_message, Optional<StacktraceStack> const& stacktrace) {
    auto const crash_folder = CrashFolder();
    if (!crash_folder) {
        auto _ = StdPrint(StdStream::Out, "Crash folder is not set, cannot write crash report\n");
        return ErrorCode {FilesystemError::PathDoesNotExist};
    }

    ArenaAllocatorWithInlineStorage<1000> arena {PageAllocator::Instance()};

    sentry::Sentry fallback_sentry_instance {};
    auto sentry = GetSentry(fallback_sentry_instance);
    return sentry::WriteErrorToFile(*sentry, stacktrace, *crash_folder, crash_message, arena);
}

PUBLIC void CrashHookWriteToStdout(String message) {
    auto const stacktrace = CurrentStacktrace(IS_LINUX || IS_MACOS ? 6 : 3);
    WriteErrorToStdout(message, stacktrace, true);
}

PUBLIC void CrashHookWriteCrashReport(String crash_message) {
    auto const stacktrace = CurrentStacktrace(IS_LINUX || IS_MACOS ? 6 : 3);
    auto _ = WriteErrorToFile(crash_message, stacktrace);
    WriteErrorToStdout(crash_message, stacktrace, true);
}

PUBLIC void PanicHook(char const* message_c_str, SourceLocation loc) {
    ArenaAllocatorWithInlineStorage<2000> arena {PageAllocator::Instance()};

    auto const stacktrace = CurrentStacktrace(2);
    auto const message = fmt::Format(arena, "{}\nAt {}", FromNullTerminated(message_c_str), loc);
    WriteErrorToStdout(message, stacktrace, false);

    {
        sentry::Sentry fallback_sentry_instance {};
        auto sentry = GetSentry(fallback_sentry_instance);

        DynamicArray<char> envelope {arena};
        auto envelope_writer = dyn::WriterFor(envelope);

        auto _ = sentry::EnvelopeAddEvent(*sentry,
                                          envelope_writer,
                                          {
                                              .level = sentry::ErrorEvent::Level::Fatal,
                                              .message = message,
                                              .stacktrace = stacktrace,
                                          },
                                          false);
        auto _ = sentry::EnvelopeAddSessionUpdate(*sentry, envelope_writer, sentry::SessionStatus::Crashed);

        DynamicArray<char> response {arena};
        auto const _ = sentry::SendSentryEnvelope(*sentry, envelope, dyn::WriterFor(response), true, arena);
    }
}
