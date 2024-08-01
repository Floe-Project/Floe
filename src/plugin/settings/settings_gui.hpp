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

PUBLIC UiSize GetNearestAspectRatioSizeInsideSize(UiSize size, UiSize aspect_ratio) {
    u16 const low_index = size.width / aspect_ratio.width;
    u16 const low_width = aspect_ratio.width * low_index;
    auto const height_by_width = (u16)(low_index * aspect_ratio.height);

    if (height_by_width <= size.height)
        return {low_width, height_by_width};
    else {
        u16 const height_low_index = size.height / aspect_ratio.height;
        u16 const height_low_height = aspect_ratio.height * height_low_index;
        return {(u16)(aspect_ratio.width * height_low_index), height_low_height};
    }
}

PUBLIC UiSize CurrentAspectRatio(Settings::Gui const& gui) {
    return gui.show_keyboard ? k_aspect_ratio_with_keyboard : k_aspect_ratio_without_keyboard;
}

PUBLIC UiSize WindowSize(Settings::Gui const& gui) {
    return CreateFromWidth(gui.window_width, CurrentAspectRatio(gui));
}

// We don't set the height because it's calculated based on the aspect ratio and whether the gui keyboard
// is shown or not
PUBLIC void SetWindowSize(Settings::Gui& gui, SettingsTracking& tracking, u16 width) {
    gui.window_width = CreateFromWidth(width, k_aspect_ratio_without_keyboard).width;
    ASSERT(gui.window_width != 0);
    tracking.changed = true;
    tracking.window_size_change_listeners.Call();
}

PUBLIC f32 KeyboardHeight(Settings::Gui const& gui) {
    auto const width = gui.window_width;
    return (f32)(CreateFromWidth(width, k_aspect_ratio_with_keyboard).height -
                 CreateFromWidth(width, k_aspect_ratio_without_keyboard).height);
}

PUBLIC void SetShowKeyboard(Settings::Gui& gui, SettingsTracking& tracking, bool show) {
    gui.show_keyboard = show;
    tracking.changed = true;
    tracking.window_size_change_listeners.Call();
}

} // namespace gui_settings
