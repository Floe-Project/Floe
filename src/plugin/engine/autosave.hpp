// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common_infrastructure/paths.hpp"

#include "state/state_snapshot.hpp"

struct AutosaveState {
    enum class State { Idle, PendingSave, Saved };
    DynamicArrayBounded<char, 16> instance_id;
    TimePoint last_save_time {};
    Mutex mutex {};
    StateSnapshot snapshot {};
    State state {State::Idle};
};

// Run from main thread
// Check if an autosave is needed, and if so, create a snapshot and queue it.
void InitAutosaveState(AutosaveState& state, u64& random_seed, StateSnapshot const& initial_state);
bool AutosaveNeeded(AutosaveState const& state);
void QueueAutosave(AutosaveState& state, StateSnapshot const& snapshot);

// Run from background thread
void AutosaveToFileIfNeeded(AutosaveState& state, FloePaths const& paths);
