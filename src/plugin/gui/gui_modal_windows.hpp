// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "framework/gui_imgui.hpp"

struct Gui;
struct PluginInstance;

enum class ModalWindowType {
    About,
    Licences,
    Metrics,
    LoadError,
    InstInfo,
    Settings,
    InstallWizard,
    Count,
};

void OpenModalIfNotAlready(imgui::Context& imgui, ModalWindowType type);
void DoModalWindows(Gui* g);
