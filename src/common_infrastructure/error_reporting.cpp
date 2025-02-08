// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "error_reporting.hpp"

#include "sentry/sentry_background_queue.hpp"

__attribute__((visibility("hidden"))) static CountedInitFlag g_init_flag {};
__attribute__((visibility("hidden"))) static Atomic<sentry::BackgroundQueue*> g_queue = nullptr;
__attribute__((visibility("hidden"))) alignas(sentry::BackgroundQueue) static u8
    g_worker_storage[sizeof(sentry::BackgroundQueue)];
__attribute__((visibility("hidden"))) static MutexThin g_reported_error_ids_mutex = {};
__attribute__((visibility("hidden"))) static DynamicArrayBounded<u64, 48> g_reported_error_ids = {};

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

namespace detail {

bool ErrorSentBefore(u64 error_id) {
    g_reported_error_ids_mutex.Lock();
    DEFER { g_reported_error_ids_mutex.Unlock(); };
    return Contains(g_reported_error_ids, error_id);
}

void SetErrorSent(u64 error_id) {
    g_reported_error_ids_mutex.Lock();
    DEFER { g_reported_error_ids_mutex.Unlock(); };
    if (g_reported_error_ids.size != g_reported_error_ids.Capacity())
        dyn::Append(g_reported_error_ids, error_id);
}

void ReportError(sentry::Error&& error, u64 error_id) {
    SetErrorSent(error_id);

    // For debug purposes, log the error.
    Log(ModuleName::ErrorReporting, LogLevel::Debug, [&error](Writer writer) -> ErrorCodeOr<void> {
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

    // Option 1: enqueue the error for the background thread
    if (auto q = g_queue.Load(LoadMemoryOrder::Acquire); q && !PanicOccurred()) {
        sentry::ReportError(*q, Move(error));
        return;
    }

    // Option 2: write the message to file directly
    if (WriteErrorToFile(*sentry::SentryOrFallback {}, error).Succeeded()) return;
}

} // namespace detail

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
