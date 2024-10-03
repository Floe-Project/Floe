// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/thread_extra/thread_pool.hpp"
#include "utils/thread_extra/threadsafe_listener_array.hpp"

#include "rescan_mode.hpp"

struct ScannedFolder {
    ScannedFolder(bool recursive);
    ~ScannedFolder();

    // FileWatcher watcher;
    bool recursive {};
    Atomic<bool> needs_rescan {true};
    Atomic<u32> async_scans {0};
    ThreadsafeListenerArray<TrivialFixedSizeFunction<16, void()>> listeners {};
    Mutex overall_mutex {};
    ArenaAllocator thread_arena {Malloc::Instance()};
};

void BeginScan(ScannedFolder& scanned_folder);
void EndScan(ScannedFolder& scanned_folder);
void ShutdownIfNeeded(ScannedFolder& scanned_folder);
bool HandleRescanRequest(ScannedFolder& folder,
                         ThreadPool* thread_pool,
                         RescanMode mode,
                         Span<String const> folders_to_scan,
                         TrivialFixedSizeFunction<16, void(Span<String const>)> const& scan);
