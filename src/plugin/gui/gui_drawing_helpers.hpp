// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui_fwd.hpp"

namespace draw {

void DropShadow(imgui::Context const& imgui, Rect r, Optional<f32> rounding = {});
f32x2 GetTextSize(graphics::Font* font, String str, Optional<f32> wrap_width = {});
f32 GetTextWidth(graphics::Font* font, String str, Optional<f32> wrap_width = {});

void VoiceMarkerLine(imgui::Context const& imgui,
                     f32x2 pos,
                     f32 height,
                     f32 left_min,
                     Optional<Line> upper_line,
                     f32 opacity = 1);

} // namespace draw
