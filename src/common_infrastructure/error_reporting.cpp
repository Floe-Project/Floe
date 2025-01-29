// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "error_reporting.hpp"

#include "sentry/sentry_background_queue.hpp"

static Atomic<sentry::BackgroundQueue*> g_queue = nullptr;
alignas(sentry::BackgroundQueue) static u8 g_worker_storage[sizeof(sentry::BackgroundQueue)];

void InitBackgroundErrorReporting(Span<sentry::Tag const> tags) {
    auto existing = g_queue.Load(LoadMemoryOrder::Acquire);
    if (existing) return;

    WebGlobalInit();

    auto worker = PLACEMENT_NEW(g_worker_storage) sentry::BackgroundQueue {};
    sentry::StartThread(*worker, tags);
    g_queue.Store(worker, StoreMemoryOrder::Release);
}

void ReportError(sentry::Error&& error) {
    // Always log the error
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
        error.message);
    if (error.stacktrace) {
        StacktraceToCallback(*error.stacktrace, [](FrameInfo const& frame) {
            LogInfo({}, "  {}:{}: {}", frame.filename, frame.line, frame.function_name);
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

void RequestBackgroundErrorReportingEnd() {
    auto q = g_queue.Load(LoadMemoryOrder::Acquire);
    ASSERT(q);
    sentry::RequestThreadEnd(*q);
}

void WaitForBackgroundErrorReportingEnd() {
    auto q = g_queue.Load(LoadMemoryOrder::Acquire);
    ASSERT(q);
    sentry::WaitForThreadEnd(*q);

    WebGlobalCleanup();
}
