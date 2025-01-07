// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

struct SettingsPanelState {
    enum class Tab : u32 {
        Appearance,
        Folders,
        Packages,
        Count,
    };
    bool open {};
    Tab tab {Tab::Appearance};
};
