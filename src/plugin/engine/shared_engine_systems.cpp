// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shared_engine_systems.hpp"

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "plugin/plugin.hpp"
#include "settings/settings_file.hpp"

void SharedEngineSystems::StartPollingThreadIfNeeded() {
    if (polling_running.Load(LoadMemoryOrder::Relaxed)) return;
    polling_running.Store(1, StoreMemoryOrder::Relaxed);
    polling_thread.Start(
        [this]() {
            while (polling_running.Load(LoadMemoryOrder::Relaxed)) {
                WaitIfValueIsExpected(polling_running, 1, 1000u);
                {
                    floe_instances_mutex.Lock();
                    DEFER { floe_instances_mutex.Unlock(); };
                    for (auto plugin : floe_instances)
                        if (plugin) OnPollThread(*plugin);
                }
            }
        },
        "polling");
}

constexpr String k_sentry_dsn =
#ifdef SENTRY_DSN
    SENTRY_DSN;
#else
    "";
#endif

SharedEngineSystems::SharedEngineSystems()
    : arena(PageAllocator::Instance(), Kb(4))
    , paths(CreateFloePaths(arena))
    , settings {.paths = paths}
    , sample_library_server(thread_pool,
                            paths.always_scanned_folder[ToInt(ScanFolderType::Libraries)],
                            error_notifications) {
    if constexpr (k_sentry_dsn.size) sentry::StartSenderThread(sentry_sender_thread, k_sentry_dsn);

    settings.tracking.on_filesystem_change = [this](ScanFolderType type) {
        ASSERT(CheckThreadName("main"));
        switch (type) {
            case ScanFolderType::Presets:
                preset_listing.scanned_folder.needs_rescan.Store(true, StoreMemoryOrder::Relaxed);
                break;
            case ScanFolderType::Libraries: {
                sample_lib_server::SetExtraScanFolders(
                    sample_library_server,
                    settings.settings.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)]);
                break;
            }
            case ScanFolderType::Count: PanicIfReached();
        }
    };

    settings.tracking.on_window_size_change = [this]() {
        ASSERT(CheckThreadName("main"));
        for (auto plugin : floe_instances)
            if (plugin) RequestGuiResize(*plugin);
    };

    thread_pool.Init("global", {});

    InitSettingsFile(settings, paths);

    ASSERT(settings.settings.gui.window_width != 0);

    sample_lib_server::SetExtraScanFolders(
        sample_library_server,
        settings.settings.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)]);
}

SharedEngineSystems::~SharedEngineSystems() {
    if (polling_running.Load(LoadMemoryOrder::Relaxed)) {
        polling_running.Store(0, StoreMemoryOrder::Relaxed);
        WakeWaitingThreads(polling_running, NumWaitingThreads::All);
        polling_thread.Join();
    }

    settings.tracking.on_filesystem_change = {};

    DeinitSettingsFile(settings);

    {
        auto outcome = WriteSettingsFileIfChanged(settings);
        if (outcome.HasError())
            g_log.Error("global"_log_module, "Failed to write settings file: {}", outcome.Error());
    }

    if constexpr (k_sentry_dsn.size) {
        sentry::RequestEndSenderThread(sentry_sender_thread, sentry::Sentry::Session::Status::Exited);
        sentry::WaitForSenderThreadEnd(sentry_sender_thread);
    }
}

void SharedEngineSystems::RegisterFloeInstance(clap_plugin const* plugin, FloeInstanceIndex index) {
    floe_instances_mutex.Lock();
    DEFER { floe_instances_mutex.Unlock(); };
    floe_instances[index] = plugin;
}

void SharedEngineSystems::UnregisterFloeInstance(FloeInstanceIndex index) {
    floe_instances_mutex.Lock();
    DEFER { floe_instances_mutex.Unlock(); };
    floe_instances[index] = nullptr;
}
