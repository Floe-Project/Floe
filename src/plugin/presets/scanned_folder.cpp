// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scanned_folder.hpp"

ScannedFolder::ScannedFolder(bool recursive) : recursive(recursive) {}

ScannedFolder::~ScannedFolder() {
    while (async_scans.Load(LoadMemoryOrder::Relaxed) != 0)
        SpinLoopPause();
}

void BeginScan(ScannedFolder& scanned_folder) { scanned_folder.overall_mutex.Lock(); }

void EndScan(ScannedFolder& scanned_folder) {
    scanned_folder.overall_mutex.Unlock();
    scanned_folder.listeners.Call();
}

void ShutdownIfNeeded(ScannedFolder& scanned_folder) {
    ScopedMutexLock const lock(scanned_folder.overall_mutex);
}

bool HandleRescanRequest(ScannedFolder& folder,
                         ThreadPool* thread_pool,
                         RescanMode mode,
                         Span<String const> folders_to_scan,
                         TrivialFixedSizeFunction<16, void(Span<String const>)> const& scan) {
    if (mode != RescanMode::DontRescan) {
        if (folder.needs_rescan.Exchange(false, RmwMemoryOrder::Relaxed)) {
            if (mode == RescanMode::RescanSyncIfNeeded) mode = RescanMode::RescanSync;
            if (mode == RescanMode::RescanAsyncIfNeeded) mode = RescanMode::RescanAsync;
        } else {
            if (mode == RescanMode::RescanSyncIfNeeded) mode = RescanMode::DontRescan;
            if (mode == RescanMode::RescanAsyncIfNeeded) mode = RescanMode::DontRescan;
        }
    }

    switch (mode) {
        case RescanMode::DontRescan: {
            break;
        }
        case RescanMode::RescanSync: {
            folder.async_scans.FetchAdd(1, RmwMemoryOrder::Acquire);
            scan(folders_to_scan);
            folder.async_scans.FetchSub(1, RmwMemoryOrder::Release);
            break;
        }
        case RescanMode::RescanAsync: {
            if (folder.async_scans.FetchAdd(1, RmwMemoryOrder::Acquire) == 0) {
                folder.thread_arena.ResetCursorAndConsolidateRegions();
                ASSERT(thread_pool);
                thread_pool->AddJob(
                    [folders_to_scan = folder.thread_arena.Clone(folders_to_scan, CloneType::Deep),
                     &folder,
                     scan = scan]() {
                        scan(folders_to_scan);
                        folder.async_scans.FetchSub(1, RmwMemoryOrder::Release);
                    });
            } else {
                folder.async_scans.FetchSub(1, RmwMemoryOrder::Release);
            }
            break;
        }
        case RescanMode::RescanSyncIfNeeded:
        case RescanMode::RescanAsyncIfNeeded: {
            PanicIfReached();
            break;
        }
    }

    return folder.async_scans.Load(LoadMemoryOrder::Relaxed) != 0;
}
