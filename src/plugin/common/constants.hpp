// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "config.h"

#define FLOE_EDITOR_ENABLED !PRODUCTION_BUILD

static constexpr bool k_editor_enabled = FLOE_EDITOR_ENABLED;

#define VERSION_HEX VERSION_PACKED(MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION)

#if FLOE_IS_BETA
#define PRODUCT_NAME "Floe Beta"
constexpr Version k_floe_version {FLOE_MAJOR_VERSION,
                                  FLOE_MINOR_VERSION,
                                  FLOE_PATCH_VERSION,
                                  (u8)FLOE_BETA_VERSION};
#else
#define PRODUCT_NAME "Floe"
Version const k_floe_version {FLOE_MAJOR_VERSION, FLOE_MINOR_VERSION, FLOE_PATCH_VERSION};
#endif

constexpr u32 k_max_num_active_voices = 32;
constexpr u32 k_num_voices = 64;
constexpr u32 k_max_num_voice_samples = 4;
constexpr u32 k_num_layer_eq_bands = 2;
constexpr u32 k_num_layers = 3;
constexpr usize k_max_library_name_size = 64;
constexpr usize k_max_instrument_name_size = 64;
constexpr usize k_max_ir_name_size = 64;
constexpr f32 k_erroneous_sample_value = 1000.0f;
constexpr u8 k_latest_engine_version = 2; // shown on the GUI as compatibility mode
constexpr String k_core_library_name = "Core";
constexpr String k_builtin_library_name = "Built-in";

constexpr auto k_repo_subdirs_floe_test_presets = Array {"presets"_s};
constexpr auto k_repo_subdirs_floe_test_libraries = Array {"libraries"_s};
