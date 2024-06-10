// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui_fwd.hpp"

static constexpr int k_highect_oct_on_keyboard = 10;
static constexpr int k_num_octaves_shown = 8;
static constexpr int k_lowest_starting_oct = 0;
static constexpr int k_highest_starting_oct = ((k_highect_oct_on_keyboard + 1) - k_num_octaves_shown);
static constexpr int k_octave_default_offset = 2;
static constexpr int k_octave_lowest = (k_lowest_starting_oct - k_octave_default_offset);
static constexpr int k_octave_highest = (k_highest_starting_oct - k_octave_default_offset);

struct KeyboardGuiKeyPressed {
    bool is_down;
    u7 note;
    f32 velocity;
};

Optional<KeyboardGuiKeyPressed> KeyboardGui(Gui* g, Rect r, int starting_octave);
