// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_editor_ui_style.hpp"

#include <stb/stb_sprintf.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/debug/debug.hpp"

#include "common/constants.hpp"
#include "gui/framework/colours.hpp"

f32 ui_sizes[ToInt(UiSizeId::Count)] = {
#define GUI_SIZE(cat, n, v, unit) v,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
};

UiSizeUnit ui_sizes_units[ToInt(UiSizeId::Count)] {
#define GUI_SIZE(cat, n, v, unit) UiSizeUnit::unit,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
};

String ui_sizes_names[ToInt(UiSizeId::Count)] = {
#define GUI_SIZE(cat, n, v, unit) #n,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wglobal-constructors"

EditorCol ui_cols[k_max_num_colours] = {
#define GUI_COL(name, val, based_on, bright, alpha) {String(name), val, String(based_on), bright, alpha},
#include COLOURS_DEF_FILENAME
#undef GUI_COL
};

EditorColMap ui_col_map[ToInt(UiColMap::Count)] = {
#define GUI_COL_MAP(cat, n, col, high_contrast_col) {String(col), String(high_contrast_col)},
#include COLOUR_MAP_DEF_FILENAME
#undef GUI_COL_MAP
};

#pragma clang diagnostic pop

namespace editor {

int FindColourIndex(String str) {
    for (auto const i : Range(k_max_num_colours))
        if (ui_cols[i].name == str) return i;
    return -1;
}

} // namespace editor

bool editor::g_high_contrast_gui = false;

#if FLOE_EDITOR_ENABLED

String ui_sizes_categories[ToInt(UiSizeId::Count)] = {
#define GUI_SIZE(cat, n, v, unit) #cat,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
};

String const ui_col_map_names[ToInt(UiColMap::Count)] = {
#define GUI_COL_MAP(cat, n, v, high_contrast_col) #n,
#include COLOUR_MAP_DEF_FILENAME
#undef GUI_COL_MAP
};

String ui_col_map_categories[ToInt(UiColMap::Count)] = {
#define GUI_COL_MAP(cat, n, v, high_contrast_col) #cat,
#include COLOUR_MAP_DEF_FILENAME
#undef GUI_COL_MAP
};

static String UiStyleFilepath(Allocator& a, String filename) {
    return path::Join(a, Array {path::Directory(__FILE__).Value(), filename});
}

static void WriteHeader(Writer writer) {
    // REUSE-IgnoreStart
    auto _ = fmt::FormatToWriter(
        writer,
        "// Copyright 2018-2024 Sam Windell\n// SPDX-License-Identifier: GPL-3.0-or-later\n\n");
    // REUSE-IgnoreEnd
}

void WriteColoursFile() {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};
    auto outcome = OpenFile(UiStyleFilepath(scratch_arena, COLOURS_DEF_FILENAME), FileMode::Write);
    if (outcome.HasError()) {
        DebugLn("{} failed: {}", __FUNCTION__, outcome.Error());
        return;
    }

    WriteHeader(outcome.Value().Writer());

    for (auto const& c : ui_cols) {
        auto o = fmt::FormatToWriter(outcome.Value().Writer(),
                                     "GUI_COL(\"{}\", 0x{08x}, \"{}\", {.2}f, {.2}f)\n",
                                     c.name.Items(),
                                     c.col,
                                     c.based_on.Items(),
                                     c.with_brightness,
                                     c.with_alpha);
        if (o.HasError())
            DebugLn("could not write to file {} for reasion {}", COLOURS_DEF_FILENAME, o.Error());
    }
}

void WriteSizesFile() {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};
    auto outcome = OpenFile(UiStyleFilepath(scratch_arena, SIZES_DEF_FILENAME), FileMode::Write);
    if (outcome.HasError()) {
        DebugLn("{} failed: {}", __FUNCTION__, outcome.Error());
        return;
    }

    WriteHeader(outcome.Value().Writer());

    for (auto const i : Range(ToInt(UiSizeId::Count))) {
        auto const sz = ui_sizes[i];
        String const name = ui_sizes_names[i];
        auto unit_name = k_ui_size_units_text[ToInt(ui_sizes_units[i])];
        auto cat = ui_sizes_categories[i];
        auto o = fmt::FormatToWriter(outcome.Value().Writer(),
                                     "GUI_SIZE({}, {}, {.6}f, {})\n",
                                     cat,
                                     name,
                                     sz,
                                     unit_name);
        if (o.HasError()) DebugLn("could not write to file {} for reason {}", SIZES_DEF_FILENAME, o.Error());
    }
}

void WriteColourMapFile() {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};
    auto outcome = OpenFile(UiStyleFilepath(scratch_arena, COLOUR_MAP_DEF_FILENAME), FileMode::Write);
    if (outcome.HasError()) {
        DebugLn("{} failed: {}", __FUNCTION__, outcome.Error());
        return;
    }

    WriteHeader(outcome.Value().Writer());

    for (auto const i : Range(ToInt(UiColMap::Count))) {
        auto const& v = ui_col_map[i];
        auto name = ui_col_map_names[i];
        auto cat = ui_col_map_categories[i];
        auto o = fmt::FormatToWriter(outcome.Value().Writer(),
                                     "GUI_COL_MAP({}, {}, \"{}\", \"{}\")\n",
                                     cat,
                                     name,
                                     v.colour.Items(),
                                     v.high_contrast_colour.Items());
        if (o.HasError())
            DebugLn("could not write to file {} for reason {}", COLOUR_MAP_DEF_FILENAME, o.Error());
    }
}

void SizesGUISliders(EditorGUI* g, String search) {
    EditorHeading(g, "Sizes");

    [[clang::no_destroy]] static DynamicArray<String> categories {Malloc::Instance()};
    if (categories.size == 0)
        for (auto const i : Range(ToInt(UiSizeId::Count)))
            dyn::AppendIfNotAlreadyThere(categories, ui_sizes_categories[i]);

    for (auto cat : categories) {
        g->imgui->PushID(cat);
        DEFER { g->imgui->PopID(); };

        bool contains_values = search.size && ContainsCaseInsensitiveAscii(cat, search);
        if (!contains_values) {
            for (auto const i : Range(ToInt(UiSizeId::Count))) {
                if (ui_sizes_categories[i] != cat) continue;
                if (!ContainsCaseInsensitiveAscii(ui_sizes_names[i], search)) continue;
                contains_values = true;
                break;
            }
        }

        if (!contains_values) continue;

        EditorHeading(g, cat);

        for (auto const i : Range(ToInt(UiSizeId::Count))) {
            if (ui_sizes_categories[i] != cat) continue;
            auto name = ui_sizes_names[i];
            if (!ContainsCaseInsensitiveAscii(name, search) && !ContainsCaseInsensitiveAscii(cat, search))
                continue;

            f32 sz = ui_sizes[i];

            Rect const label_r = EditorGetLeftR(g);
            Rect const sr = EditorGetRightR(g);

            auto settings = imgui::DefTextInputDraggerFloat();
            settings.slider_settings.sensitivity = 2;
            bool const changed =
                g->imgui->TextInputDraggerFloat(settings, sr, g->imgui->GetID(name), 0, 1500, sz);
            EditorLabel(g, label_r, name, TextJustification::CentredRight);

            if (changed) {
                ui_sizes[i] = sz;
                WriteSizesFile();
            }

            EditorIncrementPos(g);
        }
    }
}

static auto GetColourNames(bool include_none) {
    DynamicArrayInline<String, k_max_num_colours + 1> colour_names;
    if (include_none) dyn::Append(colour_names, "---");
    for (auto const i : Range(k_max_num_colours))
        dyn::Append(colour_names, ui_cols[i].name);
    return colour_names;
}

void ColourMapGUIMenus(EditorGUI* g, String search, String colour_search, bool high_contrast) {
    EditorHeading(g, "Colour Mapping");

    [[clang::no_destroy]] static DynamicArray<String> categories {Malloc::Instance()};
    if (categories.size == 0)
        for (auto const i : Range(ToInt(UiColMap::Count)))
            dyn::AppendIfNotAlreadyThere(categories, ui_col_map_categories[i]);
    auto col_names = GetColourNames(high_contrast);

    for (auto cat : categories) {
        g->imgui->PushID(cat);
        DEFER { g->imgui->PopID(); };

        bool contains_values = search.size && ContainsCaseInsensitiveAscii(cat, search);
        if (!contains_values) {
            for (auto const i : Range(ToInt(UiColMap::Count))) {
                auto& col_map = high_contrast ? ui_col_map[i].high_contrast_colour : ui_col_map[i].colour;

                if (ui_col_map_categories[i] != cat) continue;
                if (!ContainsCaseInsensitiveAscii(ui_col_map_names[i], search)) continue;
                if (col_map.size && !ContainsCaseInsensitiveAscii(col_map, colour_search)) continue;
                contains_values = true;
                break;
            }
        }

        if (!contains_values) continue;

        EditorHeading(g, cat);

        for (auto const i : Range(ToInt(UiColMap::Count))) {
            auto& col_map = high_contrast ? ui_col_map[i].high_contrast_colour : ui_col_map[i].colour;

            if (ui_col_map_categories[i] != cat) continue;
            auto name = ui_col_map_names[i];
            if (!ContainsCaseInsensitiveAscii(name, search) && !ContainsCaseInsensitiveAscii(cat, search))
                continue;
            if (col_map.size && !ContainsCaseInsensitiveAscii(col_map, colour_search)) continue;

            g->imgui->PushID((u64)i);
            DEFER { g->imgui->PopID(); };
            Rect const label_r = EditorGetLeftR(g);
            Rect const sr = EditorGetRightR(g);

            int index = editor::FindColourIndex(col_map);
            if (index == -1 && high_contrast) index = 0;
            bool const changed = EditorMenu(g, sr, col_names.Items(), index);
            EditorLabel(g, label_r, name, TextJustification::CentredRight);

            if (changed) {
                if (high_contrast && index == 0)
                    dyn::Clear(col_map);
                else
                    dyn::Assign(col_map, ui_cols[index + high_contrast].name);
                WriteColourMapFile();
            }

            EditorIncrementPos(g);
        }
        EditorIncrementPos(g);
    }
}

static void RecalculateBasedOnCol(EditorCol& c, EditorCol const& other_c) {
    c.col = other_c.col;
    c.col = colours::ChangeBrightness(c.col, Pow(2.0f, c.with_brightness));
    c.col = colours::ChangeAlpha(c.col, Pow(2.0f, c.with_alpha));
}

void ColoursGUISliders(EditorGUI* gui, String search) {
    auto imgui = gui->imgui;
    auto pad = 1.0f;
    auto h = gui->item_h;

    EditorHeading(gui, "Colours");

    for (auto const index : Range(k_max_num_colours)) {
        auto& c = ui_cols[index];
        if (c.name.size && !ContainsCaseInsensitiveAscii(c.name, search)) continue;

        colours::Col const col = colours::FromU32(c.col);
        auto hex_rgb = u32(col.r << 16 | col.g << 8 | col.b);
        f32 a;
        f32 b;
        f32 g;
        f32 r;
        a = col.a / 255.0f;
        b = col.b / 255.0f;
        g = col.g / 255.0f;
        r = col.r / 255.0f;
        f32 hue;
        f32 sat;
        f32 val;
        f32 alpha;
        colours::ConvertRGBtoHSV(r, g, b, hue, sat, val);
        alpha = a;

        imgui->PushID(index);
        auto id = imgui->GetID(index);

        f32 x_pos = 0;
        Rect const label_r = {x_pos, gui->y_pos, imgui->Width() / 3.5f, h};
        x_pos += label_r.w;
        Rect const hex_col_r = {x_pos, gui->y_pos, imgui->Width() / 8, h};
        x_pos += hex_col_r.w + pad;
        Rect col_preview_r = {x_pos, gui->y_pos, h - pad, h};
        x_pos += col_preview_r.w + pad;
        auto remaining_w = imgui->Width() - x_pos;
        Rect const edit_button_r = {x_pos, gui->y_pos, (remaining_w / 12) * 2 - pad, h};
        x_pos += edit_button_r.w + pad;
        Rect const based_on_r = {x_pos, gui->y_pos, (remaining_w / 12) * 6 - pad, h};
        x_pos += based_on_r.w + pad;
        Rect const bright_r = {x_pos, gui->y_pos, (remaining_w / 12) * 2 - pad, h};
        x_pos += bright_r.w + pad;
        Rect const alpha_r = {x_pos, gui->y_pos, (remaining_w / 12) * 2 - pad, h};

        bool hex_code_changed = false;
        bool hsv_changed = false;

        {
            char display[32];
            stbsp_sprintf(display, "%06x", hex_rgb);
            if (c.based_on.size == 0) {
                auto settings = imgui::DefTextInput();
                settings.text_flags.chars_hexadecimal = true;
                auto const res = gui->imgui->TextInput(settings, hex_col_r, id, FromNullTerminated(display));
                hex_code_changed = res.buffer_changed;
                if (hex_code_changed) {
                    char const* start = res.text.data;
                    if (start[0] == '#') start++;
                    auto rgb = ParseInt(FromNullTerminated(start), ParseIntBase::Hexadecimal).ValueOr(0);
                    colours::Col cs = {};
                    cs.a = col.a;
                    cs.r = (uint8_t)((rgb & 0xff0000) >> 16);
                    cs.g = (uint8_t)((rgb & 0xff00) >> 8);
                    cs.b = (uint8_t)(rgb & 0xff);
                    c.col = colours::ToU32(cs);
                }
            } else {
                EditorLabel(gui, hex_col_r, FromNullTerminated(display), TextJustification::CentredLeft);
            }
        }

        if (c.based_on.size == 0) {
            auto pop_id = gui->imgui->GetID("Pop");
            if (imgui->PopupButton(imgui::DefButtonPopup(),
                                   edit_button_r,
                                   imgui->GetID("Edit"),
                                   pop_id,
                                   "Edit")) {
                static f32 static_hue;
                static f32 static_val;
                static f32 static_sat;
                static f32 static_alpha;
                if (imgui->DidPopupMenuJustOpen(pop_id)) {
                    static_hue = hue;
                    static_val = val;
                    static_sat = sat;
                    static_alpha = alpha;
                }

                f32 const pop_w = (f32)imgui->platform->window_size.width / 3.5f;
                f32 const text_size = pop_w / 4;
                f32 const itm_w = (pop_w - text_size) / 3;
                f32 pop_pos = 0;

                auto dragger_set = imgui::DefTextInputDraggerFloat();
                dragger_set.format = "{.4}";
                dragger_set.slider_settings.sensitivity = 100;

                imgui->Text(imgui::DefText(), {0, pop_pos, text_size, h}, "Alpha");
                hsv_changed |= imgui->TextInputDraggerFloat(dragger_set,
                                                            {text_size + 0 * itm_w, pop_pos, itm_w - pad, h},
                                                            imgui->GetID(&alpha),
                                                            0,
                                                            1,
                                                            static_alpha);
                pop_pos += h + pad;

                imgui->Text(imgui::DefText(), {0, pop_pos, text_size, h}, "Hue");
                hsv_changed |= imgui->TextInputDraggerFloat(dragger_set,
                                                            {text_size + 0 * itm_w, pop_pos, itm_w - pad, h},
                                                            imgui->GetID(&hue),
                                                            0,
                                                            1,
                                                            static_hue);
                pop_pos += h + pad;

                imgui->Text(imgui::DefText(), {0, pop_pos, text_size, h}, "Sat");
                hsv_changed |= imgui->TextInputDraggerFloat(dragger_set,
                                                            {text_size + 0 * itm_w, pop_pos, itm_w - pad, h},
                                                            imgui->GetID(&sat),
                                                            0,
                                                            1,
                                                            static_sat);
                pop_pos += h + pad;

                imgui->Text(imgui::DefText(), {0, pop_pos, text_size, h}, "Val");
                hsv_changed |= imgui->TextInputDraggerFloat(dragger_set,
                                                            {text_size + 0 * itm_w, pop_pos, itm_w - pad, h},
                                                            imgui->GetID(&val),
                                                            0,
                                                            1,
                                                            static_val);

                if (hsv_changed) {
                    f32 r1;
                    f32 g1;
                    f32 b1;
                    colours::ConvertHSVtoRGB(static_hue, static_sat, static_val, r1, g1, b1);

                    colours::Col new_col;
                    new_col.a = uint8_t(static_alpha * 255.0f);
                    new_col.r = uint8_t(r1 * 255.0f);
                    new_col.g = uint8_t(g1 * 255.0f);
                    new_col.b = uint8_t(b1 * 255.0f);

                    u32 const col_from_hsv = colours::ToU32(new_col);
                    c.col = col_from_hsv;
                }
                imgui->EndWindow();
            }
        }

        {
            imgui->RegisterAndConvertRect(&col_preview_r);
            imgui->graphics->AddRectFilled(col_preview_r.Min(), col_preview_r.Max(), c.col);
        }

        auto float_dragger = [&](Rect slider_r, imgui::Id id, f32 min, f32 max, f32& value) {
            auto settings = imgui::DefTextInputDraggerFloat();
            settings.format = "{.3}";
            settings.slider_settings.sensitivity = 100;
            return gui->imgui->TextInputDraggerFloat(settings, slider_r, id, min, max, value);
        };

        auto text_editor = [&](Rect edit_r, imgui::Id id, ColourString& str) {
            dyn::NullTerminated(str);
            auto settings = imgui::DefTextInput();
            auto const res = gui->imgui->TextInput(settings, edit_r, id, str);
            if (res.enter_pressed) {
                dyn::Assign(str, res.text);
                return true;
            }
            return false;
        };

        ColourString const starting_name = c.name;
        if (text_editor(label_r, imgui->GetID("name"), c.name)) {
            hex_code_changed = true;
            for (auto& m : ui_col_map) {
                if (m.colour == starting_name) dyn::Assign(m.colour, c.name);
                if (m.high_contrast_colour == starting_name) dyn::Assign(m.high_contrast_colour, c.name);
            }
            for (auto& other_c : ui_cols)
                if (other_c.based_on.size && other_c.based_on == starting_name)
                    dyn::Assign(other_c.based_on, c.name);

            WriteColourMapFile();
        }

        bool recalculate_val = false;
        if (c.based_on.size) {
            recalculate_val |= float_dragger(bright_r, imgui->GetID("Light Scale"), -8, 8, c.with_brightness);
            recalculate_val |= float_dragger(alpha_r, imgui->GetID("Alpha"), -8, 8, c.with_alpha);
        }
        if (text_editor(based_on_r, imgui->GetID("based"), c.based_on)) {
            bool valid = false;
            for (auto const& other_c : ui_cols)
                if (other_c.name.size && other_c.name == c.based_on) valid = true;

            if (!valid) dyn::Clear(c.based_on);

            recalculate_val = true;
        }

        if (recalculate_val) {
            hex_code_changed = true;
            for (auto const& other_c : ui_cols) {
                if (other_c.name.size && other_c.name == c.based_on) {
                    RecalculateBasedOnCol(c, other_c);
                    hex_code_changed = true;
                    break;
                }
            }
        }

        if (hex_code_changed || hsv_changed) {
            for (auto& other_c : ui_cols)
                if (other_c.based_on.size && other_c.based_on == c.name) RecalculateBasedOnCol(other_c, c);

            WriteColoursFile();
            imgui->platform->gui_update_requirements.requires_another_update = true;
        }

        EditorIncrementPos(gui);
        imgui->PopID();
    }
}

#else

void WriteColoursFile() {}
void WriteSizesFile() {}
void WriteColourMapFile() {}

void SizesGUISliders(EditorGUI*, String) {}
void ColourMapGUIMenus(EditorGUI*, String, String, bool) {}
void ColoursGUISliders(EditorGUI*, String) {}

#endif
