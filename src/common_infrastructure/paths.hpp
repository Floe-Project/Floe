// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/logger/logger.hpp"

enum class ScanFolderType { Presets, Libraries, Count };

struct FloePaths {
    String always_scanned_folder[ToInt(ScanFolderType::Count)];
    String settings_write_path;
    Span<String> possible_settings_paths; // sorted. the first is most recommended path to read
};

static Span<String> PossibleSettingsPaths(ArenaAllocator& arena) {
    DynamicArray<String> result {arena};
    result.Reserve(5);

    // Best path
    LineWriter writer {k_main_log_module, LogLevel::Error, g_log};
    dyn::Append(result,
                FloeKnownDirectory(arena,
                                   FloeKnownDirectoryType::Settings,
                                   "settings.ini"_s,
                                   {
                                       .create = true,
                                       .error_log = &writer.writer,
                                   }));

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

        try_add_path(KnownDirectoryType::LegacyAllUsersSettings,
                     Array {"FrozenPlain"_s, "Mirage", "Settings"},
                     "mirage.json"_s);

        if constexpr (IS_WINDOWS)
            try_add_path(KnownDirectoryType::LegacyPluginSettings,
                         Array {"FrozenPlain"_s, "Mirage"},
                         "mirage.json"_s);
        else
            try_add_path(KnownDirectoryType::LegacyPluginSettings, Array {"FrozenPlain"_s}, "mirage.json"_s);

        if constexpr (IS_MACOS) {
            try_add_path(KnownDirectoryType::LegacyAllUsersData,
                         Array {"FrozenPlain"_s, "Mirage"},
                         "mirage.json");
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
    auto error_writer = ErrorWriter(g_log);
    return FloeKnownDirectory(allocator,
                              dir_type,
                              k_nullopt,
                              {.create = true, .error_log = &error_writer.writer});
}

PUBLIC FloePaths CreateFloePaths(ArenaAllocator& arena) {
    auto const possible_settings_paths = PossibleSettingsPaths(arena);

    FloePaths result {
        .settings_write_path = possible_settings_paths[0],
        .possible_settings_paths = possible_settings_paths,
    };

    for (auto const type : Range(ToInt(ScanFolderType::Count)))
        result.always_scanned_folder[type] = AlwaysScannedFolder((ScanFolderType)type, arena);

    return result;
}
