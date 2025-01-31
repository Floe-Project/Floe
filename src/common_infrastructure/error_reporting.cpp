// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "error_reporting.hpp"

#include "sentry/sentry_background_queue.hpp"

__attribute__((visibility("hidden"))) static CountedInitFlag g_init_flag {};
__attribute__((visibility("hidden"))) static Atomic<sentry::BackgroundQueue*> g_queue = nullptr;
__attribute__((visibility("hidden"))) alignas(sentry::BackgroundQueue) static u8
    g_worker_storage[sizeof(sentry::BackgroundQueue)];

void InitBackgroundErrorReporting(Span<sentry::Tag const> tags) {
    CountedInit(g_init_flag, [&]() {
        auto existing = g_queue.Load(LoadMemoryOrder::Acquire);
        if (existing) return;

        WebGlobalInit();

        auto worker = PLACEMENT_NEW(g_worker_storage) sentry::BackgroundQueue {};
        sentry::StartThread(*worker, tags);
        g_queue.Store(worker, StoreMemoryOrder::Release);
    });
}

void ReportError(sentry::Error&& error) {
    // For debug purposes, log the error.
    if constexpr (!PRODUCTION_BUILD) {
        Log({},
            ({
                LogLevel l;
                switch (error.level) {
                    case sentry::ErrorEvent::Level::Fatal: l = LogLevel::Error; break;
                    case sentry::ErrorEvent::Level::Error: l = LogLevel::Error; break;
                    case sentry::ErrorEvent::Level::Warning: l = LogLevel::Warning; break;
                    case sentry::ErrorEvent::Level::Info: l = LogLevel::Info; break;
                    case sentry::ErrorEvent::Level::Debug: l = LogLevel::Debug; break;
                }
                l;
            }),
            [&error](Writer writer) -> ErrorCodeOr<void> {
                TRY(fmt::FormatToWriter(writer, "Error reported: {}\n", error.message));
                if (error.stacktrace)
                    TRY(WriteStacktrace(*error.stacktrace,
                                        writer,
                                        {
                                            .ansi_colours = false,
                                            .demangle = true,
                                        }));
                return k_success;
            });
    }

    // Option 1: enqueue the error for the background thread
    if (auto q = g_queue.Load(LoadMemoryOrder::Acquire); q && !PanicOccurred()) {
        sentry::ReportError(*q, Move(error));
        return;
    }

    // Option 2: write the message to file directly
    if (WriteErrorToFile(*sentry::SentryOrFallback {}, error).Succeeded()) return;
}

void ShutdownBackgroundErrorReporting() {
    CountedDeinit(g_init_flag, [&]() {
        LogDebug({}, "Shutting down background error reporting");
        auto q = g_queue.Load(LoadMemoryOrder::Acquire);
        ASSERT(q);
        sentry::RequestThreadEnd(*q);
        sentry::WaitForThreadEnd(*q);

        WebGlobalCleanup();
    });
}
