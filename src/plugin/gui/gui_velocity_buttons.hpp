// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "descriptors/param_descriptors.hpp"
#include "gui_framework/gui_imgui.hpp"

imgui::DrawButton* GetVelocityButtonDrawingFunction(param_values::VelocityMappingMode button_ind);
