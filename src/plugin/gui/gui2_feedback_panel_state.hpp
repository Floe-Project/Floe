// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

struct FeedbackPanelState {
    bool open {};
    DynamicArrayBounded<char, Kb(4)> description {};
    DynamicArrayBounded<char, 64> email {};
    bool send_diagnostic_data {true};
};
