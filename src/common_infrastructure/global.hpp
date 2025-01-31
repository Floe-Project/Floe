// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "error_reporting.hpp"

struct GlobalInitOptions {
    Optional<String> current_binary_path {};
    bool init_error_reporting = false;
    bool set_main_thread = false;
};

namespace detail {

static void StartupTracy() {
#ifdef TRACY_ENABLE
    ___tracy_startup_profiler();
#endif
}

static void ShutdownTracy() {
#ifdef TRACY_ENABLE
    ___tracy_shutdown_profiler();
#endif
}

} // namespace detail

PUBLIC void GlobalInit(GlobalInitOptions options) {
    if (options.set_main_thread) SetThreadName("main");

    SetPanicHook([](char const* message_c_str, SourceLocation loc) {
        // We don't have to be signal-safe here.

        ArenaAllocatorWithInlineStorage<2000> arena {PageAllocator::Instance()};

        auto const stacktrace = CurrentStacktrace(4);
        auto const message = fmt::Format(arena, "[panic] {}\nAt {}", FromNullTerminated(message_c_str), loc);

        // Step 1: log the error for easier local debugging.
        LogError(k_global_log_module, "{}", message);
        if (stacktrace) {
            auto stacktrace_str = StacktraceString(*stacktrace,
                                                   arena,
                                                   {
                                                       .ansi_colours = false,
                                                       .demangle = true,
                                                   });
            LogError(k_global_log_module, "\n{}", stacktrace_str);
        }

        // Step 2: send an error report to Sentry.
        {
            sentry::SentryOrFallback sentry {};
            DynamicArray<char> response {arena};
            TRY_OR(sentry::SubmitCrash(*sentry,
                                       stacktrace,
                                       message,
                                       arena,
                                       {
                                           .write_to_file_if_needed = true,
                                           .response = dyn::WriterFor(response),
                                           .request_options =
                                               {
                                                   .timeout_seconds = 3,
                                               },
                                       }),
                   {
                       LogError(sentry::k_log_module,
                                "Failed to submit panic to Sentry: {}, {}",
                                error,
                                response);
                   });
        }
    });

    if (auto const err = InitStacktraceState(options.current_binary_path))
        ReportError(sentry::Error::Level::Warning, "Failed to initialize stacktrace state: {}", *err);

    InitLogger({.destination = ({
                    LogConfig::Destination d;
                    switch (g_final_binary_type) {
                        case FinalBinaryType::Clap:
                        case FinalBinaryType::AuV2:
                        case FinalBinaryType::Vst3: d = LogConfig::Destination::File; break;
                        case FinalBinaryType::Standalone:
                        case FinalBinaryType::Packager:
                        case FinalBinaryType::WindowsInstaller:
                        case FinalBinaryType::Tests: d = LogConfig::Destination::Stderr; break;
                    }
                    d;
                })});

    InitLogFolderIfNeeded();

    detail::StartupTracy();

    // after tracy
    BeginCrashDetection([](String crash_message, Optional<uintptr> program_counter) {
        // This function is async-signal-safe.

        FixedSizeAllocator<4000> allocator {nullptr};

        auto const stacktrace = CurrentStacktrace(IS_LINUX || IS_MACOS ? 6 : 3);
        auto const message = fmt::Format(allocator, "[crash] {}", crash_message);

        // Step 1: dump info to stderr.
        {
            auto writer = StdWriter(StdStream::Err);
            auto _ = fmt::FormatToWriter(writer,
                                         "\n" ANSI_COLOUR_SET_FOREGROUND_RED "{}" ANSI_COLOUR_RESET "\n",
                                         message);
            if (program_counter) {
                auto _ = WriteInfoForProgramCounter(*program_counter,
                                                    writer,
                                                    {
                                                        .ansi_colours = true,
                                                        .demangle = false,
                                                    });
            }
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

        // Step 2: write a crash report to a file in the Sentry format.
        {
            auto const log_folder = LogFolder();
            if (!log_folder) {
                auto _ = StdPrint(StdStream::Err, "Log folder is not set, cannot write crash report\n");
                return;
            }

            sentry::SentryOrFallback sentry {};
            auto _ = sentry::WriteCrashToFile(*sentry, stacktrace, *log_folder, message, allocator);
            if (program_counter)
                auto _ = sentry::WriteCrashToFile(*sentry,
                                                  Array {*program_counter},
                                                  *log_folder,
                                                  "At program counter",
                                                  allocator);
        }
    });

    if (options.init_error_reporting) InitBackgroundErrorReporting({});
}

struct GlobalShutdownOptions {
    bool shutdown_error_reporting = false;
};

PUBLIC void GlobalDeinit(GlobalShutdownOptions options) {
    if (options.shutdown_error_reporting) ShutdownBackgroundErrorReporting();

    EndCrashDetection(); // before tracy

    detail::ShutdownTracy();

    ShutdownStacktraceState();

    ShutdownLogger();
}
