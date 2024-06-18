// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/logger/logger.hpp"
#include "utils/thread_extra/thread_pool.hpp"

#include "common/paths.hpp"
#include "plugin.hpp"
#include "presets_folder.hpp"
#include "sample_library_loader.hpp"
#include "settings/settings_file.hpp"

struct CrossInstanceSystems {
    CrossInstanceSystems();
    ~CrossInstanceSystems();

    u64 folder_settings_listener_id;
    ArenaAllocator arena;
    ThreadsafeErrorNotifications error_notifications {};
    Logger& logger;
    FloePaths paths;
    SettingsFile settings;
    ThreadPool thread_pool;
    PresetsListing preset_listing {paths.always_scanned_folders[ToInt(ScanFolderType::Presets)],
                                   error_notifications};
    sample_lib_loader::AvailableLibraries available_libraries;
    sample_lib_loader::LoadingThread sample_library_loader;
};
