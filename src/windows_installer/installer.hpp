// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"

#include "resources.h"

enum class ComponentTypes : u32 {
    Clap,
    VST3,
#ifdef CORE_LIBRARY_ZIP_PATH
    CoreLibrary,
#endif
    Count,
};

struct ComponentInfo {
    String name;
    Optional<KnownDirectories> install_dir;
    String install_dir_fallback;
    String filename;
    int resource_id;
};

constexpr auto k_plugin_infos = Array {
    ComponentInfo {
        .name = "Floe CLAP Plugin v" FLOE_VERSION_STRING,
        .install_dir = KnownDirectories::ClapPlugin,
        .install_dir_fallback = "C:\\Program Files\\Common Files\\CLAP"_s,
        .filename = path::Filename(CLAP_PLUGIN_PATH),
        .resource_id = CLAP_PLUGIN_RC_ID,
    },
    ComponentInfo {
        .name = "Floe VST3 Plugin v" FLOE_VERSION_STRING,
        .install_dir = KnownDirectories::Vst3Plugin,
        .install_dir_fallback = "C:\\Program Files\\Common Files\\VST3"_s,
        .filename = path::Filename(VST3_PLUGIN_PATH),
        .resource_id = VST3_PLUGIN_RC_ID,
    },
#ifdef CORE_LIBRARY_ZIP_PATH
    ComponentInfo {
        .name = "Floe Core Library",
        .install_dir = nullopt,
        .install_dir_fallback = "C:\\Users\\Public\\Floe"_s,
        .filename = "Core",
        .resource_id = CORE_LIBRARY_RC_ID,
    },
#endif
};

static_assert(k_plugin_infos.size == ToInt(ComponentTypes::Count));

ErrorCodeOr<Span<u8 const>> GetResource(int resource_id);
