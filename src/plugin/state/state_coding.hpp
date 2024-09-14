// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "state_snapshot.hpp"

#define FLOE_PRESET_FILE_EXTENSION ".floe-preset"

struct CodeStateArguments {
    enum class Mode { Decode, Encode };

    Mode mode;
    FunctionRef<ErrorCodeOr<void>(void* data, usize bytes)> read_or_write_data;
    StateSource source;
    bool abbreviated_read;
};

// "Code" as in decode/encode
ErrorCodeOr<void> CodeState(StateSnapshot& state, CodeStateArguments const& args);

ErrorCodeOr<void> DecodeJsonState(StateSnapshot& state, ArenaAllocator& scratch_arena, String json_data);

ErrorCodeOr<StateSnapshot> LoadPresetFile(String filepath, ArenaAllocator& scratch_arena);

ErrorCodeOr<void> SavePresetFile(String path, StateSnapshot const& state);
