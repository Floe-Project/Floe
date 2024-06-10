// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "framework/gui_imgui.hpp"

struct Gui;
struct PluginInstance;

enum StandaloneWindows {
    StandaloneWindowsAbout,
    StandaloneWindowsLicences,
    StandaloneWindowsMetrics,
    StandaloneWindowsLoadError,
    StandaloneWindowsInstInfo,
    StandaloneWindowsSettings,
    StandaloneWindowsCount
};

imgui::Id GetStandaloneID(StandaloneWindows type);
void OpenStandalone(imgui::Context& imgui, StandaloneWindows type);
bool IsAnyStandloneOpen(imgui::Context& imgui);

bool DoStandaloneCloseButton(Gui* g);
void DoStandaloneErrorGUI(Gui* g);
void DoErrorsStandalone(Gui* g);
void DoMetricsStandalone(Gui* g);
void DoLoadingOverlay(Gui* g);

void DoInstrumentInfoStandalone(Gui* g);
void DoAboutStandalone(Gui* g);
void DoLicencesStandalone(Gui* g);
void DoSettingsStandalone(Gui* g);
