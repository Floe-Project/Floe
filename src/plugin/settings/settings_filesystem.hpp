// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "settings/settings_file.hpp"

namespace filesystem_settings {

namespace detail {

static Span<String>* ScanFolderPaths(Settings& settings, ScanFolderType type) {
    switch (type) {
        case ScanFolderType::Presets: return &settings.filesystem.extra_presets_scan_folders;
        case ScanFolderType::Libraries: return &settings.filesystem.extra_libraries_scan_folders;
        case ScanFolderType::Count: PanicIfReached();
    }
    return nullptr;
}

} // namespace detail

PUBLIC void AddScanFolder(SettingsFile& settings, ScanFolderType type, String path) {
    if (Find(settings.paths.always_scanned_folders[ToInt(type)], path)) return;

    auto& paths = *detail::ScanFolderPaths(settings.settings, type);

    auto folders = DynamicArray<String>::FromOwnedSpan(paths, settings.arena);
    dyn::AppendIfNotAlreadyThere(folders, path);
    paths = folders.ToOwnedSpan();

    settings.tracking.changed = true;
    settings.tracking.filesystem_change_listeners.Call(type);
}

PUBLIC void RemoveScanFolder(SettingsFile& settings, ScanFolderType type, String path) {
    auto& paths = *detail::ScanFolderPaths(settings.settings, type);

    auto folders = DynamicArray<String>::FromOwnedSpan(paths, settings.arena);
    dyn::RemoveValueSwapLast(folders, path);
    paths = folders.ToOwnedSpan();

    settings.tracking.changed = true;
    settings.tracking.filesystem_change_listeners.Call(type);
}

} // namespace filesystem_settings
