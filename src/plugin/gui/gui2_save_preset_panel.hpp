// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "common_infrastructure/constants.hpp"

#include "state/state_snapshot.hpp"

struct FilePickerState;
struct Engine;
struct GuiBoxSystem;
struct FloePaths;

struct SavePresetPanelContext {
    Engine& engine;
    FilePickerState& file_picker_state;
    FloePaths const& paths;
};

struct SavePresetPanelState {
    bool open;
    StateMetadata metadata;
};

void OnEngineStateChange(SavePresetPanelState& state, Engine const& engine);

void DoSavePresetPanel(GuiBoxSystem& box_system,
                       SavePresetPanelContext& context,
                       SavePresetPanelState& state);
