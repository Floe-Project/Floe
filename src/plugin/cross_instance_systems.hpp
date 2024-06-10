// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/logger/logger.hpp"
#include "utils/thread_extra/thread_pool.hpp"

#include "common/paths.hpp"
#include "presets_folder.hpp"
#include "sample_library_loader.hpp"
#include "settings/settings_file.hpp"

struct FloeLogger : Logger {
    FloeLogger(ArenaAllocator& arena) : arena(arena) {}

    void LogFunction(String str, LogLevel level, bool add_newline) override;

    ArenaAllocator& arena;
    DynamicArray<char> graphics_info {arena};
    Optional<String> m_path;
    bool m_printed_graphics {};
};

struct CrossInstanceSystems {
    CrossInstanceSystems();
    ~CrossInstanceSystems();

    u64 folder_settings_listener_id;
    ArenaAllocator arena;
    ThreadsafeErrorNotifications error_notifications {};
    FloeLogger logger;
    FloePaths paths;
    SettingsFile settings;
    ThreadPool thread_pool;
    PresetsListing preset_listing {paths.always_scanned_folders[ToInt(ScanFolderType::Presets)],
                                   error_notifications};
    sample_lib_loader::AvailableLibraries available_libraries;
    sample_lib_loader::LoadingThread sample_library_loader;
};
