// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_label_widgets.hpp"

#include "gui.hpp"
#include "processor/param.hpp"

namespace labels {

//
//
//
void Label(Gui* g, Rect r, String str, Style const& style) { buttons::FakeButton(g, r, str, style); }

void Label(Gui* g, ::Parameter const& param, Rect r, Style const& style) {
    Label(g, r, param.info.gui_label, style);
}

void Label(Gui* g, LayID id, String str, Style const& style) { Label(g, g->layout.GetRect(id), str, style); }

void Label(Gui* g, ::Parameter const& param, LayID id, Style const& style) {
    Label(g, param, g->layout.GetRect(id), style);
}

} // namespace labels
