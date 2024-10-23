// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "settings/settings_file.hpp"

namespace gui_settings {

// This will be nudged to a value that can have a whole-number height component
constexpr u16 k_default_gui_width_approx = 910;

constexpr UiSize k_aspect_ratio_without_keyboard = {100, 61};
constexpr UiSize k_aspect_ratio_with_keyboard = {100, 68};

constexpr u16 k_min_gui_width = k_aspect_ratio_with_keyboard.width * 2;

static_assert(k_aspect_ratio_with_keyboard.width == k_aspect_ratio_without_keyboard.width,
              "We assume this to be the case in a couple of places.");

constexpr UiSize CreateFromWidth(u16 target_width, UiSize aspect_ratio) {
    u16 const low_index = target_width / aspect_ratio.width;
    u16 const high_index = low_index + 1;
    u16 const low_width = aspect_ratio.width * low_index;
    u16 const high_width = aspect_ratio.width * high_index;

    if ((target_width - low_width) < (high_width - target_width))
        return {low_width, (u16)(low_index * aspect_ratio.height)};
    else
        return {high_width, (u16)(high_index * aspect_ratio.height)};
}

constexpr auto GreatestCommonDivisor(auto a, auto b) {
    while (b != 0) {
        auto const t = b;
        b = a % b;
        a = t;
    }
    return a;
}

constexpr UiSize SimplifyAspectRatio(UiSize aspect_ratio) {
    auto const gcd = GreatestCommonDivisor(aspect_ratio.width, aspect_ratio.height);
    return {(u16)(aspect_ratio.width / gcd), (u16)(aspect_ratio.height / gcd)};
}

constexpr UiSize GetNearestAspectRatioSizeInsideSize(UiSize size, UiSize aspect_ratio) {
    aspect_ratio = SimplifyAspectRatio(aspect_ratio);

    if (aspect_ratio.width == 0 || aspect_ratio.height == 0) return {0, 0};
    if (aspect_ratio.width > size.width || aspect_ratio.height > size.height) return {0, 0};

    u32 const low_index = size.width / aspect_ratio.width;
    u32 const low_width = aspect_ratio.width * low_index;
    auto const height_by_width = low_index * aspect_ratio.height;

    if (height_by_width <= size.height) {
        ASSERT(height_by_width <= LargestRepresentableValue<u16>());
        return {(u16)low_width, (u16)height_by_width};
    } else {
        u32 const height_low_index = size.height / aspect_ratio.height;
        u32 const low_height = aspect_ratio.height * height_low_index;
        auto const width_by_height = height_low_index * aspect_ratio.width;
        ASSERT(width_by_height <= size.width);
        return {(u16)width_by_height, (u16)low_height};
    }
}

PUBLIC UiSize CurrentAspectRatio(Settings::Gui const& gui) {
    ASSERT(CheckThreadName("main"));
    return gui.show_keyboard ? k_aspect_ratio_with_keyboard : k_aspect_ratio_without_keyboard;
}

PUBLIC UiSize WindowSize(Settings::Gui const& gui) {
    ASSERT(CheckThreadName("main"));
    auto const w = CreateFromWidth(gui.window_width, CurrentAspectRatio(gui));
    ASSERT(w.width >= k_min_gui_width);
    return w;
}

// We don't set the height because it's calculated based on the aspect ratio and whether the gui keyboard
// is shown or not
PUBLIC void SetWindowSize(Settings::Gui& gui, SettingsTracking& tracking, u16 width) {
    ASSERT(CheckThreadName("main"));
    auto new_width = CreateFromWidth(width, k_aspect_ratio_without_keyboard).width;
    if (new_width < k_min_gui_width) new_width = k_min_gui_width;
    if (gui.window_width == new_width) return;
    gui.window_width = new_width;
    tracking.changed = true;
    if (tracking.on_window_size_change) tracking.on_window_size_change();
}

PUBLIC f32 KeyboardHeight(Settings::Gui const& gui) {
    ASSERT(CheckThreadName("main"));
    auto const width = gui.window_width;
    return (f32)(CreateFromWidth(width, k_aspect_ratio_with_keyboard).height -
                 CreateFromWidth(width, k_aspect_ratio_without_keyboard).height);
}

PUBLIC void SetShowKeyboard(Settings::Gui& gui, SettingsTracking& tracking, bool show) {
    ASSERT(CheckThreadName("main"));
    gui.show_keyboard = show;
    tracking.changed = true;
    if (tracking.on_window_size_change) tracking.on_window_size_change();
}

} // namespace gui_settings
