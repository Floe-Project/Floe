// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common_infrastructure/paths.hpp"

// Settings are stored in an INI-like file format. Duplicate keys are allowed meaning there can be a list of
// values for one key. We keep track of all the lines in the file that we don't use, so we can write them back
// to the file and therefore avoiding issues if the file is read by another version of Floe.

constexpr usize k_max_extra_scan_folders {16};

struct Settings {
    struct Filesystem {
        Array<DynamicArrayBounded<String, k_max_extra_scan_folders>, ToInt(ScanFolderType::Count)>
            extra_scan_folders {};
        Array<String, ToInt(ScanFolderType::Count)> install_location {};
    } filesystem;

    struct Midi {
        struct CcToParamMapping {
            struct Param {
                Param* next {};
                u32 id {};
            };

            CcToParamMapping* next {};
            u8 cc_num {};
            Param* param {};
        };

        // linked list for easier use and smaller memory usage when inserting/removing using the arena
        // allocator
        CcToParamMapping* cc_to_param_mapping {};
    } midi;

    struct Gui {
        int keyboard_octave {0};
        bool show_tooltips {true};
        bool high_contrast_gui {false};
        bool show_keyboard {true};
        int presets_random_mode {0};
        u16 window_width {0};
    } gui;

    // general
    bool online_reporting_disabled {};

    // We keep hold of entries in the file that we don't use. Other versions of Floe might still want these
    // so lets keep hold of them, and write them back to the file.
    Span<String> unknown_lines_from_file {};

    PathPool path_pool;
};

struct SettingsTracking {
    enum class ChangeType { WindowSize, ScanFolder, OnlineReportingDisabled };
    using Change = TaggedUnion<ChangeType, TypeAndTag<ScanFolderType, ChangeType::ScanFolder>>;

    TrivialFixedSizeFunction<8, void(Change)> on_change {};
    bool changed {};
};

struct SettingsFile {
    FloePaths const& paths;
    ArenaAllocator arena {PageAllocator::Instance()};
    SettingsTracking tracking;
    Settings settings;
    s128 last_modified_time {};
    ArenaAllocator watcher_scratch {PageAllocator::Instance()};
    ArenaAllocator watcher_arena {PageAllocator::Instance()};
    Optional<DirectoryWatcher> watcher;
    TimePoint last_watcher_poll_time {};
};

void InitSettingsFile(SettingsFile& settings, FloePaths const& paths);
void DeinitSettingsFile(SettingsFile& settings);
void PollForSettingsFileChanges(SettingsFile& settings);

struct SettingsReadResult {
    Settings settings {};
    s128 last_modified_time {};
};

// The arena must outlive the Settings
Optional<SettingsReadResult> FindAndReadSettingsFile(ArenaAllocator& a, FloePaths const& paths);
ErrorCodeOr<SettingsReadResult> ReadSettingsFile(ArenaAllocator& a, String path);
bool InitialiseSettingsFileData(Settings& file,
                                FloePaths const& paths,
                                ArenaAllocator& arena,
                                bool file_is_brand_new);

ErrorCodeOr<void>
WriteSettingsFile(Settings const& data, FloePaths const& paths, String path, s128 last_write_time = {});
ErrorCodeOr<void> WriteSettingsFileIfChanged(SettingsFile& settings);
