// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "config.h"

#define FLOE_EDITOR_ENABLED !PRODUCTION_BUILD

constexpr bool k_editor_enabled = FLOE_EDITOR_ENABLED;

#define VERSION_HEX VERSION_PACKED(MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION)

#define PRODUCT_NAME "Floe"
constexpr Version k_floe_version {FLOE_MAJOR_VERSION, FLOE_MINOR_VERSION, FLOE_PATCH_VERSION};

#define FLOE_PRESET_FILE_EXTENSION ".floe-preset"

constexpr u32 k_max_num_active_voices = 32;
constexpr u32 k_num_voices = 64;
constexpr u32 k_max_num_voice_samples = 4;
constexpr u32 k_num_layer_eq_bands = 2;
constexpr u32 k_num_layers = 3;
constexpr usize k_max_library_author_size = 64;
constexpr usize k_max_library_name_size = 64;
constexpr usize k_max_instrument_name_size = 64;
constexpr usize k_max_ir_name_size = 64;
constexpr f32 k_erroneous_sample_value = 1000.0f;

constexpr auto k_repo_subdirs_floe_test_presets = "presets"_s;
constexpr auto k_repo_subdirs_floe_test_libraries = "libraries"_s;
