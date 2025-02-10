// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

enum class UiSizeUnit : u8 {
    None,
    Points,
    Count,
};

constexpr String k_ui_size_units_text[] = {"None", "Points"};
static_assert(ArraySize(k_ui_size_units_text) == ToInt(UiSizeUnit::Count));

// The build system needs to -I the directory containing these files
#define COLOURS_DEF_FILENAME    "gui_colours.def"
#define SIZES_DEF_FILENAME      "gui_sizes.def"
#define COLOUR_MAP_DEF_FILENAME "gui_colour_map.def"

enum class UiSizeId : u16 {
#define GUI_SIZE(cat, n, v, unit) cat##n,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
    Count
};

static constexpr int k_max_num_colours = 74;

enum class UiColMap : u16 {
#define GUI_COL_MAP(cat, n, v, high_contrast_col) cat##n,
#include COLOUR_MAP_DEF_FILENAME
#undef GUI_COL_MAP
    Count
};

struct ColourString {
    constexpr ColourString() = default;
    constexpr ColourString(String s) : size(s.size) { __builtin_memcpy(data, s.data, s.size); }
    constexpr operator String() const { return {data, size}; }
    void NullTerminate() {
        ASSERT(size < ArraySize(data));
        data[size] = 0;
    }
    usize size {};
    char data[30] {};
};

struct EditorCol {
    ColourString name {};
    u32 col {};

    ColourString based_on {}; // empty for disabled
    f32 with_brightness = 0; // valid if based_on is not empty. 0 to disable
    f32 with_alpha = 0; // valid if based_on is not empty. 0 to disable
};

struct EditorColMap {
    ColourString colour;
    ColourString high_contrast_colour;
};

struct LiveEditGui {
    f32 ui_sizes[ToInt(UiSizeId::Count)];
    UiSizeUnit ui_sizes_units[ToInt(UiSizeId::Count)];
    String ui_sizes_names[ToInt(UiSizeId::Count)];
    EditorCol ui_cols[k_max_num_colours];
    EditorColMap ui_col_map[ToInt(UiColMap::Count)];
};
