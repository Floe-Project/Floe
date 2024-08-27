// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"

enum class ScanFolderType { Presets, Libraries, Count };
enum class LocationType { User, AllUsers };

struct FloePaths {
    Span<String> always_scanned_folders[ToInt(ScanFolderType::Count)];
    String settings_write_path;
    Span<String> possible_settings_paths; // sorted. the first is most recommended path to read
};

static Span<String> PossibleSettingsPaths(ArenaAllocator& arena) {
    DynamicArray<String> result {arena};
    result.Reserve(5);

    auto try_add_path = [&](KnownDirectories known_dir, Span<String const> sub_paths) {
        if (auto o = KnownDirectory(arena, known_dir); o.HasValue()) {
            auto p = DynamicArray<char>::FromOwnedSpan(o.Value(), arena);
            path::JoinAppend(p, sub_paths);
            dyn::Append(result, p.ToOwnedSpan());
        }
    };

    // Best path
    try_add_path(KnownDirectories::PluginSettings, Array {"Floe"_s, "settings.ini"_s});

    // Legacy paths
    // In the past some of these were poorly chosen as locations for saving settings due to file permissions.
    {
        try_add_path(KnownDirectories::AllUsersSettings,
                     Array {"FrozenPlain"_s, "Mirage", "Settings", "mirage.json"_s});

        if constexpr (IS_WINDOWS)
            try_add_path(KnownDirectories::PluginSettings,
                         Array {"FrozenPlain"_s, "Mirage", "mirage.json"_s});
        else
            try_add_path(KnownDirectories::PluginSettings, Array {"FrozenPlain"_s, "mirage.json"_s});

        if constexpr (IS_MACOS) {
            try_add_path(KnownDirectories::AllUsersData, Array {"FrozenPlain"_s, "Mirage", "mirage.json"});
            try_add_path(KnownDirectories::Data, Array {"FrozenPlain"_s, "Mirage", "mirage.json"});
        }
    }

    return result.ToOwnedSpan();
}

static ErrorCodeOr<String>
AlwaysScannedFolders(ScanFolderType type, LocationType location_type, ArenaAllocator& allocator) {
    auto const dir = TRY(KnownDirectory(allocator, ({
                                            KnownDirectories k;
                                            switch (location_type) {
                                                case LocationType::User: k = KnownDirectories::Data; break;
                                                case LocationType::AllUsers:
                                                    k = KnownDirectories::AllUsersData;
                                                    break;
                                            }
                                            k;
                                        })));
    auto path = DynamicArray<char>::FromOwnedSpan(dir, allocator);
    switch (type) {
        case ScanFolderType::Libraries: path::JoinAppend(path, Array {"Floe"_s, "Libraries"}); break;
        case ScanFolderType::Presets: path::JoinAppend(path, Array {"Floe"_s, "Presets"}); break;
        case ScanFolderType::Count: PanicIfReached();
    }
    return path.ToOwnedSpan();
}

PUBLIC Span<String> AlwaysScannedFolders(ScanFolderType type, ArenaAllocator& allocator) {
    DynamicArray<String> result {allocator};
    auto const locations = Array {LocationType::AllUsers, LocationType::User};
    result.Reserve(locations.size);
    for (auto const location_type : locations)
        if (auto const p = AlwaysScannedFolders(type, location_type, allocator); p.HasValue())
            dyn::Append(result, p.Value());

    return result.ToOwnedSpan();
}

PUBLIC FloePaths CreateFloePaths(ArenaAllocator& arena) {
    auto const possible_settings_paths = PossibleSettingsPaths(arena);

    FloePaths result {
        .settings_write_path = possible_settings_paths[0],
        .possible_settings_paths = possible_settings_paths,
    };

    for (auto const type : Range(ToInt(ScanFolderType::Count)))
        result.always_scanned_folders[type] = AlwaysScannedFolders((ScanFolderType)type, arena);

    return result;
}
