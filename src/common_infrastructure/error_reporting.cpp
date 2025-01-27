// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "error_reporting.hpp"

#include "sentry/sentry_background_queue.hpp"

static Atomic<sentry::BackgroundQueue*> g_queue = nullptr;
alignas(sentry::BackgroundQueue) static u8 g_worker_storage[sizeof(sentry::BackgroundQueue)];

void InitErrorReporting(Span<sentry::Tag const> tags) {
    auto existing = g_queue.Load(LoadMemoryOrder::Acquire);
    if (existing) return;

    WebGlobalInit();

    auto worker = PLACEMENT_NEW(g_worker_storage) sentry::BackgroundQueue {};
    StartThread(*worker, tags);
    g_queue.Store(worker, StoreMemoryOrder::Release);
}

void ReportError(sentry::Error&& error) {
    // Option 1: send the error to the background thread
    if (auto q = g_queue.Load(LoadMemoryOrder::Acquire); q && !PanicOccurred()) {
        ReportError(*q, Move(error));
        return;
    }

    // Option 2: write the message to file directly
    if (WriteErrorToFile(*sentry::SentryOrFallback {}, error).Succeeded()) return;

    // Option 3: write the message to stderr
    auto writer = StdWriter(StdStream::Err);
    auto _ = fmt::FormatToWriter(writer,
                                 "\n" ANSI_COLOUR_SET_FOREGROUND_RED "{}" ANSI_COLOUR_RESET "\n",
                                 error.message);
    if (error.stacktrace) {
        auto _ = WriteStacktrace(*error.stacktrace,
                                 writer,
                                 {
                                     .ansi_colours = true,
                                     .demangle = true,
                                 });
    }
    auto _ = writer.WriteChar('\n');
}

void RequestErrorReportingEnd() {
    auto q = g_queue.Load(LoadMemoryOrder::Acquire);
    ASSERT(q);
    sentry::RequestThreadEnd(*q);
}

void WaitForErrorReportingEnd() {
    auto q = g_queue.Load(LoadMemoryOrder::Acquire);
    ASSERT(q);
    WaitForThreadEnd(*q);

    WebGlobalCleanup();
}
