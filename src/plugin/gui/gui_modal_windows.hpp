// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/gui_imgui.hpp"

struct Gui;

enum class ModalWindowType {
    LoadError,
    Count,
};

void OpenModalIfNotAlready(imgui::Context& imgui, ModalWindowType type);
void DoModalWindows(Gui* g);
