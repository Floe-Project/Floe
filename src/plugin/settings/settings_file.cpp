// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_file.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "tests/framework.hpp"
#include "utils/json/json_reader.hpp"

#include "config.h"
#include "param_info.hpp"
#include "settings_gui.hpp"
#include "settings_midi.hpp"

SettingsFile::SettingsFile(FloePaths const& paths) : paths(paths) {}

struct LegacyJsonSettingsParser {
    LegacyJsonSettingsParser(Settings& content, ArenaAllocator& a, ArenaAllocator& scratch_arena)
        : content(content)
        , allocator(a)
        , scratch_arena(scratch_arena) {}

    bool HandleEvent(json::EventHandlerStack& handler_stack, json::Event const& event) {
        Optional<String> folder {};
        if (SetIfMatchingRef(event, "extra_presets_folder", folder)) {
            if (path::IsAbsolute(*folder)) {
                auto paths = allocator.AllocateExactSizeUninitialised<String>(1);
                PLACEMENT_NEW(&paths[0]) String(*folder);
                content.filesystem.extra_presets_scan_folders = paths;
            }
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
                                            k_param_infos
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
        if (SetIfMatching(event, "SortLibsAlpha", content.gui.sort_libraries_alphabetically)) return true;
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

    DynamicArray<String> folders {content_arena};
    for (auto p : parser.libraries) {
        auto const dir = path::Directory(p);
        if (dir && !Find(paths.always_scanned_folders[ToInt(ScanFolderType::Libraries)], *dir))
            if (auto const d = path::Directory(p)) dyn::AppendIfNotAlreadyThere(folders, *d);
    }
    content.filesystem.extra_libraries_scan_folders = folders.ToOwnedSpan();

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
    return nullopt;
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

static void
Parse(Settings& content, ArenaAllocator& content_allocator, ArenaAllocator& scratch_arena, String file_data) {
    DynamicArray<String> unknown_lines {scratch_arena};
    DynamicArray<String> additional_library_paths {scratch_arena};
    DynamicArray<String> extra_libraries_folders {scratch_arena};
    DynamicArray<String> presets_folders {scratch_arena};

    Optional<usize> cursor = 0uz;
    while (cursor) {
        auto const line = SplitWithIterator(file_data, cursor, '\n');
        if (line.size == 0) continue;
        if (StartsWith(line, ';')) continue;

        {
            // The same key is allowed to appear more than once. We just append each value to an array.
            String path {};
            if (SetIfMatching(line, "extra_libraries_folder", path)) {
                if (path::IsAbsolute(path)) dyn::Append(extra_libraries_folders, path);
                continue;
            }
        }

        {
            // The same key is allowed to appear more than once. We just append each value to an array.
            String path {};
            if (SetIfMatching(line, "extra_presets_folder", path)) {
                if (path::IsAbsolute(path)) dyn::Append(presets_folders, path);
                continue;
            }
        }

        {
            // The same key is allowed to appear more than once. We just append each value to an array.
            String path {};
            if (SetIfMatching(line, "additional_library_path", path)) {
                if (path::IsAbsolute(path)) dyn::Append(additional_library_paths, path);
                continue;
            }
        }

        {
            String value {};
            if (SetIfMatching(line, "cc_to_param_id_map", value)) {
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

        if (SetIfMatching(line, "show_tooltips", content.gui.show_tooltips)) continue;
        if (SetIfMatching(line, "window_width", content.gui.window_width)) continue;
        if (SetIfMatching(line, "gui_keyboard_octave", content.gui.keyboard_octave)) continue;
        if (SetIfMatching(line, "presets_random_mode", content.gui.presets_random_mode)) continue;
        if (SetIfMatching(line, "show_keyboard", content.gui.show_keyboard)) continue;
        if (SetIfMatching(line, "high_contrast_gui", content.gui.high_contrast_gui)) continue;
        if (SetIfMatching(line, "sort_libraries_alphabetically", content.gui.sort_libraries_alphabetically))
            continue;

        dyn::Append(unknown_lines, line);
    }

    content.filesystem.extra_libraries_scan_folders =
        content_allocator.Clone(extra_libraries_folders, CloneType::Deep);
    content.filesystem.extra_presets_scan_folders = content_allocator.Clone(presets_folders, CloneType::Deep);
    content.unknown_lines_from_file = content_allocator.Clone(unknown_lines, CloneType::Deep);
}

ErrorCodeOr<void> WriteFile(Settings const& data, String path) {
    ArenaAllocatorWithInlineStorage<4000> scratch_arena;

    auto _ = CreateDirectory(path::Directory(path).ValueOr({}),
                             {
                                 .create_intermediate_directories = true,
                                 .fail_if_exists = false,
                             });

    auto file = TRY(OpenFile(path, FileMode::Write));
    auto writer = file.Writer();

    TRY(fmt::AppendLine(writer, "show_tooltips = {}", data.gui.show_tooltips));

    TRY(fmt::AppendLine(writer, "gui_keyboard_octave = {}", data.gui.keyboard_octave));
    TRY(fmt::AppendLine(writer, "show_tooltips = {}", data.gui.show_tooltips));
    TRY(fmt::AppendLine(writer, "high_contrast_gui = {}", data.gui.high_contrast_gui));
    TRY(fmt::AppendLine(writer,
                        "sort_libraries_alphabetically = {}",
                        data.gui.sort_libraries_alphabetically));
    TRY(fmt::AppendLine(writer, "show_keyboard = {}", data.gui.show_keyboard));
    TRY(fmt::AppendLine(writer, "presets_random_mode = {}", data.gui.presets_random_mode));
    TRY(fmt::AppendLine(writer, "window_width = {}", data.gui.window_width));

    for (auto p : data.filesystem.extra_libraries_scan_folders)
        TRY(fmt::AppendLine(writer, "extra_libraries_folder = {}", p));
    for (auto p : data.filesystem.extra_presets_scan_folders)
        TRY(fmt::AppendLine(writer, "extra_presets_folder = {}", p));

    for (auto cc = data.midi.cc_to_param_mapping; cc != nullptr; cc = cc->next) {
        DynamicArray<char> buf {scratch_arena};
        for (auto param = cc->param; param != nullptr; param = param->next)
            fmt::Append(buf, "{},", param->id);
        if (!buf.size) continue;

        dyn::Pop(buf); // remove last comma
        TRY(fmt::AppendLine(writer, "cc_to_param_id_map = {}:{}", cc->cc_num, buf));
    }

    for (auto line : data.unknown_lines_from_file)
        TRY(fmt::AppendLineRaw(writer, line));

    return k_success;
}

} // namespace ini

bool InitialiseSettingsFileData(Settings& file, ArenaAllocator& arena, bool file_is_brand_new) {
    bool changed = false;
    changed = changed || midi_settings::Initialise(file.midi, arena, file_is_brand_new);
    if (file.gui.window_width == 0) {
        file.gui.window_width = gui_settings::CreateFromWidth(gui_settings::k_default_gui_width_approx,
                                                              gui_settings::k_aspect_ratio_without_keyboard)
                                    .width;
    }
    return changed;
}

Optional<Settings> FindAndReadSettingsFile(ArenaAllocator& a, FloePaths const& paths) {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};

    String file_data {};
    bool is_json {};
    for (auto const p : paths.possible_settings_paths) {
        auto read_outcome = ReadEntireFile(p, a);
        if (read_outcome.HasError()) continue;

        is_json = path::Extension(p) == ".json"_s;
        file_data = read_outcome.ReleaseValue();
        break;
    }

    if (file_data.size == 0) return nullopt;

    Settings result {};

    if (!is_json) {
        ini::Parse(result, a, scratch_arena, file_data);
    } else if (auto parsed_json = ParseLegacyJsonFile(result, paths, a, scratch_arena, file_data);
               !parsed_json) {
        // The file is not valid json. Let's say it's not an error though. Instead, let's just use
        // default values.
        return nullopt;
    }

    return result;
}

ErrorCodeOr<void> WriteSettingsFile(Settings const& data, String path) { return ini::WriteFile(data, path); }

ErrorCodeOr<void> WriteSettingsFileIfChanged(SettingsFile& settings) {
    if (Exchange(settings.tracking.changed, false))
        return ini::WriteFile(settings.settings, settings.paths.settings_write_path);
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

    CHECK_EQ(result.filesystem.extra_libraries_scan_folders.size, 1uz);
    if (result.filesystem.extra_libraries_scan_folders.size)
        CHECK_EQ(result.filesystem.extra_libraries_scan_folders[0],
                 fmt::Format(scratch_arena, "{}mdatas", root_dir));

    auto const changed = InitialiseSettingsFileData(result, arena, false);
    CHECK(!changed);

    CHECK_EQ(result.filesystem.extra_presets_scan_folders.size, 1uz);
    if (result.filesystem.extra_presets_scan_folders.size) {
        CHECK_EQ(result.filesystem.extra_presets_scan_folders[0],
                 fmt::Format(scratch_arena, "{}Presets"_s, root_dir));
    }

    CHECK(result.gui.high_contrast_gui == false);
    CHECK(result.gui.show_keyboard == true);
    CHECK(result.gui.show_tooltips == true);
    CHECK_EQ(result.gui.keyboard_octave, 0);

    REQUIRE(result.midi.cc_to_param_mapping);
    CHECK(result.midi.cc_to_param_mapping->cc_num == 1);
    CHECK(result.midi.cc_to_param_mapping->param->id == k_param_infos[ToInt(ParamIndex::MasterVolume)].id);

    return k_success;
}

TEST_CASE(TestIniParsing) {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};

    DynamicArray<char> ini {R"foo(show_tooltips = true
gui_keyboard_octave = 0
show_tooltips = true
high_contrast_gui = true
sort_libraries_alphabetically = true
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
        CHECK_EQ(data.filesystem.extra_libraries_scan_folders.size, 2uz);
        if (data.filesystem.extra_libraries_scan_folders.size) {
            CHECK_EQ(data.filesystem.extra_libraries_scan_folders[0],
                     fmt::Format(scratch_arena, "{}Libraries"_s, root_dir));
            CHECK_EQ(data.filesystem.extra_libraries_scan_folders[1],
                     fmt::Format(scratch_arena, "{}Floe Libraries"_s, root_dir));
        }

        CHECK_EQ(data.filesystem.extra_presets_scan_folders.size, 2uz);
        if (data.filesystem.extra_presets_scan_folders.size) {
            CHECK_EQ(data.filesystem.extra_presets_scan_folders[0],
                     fmt::Format(scratch_arena, "{}Projects/Test"_s, root_dir));
            CHECK_EQ(data.filesystem.extra_presets_scan_folders[1],
                     fmt::Format(scratch_arena, "{}Presets"_s, root_dir));
        }

        CHECK_EQ(data.gui.keyboard_octave, 0);
        CHECK_EQ(data.gui.show_tooltips, true);
        CHECK_EQ(data.gui.high_contrast_gui, true);
        CHECK_EQ(data.gui.show_keyboard, true);

        CHECK(data.midi.cc_to_param_mapping);
        CHECK_EQ(data.midi.cc_to_param_mapping->cc_num, 10);
        DynamicArrayInline<u32, 3> expected_ids {ArrayT<u32>({1, 3, 4})};
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
        TRY(WriteSettingsFile(data, path));

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
