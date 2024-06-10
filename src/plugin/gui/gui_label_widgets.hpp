// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_button_widgets.hpp"
#include "layout.hpp"

namespace labels {

using Style = buttons::Style;

PUBLIC Style FakeMenuItem() { return buttons::MenuItem(false); }
Style CentredLeft(u32 col);
Style TopLeft(u32 col);
Style Title(u32 col);
Style BrowserHeading();
Style PresetSectionHeading();
Style PresetBrowserFolder();
Style PresetBrowserFolderPath();
Style Parameter(bool greyed_out = false);
Style ParameterCentred(bool greyed_out = false);
Style ErrorWindowLabel();
Style WaveformLoadingLabel();

void Label(Gui* g, Rect r, String str, Style const& style);
void Label(Gui* g, ::Parameter const& param, Rect r, Style const& style);

void Label(Gui* g, LayID r, String str, Style const& style);
void Label(Gui* g, ::Parameter const& param, LayID r, Style const& style);

} // namespace labels
