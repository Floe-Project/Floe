// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "framework/gui_imgui.hpp"

struct Gui;

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

enum class InstallWizardPage {
    Introduction,
    SelectPackages,
    SelectDestination,
    Install,
    Summary,
    Count,
};

struct InstallWizardState {
    InstallWizardPage page = {};
    ArenaAllocator arena {PageAllocator::Instance()};
    ArenaList<String, false> selected_package_paths {arena};
};

void OpenInstallWizard(Gui* g);
void InstallWizardSelectFilesDialogResults(Gui* g, Span<MutableString> paths);
