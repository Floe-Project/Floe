// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "framework/gui_imgui.hpp"
#include "gui_editor_widgets.hpp"

enum class UiSizeUnit : u8 {
    None,
    Points,
    Count,
};

constexpr String k_ui_size_units_text[] = {"None", "Points"};

static_assert(ArraySize(k_ui_size_units_text) == ToInt(UiSizeUnit::Count));

#define COLOURS_DEF_FILENAME    "gui_colours.def"
#define SIZES_DEF_FILENAME      "gui_sizes.def"
#define COLOUR_MAP_DEF_FILENAME "gui_colour_map.def"

enum class UiSizeId : u16 {
#define GUI_SIZE(cat, n, v, unit) cat##n,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
    Count
};

extern f32 ui_sizes[ToInt(UiSizeId::Count)];
extern UiSizeUnit ui_sizes_units[ToInt(UiSizeId::Count)];

using ColourString = DynamicArrayInline<char, 30>;

struct EditorCol {
    ColourString name {};
    u32 col {};

    ColourString based_on {}; // empty for disabled
    f32 with_brightness = 0; // valid if based_on is not empty. 0 to disable
    f32 with_alpha = 0; // valid if based_on is not empty. 0 to disable
};

static constexpr int k_max_num_colours = 74;
extern EditorCol ui_cols[k_max_num_colours];

enum class UiColMap : u16 {
#define GUI_COL_MAP(cat, n, v, high_contrast_col) cat##n,
#include COLOUR_MAP_DEF_FILENAME
#undef GUI_COL_MAP
    Count
};

struct EditorColMap {
    ColourString colour;
    ColourString high_contrast_colour;
};

extern EditorColMap ui_col_map[ToInt(UiColMap::Count)];

// Get Mapped Colour
#define GMC(v) editor::GetCol(UiColMap::v)
// Get Mapped Categoried Colour
#define GMCC(category, val) editor::GetCol(UiColMap::category##val)

namespace editor {

int FindColourIndex(String str);

extern bool g_high_contrast_gui; // IMPROVE: this is hacky

inline u32 GetCol(UiColMap type) {
    String col_string = ui_col_map[ToInt(type)].colour;
    if (g_high_contrast_gui && ui_col_map[ToInt(type)].high_contrast_colour.size)
        col_string = ui_col_map[ToInt(type)].high_contrast_colour;

    if (auto index = FindColourIndex(col_string); index != -1) return ui_cols[index].col;
    return {};
}

inline f32 GetSize(imgui::Context const& s, UiSizeId size_id) {
    f32 res = 1;
    switch (ui_sizes_units[ToInt(size_id)]) {
        case UiSizeUnit::Points: res = s.PointsToPixels(ui_sizes[ToInt(size_id)]); break;
        case UiSizeUnit::None: res = ui_sizes[ToInt(size_id)]; break;
        case UiSizeUnit::Count: PanicIfReached();
    }
    return res;
}

} // namespace editor

void WriteColoursFile();
void WriteSizesFile();
void WriteColourMapFile();

void SizesGUISliders(EditorGUI* g, String search);
void ColourMapGUIMenus(EditorGUI* g, String search, String colour_search, bool show_high_contrast);
void ColoursGUISliders(EditorGUI* gui, String search);
