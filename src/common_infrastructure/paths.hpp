// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"

#include "error_reporting.hpp"
#include "settings/settings_file.hpp"

constexpr usize k_max_extra_scan_folders {16};
enum class ScanFolderType : u8 { Presets, Libraries, Count };

struct FloePaths {
    String always_scanned_folder[ToInt(ScanFolderType::Count)];
    String settings_write_path;
    Span<String> possible_settings_paths; // sorted. the first is most recommended path to read
    String autosave_path;
};

static Span<String> PossibleSettingsPaths(ArenaAllocator& arena) {
    DynamicArray<String> result {arena};
    result.Reserve(5);

    // Best path
    {
        String error_log {};
        dyn::Append(result, SettingsFilepath(&error_log));
        if (error_log.size) {
            ReportError(sentry::ErrorEvent::Level::Warning,
                        HashComptime("settings path"),
                        "Failed to get known settings directory {}\n{}",
                        Last(result),
                        error_log);
        }
    }

    // Legacy paths
    // In the past some of these were poorly chosen as locations for saving settings due to file permissions.
    {
        auto try_add_path = [&](KnownDirectoryType known_dir, Span<String const> sub_paths, String filename) {
            dyn::Append(result,
                        KnownDirectoryWithSubdirectories(arena,
                                                         known_dir,
                                                         sub_paths,
                                                         filename,
                                                         {.create = false, .error_log = nullptr}));
        };

        // C:/ProgramData/FrozenPlain/Mirage/mirage.json
        // /Library/Application Support/FrozenPlain/Mirage/mirage.json
        try_add_path(KnownDirectoryType::LegacyAllUsersSettings,
                     Array {"FrozenPlain"_s, "Mirage", "Settings"},
                     "mirage.json"_s);

        if constexpr (IS_WINDOWS)
            // ~/AppData/Roaming/FrozenPlain/Mirage/mirage.json
            try_add_path(KnownDirectoryType::LegacyPluginSettings,
                         Array {"FrozenPlain"_s, "Mirage"},
                         "mirage.json"_s);
        else
            // ~/Music/Audio Music Apps/Plug-In Settings/FrozenPlain/Mirage/mirage.json
            try_add_path(KnownDirectoryType::LegacyPluginSettings, Array {"FrozenPlain"_s}, "mirage.json"_s);

        if constexpr (IS_MACOS) {
            // /Library/Application Support/FrozenPlain/Mirage/mirage.json
            try_add_path(KnownDirectoryType::LegacyAllUsersData,
                         Array {"FrozenPlain"_s, "Mirage"},
                         "mirage.json");
            // ~/Library/Application Support/FrozenPlain/Mirage/mirage.json
            try_add_path(KnownDirectoryType::LegacyData, Array {"FrozenPlain"_s, "Mirage"}, "mirage.json");
        }
    }

    return result.ToOwnedSpan();
}

static String AlwaysScannedFolder(ScanFolderType type, ArenaAllocator& allocator) {
    FloeKnownDirectoryType dir_type {};
    switch (type) {
        case ScanFolderType::Libraries: dir_type = FloeKnownDirectoryType::Libraries; break;
        case ScanFolderType::Presets: dir_type = FloeKnownDirectoryType::Presets; break;
        case ScanFolderType::Count: PanicIfReached();
    }
    ArenaAllocatorWithInlineStorage<500> scratch_arena {PageAllocator::Instance()};
    DynamicArray<char> error_log {scratch_arena};
    auto error_writer = dyn::WriterFor(error_log);
    auto const result =
        FloeKnownDirectory(allocator, dir_type, k_nullopt, {.create = true, .error_log = &error_writer});
    if (error_log.size) {
        ReportError(sentry::ErrorEvent::Level::Warning,
                    HashComptime("dir") + ToInt(dir_type),
                    "Failed to get always scanned folder {}\n{}",
                    result,
                    error_log);
    }
    return result;
}

PUBLIC FloePaths CreateFloePaths(ArenaAllocator& arena) {
    auto const possible_settings_paths = PossibleSettingsPaths(arena);

    FloePaths result {
        .settings_write_path = possible_settings_paths[0],
        .possible_settings_paths = possible_settings_paths,
    };

    for (auto const type : Range(ToInt(ScanFolderType::Count)))
        result.always_scanned_folder[type] = AlwaysScannedFolder((ScanFolderType)type, arena);

    {
        DynamicArrayBounded<char, Kb(1)> error_log;
        auto writer = dyn::WriterFor(error_log);
        result.autosave_path = FloeKnownDirectory(arena,
                                                  FloeKnownDirectoryType::Autosaves,
                                                  k_nullopt,
                                                  {.create = true, .error_log = &writer});
        if (error_log.size) {
            ReportError(sentry::ErrorEvent::Level::Warning,
                        HashComptime("autosave path"),
                        "Failed to get autosave path {}\n{}",
                        result.autosave_path,
                        error_log);
        }
    }

    return result;
}

// TODO: better unify these functions with the FloePaths structu - perhaps make FloePaths the first argument.
// Perhaps add as static functions.

namespace filesystem_settings {

PUBLIC DynamicArrayBounded<String, k_max_extra_scan_folders>
ExtraScanFolders(sts::Settings& settings, FloePaths const& paths, ScanFolderType type) {
    ASSERT(CheckThreadName("main"));
    String key {};
    switch (type) {
        case ScanFolderType::Presets: key = sts::key::k_extra_presets_folder; break;
        case ScanFolderType::Libraries: key = sts::key::k_extra_libraries_folder; break;
        case ScanFolderType::Count: PanicIfReached(); break;
    }

    auto values = sts::LookupValue(settings, key);
    if (!values) return {};

    DynamicArrayBounded<String, k_max_extra_scan_folders> result;
    for (auto node = &*values; node; node = node->next) {
        auto const v = node->TryGet<String>();
        if (!v) continue;
        if (!path::IsAbsolute(*v)) continue;
        if (!IsValidUtf8(*v)) continue;
        if (path::Equal(*v, paths.always_scanned_folder[ToInt(type)])) continue;
        dyn::AppendIfNotAlreadyThere(result, *v);
    }
    return result;
}

constexpr String InstallLocationSettingsKey(ScanFolderType type) {
    switch (type) {
        case ScanFolderType::Presets: return sts::key::k_presets_install_location;
        case ScanFolderType::Libraries: return sts::key::k_libraries_install_location;
        case ScanFolderType::Count: PanicIfReached(); break;
    }
    return {};
}

PUBLIC String InstallLocation(sts::Settings& settings, FloePaths const& paths, ScanFolderType type) {
    auto v = sts::LookupString(settings, InstallLocationSettingsKey(type));
    auto const fallback = paths.always_scanned_folder[ToInt(type)];
    if (!v) return fallback;
    if (!path::IsAbsolute(*v) || !IsValidUtf8(*v)) return fallback;
    if (!Contains(ExtraScanFolders(settings, paths, type), *v)) return fallback;
    return *v;
}

PUBLIC void
SetInstallLocation(sts::Settings& settings, FloePaths const& paths, ScanFolderType type, String path) {
    ASSERT(CheckThreadName("main"));
    ASSERT(path::IsAbsolute(path));
    ASSERT(IsValidUtf8(path));
    if (!Contains(ExtraScanFolders(settings, paths, type), path)) return;

    sts::SetValue(settings, InstallLocationSettingsKey(type), path);
}

constexpr String ScanFolderSettingsKey(ScanFolderType type) {
    switch (type) {
        case ScanFolderType::Presets: return sts::key::k_extra_presets_folder;
        case ScanFolderType::Libraries: return sts::key::k_extra_libraries_folder;
        case ScanFolderType::Count: PanicIfReached(); break;
    }
    return {};
}

PUBLIC void AddScanFolder(sts::Settings& settings, FloePaths const& paths, ScanFolderType type, String path) {
    ASSERT(CheckThreadName("main"));
    ASSERT(path::IsAbsolute(path));
    ASSERT(IsValidUtf8(path));
    if (path::Equal(path, paths.always_scanned_folder[ToInt(type)])) return;

    sts::AddValue(settings, ScanFolderSettingsKey(type), path);
}

PUBLIC void
RemoveScanFolder(sts::Settings& settings, FloePaths const& paths, ScanFolderType type, String path) {
    ASSERT(CheckThreadName("main"));

    if (sts::RemoveValue(settings, ScanFolderSettingsKey(type), path)) {
        if (path == InstallLocation(settings, paths, type))
            SetInstallLocation(settings, paths, type, paths.always_scanned_folder[ToInt(type)]);
    }
}

} // namespace filesystem_settings
