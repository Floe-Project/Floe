// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui_fwd.hpp"
#include "layout.hpp"

namespace knobs {

struct Style {
    Style& GreyedOut(bool state) {
        greyed_out = state;
        return *this;
    }

    bool bidirectional {};
    bool greyed_out {};
    u32 highlight_col {};
    u32 line_col {};
    bool is_fake {};
    Optional<f32> overload_position {};
};

Style DefaultKnob(Optional<u32> highlight_col = {});
Style BidirectionalKnob(Optional<u32> highlight_col = {});
Style FakeKnobStyle();

bool Knob(Gui* g, imgui::Id id, Rect r, f32& percent, f32 default_percent, Style const& style);
bool Knob(Gui* g, imgui::Id id, Parameter const& param, Rect r, Style const& style);
bool Knob(Gui* g, Parameter const& param, Rect r, Style const& style);

bool Knob(Gui* g, imgui::Id id, LayID r, f32& percent, f32 default_percent, Style const& style);
bool Knob(Gui* g, imgui::Id id, Parameter const& param, LayID r, Style const& style);
bool Knob(Gui* g, Parameter const& param, LayID r, Style const& style);

void FakeKnob(Gui* g, Rect r);

} // namespace knobs
