// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cross_instance_systems.hpp"

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "settings/settings_file.hpp"

CrossInstanceSystems::CrossInstanceSystems()
    : arena(PageAllocator::Instance(), Kb(4))
    , paths(CreateFloePaths(arena))
    , settings(paths)
    , sample_library_server(thread_pool,
                            paths.always_scanned_folders[ToInt(ScanFolderType::Libraries)],
                            error_notifications) {
    folder_settings_listener_id =
        settings.tracking.filesystem_change_listeners.Add([this](ScanFolderType type) {
            switch (type) {
                case ScanFolderType::Presets:
                    preset_listing.scanned_folder.needs_rescan.Store(true, StoreMemoryOrder::Relaxed);
                    break;
                case ScanFolderType::Libraries: {
                    sample_lib_server::SetExtraScanFolders(
                        sample_library_server,
                        settings.settings.filesystem.extra_libraries_scan_folders);
                    break;
                }
                case ScanFolderType::Count: PanicIfReached();
            }
        });
    thread_pool.Init("Global", {});

    InitSettingsFile(settings, paths);

    ASSERT(settings.settings.gui.window_width != 0);

    sample_lib_server::SetExtraScanFolders(sample_library_server,
                                           settings.settings.filesystem.extra_libraries_scan_folders);
}

CrossInstanceSystems::~CrossInstanceSystems() {
    DeinitSettingsFile(settings);

    {
        auto outcome = WriteSettingsFileIfChanged(settings);
        if (outcome.HasError())
            g_log.ErrorLn("global"_cat, "Failed to write settings file: {}", outcome.Error());
    }

    settings.tracking.filesystem_change_listeners.Remove(folder_settings_listener_id);
}
