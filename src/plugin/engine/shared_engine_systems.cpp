// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shared_engine_systems.hpp"

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common_infrastructure/error_reporting.hpp"

#include "plugin/plugin.hpp"
#include "settings/settings.hpp"

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
    , settings {.paths = paths}
    , sample_library_server(thread_pool,
                            paths.always_scanned_folder[ToInt(ScanFolderType::Libraries)],
                            error_notifications) {
    InitBackgroundErrorReporting(tags);

    settings.tracking.on_change = [this](SettingsTracking::Change change) {
        ASSERT(CheckThreadName("main"));

        switch (change.tag) {
            case SettingsTracking::ChangeType::ScanFolder: {
                auto type = change.Get<ScanFolderType>();
                switch (type) {
                    case ScanFolderType::Presets:
                        preset_listing.scanned_folder.needs_rescan.Store(true, StoreMemoryOrder::Relaxed);
                        break;
                    case ScanFolderType::Libraries: {
                        sample_lib_server::SetExtraScanFolders(
                            sample_library_server,
                            settings.settings.filesystem
                                .extra_scan_folders[ToInt(ScanFolderType::Libraries)]);
                        break;
                    }
                    case ScanFolderType::Count: PanicIfReached();
                }
                break;
            }
            case SettingsTracking::ChangeType::WindowSize: {
                registered_floe_instances_mutex.Lock();
                DEFER { registered_floe_instances_mutex.Unlock(); };
                for (auto index : registered_floe_instances)
                    RequestGuiResize(index);
                break;
            }
            case SettingsTracking::ChangeType::OnlineReportingDisabled: {
                if (auto sentry = sentry::GlobalSentry()) {
                    sentry->online_reporting_disabled.Store(settings.settings.online_reporting_disabled,
                                                            StoreMemoryOrder::Relaxed);
                }

                break;
            }
        }
    };

    thread_pool.Init("global", {});

    InitSettingsFile(settings, paths);

    ASSERT(settings.settings.gui.window_width != 0);

    sample_lib_server::SetExtraScanFolders(
        sample_library_server,
        settings.settings.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)]);
}

SharedEngineSystems::~SharedEngineSystems() {
    if (polling_running.Load(LoadMemoryOrder::Acquire)) {
        polling_running.Store(0, StoreMemoryOrder::Release);
        WakeWaitingThreads(polling_running, NumWaitingThreads::All);
        polling_thread.Join();
    }

    settings.tracking.on_change = {};

    DeinitSettingsFile(settings);

    {
        auto outcome = WriteSettingsFileIfChanged(settings);
        if (outcome.HasError())
            LogError(ModuleName::Global, "Failed to write settings file: {}", outcome.Error());
    }

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
