// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "os/threading.hpp"
#include "utils/thread_extra/thread_extra.hpp"

#include "sentry.hpp"

// - In Sentry, events are 'errors' even if they have the level Info.
// - We should allow setting additional tags and breadcrumbs (perhaps a ring buffer) on the sentry object, and
// then add them to the envelope when sending. For example, we should add DAW name info, and add breadcrumbs
// for significant things in the plugin's lifetime, so that if there is an error or crash we have some more
// info.

namespace sentry {

struct SenderThread {
    struct Message {
        ArenaAllocator arena {Malloc::Instance()};
        Sentry::Event event;
    };

    constexpr static u32 k_dont_end = (u32)-1;

    Thread thread;
    Atomic<UnderlyingType<Sentry::Sentry::Session::Status>> end_with_status = (u32)-1;
    WorkSignaller signaller;
    String dsn; // must be static
    ThreadsafeQueue<Message> messages {Malloc::Instance()};
};

namespace detail {

static void BackgroundThread(SenderThread& sender_thread) {
    Sentry sentry;
    if (!InitSentry(sentry, sender_thread.dsn)) return;

    ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};

    // start session
    {
        DynamicArray<char> envelope {scratch_arena};
        auto writer = dyn::WriterFor(envelope);
        auto _ = EnvelopeAddHeader(sentry, writer);
        auto _ = EnvelopeAddSessionUpdate(sentry, writer, {.status = Sentry::Session::Status::Ok});

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
            auto _ = EnvelopeAddEvent(sentry, writer, msg->event);
        }

        bool end = false;
        if (auto const status = sender_thread.end_with_status.Load(LoadMemoryOrder::Relaxed);
            status != SenderThread::k_dont_end) {
            if (!envelope.size) auto _ = EnvelopeAddHeader(sentry, writer);
            auto _ = EnvelopeAddSessionUpdate(sentry, writer, {.status = Sentry::Session::Status(status)});
            end = true;
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
PUBLIC bool StartSenderThread(SenderThread& sender_thread, String dsn) {
    sender_thread.dsn = dsn;
    sender_thread.thread.Start([&sender_thread]() { detail::BackgroundThread(sender_thread); }, "sentry", {});
    return true;
}

PUBLIC void RequestEndSenderThread(SenderThread& sender_thread, Sentry::Session::Status status) {
    sender_thread.end_with_status.Store(ToInt(status), StoreMemoryOrder::Relaxed);
    sender_thread.signaller.Signal();
}

PUBLIC void WaitForSenderThreadEnd(SenderThread& sender_thread) { sender_thread.thread.Join(); }

PUBLIC void SendEvent(SenderThread& sender_thread, SenderThread::Message&& msg) {
    sender_thread.messages.Push(Move(msg));
    sender_thread.signaller.Signal();
}

} // namespace sentry
