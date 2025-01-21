// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "os/threading.hpp"
#include "utils/thread_extra/thread_extra.hpp"

#include "sentry.hpp"

namespace sentry {

struct SenderThread {
    // Create a message and then fill in the fields, allocating using the message's arena. Move the message
    // into the queue.
    struct ErrorMessage : ErrorEvent {
        ArenaAllocator arena {Malloc::Instance()};
    };

    constexpr static u32 k_dont_end = (u32)-1;

    Thread thread;
    Atomic<bool> end_thread = false;
    WorkSignaller signaller;
    String dsn; // must outlive this object
    ThreadsafeQueue<ErrorMessage> messages {Malloc::Instance()};
    ArenaAllocator tag_arena {Malloc::Instance()};
    Span<Tag> tags {}; // allocate with tag_arena
};

namespace detail {

static void BackgroundThread(SenderThread& sender_thread) {
    auto& sentry = *({
        auto s = InitGlobalSentry(sender_thread.dsn, sender_thread.tags);
        if (!s) return;
        s;
    });

    ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};

    // start session
    {
        DynamicArray<char> envelope {scratch_arena};
        auto writer = dyn::WriterFor(envelope);
        auto _ = EnvelopeAddHeader(sentry, writer);
        auto _ = EnvelopeAddSessionUpdate(sentry, writer, SessionUpdateType::Start);

        auto const o = SendSentryEnvelope(sentry, envelope, {}, scratch_arena);
        if (o.HasError()) {
            if constexpr (!PRODUCTION_BUILD) __builtin_debugtrap();
        }
    }

    while (true) {
        sender_thread.signaller.WaitUntilSignalledOrSpurious(1000u);
        scratch_arena.ResetCursorAndConsolidateRegions();

        DynamicArray<char> envelope {scratch_arena};
        auto writer = dyn::WriterFor(envelope);

        while (auto msg = sender_thread.messages.TryPop()) {
            if (!envelope.size) auto _ = EnvelopeAddHeader(sentry, writer);
            auto _ = EnvelopeAddEvent(sentry, writer, *msg);
        }

        auto const end = sender_thread.end_thread.Load(LoadMemoryOrder::Relaxed);
        if (end) {
            if (!envelope.size) auto _ = EnvelopeAddHeader(sentry, writer);
            auto _ = EnvelopeAddSessionUpdate(sentry, writer, SessionUpdateType::EndedNormally);
        }

        if (envelope.size) {
            auto const o = SendSentryEnvelope(sentry, envelope, {}, scratch_arena);
            if (o.HasError()) {
                if constexpr (!PRODUCTION_BUILD) __builtin_debugtrap();
            }
        }

        if (end) break;
    }
}

} // namespace detail

// dsn must be static
PUBLIC bool StartSenderThread(SenderThread& sender_thread, String dsn, Span<Tag const> tags) {
    sender_thread.tags = sender_thread.tag_arena.Clone(tags, CloneType::Deep);
    sender_thread.dsn = dsn;
    sender_thread.thread.Start([&sender_thread]() { detail::BackgroundThread(sender_thread); }, "sentry", {});
    return true;
}

PUBLIC void RequestEndSenderThread(SenderThread& sender_thread) {
    ASSERT(sender_thread.end_thread.Load(LoadMemoryOrder::Relaxed) == false);
    sender_thread.end_thread.Store(true, StoreMemoryOrder::Relaxed);
    sender_thread.signaller.Signal();
}

PUBLIC void WaitForSenderThreadEnd(SenderThread& sender_thread) { sender_thread.thread.Join(); }

PUBLIC void SendErrorMessage(SenderThread& sender_thread, SenderThread::ErrorMessage&& msg) {
    sender_thread.messages.Push(Move(msg));
    sender_thread.signaller.Signal();
}

} // namespace sentry
