// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_file.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "tests/framework.hpp"
#include "utils/json/json_reader.hpp"

#include "common_infrastructure/common_errors.hpp"

#include "config.h"
#include "descriptors/param_descriptors.hpp"
#include "settings_gui.hpp"
#include "settings_midi.hpp"

constexpr auto k_log_mod = "🔧settings"_log_module;

struct LegacyJsonSettingsParser {
    LegacyJsonSettingsParser(Settings& content, ArenaAllocator& a, ArenaAllocator& scratch_arena)
        : content(content)
        , allocator(a)
        , scratch_arena(scratch_arena) {}

    bool HandleEvent(json::EventHandlerStack& handler_stack, json::Event const& event) {
        Optional<String> folder {};
        if (SetIfMatchingRef(event, "extra_presets_folder", folder)) {
            if (path::IsAbsolute(*folder))
                dyn::Append(content.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)], *folder);
            return true;
        }
        if (json::SetIfMatchingArray(
                handler_stack,
                event,
                "libraries",
                [this](json::EventHandlerStack& handler_stack, json::Event const& event) {
                    return HandleLibraries(handler_stack, event);
                }))
            return true;
        if (json::SetIfMatchingObject(
                handler_stack,
                event,
                "default_ccs",
                [this](json::EventHandlerStack& handler_stack, json::Event const& event) {
                    return HandleLegacyCcToParamMapping(handler_stack, event);
                }))
            return true;
        if (json::SetIfMatchingObject(
                handler_stack,
                event,
                "gui_settings",
                [this](json::EventHandlerStack& handler_stack, json::Event const& event) {
                    return HandleGUISettings(handler_stack, event);
                }))
            return true;
        return false;
    }

    bool HandleLibraries(json::EventHandlerStack& handler_stack, json::Event const& event) {
        if (json::SetIfMatchingObject(handler_stack,
                                      event,
                                      "",
                                      [this](json::EventHandlerStack&, json::Event const& event) {
                                          // NOTE: there also might be a field for "name", but we no longer
                                          // need it
                                          String library_path {};
                                          if (json::SetIfMatchingRef(event, "path", library_path)) {
                                              if (path::IsAbsolute(library_path))
                                                  dyn::Append(libraries, library_path);
                                              return true;
                                          }
                                          return false;
                                      })) {
            return true;
        }
        return false;
    }

    bool HandleLegacyCcToParamMapping(json::EventHandlerStack& handler_stack, json::Event const& event) {
        if (event.type == json::EventType::ArrayStart) {
            if (auto o = ParseInt(event.key, ParseIntBase::Decimal); o.HasValue()) {
                auto num = o.Value();
                if (num > 0 && num <= 127) {
                    cc_num = (u8)num;

                    if (json::SetIfMatchingArray(
                            handler_stack,
                            event,
                            event.key,
                            [&](json::EventHandlerStack&, json::Event const& event) {
                                if (event.type == json::EventType::String) {
                                    auto result = ParamFromLegacyId(event.string);
                                    if (result && result->tag == ParamExistance::StillExists) {
                                        midi_settings::AddPersistentCcToParamMapping(
                                            content.midi,
                                            allocator,
                                            cc_num,
                                            k_param_descriptors
                                                [ToInt(result->GetFromTag<ParamExistance::StillExists>())]
                                                    .id);
                                    }
                                    return true;
                                }
                                return false;
                            })) {
                        return true;
                    }
                }
            } else {
                PanicIfReached();
            }
        }

        return false;
    }

    bool HandleGUISettings(json::EventHandlerStack& handler_stack, json::Event const& event) {
        u16 gui_size_index {};
        if (SetIfMatching(event, "GUISize", gui_size_index)) {
            // We used to set the window size based on an index in an array. We recreate that behaviour
            // here.
            constexpr u16 k_window_width_presets[] = {580, 690, 800, 910, 1020, 1130, 1240};
            content.gui.window_width =
                k_window_width_presets[Min(gui_size_index, (u16)(ArraySize(k_window_width_presets) - 1))];
            return true;
        }

        if (SetIfMatching(event, "KeyboardOctave", content.gui.keyboard_octave)) return true;
        if (SetIfMatching(event, "PresetRandomMode", content.gui.presets_random_mode)) return true;
        if (SetIfMatching(event, "ShowKeyboard", content.gui.show_keyboard)) return true;
        if (SetIfMatching(event, "ShowNews", legacy.show_news_field)) return true;
        if (SetIfMatching(event, "ShowTooltips", content.gui.show_tooltips)) return true;
        if (SetIfMatching(event, "HighContrast", content.gui.high_contrast_gui)) return true;
        if (json::SetIfMatchingArray(handler_stack,
                                     event,
                                     "DismissedNotfications",
                                     [this](json::EventHandlerStack&, json::Event const& event) {
                                         if (event.type == json::EventType::String) {
                                             dyn::Append(dismissed_notifications, HashFnv1a(event.string));
                                             return true;
                                         }
                                         return false;
                                     }))
            return true;
        return false;
    }

    Settings& content;
    ArenaAllocator& allocator;
    ArenaAllocator& scratch_arena;
    DynamicArray<String> libraries {scratch_arena};

    struct {
        Optional<bool> show_news_field {};
    } legacy {};

    u8 cc_num {};

    DynamicArray<u64> dismissed_notifications {scratch_arena};
};

static bool ParseLegacyJsonFile(Settings& content,
                                FloePaths const& paths,
                                ArenaAllocator& content_arena,
                                ArenaAllocator& scratch_arena,
                                String json) {
    LegacyJsonSettingsParser parser {content, content_arena, scratch_arena};

    auto const o = json::Parse(json,
                               [&parser](json::EventHandlerStack& handler_stack, json::Event const& event) {
                                   return parser.HandleEvent(handler_stack, event);
                               },
                               scratch_arena,
                               {});
    if (o.HasError()) return false;

    dyn::Clear(content.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)]);
    for (auto p : parser.libraries) {
        auto const dir = path::Directory(p);
        if (dir && *dir != paths.always_scanned_folder[ToInt(ScanFolderType::Libraries)])
            if (auto const d = path::Directory(p))
                dyn::AppendIfNotAlreadyThere(
                    content.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)],
                    *d);
    }

    return true;
}

namespace ini {

static Optional<String> ValueIfKeyMatches(String line, String key) {
    if (StartsWithSpan(line, key)) {
        auto l = line;
        l.RemovePrefix(key.size);
        l = WhitespaceStrippedStart(l);
        if (l.size && l[0] == '=') {
            l.RemovePrefix(1);
            l = WhitespaceStripped(l);
            if (l.size) return l;
        }
    }
    return k_nullopt;
}

static bool SetIfMatching(String line, String key, bool& value) {
    if (auto value_string = ValueIfKeyMatches(line, key)) {
        value = IsEqualToCaseInsensitiveAscii(value_string.Value(), "true"_s);
        return true;
    }
    return false;
}

static bool SetIfMatching(String line, String key, String& value) {
    if (auto value_string = ValueIfKeyMatches(line, key)) {
        value = *value_string;
        return true;
    }
    return false;
}

template <Integral Type>
requires(!Same<Type, bool>)
static bool SetIfMatching(String line, String key, Type& value) {
    if (auto value_string = ValueIfKeyMatches(line, key)) {
        if (auto o = ParseInt(*value_string, ParseIntBase::Decimal); o.HasValue()) value = (Type)o.Value();
        return true;
    }
    return false;
}

enum class KeyType : u32 {
    CcToParamIdMap,
    ExtraLibrariesFolder,
    ExtraPresetsFolder,
    LibrariesInstallLocation,
    PresetsInstallLocation,
    GuiKeyboardOctave,
    HighContrastGui,
    PresetsRandomMode,
    ShowKeyboard,
    ShowTooltips,
    WindowWidth,
    Count,
};

constexpr String Key(KeyType k) {
    switch (k) {
        case KeyType::CcToParamIdMap: return "cc_to_param_id_map"_s;
        case KeyType::ExtraLibrariesFolder: return "extra_libraries_folder"_s;
        case KeyType::ExtraPresetsFolder: return "extra_presets_folder"_s;
        case KeyType::LibrariesInstallLocation: return "libraries_install_location"_s;
        case KeyType::PresetsInstallLocation: return "presets_install_location"_s;
        case KeyType::GuiKeyboardOctave: return "gui_keyboard_octave"_s;
        case KeyType::HighContrastGui: return "high_contrast_gui"_s;
        case KeyType::PresetsRandomMode: return "presets_random_mode"_s;
        case KeyType::ShowKeyboard: return "show_keyboard"_s;
        case KeyType::ShowTooltips: return "show_tooltips"_s;
        case KeyType::WindowWidth: return "window_width"_s;
        case KeyType::Count: PanicIfReached();
    }
}

constexpr KeyType ScanFolderKeyType(ScanFolderType scan_folder) {
    switch (scan_folder) {
        case ScanFolderType::Presets: return KeyType::ExtraPresetsFolder;
        case ScanFolderType::Libraries: return KeyType::ExtraLibrariesFolder;
        case ScanFolderType::Count: PanicIfReached();
    }
    return {};
}

constexpr KeyType InstallLocationKeyType(ScanFolderType scan_folder) {
    switch (scan_folder) {
        case ScanFolderType::Presets: return KeyType::PresetsInstallLocation;
        case ScanFolderType::Libraries: return KeyType::LibrariesInstallLocation;
        case ScanFolderType::Count: PanicIfReached();
    }
    return {};
}

static void
Parse(Settings& content, ArenaAllocator& content_allocator, ArenaAllocator& scratch_arena, String file_data) {
    DynamicArray<String> unknown_lines {scratch_arena};

    Optional<usize> cursor = 0uz;
    while (cursor) {
        auto const line = SplitWithIterator(file_data, cursor, '\n');
        if (line.size == 0) continue;
        if (StartsWith(line, ';')) continue;

        {
            String value {};
            if (SetIfMatching(line, Key(KeyType::CcToParamIdMap), value)) {
                if (auto equal_pos = Find(value, ':')) {
                    if (auto r = ParseInt(value.SubSpan(0, equal_pos.Value()), ParseIntBase::Decimal);
                        r.HasValue()) {
                        auto const cc_num = r.Value();
                        if (cc_num > 0 && cc_num < 128) {
                            auto const id_strs = value.SubSpan(equal_pos.Value() + 1);
                            if (id_strs.size) {
                                DynamicArray<u32> const ids {scratch_arena};
                                Optional<usize> ids_str_cursor = 0uz;
                                while (ids_str_cursor) {
                                    auto const item = SplitWithIterator(id_strs, ids_str_cursor, ',');
                                    if (auto id_result = ParseInt(item, ParseIntBase::Decimal);
                                        id_result.HasValue()) {
                                        midi_settings::AddPersistentCcToParamMapping(content.midi,
                                                                                     content_allocator,
                                                                                     (u8)cc_num,
                                                                                     (u32)id_result.Value());
                                    }
                                }
                            }
                        }
                    }
                }
                continue;
            }
        }

        bool matched = false;
        for (auto const scan_folder_type : Range(ToInt(ScanFolderType::Count))) {
            {
                // The same key is allowed to appear more than once. We just append each value to an array.
                String path {};
                if (SetIfMatching(line, Key(ScanFolderKeyType((ScanFolderType)scan_folder_type)), path)) {
                    if (path::IsAbsolute(path))
                        dyn::Append(content.filesystem.extra_scan_folders[scan_folder_type],
                                    content.path_pool.Clone(path, content_allocator));
                    matched = true;
                    break;
                }
            }

            {
                String path {};
                if (SetIfMatching(line,
                                  Key(InstallLocationKeyType((ScanFolderType)scan_folder_type)),
                                  path)) {
                    if (path::IsAbsolute(path))
                        content.filesystem.install_location[scan_folder_type] =
                            content.path_pool.Clone(path, content_allocator);
                    matched = true;
                    break;
                }
            }
        }
        if (matched) continue;

        if (SetIfMatching(line, Key(KeyType::GuiKeyboardOctave), content.gui.keyboard_octave)) continue;
        if (SetIfMatching(line, Key(KeyType::HighContrastGui), content.gui.high_contrast_gui)) continue;
        if (SetIfMatching(line, Key(KeyType::PresetsRandomMode), content.gui.presets_random_mode)) continue;
        if (SetIfMatching(line, Key(KeyType::ShowKeyboard), content.gui.show_keyboard)) continue;
        if (SetIfMatching(line, Key(KeyType::ShowTooltips), content.gui.show_tooltips)) continue;
        if (SetIfMatching(line, Key(KeyType::WindowWidth), content.gui.window_width)) continue;

        dyn::Append(unknown_lines, line);
    }

    content.unknown_lines_from_file = content_allocator.Clone(unknown_lines, CloneType::Deep);
}

ErrorCodeOr<void> WriteFile(Settings const& data, FloePaths const& paths, String path, s128 time) {
    g_log.Debug(k_log_mod, "Writing settings file: {}. Gui size: {}", path, data.gui.window_width);
    ArenaAllocatorWithInlineStorage<4000> scratch_arena {Malloc::Instance()};

    DynamicArray<char> file_data {scratch_arena};

    // generate the file data
    {
        auto writer = dyn::WriterFor(file_data);

        for (auto cc = data.midi.cc_to_param_mapping; cc != nullptr; cc = cc->next) {
            DynamicArray<char> buf {scratch_arena};
            for (auto param = cc->param; param != nullptr; param = param->next)
                fmt::Append(buf, "{},", param->id);
            if (!buf.size) continue;

            dyn::Pop(buf); // remove last comma
            TRY(fmt::AppendLine(writer, "{} = {}:{}", Key(KeyType::CcToParamIdMap), cc->cc_num, buf));
        }

        for (auto const scan_folder_type : Range(ToInt(ScanFolderType::Count))) {
            for (auto p : data.filesystem.extra_scan_folders[scan_folder_type]) {
                TRY(fmt::AppendLine(writer,
                                    "{} = {}",
                                    Key(ScanFolderKeyType((ScanFolderType)scan_folder_type)),
                                    p));
            }
            if (path::IsAbsolute(data.filesystem.install_location[scan_folder_type]) &&
                data.filesystem.install_location[scan_folder_type] !=
                    paths.always_scanned_folder[scan_folder_type])
                TRY(fmt::AppendLine(writer,
                                    "{} = {}",
                                    Key(InstallLocationKeyType((ScanFolderType)scan_folder_type)),
                                    data.filesystem.install_location[scan_folder_type]));
        }

        TRY(fmt::AppendLine(writer, "{} = {}", Key(KeyType::GuiKeyboardOctave), data.gui.keyboard_octave));
        TRY(fmt::AppendLine(writer, "{} = {}", Key(KeyType::HighContrastGui), data.gui.high_contrast_gui));
        TRY(fmt::AppendLine(writer,
                            "{} = {}",
                            Key(KeyType::PresetsRandomMode),
                            data.gui.presets_random_mode));
        TRY(fmt::AppendLine(writer, "{} = {}", Key(KeyType::ShowKeyboard), data.gui.show_keyboard));
        TRY(fmt::AppendLine(writer, "{} = {}", Key(KeyType::ShowTooltips), data.gui.show_tooltips));
        TRY(fmt::AppendLine(writer, "{} = {}", Key(KeyType::WindowWidth), data.gui.window_width));

        for (auto line : data.unknown_lines_from_file)
            TRY(fmt::AppendLineRaw(writer, line));
    }

    // write the file
    {
        auto _ = CreateDirectory(path::Directory(path).ValueOr({}),
                                 {
                                     .create_intermediate_directories = true,
                                     .fail_if_exists = false,
                                 });

        auto file = TRY(OpenFile(path, FileMode::WriteEveryoneReadWrite));

        TRY(file.Lock(FileLockType::Exclusive));
        DEFER { auto _ = file.Unlock(); };

        TRY(file.Write(file_data));
        TRY(file.Flush());
        TRY(file.SetLastModifiedTimeNsSinceEpoch(time));
    }

    return k_success;
}

} // namespace ini

void InitSettingsFile(SettingsFile& settings, FloePaths const& paths) {
    bool file_is_new = false;
    auto opt_data = FindAndReadSettingsFile(settings.arena, paths);
    if (!opt_data)
        file_is_new = true;
    else {
        settings.settings = opt_data->settings;
        settings.last_modified_time = opt_data->last_modified_time;
    }
    if (InitialiseSettingsFileData(settings.settings, paths, settings.arena, file_is_new))
        settings.tracking.changed = true;

    auto watcher = CreateDirectoryWatcher(settings.watcher_arena);
    if (watcher.HasValue()) settings.watcher.Emplace(watcher.ReleaseValue());
}

void DeinitSettingsFile(SettingsFile& settings) {
    if (settings.watcher.HasValue()) DestoryDirectoryWatcher(settings.watcher.Value());
}

// This is a simple implementation that should reduce the chances of the settings file being overwritten if
// there are multiple processes of Floe running. I don't think this is a common scenario though, plugins tend
// to be in the same process and therefore if we are using global memory, they share the same memory.
void PollForSettingsFileChanges(SettingsFile& settings) {
    ASSERT(CheckThreadName("main"));

    if (settings.last_watcher_poll_time + 0.3 > TimePoint::Now()) return;
    DEFER { settings.last_watcher_poll_time = TimePoint::Now(); };

    if (!settings.watcher.HasValue()) return;

    auto const dir = path::Directory(settings.paths.settings_write_path);
    if (!dir) return;

    DirectoryToWatch watch_dir {
        .path = *dir,
        .recursive = false,
    };
    auto const outcome = PollDirectoryChanges(settings.watcher.Value(),
                                              {
                                                  .dirs_to_watch = Array {watch_dir},
                                                  .result_arena = settings.watcher_scratch,
                                                  .scratch_arena = settings.watcher_scratch,
                                              });
    DEFER { settings.watcher_scratch.ResetCursorAndConsolidateRegions(); };

    if (outcome.HasError()) return;
    auto const& changes = outcome.Value();
    for (auto const& change : changes) {
        for (auto const& subpath : change.subpath_changesets) {
            if (path::Equal(subpath.subpath, path::Filename(settings.paths.settings_write_path))) {
                auto const last_modified_time =
                    LastModifiedTimeNsSinceEpoch(settings.paths.settings_write_path);

                if (last_modified_time.HasError()) {
                    if (last_modified_time.Error() == FilesystemError::PathDoesNotExist) {
                        settings.arena.ResetCursorAndConsolidateRegions();
                        settings.settings = {};
                        settings.last_modified_time = 0;
                    }
                    continue;
                }

                if (last_modified_time.Value() != settings.last_modified_time) {
                    settings.arena.ResetCursorAndConsolidateRegions();
                    auto data = ReadSettingsFile(settings.arena, settings.paths.settings_write_path);
                    if (data.HasValue()) {
                        settings.settings = data.Value().settings;
                        settings.last_modified_time = data.Value().last_modified_time;
                    } else {
                        settings.settings = {};
                        settings.last_modified_time = 0;
                    }
                }
            }
        }
    }
}

bool InitialiseSettingsFileData(Settings& file,
                                FloePaths const& floe_paths,
                                ArenaAllocator& arena,
                                bool file_is_brand_new) {
    bool changed = false;
    changed = changed || midi_settings::Initialise(file.midi, arena, file_is_brand_new);
    if (file.gui.window_width < gui_settings::k_min_gui_width) {
        file.gui.window_width = gui_settings::CreateFromWidth(gui_settings::k_default_gui_width_approx,
                                                              gui_settings::k_aspect_ratio_without_keyboard)
                                    .width;
    }
    for (auto const scan_folder_type : Range(ToInt(ScanFolderType::Count))) {
        auto const default_folder = floe_paths.always_scanned_folder[scan_folder_type];
        auto& loc = file.filesystem.install_location[scan_folder_type];
        if (!path::IsAbsolute(loc))
            loc = default_folder;
        else if (loc != default_folder && !Find(file.filesystem.extra_scan_folders[scan_folder_type], loc))
            loc = default_folder;
    }
    return changed;
}

ErrorCodeOr<SettingsReadResult> ReadSettingsFile(ArenaAllocator& a, String path) {
    g_log.Debug(k_log_mod, "Reading settings file: {}", path);
    auto file = TRY(OpenFile(path, FileMode::Read));
    TRY(file.Lock(FileLockType::Shared));
    DEFER { auto _ = file.Unlock(); };

    SettingsReadResult result {};
    result.last_modified_time = TRY(file.LastModifiedTimeNsSinceEpoch());

    auto const file_data = TRY(file.ReadWholeFile(a));

    ArenaAllocator scratch_arena {PageAllocator::Instance()};

    if (path::Equal(path::Extension(path), ".json"_s)) {
        if (!ParseLegacyJsonFile(result.settings, {}, a, scratch_arena, file_data))
            return ErrorCode {CommonError::InvalidFileFormat};
    } else {
        ini::Parse(result.settings, a, scratch_arena, file_data);
    }

    return result;
}

Optional<SettingsReadResult> FindAndReadSettingsFile(ArenaAllocator& a, FloePaths const& paths) {
    for (auto const p : paths.possible_settings_paths)
        if (auto const o = ReadSettingsFile(a, p); o.HasValue()) return o.Value();
    return k_nullopt;
}

ErrorCodeOr<void> WriteSettingsFile(Settings const& data, FloePaths const& paths, String path, s128 time) {
    return ini::WriteFile(data, paths, path, time);
}

ErrorCodeOr<void> WriteSettingsFileIfChanged(SettingsFile& settings) {
    if (Exchange(settings.tracking.changed, false)) {
        settings.last_modified_time = NanosecondsSinceEpoch();
        return ini::WriteFile(settings.settings,
                              settings.paths,
                              settings.paths.settings_write_path,
                              settings.last_modified_time);
    }
    return k_success;
}

TEST_CASE(TestJsonParsing) {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};

    DynamicArray<char> json {R"foo({
    "extra_presets_folder": "{ROOT}Presets",
    "use_old_param_mapping": false,
    "dismiss_all_notifications": false,
    "floe_version": "2.1.0",
    "libraries": [
        {
            "name": "Slow",
            "path": "{ROOT}mdatas/slow.mdata"
        },
        {
            "name": "Wraith",
            "path": "{ROOT}mdatas/wraith.mdata"
        }
    ],
    "default_ccs": {
        "1": [
            "MastVol"
        ]
    },
    "gui_settings": {
        "DismissedNotfications": [
            "Slow library released",
            "Arctic Strings library released"
        ],
        "GUIWidth": 1200,
        "HideMarketing": true,
        "KeyboardOctave": 0,
        "NewsPrefsDecided": false,
        "PresetRandomMode": 0,
        "ShowKeyboard": true,
        "ShowTooltips": true,
        "HighContrast": false,
        "SortLibsAlpha": false
    }
})foo"_s,
                             scratch_arena};
    auto const root_dir = IS_WINDOWS ? "C:/"_s : "/";
    dyn::Replace(json, "{ROOT}"_s, root_dir);

    ArenaAllocator arena {Malloc::Instance()};
    Settings result {};
    FloePaths const paths {};

    auto const parsed_json = ParseLegacyJsonFile(result, paths, arena, scratch_arena, json);

    REQUIRE(parsed_json);

    CHECK_EQ(result.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)].size, 1uz);
    if (result.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)].size)
        CHECK_EQ(result.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)][0],
                 fmt::Format(scratch_arena, "{}mdatas", root_dir));

    auto const changed = InitialiseSettingsFileData(result, paths, arena, false);
    CHECK(!changed);

    CHECK_EQ(result.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)].size, 1uz);
    if (result.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)].size) {
        CHECK_EQ(result.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)][0],
                 fmt::Format(scratch_arena, "{}Presets"_s, root_dir));
    }

    CHECK(result.gui.high_contrast_gui == false);
    CHECK(result.gui.show_keyboard == true);
    CHECK(result.gui.show_tooltips == true);
    CHECK_EQ(result.gui.keyboard_octave, 0);

    REQUIRE(result.midi.cc_to_param_mapping);
    CHECK(result.midi.cc_to_param_mapping->cc_num == 1);
    CHECK(result.midi.cc_to_param_mapping->param->id ==
          k_param_descriptors[ToInt(ParamIndex::MasterVolume)].id);

    return k_success;
}

TEST_CASE(TestIniParsing) {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};
    FloePaths const paths {};

    DynamicArray<char> ini {R"foo(show_tooltips = true
gui_keyboard_octave = 0
show_tooltips = true
high_contrast_gui = true
show_keyboard = true
presets_random_mode = 3
window_width = 1200
cc_to_param_id_map = 10:1,3,4
extra_libraries_folder = {ROOT}Libraries
extra_libraries_folder = {ROOT}Floe Libraries
extra_presets_folder = {ROOT}Projects/Test
extra_presets_folder = {ROOT}Presets
non_existent_key = novalue)foo"_s,
                            scratch_arena};
    auto const root_dir = IS_WINDOWS ? "C:/"_s : "/";
    dyn::Replace(ini, "{ROOT}"_s, root_dir);

    ArenaAllocator arena {Malloc::Instance()};
    Settings data {};
    ini::Parse(data, arena, scratch_arena, ini);

    auto check_data = [&](Settings const& data) {
        CHECK_EQ(data.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)].size, 2uz);
        if (data.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)].size) {
            CHECK_EQ(data.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)][0],
                     fmt::Format(scratch_arena, "{}Libraries"_s, root_dir));
            CHECK_EQ(data.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)][1],
                     fmt::Format(scratch_arena, "{}Floe Libraries"_s, root_dir));
        }

        CHECK_EQ(data.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)].size, 2uz);
        if (data.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)].size) {
            CHECK_EQ(data.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)][0],
                     fmt::Format(scratch_arena, "{}Projects/Test"_s, root_dir));
            CHECK_EQ(data.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)][1],
                     fmt::Format(scratch_arena, "{}Presets"_s, root_dir));
        }

        CHECK_EQ(data.gui.keyboard_octave, 0);
        CHECK_EQ(data.gui.show_tooltips, true);
        CHECK_EQ(data.gui.high_contrast_gui, true);
        CHECK_EQ(data.gui.show_keyboard, true);

        REQUIRE(data.midi.cc_to_param_mapping);
        CHECK_EQ(data.midi.cc_to_param_mapping->cc_num, 10);
        DynamicArrayBounded<u32, 3> expected_ids {ArrayT<u32>({1, 3, 4})};
        for (auto param = data.midi.cc_to_param_mapping->param; param != nullptr; param = param->next) {
            auto const found = Find(expected_ids, param->id);
            CHECK(found);
            dyn::RemoveSwapLast(expected_ids, *found);
        }
        CHECK_EQ(expected_ids.size, 0uz);

        CHECK_EQ(data.unknown_lines_from_file.size, 1uz);
        CHECK_EQ(data.unknown_lines_from_file[0], "non_existent_key = novalue"_s);
    };

    check_data(data);

    {
        auto const path = path::Join(scratch_arena, Array {tests::TempFolder(tester), "settings.ini"_s});
        TRY(WriteSettingsFile(data, paths, path));

        Settings reparsed_data {};
        ini::Parse(reparsed_data, arena, scratch_arena, TRY(ReadEntireFile(path, arena)));
        check_data(reparsed_data);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterSettingsFileTests) {
    REGISTER_TEST(TestJsonParsing);
    REGISTER_TEST(TestIniParsing);
}
