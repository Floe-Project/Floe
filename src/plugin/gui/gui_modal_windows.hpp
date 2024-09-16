// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui_framework/gui_imgui.hpp"

struct Gui;

enum class ModalWindowType {
    About,
    Licences,
    Metrics,
    LoadError,
    Settings,
    InstallPackages,
    Count,
};

void OpenModalIfNotAlready(imgui::Context& imgui, ModalWindowType type);
void DoModalWindows(Gui* g);

enum class InstallPackagesState {
    SelectFiles,
    Installing,
    Done,
    Count,
};

struct InstallPackagesData {
    InstallPackagesState state = {};

    // main-thread if state != Installing, else worker-thread
    ArenaAllocator arena {PageAllocator::Instance()};
    ArenaList<String, false> selected_package_paths {arena};

    Atomic<bool> installing_packages {};
};

void OpenInstallPackagesModal(Gui* g);
void InstallPackagesSelectFilesDialogResults(Gui* g, Span<MutableString> paths);
void ShutdownInstallPackagesModal(InstallPackagesData& state);
