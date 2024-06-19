// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

#include "gui_button_widgets.hpp"
#include "gui_fwd.hpp"

namespace draggers {

struct Style {
    Style WithNoBackground() const {
        Style r {*this};
        r.background = 0;
        return r;
    }

    Style WithSensitivity(f32 v) const {
        Style r {*this};
        r.sensitivity = v;
        return r;
    }

    f32 sensitivity {250};
    bool always_show_plus {};
    u32 background {};
    u32 text {};
    u32 selection_back {};
    u32 cursor {};
    buttons::Style button_style;
};

Style DefaultStyle(imgui::Context const& imgui);

bool Dragger(Gui* g, imgui::Id id, Rect r, int min, int max, int& value, Style const& style);
bool Dragger(Gui* g, Parameter const& param, Rect r, Style const& style);

bool Dragger(Gui* g, imgui::Id id, LayID lay_id, int min, int max, int& value, Style const& style);
bool Dragger(Gui* g, Parameter const& param, LayID lay_id, Style const& style);

} // namespace draggers
