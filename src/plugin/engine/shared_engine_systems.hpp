// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/thread_extra/thread_pool.hpp"

#include "common_infrastructure/paths.hpp"

#include "clap/plugin.h"
#include "presets/presets_folder.hpp"
#include "sample_lib_server/sample_library_server.hpp"
#include "settings/settings_file.hpp"

// Shared across plugin instances of the engine. This usually happens when the plugin is loaded multiple times
// in the host. Sometimes though, the host will load plugin instances in separate processes for
// crash-protection.

struct SharedEngineSystems {
    SharedEngineSystems();
    ~SharedEngineSystems();

    void RegisterFloeInstance(clap_plugin const* plugin, FloeInstanceIndex index);
    void UnregisterFloeInstance(FloeInstanceIndex index);

    // indexable by FloeInstanceInde
    Array<clap_plugin const*, k_max_num_floe_instances> floe_instances {};

    ArenaAllocator arena;
    ThreadsafeErrorNotifications error_notifications {};
    FloePaths paths;
    SettingsFile settings;
    ThreadPool thread_pool;
    PresetsListing preset_listing {paths.always_scanned_folder[ToInt(ScanFolderType::Presets)],
                                   error_notifications};
    sample_lib_server::Server sample_library_server;
};
