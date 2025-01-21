// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "sentry/sentry.hpp"

// Higher-level API building on top of BeginCrashDetection

static Span<char> g_crash_folder_path {};

PUBLIC void FloeBeginCrashDetection() {
    g_crash_folder_path = FloeKnownDirectory(PageAllocator::Instance(),
                                             FloeKnownDirectoryType::Logs,
                                             k_nullopt,
                                             {.create = true});

    BeginCrashDetection([](String crash_message) {
        ArenaAllocatorWithInlineStorage<1000> arena {PageAllocator::Instance()};

        auto sentry = sentry::g_instance.Load(LoadMemoryOrder::Acquire);
        sentry::Sentry fallback_sentry_instance {};
        bool is_barebones = false;
        if (!sentry) {
            is_barebones = true;
            sentry::InitBarebonesSentry(fallback_sentry_instance);
            sentry = &fallback_sentry_instance;
        }
        auto const o = sentry::WriteCrashToFile(*sentry,
                                                is_barebones,
                                                g_crash_folder_path,
                                                CurrentStacktrace(2),
                                                crash_message,
                                                arena);
        // TODO: better error
        if (o.HasError()) auto _ = StdPrint(StdStream::Out, "Failed to write crash to file");
    });
}

PUBLIC void FloeEndCrashDetection() {
    EndCrashDetection();
    PageAllocator::Instance().Free(g_crash_folder_path.ToByteSpan());
}
