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
    struct ErrorMessage : ErrorEvent {
        ArenaAllocator arena {Malloc::Instance()};
    };

    Thread thread;
    Atomic<bool> end_thread = false;
    WorkSignaller signaller;
    ThreadsafeQueue<ErrorMessage> messages {Malloc::Instance()};
    ArenaAllocator tag_arena {Malloc::Instance()};
};

namespace detail {

static void BackgroundThread(Worker& worker, Span<Tag const> tags) {
    if constexpr (!k_active) PanicIfReached();

    auto& sentry = *({
        auto s = InitGlobalSentry(ParseDsnOrThrow(k_dsn), tags);
        if (!s) return;
        s;
    });

    ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};

    if (auto const o = ConsumeAndSendErrorFiles(sentry, *LogFolder(), scratch_arena); o.HasError())
        g_log.Error(k_log_module, "Failed to check for crash files: {}", o.Error());

    // start session
    {
        DynamicArray<char> envelope {scratch_arena};
        auto writer = dyn::WriterFor(envelope);
        auto _ = EnvelopeAddSessionUpdate(sentry, writer, SessionStatus::Ok);

        DynamicArray<char> response {scratch_arena};

        auto const o = SendSentryEnvelope(sentry, envelope, dyn::WriterFor(response), true, scratch_arena);
        if (o.HasError())
            StdPrintF(StdStream::Err, "Failed to send Sentry envelope: {}, {}", o.Error(), response);
        else
            StdPrintF(StdStream::Err, "Sent Sentry envelope: {}", response);
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

        auto const end = worker.end_thread.Load(LoadMemoryOrder::Acquire);
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

    auto cloned_tags = tags.Clone(worker.tag_arena, CloneType::Deep);
    worker.thread.Start([&worker, cloned_tags]() { detail::BackgroundThread(worker, cloned_tags); },
                        "sentry",
                        {});
    return true;
}

PUBLIC void RequestThreadEnd(Worker& worker) {
    if constexpr (!k_active) return;
    if (worker.end_thread.Load(LoadMemoryOrder::Acquire)) return;

    worker.end_thread.Store(true, StoreMemoryOrder::Release);
    worker.signaller.Signal();
}

PUBLIC void WaitForThreadEnd(Worker& worker) {
    if constexpr (!k_active) return;

    ASSERT(worker.end_thread.Load(LoadMemoryOrder::Acquire) == true);
    if (worker.thread.Joinable()) worker.thread.Join();

    // it's possible there's still messages in the queue, let's write them to file
    if (auto sentry = GlobalSentry())
        while (auto msg = worker.messages.TryPop())
            auto _ = WriteErrorToFile(*sentry, *msg);
}

// thread-safe
// Create a message and then fill in the fields, allocating using the message's arena.
// Must not be called after WaitForThreadEnd()
PUBLIC void SendErrorMessage(Worker& worker, Worker::ErrorMessage&& msg) {
    if constexpr (!k_active) return;

    if (!worker.end_thread.Load(LoadMemoryOrder::Acquire)) {
        worker.messages.Push(Move(msg));
        worker.signaller.Signal();
    } else {
        // we're shutting down, write the message to file
        if (auto sentry = GlobalSentry()) auto _ = WriteErrorToFile(*sentry, msg);
    }
}

// not thread-safe, call once at the start of the program
Worker* InitGlobalWorker(Span<Tag const> tags);

// thread-safe, guaranteed to be valid if InitGlobalWorker() has been called
Worker* GlobalWorker();

PUBLIC void SendErrorMessage(Worker::ErrorMessage&& msg) {
    // Option 1: send the message to the worker thread
    if (auto w = GlobalWorker()) {
        SendErrorMessage(*w, Move(msg));
        return;
    }

    // Option 2: write the message to file
    bool written_to_file = false;
    if (auto const crash_folder = LogFolder()) {
        ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};
        Sentry fallback_sentry_instance {};
        auto sentry = sentry::GlobalSentry();
        if (!sentry) {
            // we've crashed without there being rich context available, but we can still generate a barebones
            // crash report
            sentry::InitBarebonesSentry(fallback_sentry_instance);
            sentry = &fallback_sentry_instance;
        }

        InitBarebonesSentry(*sentry);
        if (WriteErrorToFile(*sentry, msg).Succeeded()) written_to_file = true;
    }

    // Option 3: write the message to stderr
    if (!written_to_file) {
        auto writer = StdWriter(StdStream::Err);
        auto _ = fmt::FormatToWriter(writer,
                                     "\n" ANSI_COLOUR_SET_FOREGROUND_RED "{}" ANSI_COLOUR_RESET "\n",
                                     msg.message);
        if (msg.stacktrace) {
            auto _ = WriteStacktrace(*msg.stacktrace,
                                     writer,
                                     {
                                         .ansi_colours = true,
                                         .demangle = true,
                                     });
        }
        auto _ = writer.WriteChar('\n');
    }
}

} // namespace sentry
