// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/entry.h>

#include "utils/debug/debug.hpp"
#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/global.hpp"

#include "clap/factory/plugin-factory.h"
#include "plugin.hpp"

static u32 ClapFactoryGetPluginCount(clap_plugin_factory const* factory) {
    if (!factory) return 0;
    if (PanicOccurred()) return 0;
    return 1;
}

static clap_plugin_descriptor const* ClapFactoryGetPluginDescriptor(clap_plugin_factory const* factory,
                                                                    uint32_t index) {
    if (!factory) return nullptr;
    if (PanicOccurred()) return nullptr;
    if (index != 0) return nullptr;
    return &k_plugin_info;
}

static clap_plugin const*
ClapFactoryCreatePlugin(clap_plugin_factory const* factory, clap_host_t const* host, char const* plugin_id) {
    if (PanicOccurred()) return nullptr;
    if (!factory || !host || !plugin_id) return nullptr;

    try {
        if (NullTermStringsEqual(plugin_id, k_plugin_info.id)) return CreateFloeInstance(host);
    } catch (PanicException) {
    }
    return nullptr;
}

static clap_plugin_factory const factory = {
    .get_plugin_count = ClapFactoryGetPluginCount,
    .get_plugin_descriptor = ClapFactoryGetPluginDescriptor,
    .create_plugin = ClapFactoryCreatePlugin,
};

#if __linux__
// https://github.com/ziglang/zig/issues/17908
// NOLINTBEGIN
extern "C" void* __dso_handle;
extern "C" void __cxa_finalize(void*);
__attribute__((destructor)) void ZigBugWorkaround() { __cxa_finalize(__dso_handle); }
// NOLINTEND
#endif

static bool g_init = false;

static Atomic<u32> g_inside_init {0};

// init and deinit are never called at the same time as any other clap function, including itself.
// Might be called more than once. See the clap docs for full details.
static bool ClapEntryInit(char const* plugin_path_c_str) {
    if (PanicOccurred()) return false;

    try {
        if (Exchange(g_init, true)) return true; // already initialised

        auto const inside_init = g_inside_init.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
        DEFER { g_inside_init.FetchSub(1, RmwMemoryOrder::AcquireRelease); };
        if (inside_init) return false; // The host is misbehaving

        if (!plugin_path_c_str) return false;
        auto const plugin_path = FromNullTerminated(plugin_path_c_str);
        constexpr auto k_plugin_path_max_len = Kb(2);
        if (plugin_path.size > k_plugin_path_max_len) return false;
        if (!path::IsAbsolute(plugin_path)) return false;
        if (!IsValidUtf8(plugin_path)) return false;

        DynamicArrayBounded<char, k_plugin_path_max_len> modified_plugin_path;
        GlobalInit({
            .current_binary_path = ({
                String p = plugin_path;
                // the CLAP spec says that the path is to the bundle on macOS, so we need to append
                // the subpaths to get the binary path
                if (IS_MACOS && g_final_binary_type != FinalBinaryType::Standalone) {
                    constexpr String k_subpath = "/Contents/MacOS/Floe"_s;
                    if (p.size + k_subpath.size > k_plugin_path_max_len) return false;
                    dyn::AppendSpanAssumeCapacity(modified_plugin_path, p);
                    dyn::AppendSpanAssumeCapacity(modified_plugin_path, k_subpath);
                    p = modified_plugin_path;
                    if constexpr (!PRODUCTION_BUILD) ASSERT(GetFileType(p).HasValue());
                }
                p;
            }),
            .init_error_reporting = false,
            .set_main_thread = false,
        });

        LogInfo(ModuleName::Clap,
                "entry.init: ver: " FLOE_VERSION_STRING ", os: " OS_DISPLAY_NAME
                ", arch: " ARCH_DISPLAY_NAME);

        LogDebug(ModuleName::Global, "given plugin path: {}", plugin_path);

        return true;
    } catch (PanicException) {
        return false;
    }
}

static Atomic<u32> g_inside_deinit {0};

static void ClapEntryDeinit() {
    if (PanicOccurred()) return;

    try {
        if (!Exchange(g_init, false)) return; // already deinitialised

        auto const inside_deinit = g_inside_deinit.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
        DEFER { g_inside_deinit.FetchSub(1, RmwMemoryOrder::AcquireRelease); };
        if (inside_deinit) return; // The host is misbehaving

        LogInfo(ModuleName::Clap, "entry.deinit");

        GlobalDeinit({
            .shutdown_error_reporting = false,
        });
    } catch (PanicException) {
    }
}

static void const* ClapEntryGetFactory(char const* factory_id) {
    if (!factory_id) return nullptr;
    if (PanicOccurred()) return nullptr;
    LogInfo(ModuleName::Clap, "entry.get_factory");
    if (NullTermStringsEqual(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &factory;
    return nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = ClapEntryInit,
    .deinit = ClapEntryDeinit,
    .get_factory = ClapEntryGetFactory,
};
