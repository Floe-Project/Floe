// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/preferences.hpp"

enum class GuiSetting : u8 {
    ShowTooltips,
    ShowKeyboard,
    HighContrastGui,
    ShowInstanceName,
    WindowWidth,
    Count,
};

sts::Descriptor SettingDescriptor(GuiSetting);

UiSize DesiredAspectRatio(sts::Preferences const&);
UiSize DesiredWindowSize(sts::Preferences const&);
