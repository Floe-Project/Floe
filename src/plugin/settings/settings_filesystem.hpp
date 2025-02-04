// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "os/threading.hpp"

#include "settings/settings.hpp"

namespace filesystem_settings {

PUBLIC void SetInstallLocation(SettingsFile& settings, ScanFolderType type, String path) {
    ASSERT(CheckThreadName("main"));
    if (!path::IsAbsolute(path)) return;
    auto& install_location = settings.settings.filesystem.install_location[ToInt(type)];
    if (install_location == path) return;

    // the install location must be in the list of scan folders
    if (path != settings.paths.always_scanned_folder[ToInt(type)] &&
        !Find(settings.settings.filesystem.extra_scan_folders[ToInt(type)], path)) {
        return;
    }

    settings.settings.path_pool.Free(install_location);
    install_location = settings.settings.path_pool.Clone(path, settings.arena);
    settings.tracking.changed = true;
}

PUBLIC void AddScanFolder(SettingsFile& settings, ScanFolderType type, String path) {
    ASSERT(CheckThreadName("main"));
    if (!path::IsAbsolute(path)) return;
    if (path == settings.paths.always_scanned_folder[ToInt(type)]) return;

    if (dyn::AppendIfNotAlreadyThere(settings.settings.filesystem.extra_scan_folders[ToInt(type)],
                                     settings.settings.path_pool.Clone(path, settings.arena))) {
        settings.tracking.changed = true;
        if (settings.tracking.on_change) settings.tracking.on_change(type);
    }
}

PUBLIC void RemoveScanFolder(SettingsFile& settings, ScanFolderType type, String path) {
    ASSERT(CheckThreadName("main"));
    auto& paths = settings.settings.filesystem.extra_scan_folders[ToInt(type)];
    auto opt_index = Find(paths, path);
    if (opt_index) {
        if (path == settings.settings.filesystem.install_location[ToInt(type)])
            SetInstallLocation(settings, type, settings.paths.always_scanned_folder[ToInt(type)]);

        settings.settings.path_pool.Free(path);
        dyn::RemoveSwapLast(paths, *opt_index);

        settings.tracking.changed = true;
        if (settings.tracking.on_change) settings.tracking.on_change(type);
    }
}

} // namespace filesystem_settings
