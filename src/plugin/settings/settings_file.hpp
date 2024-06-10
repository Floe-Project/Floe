// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/thread_extra/atomic_listener_array.hpp"

#include "common/paths.hpp"

//
// Settings file format
// ----------------------------------------------------------------------------------------------------------
//
// The settings file used to be JSON. When a change was made to the settings via GUI, the whole JSON file
// would be overwritten. This meant any fields that were not understood (such as fields used by an older/newer
// version of Floe) would be discarded. This causes issues when multiple versions of Floe try to use the
// same file.
//
// To fix this backwards compatibility issue, we could use JSON like before but it is a bit more complicated
// due to the fact that JSON can use objects and arrays. We'd need to do more advanced parsing and storing of
// unrecognised fields and save them back out to file.
//
// To simplify this we can instead switch to an INI format - we don't need anything fancier. For the
// compatibility issues, all we need to do is store any _lines_ that we don't recognise and write those back
// as-is to the file when needed. This way any other versions of Floe will still find the fields they want.
//

struct Settings {
    struct Filesystem {
        Span<String> extra_presets_scan_folders {};
        Span<String> extra_libraries_scan_folders {};
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
        bool sort_libraries_alphabetically {false};
        bool show_keyboard {true};
        int presets_random_mode {0};
        u16 window_width {0};
    } gui;

    // We keep hold of entries in the file that we don't use. Other versions of Floe might still want these
    // so lets keep hold of them, and write them back to the file.
    Span<String> unknown_lines_from_file {};
};

using FolderChangeListeners = AtomicListenerArray<TrivialFixedSizeFunction<16, void(ScanFolderType)>>;

struct SettingsTracking {
    bool changed {};
    AtomicListenerArray<TrivialFixedSizeFunction<16, void()>> window_size_change_listeners;
    FolderChangeListeners filesystem_change_listeners;
};

struct SettingsFile {
    SettingsFile(FloePaths const& paths);
    FloePaths const& paths;
    ArenaAllocator arena {PageAllocator::Instance()};
    SettingsTracking tracking;
    Settings settings;
};

// The arena must outlive the Settings
Optional<Settings> FindAndReadSettingsFile(ArenaAllocator& a, FloePaths const& paths);
bool InitialiseSettingsFileData(Settings& file, ArenaAllocator& arena, bool file_is_brand_new);

// path should probably be WriteFilepath()
ErrorCodeOr<void> WriteSettingsFile(Settings const& data, String path);
ErrorCodeOr<void> WriteSettingsFileIfChanged(SettingsFile& settings);
