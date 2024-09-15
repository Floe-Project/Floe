// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui_framework/gui_imgui.hpp"

imgui::WindowSettings PopupWindowSettings(imgui::Context const& imgui);
imgui::WindowSettings ModalWindowSettings(imgui::Context const& imgui);
imgui::WindowSettings
FloeWindowSettings(imgui::Context const& imgui,
                   TrivialFixedSizeFunction<48, void(IMGUI_DRAW_WINDOW_BG_ARGS)> const& draw);
