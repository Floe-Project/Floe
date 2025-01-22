// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "sentry/sentry.hpp"

// Higher-level API building on top of BeginCrashDetection

static Span<char> g_crash_folder_path {};

PUBLIC void InitCrashFolder() {
    g_crash_folder_path = FloeKnownDirectory(PageAllocator::Instance(),
                                             FloeKnownDirectoryType::Logs,
                                             k_nullopt,
                                             {.create = true});
}

PUBLIC void DeinitCrashFolder() { PageAllocator::Instance().Free(g_crash_folder_path.ToByteSpan()); }

PUBLIC void WriteCrashInfoToStdout(String crash_message, Optional<StacktraceStack> const& stacktrace) {
    auto writer = StdWriter(StdStream::Out);
    auto _ = fmt::FormatToWriter(writer,
                                 "\nA fatal error occured: " ANSI_COLOUR_SET_FOREGROUND_RED
                                 "{}" ANSI_COLOUR_RESET "\n",
                                 crash_message);
    if (stacktrace) {
        auto _ = WriteStacktrace(*stacktrace,
                                 writer,
                                 {
                                     .ansi_colours = true,
                                     .demangle = false,
                                 });
    }
    auto _ = writer.WriteChar('\n');
}

PUBLIC ErrorCodeOr<void> ReportDumpSentryCrashFile(String crash_message,
                                                   Optional<StacktraceStack> const& stacktrace) {
    ArenaAllocatorWithInlineStorage<1000> arena {PageAllocator::Instance()};

    auto sentry = sentry::g_instance.Load(LoadMemoryOrder::Acquire);
    sentry::Sentry fallback_sentry_instance {};
    bool is_barebones = false;
    if (!sentry) {
        // we've crashed without there being rich context available, but we can still generate a barebones
        // crash report
        is_barebones = true;
        sentry::InitBarebonesSentry(fallback_sentry_instance);
        sentry = &fallback_sentry_instance;
    }
    return sentry::WriteCrashToFile(*sentry,
                                    is_barebones,
                                    g_crash_folder_path,
                                    stacktrace,
                                    crash_message,
                                    arena);
}

PUBLIC void CrashHookWriteToStdout(String message) {
    auto const stacktrace = CurrentStacktrace(IS_LINUX || IS_MACOS ? 6 : 3);
    WriteCrashInfoToStdout(message, stacktrace);
}

PUBLIC void CrashHookWriteCrashReport(String crash_message) {
    auto const stacktrace = CurrentStacktrace(IS_LINUX || IS_MACOS ? 6 : 3);
    TRY_OR(ReportDumpSentryCrashFile(crash_message, stacktrace),
           WriteCrashInfoToStdout(crash_message, stacktrace));
}
