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
__attribute__((destructor)) void ZigBugWorkaround() {
    LogDebug(ModuleName::Global, "ZigBugWorkaround");
    __cxa_finalize(__dso_handle);
}
// NOLINTEND
#endif

static bool g_init = false;

// init and deinit are never called at the same time as any other clap function, including itself.
// Might be called more than once. See the clap docs for full details.
static bool ClapEntryInit(char const* plugin_path_c_str) {
    if (PanicOccurred()) return false;

    try {
        if (Exchange(g_init, true)) return true; // already initialised

        if (!plugin_path_c_str) return false;
        auto const plugin_path = FromNullTerminated(plugin_path_c_str);
        if (!path::IsAbsolute(plugin_path)) return false;
        if (!IsValidUtf8(plugin_path)) return false;

        [[maybe_unused]] ArenaAllocatorWithInlineStorage<2000> arena {PageAllocator::Instance()};
        GlobalInit({
            .current_binary_path = ({
                String p = plugin_path;
                // the CLAP spec says that the path is to the bundle on macOS, so we need to append
                // the subpaths to get the binary path
                if (IS_MACOS && g_final_binary_type != FinalBinaryType::Standalone) {
                    constexpr String k_subpath = "/Contents/MacOS/Floe"_s;
                    auto binary_path = arena.AllocateExactSizeUninitialised<char>(p.size + k_subpath.size);
                    usize pos = 0;
                    WriteAndIncrement(pos, binary_path, p);
                    WriteAndIncrement(pos, binary_path, k_subpath);
                    p = binary_path;
                    if constexpr (!PRODUCTION_BUILD) ASSERT(GetFileType(p).HasValue());
                }
                p;
            }),
            .init_error_reporting = false,
            .set_main_thread = false,
        });

#if !PRODUCTION_BUILD
#define VERSION GIT_COMMIT_HASH
#else
#define VERSION "v" FLOE_VERSION_STRING
#endif

#if IS_MACOS
#define OS_NAME "macOS"
#elif IS_WINDOWS
#define OS_NAME "Windows"
#elif IS_LINUX
#define OS_NAME "Linux"
#endif

#ifdef __aarch64__
#define ARCH_NAME "aarch64"
#elif defined(__x86_64__)
#define ARCH_NAME "x86_64"
#else
#error "Unsupported architecture"
#endif

        LogInfo(ModuleName::Clap, "entry.init: ver: " VERSION ", os: " OS_NAME ", arch: " ARCH_NAME);

        LogDebug(ModuleName::Global, "given plugin path: {}", plugin_path);

        return true;
    } catch (PanicException) {
        return false;
    }
}

static void ClapEntryDeinit() {
    if (PanicOccurred()) return;

    try {
        if (!Exchange(g_init, false)) return; // already deinitialised

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
