// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"

#include "error_reporting.hpp"
#include "preferences.hpp"

constexpr usize k_max_extra_scan_folders {16};
enum class ScanFolderType : u8 { Presets, Libraries, Count };

struct FloePaths {
    String always_scanned_folder[ToInt(ScanFolderType::Count)];
    String preferences_path; // path to write to
    Span<String> possible_preferences_paths; // sorted. the first is most recommended path to read
    String autosave_path;
};

static Span<String> PossiblePrefFilePaths(ArenaAllocator& arena) {
    DynamicArray<String> result {arena};
    result.Reserve(5);

    // Best path
    {
        String error_log {};
        dyn::Append(result, PreferencesFilepath(&error_log));
        if (error_log.size) {
            ReportError(sentry::ErrorEvent::Level::Warning,
                        SourceLocationHash(),
                        "Failed to get known preferences directory {}\n{}",
                        Last(result),
                        error_log);
        }
    }

    // Paths that Mirage used.
    // Some of these are actually a bit problematic for reading/writing due to permissions but it doesn't
    // matter for this case. We're just doing our best to retain any existing preferences.
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
    auto const possible_prefs_paths = PossiblePrefFilePaths(arena);

    FloePaths result {
        .preferences_path = possible_prefs_paths[0],
        .possible_preferences_paths = possible_prefs_paths,
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

namespace filesystem_prefs {

PUBLIC DynamicArrayBounded<String, k_max_extra_scan_folders>
ExtraScanFolders(sts::Preferences& prefs, FloePaths const& paths, ScanFolderType type) {
    ASSERT(CheckThreadName("main"));
    String key {};
    switch (type) {
        case ScanFolderType::Presets: key = sts::key::k_extra_presets_folder; break;
        case ScanFolderType::Libraries: key = sts::key::k_extra_libraries_folder; break;
        case ScanFolderType::Count: PanicIfReached(); break;
    }

    auto values = sts::LookupValues(prefs, key);
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

constexpr String InstallLocationPrefsKey(ScanFolderType type) {
    switch (type) {
        case ScanFolderType::Presets: return sts::key::k_presets_install_location;
        case ScanFolderType::Libraries: return sts::key::k_libraries_install_location;
        case ScanFolderType::Count: PanicIfReached(); break;
    }
    return {};
}

PUBLIC String InstallLocation(sts::Preferences& prefs, FloePaths const& paths, ScanFolderType type) {
    auto v = sts::LookupString(prefs, InstallLocationPrefsKey(type));
    auto const fallback = paths.always_scanned_folder[ToInt(type)];
    if (!v) return fallback;
    if (!path::IsAbsolute(*v) || !IsValidUtf8(*v)) return fallback;
    if (!Contains(ExtraScanFolders(prefs, paths, type), *v)) return fallback;
    return *v;
}

PUBLIC void
SetInstallLocation(sts::Preferences& prefs, FloePaths const& paths, ScanFolderType type, String path) {
    ASSERT(CheckThreadName("main"));
    ASSERT(path::IsAbsolute(path));
    ASSERT(IsValidUtf8(path));
    if (!Contains(ExtraScanFolders(prefs, paths, type), path)) return;

    sts::SetValue(prefs, InstallLocationPrefsKey(type), path);
}

constexpr String ScanFolderPrefsKey(ScanFolderType type) {
    switch (type) {
        case ScanFolderType::Presets: return sts::key::k_extra_presets_folder;
        case ScanFolderType::Libraries: return sts::key::k_extra_libraries_folder;
        case ScanFolderType::Count: PanicIfReached(); break;
    }
    return {};
}

PUBLIC void AddScanFolder(sts::Preferences& prefs, FloePaths const& paths, ScanFolderType type, String path) {
    ASSERT(CheckThreadName("main"));
    ASSERT(path::IsAbsolute(path));
    ASSERT(IsValidUtf8(path));
    if (path::Equal(path, paths.always_scanned_folder[ToInt(type)])) return;

    sts::AddValue(prefs, ScanFolderPrefsKey(type), path);
}

PUBLIC void
RemoveScanFolder(sts::Preferences& prefs, FloePaths const& paths, ScanFolderType type, String path) {
    ASSERT(CheckThreadName("main"));

    if (sts::RemoveValue(prefs, ScanFolderPrefsKey(type), path)) {
        if (path == InstallLocation(prefs, paths, type))
            SetInstallLocation(prefs, paths, type, paths.always_scanned_folder[ToInt(type)]);
    }
}

} // namespace filesystem_prefs
