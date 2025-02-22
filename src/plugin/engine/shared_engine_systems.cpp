// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shared_engine_systems.hpp"

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common_infrastructure/error_reporting.hpp"

#include "plugin/plugin.hpp"

void SharedEngineSystems::StartPollingThreadIfNeeded() {
    if (polling_running.Load(LoadMemoryOrder::Acquire)) return;
    polling_running.Store(1, StoreMemoryOrder::Release);
    polling_thread.Start(
        [this]() {
            try {
                {
                    ArenaAllocatorWithInlineStorage<2000> scratch_arena {PageAllocator::Instance()};
                    auto const o = CleanupOldLogFilesIfNeeded(scratch_arena);
                    if (o.HasError())
                        LogError(ModuleName::Global, "Failed to cleanup old log files: {}", o.Error());
                }

                while (polling_running.Load(LoadMemoryOrder::Relaxed)) {
                    WaitIfValueIsExpected(polling_running, 1, 1000u);
                    {
                        registered_floe_instances_mutex.Lock();
                        DEFER { registered_floe_instances_mutex.Unlock(); };
                        for (auto index : registered_floe_instances)
                            OnPollThread(index);
                    }
                }
            } catch (PanicException) {
            }
        },
        "polling");
}

SharedEngineSystems::SharedEngineSystems(Span<sentry::Tag const> tags)
    : arena(PageAllocator::Instance(), Kb(4))
    , paths(CreateFloePaths(arena))
    , settings {.arena = PageAllocator::Instance()}
    , sample_library_server(thread_pool,
                            paths.always_scanned_folder[ToInt(ScanFolderType::Libraries)],
                            error_notifications) {
    InitBackgroundErrorReporting(tags);

    settings.on_change = [this](sts::Key const& key, sts::Value const* value) {
        ASSERT(CheckThreadName("main"));

        if (key == sts::key::k_extra_libraries_folder) {
            DynamicArrayBounded<String, k_max_extra_scan_folders> extra_scan_folders;
            for (auto v = value; v; v = v->next) {
                if (extra_scan_folders.size == k_max_extra_scan_folders) break;
                dyn::AppendIfNotAlreadyThere(extra_scan_folders, v->Get<String>());
            }
            sample_lib_server::SetExtraScanFolders(sample_library_server, extra_scan_folders);
        } else if (key == sts::key::k_extra_presets_folder) {
            preset_listing.scanned_folder.needs_rescan.Store(true, StoreMemoryOrder::Relaxed);
        } else if (key == sts::key::k_window_width) {
            registered_floe_instances_mutex.Lock();
            DEFER { registered_floe_instances_mutex.Unlock(); };
            for (auto index : registered_floe_instances)
                RequestGuiResize(index);
        }
        ErrorReportingOnSettingsChange(key, value);
    };

    thread_pool.Init("global", {});

    sts::Init(settings, paths.possible_settings_paths);

    if (auto const value = sts::LookupValues(settings, sts::key::k_extra_libraries_folder)) {
        DynamicArrayBounded<String, k_max_extra_scan_folders> extra_scan_folders;
        for (auto v = &*value; v; v = v->next) {
            if (extra_scan_folders.size == k_max_extra_scan_folders) break;
            dyn::AppendIfNotAlreadyThere(extra_scan_folders, v->Get<String>());
        }
        sample_lib_server::SetExtraScanFolders(sample_library_server, extra_scan_folders);
    }
}

SharedEngineSystems::~SharedEngineSystems() {
    if (polling_running.Load(LoadMemoryOrder::Acquire)) {
        polling_running.Store(0, StoreMemoryOrder::Release);
        WakeWaitingThreads(polling_running, NumWaitingThreads::All);
        polling_thread.Join();
    }

    sts::WriteIfNeeded(settings);
    sts::Deinit(settings);

    ShutdownBackgroundErrorReporting();
}

void SharedEngineSystems::RegisterFloeInstance(FloeInstanceIndex index) {
    registered_floe_instances_mutex.Lock();
    DEFER { registered_floe_instances_mutex.Unlock(); };
    ASSERT(!Contains(registered_floe_instances, index));
    dyn::Append(registered_floe_instances, index);
}

void SharedEngineSystems::UnregisterFloeInstance(FloeInstanceIndex index) {
    registered_floe_instances_mutex.Lock();
    DEFER { registered_floe_instances_mutex.Unlock(); };
    auto const num_removed = dyn::RemoveValueSwapLast(registered_floe_instances, index);
    ASSERT_EQ(num_removed, 1u);
}
