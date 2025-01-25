// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "os/threading.hpp"
#include "utils/logger/logger.hpp"
#include "utils/thread_extra/thread_extra.hpp"

#include "sentry.hpp"
#include "sentry_config.hpp"

namespace sentry {

struct Worker {
    // Create a message and then fill in the fields, allocating using the message's arena. Move the message
    // into the queue.
    struct ErrorMessage : ErrorEvent {
        ArenaAllocator arena {Malloc::Instance()};
    };

    constexpr static u32 k_dont_end = (u32)-1;

    Thread thread;
    Atomic<bool> end_thread = false;
    WorkSignaller signaller;
    ThreadsafeQueue<ErrorMessage> messages {Malloc::Instance()};
    ArenaAllocator tag_arena {Malloc::Instance()};
    Span<Tag> tags {}; // allocate with tag_arena
};

namespace detail {

static void BackgroundThread(Worker& worker) {
    if constexpr (!k_active) PanicIfReached();

    auto& sentry = *({
        auto s = InitGlobalSentry(ParseDsnOrThrow(k_dsn), worker.tags);
        if (!s) return;
        s;
    });

    ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};

    auto const crash_folder =
        FloeKnownDirectory(scratch_arena, FloeKnownDirectoryType::Logs, {}, {.create = true});

    if (auto const o = ConsumeAndSendErrorFiles(sentry, crash_folder, scratch_arena); o.HasError())
        g_log.Error(k_log_module, "Failed to check for crash files: {}", o.Error());

    // start session
    {
        DynamicArray<char> envelope {scratch_arena};
        auto writer = dyn::WriterFor(envelope);
        auto _ = EnvelopeAddSessionUpdate(sentry, writer, SessionStatus::Ok);

        DynamicArray<char> response {scratch_arena};

        auto const o = SendSentryEnvelope(sentry, envelope, dyn::WriterFor(response), true, scratch_arena);
        if (o.HasError())
            g_log.Error(k_log_module, "Failed to send Sentry envelope: {}, {}", o.Error(), response);
        else
            g_log.Info(k_log_module, "Sent Sentry envelope: {}", response);
    }

    while (true) {
        worker.signaller.WaitUntilSignalledOrSpurious(1000u);
        scratch_arena.ResetCursorAndConsolidateRegions();

        DynamicArray<char> envelope {scratch_arena};
        auto writer = dyn::WriterFor(envelope);

        bool repeat = false;
        while (auto msg = worker.messages.TryPop()) {
            auto _ = EnvelopeAddEvent(sentry, writer, *msg, false);
            repeat = true;
        }
        if (repeat) worker.signaller.Signal();

        auto const end = worker.end_thread.Load(LoadMemoryOrder::Relaxed);
        if (end) auto _ = EnvelopeAddSessionUpdate(sentry, writer, SessionStatus::EndedNormally);

        if (envelope.size) {
            DynamicArray<char> response {scratch_arena};
            auto const o =
                SendSentryEnvelope(sentry, envelope, dyn::WriterFor(response), true, scratch_arena);
            if (o.HasError())
                g_log.Error(k_log_module, "Failed to send Sentry envelope: {}, {}", o.Error(), response);
            else
                g_log.Info(k_log_module, "Sent Sentry envelope: {}", response);
        }

        if (end) break;
    }
}

} // namespace detail

PUBLIC bool StartThread(Worker& worker, Span<Tag const> tags) {
    if constexpr (!k_active) return true;
    worker.tags = worker.tag_arena.Clone(tags, CloneType::Deep);
    worker.thread.Start([&worker]() { detail::BackgroundThread(worker); }, "sentry", {});
    return true;
}

PUBLIC void RequestThreadEnd(Worker& worker) {
    if constexpr (!k_active) return;
    ASSERT(worker.end_thread.Load(LoadMemoryOrder::Relaxed) == false);
    worker.end_thread.Store(true, StoreMemoryOrder::Relaxed);
    worker.signaller.Signal();
}

PUBLIC void WaitForThreadEnd(Worker& worker) {
    if constexpr (!k_active) return;
    worker.thread.Join();
}

PUBLIC void SendErrorMessage(Worker& worker, Worker::ErrorMessage&& msg) {
    if constexpr (!k_active) return;
    worker.messages.Push(Move(msg));
    worker.signaller.Signal();
}

} // namespace sentry
