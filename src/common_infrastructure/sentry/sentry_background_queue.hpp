// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "os/threading.hpp"
#include "utils/thread_extra/thread_extra.hpp"

#include "sentry.hpp"

namespace sentry {

struct BackgroundQueue {
    Thread thread;
    Atomic<bool> end_thread = false;
    WorkSignaller signaller;
    ThreadsafeQueue<Error> queue {Malloc::Instance()};
    ArenaAllocator tag_arena {Malloc::Instance()};
};

namespace detail {

static void BackgroundThread(BackgroundQueue& queue, Span<Tag const> tags) {
    auto& sentry = InitGlobalSentry(ParseDsnOrThrow(k_dsn), tags);

    ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};

    InitLogFolderIfNeeded();

    if (auto const o = ConsumeAndSubmitErrorFiles(sentry, *LogFolder(), scratch_arena); o.HasError())
        LogError(k_log_module, "Failed to consume error files: {}", o.Error());

    // start session
    if (!sentry.online_reporting_disabled.Load(LoadMemoryOrder::Relaxed) && k_online_reporting) {
        DynamicArray<char> envelope {scratch_arena};
        auto writer = dyn::WriterFor(envelope);
        auto _ = EnvelopeAddSessionUpdate(sentry, writer, SessionStatus::Ok);

        DynamicArray<char> response {scratch_arena};

        auto const o = SubmitEnvelope(sentry,
                                      envelope,
                                      scratch_arena,
                                      {
                                          .write_to_file_if_needed = true,
                                          .response = dyn::WriterFor(response),
                                          .request_options =
                                              {
                                                  .timeout_seconds = 5,
                                              },
                                      });
        if (o.HasError())
            LogError(k_log_module, "Failed to send Sentry envelope: {}, {}", o.Error(), response);
        else
            LogDebug(k_log_module, "Sent Sentry envelope: {}", response);
    }

    while (true) {
        queue.signaller.WaitUntilSignalledOrSpurious(1000u);
        scratch_arena.ResetCursorAndConsolidateRegions();

        DynamicArray<char> envelope {scratch_arena};
        auto writer = dyn::WriterFor(envelope);

        bool repeat = false;
        while (auto msg = queue.queue.TryPop()) {
            auto _ = EnvelopeAddEvent(sentry, writer, *msg, false);
            repeat = true;
        }
        if (repeat) queue.signaller.Signal();

        auto const end = queue.end_thread.Load(LoadMemoryOrder::Acquire);
        if (end && !sentry.online_reporting_disabled.Load(LoadMemoryOrder::Relaxed) && k_online_reporting)
            auto _ = EnvelopeAddSessionUpdate(sentry, writer, SessionStatus::EndedNormally);

        if (envelope.size) {
            DynamicArray<char> response {scratch_arena};
            auto const o = SubmitEnvelope(sentry,
                                          envelope,
                                          scratch_arena,
                                          {
                                              .write_to_file_if_needed = true,
                                              .response = dyn::WriterFor(response),
                                              .request_options =
                                                  {
                                                      .timeout_seconds = 5,
                                                  },
                                          });
            if (o.HasError())
                LogError(k_log_module, "Failed to send Sentry envelope: {}, {}", o.Error(), response);
            else
                LogDebug(k_log_module, "Sent Sentry envelope: {}", response);
        }

        if (end) break;
    }
}

} // namespace detail

PUBLIC bool StartThread(BackgroundQueue& queue, Span<Tag const> tags) {
    auto cloned_tags = tags.Clone(queue.tag_arena, CloneType::Deep);
    queue.thread.Start(
        [&queue, cloned_tags]() {
            try {
                detail::BackgroundThread(queue, cloned_tags);
            } catch (PanicException) {
            }
        },
        "sentry",
        {});
    return true;
}

PUBLIC void RequestThreadEnd(BackgroundQueue& queue) {
    if (queue.end_thread.Load(LoadMemoryOrder::Acquire)) return;

    queue.end_thread.Store(true, StoreMemoryOrder::Release);
    queue.signaller.Signal();
}

PUBLIC void WaitForThreadEnd(BackgroundQueue& queue) {
    ASSERT(queue.end_thread.Load(LoadMemoryOrder::Acquire) == true);
    if (queue.thread.Joinable()) queue.thread.Join();

    // it's possible there's still messages in the queue, let's write them to file
    SentryOrFallback sentry;
    while (auto const error = queue.queue.TryPop()) {
        LogDebug(k_log_module, "Errors still in queue, writing to file");
        auto _ = WriteErrorToFile(*sentry, *error);
    }
}

// thread-safe, not signal-safe
// Create a message and then fill in the fields, allocating using the message's arena.
// Must not be called after WaitForThreadEnd()
PUBLIC void ReportError(BackgroundQueue& queue, Error&& error) {
    if (!queue.end_thread.Load(LoadMemoryOrder::Acquire)) {
        queue.queue.Push(Move(error));
        queue.signaller.Signal();
    } else {
        // we're shutting down, write the message to file
        SentryOrFallback sentry;
        auto _ = WriteErrorToFile(*sentry, error);
        LogDebug(k_log_module, "Error background thread is shutting down, writing error to file");
    }
}

} // namespace sentry
