// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/thread_extra/thread_pool.hpp"

#include "common/paths.hpp"
#include "presets_folder.hpp"
#include "sample_library_server.hpp"
#include "settings/settings_file.hpp"

struct CrossInstanceSystems {
    CrossInstanceSystems();
    ~CrossInstanceSystems();

    u64 folder_settings_listener_id;
    ArenaAllocator arena;
    ThreadsafeErrorNotifications error_notifications {};
    FloePaths paths;
    SettingsFile settings;
    ThreadPool thread_pool;
    PresetsListing preset_listing {paths.always_scanned_folders[ToInt(ScanFolderType::Presets)],
                                   error_notifications};
    sample_lib_server::Server sample_library_server;
};
