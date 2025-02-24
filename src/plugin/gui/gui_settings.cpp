// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_settings.hpp"

#include "gui_framework/gui_platform.hpp"

sts::Descriptor SettingDescriptor(GuiSetting setting) {
    ASSERT(CheckThreadName("main"));
    switch (setting) {
        case GuiSetting::ShowTooltips:
            return {
                .key = sts::key::k_show_tooltips,
                .value_requirements = sts::ValueType::Bool,
                .default_value = true,
                .gui_label = "Show tooltips",
                .long_description = "Show descriptions when hovering over controls.",
            };
        case GuiSetting::ShowKeyboard:
            return {
                .key = sts::key::k_show_keyboard,
                .value_requirements = sts::ValueType::Bool,
                .default_value = true,
                .gui_label = "Show keyboard",
                .long_description = "Show the on-screen keyboard.",
            };
        case GuiSetting::HighContrastGui:
            return {
                .key = sts::key::k_high_contrast_gui,
                .value_requirements = sts::ValueType::Bool,
                .default_value = false,
                .gui_label = "High contrast GUI",
                .long_description = "Use a high contrast colour scheme.",
            };
        case GuiSetting::ShowInstanceName:
            return {
                .key = "show-instance-name"_s,
                .value_requirements = sts::ValueType::Bool,
                .default_value = true,
                .gui_label = "Show instance name",
                .long_description = "Show the name of the instance in the top panel GUI.",
            };
        case GuiSetting::WindowWidth:
            return {
                .key = sts::key::k_window_width,
                .value_requirements =
                    sts::Descriptor::IntRequirements {
                        .min_value = (s64)k_min_gui_width,
                        .max_value = (s64)k_largest_gui_size,
                        .custom_constrainer =
                            [](s64& value) {
                                static_assert(k_aspect_ratio_with_keyboard.width ==
                                              k_aspect_ratio_without_keyboard.width);
                                value =
                                    SizeWithAspectRatio((u16)value, k_aspect_ratio_without_keyboard).width;
                            },
                        .clamp_to_range = true,
                    },
                .default_value = (s64)k_default_gui_width,
                .gui_label = "Window width",
                .long_description = "The width of the main window.",
            };
        case GuiSetting::Count: PanicIfReached();
    }
}

UiSize DesiredAspectRatio(sts::Preferences const& settings) {
    ASSERT(CheckThreadName("main"));
    return sts::GetBool(settings, SettingDescriptor(GuiSetting::ShowKeyboard))
               ? k_aspect_ratio_with_keyboard
               : k_aspect_ratio_without_keyboard;
}

UiSize DesiredWindowSize(sts::Preferences const& settings) {
    ASSERT(CheckThreadName("main"));
    return SizeWithAspectRatio((u16)sts::GetInt(settings, SettingDescriptor(GuiSetting::WindowWidth)),
                               DesiredAspectRatio(settings));
}

f32 KeyboardHeight(sts::Preferences const& settings) {
    ASSERT(CheckThreadName("main"));
    static_assert(k_aspect_ratio_with_keyboard.height > k_aspect_ratio_without_keyboard.height);
    static_assert(k_aspect_ratio_with_keyboard.width == k_aspect_ratio_without_keyboard.width);
    auto const width = (u16)sts::GetInt(settings, SettingDescriptor(GuiSetting::WindowWidth));
    return (f32)(SizeWithAspectRatio(width, k_aspect_ratio_with_keyboard).height -
                 SizeWithAspectRatio(width, k_aspect_ratio_without_keyboard).height);
}
