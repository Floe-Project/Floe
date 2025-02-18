// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common_infrastructure/paths.hpp"
#include "common_infrastructure/settings/settings_file.hpp"

#include "state/state_snapshot.hpp"

struct AutosaveState {
    static constexpr u8 k_default_autosave_interval_seconds = 10;
    static constexpr u8 k_default_max_autosaves_per_instance = 16;
    static constexpr u8 k_default_autosave_delete_after_days = 7;

    enum class State { Idle, PendingSave, Saved };
    Atomic<u16> max_autosaves_per_instance {k_default_max_autosaves_per_instance};
    Atomic<u16> autosave_delete_after_days {k_default_autosave_delete_after_days};
    DynamicArrayBounded<char, 16> instance_id;
    TimePoint last_save_time {};
    Mutex mutex {};
    StateSnapshot snapshot {};
    State state {State::Idle};
};

// Run from main thread
// Check if an autosave is needed, and if so, create a snapshot and queue it.
void InitAutosaveState(AutosaveState& state, u64& random_seed, StateSnapshot const& initial_state);
bool AutosaveNeeded(AutosaveState const& state, sts::Settings const& settings);
void QueueAutosave(AutosaveState& state, sts::Settings const& settings, StateSnapshot const& snapshot);

// Run from background thread
void AutosaveToFileIfNeeded(AutosaveState& state, FloePaths const& paths);

enum class AutosaveSetting {
    AutosaveIntervalSeconds,
    MaxAutosavesPerInstance,
    AutosaveDeleteAfterDays,
    Count,
};
struct AutosaveSettingInfo {
    String gui_label;
    String key;
    u16 default_value;
    u16 min_value;
    u16 max_value;
};

AutosaveSettingInfo GetAutosaveSettingInfo(AutosaveSetting setting);

u16 GetAutosaveSetting(sts::Settings const& settings, AutosaveSetting setting);
void SetAutosaveSetting(sts::Settings& settings, AutosaveSetting setting, u16 value);
