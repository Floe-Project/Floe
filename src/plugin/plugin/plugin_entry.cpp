// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/entry.h>

#include "utils/debug/debug.hpp"
#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/global.hpp"

#include "clap/factory/plugin-factory.h"
#include "plugin.hpp"

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_panicking is initialised
static clap_plugin_factory const factory = {
    .get_plugin_count = [](clap_plugin_factory const* factory) -> uint32_t {
        if (!factory) return 0;
        if (PanicOccurred()) return 0;
        return 1;
    },
    .get_plugin_descriptor = [](clap_plugin_factory const* factory,
                                uint32_t index) -> clap_plugin_descriptor const* {
        if (!factory) return nullptr;
        ASSERT(index == 0);
        return &k_plugin_info;
    },
    .create_plugin = [](clap_plugin_factory const* factory,
                        clap_host_t const* host,
                        char const* plugin_id) -> clap_plugin_t const* {
        if (PanicOccurred()) return nullptr;
        if (!factory || !host || !plugin_id) return nullptr;

        try {
            if (NullTermStringsEqual(plugin_id, k_plugin_info.id)) return CreateFloeInstance(host);
        } catch (PanicException) {
        }
        return nullptr;
    },
};

#if __linux__
// https://github.com/ziglang/zig/issues/17908
// NOLINTBEGIN
extern "C" void* __dso_handle;
extern "C" void __cxa_finalize(void*);
__attribute__((destructor)) void ZigBugWorkaround() {
    LogDebug(k_global_log_module, "ZigBugWorkaround");
    __cxa_finalize(__dso_handle);
}
// NOLINTEND
#endif

static bool g_init = false;

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_panicking is initialised
extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry = {
    .clap_version = CLAP_VERSION,

    // init and deinit are never called at the same time as any other clap function, including itself.
    // Might be called more than once. See the clap docs for full details.
    .init = [](char const* plugin_path_c_str) -> bool {
        if (PanicOccurred()) return false;

        try {
            if (Exchange(g_init, true)) return true; // already initialised

            ASSERT(plugin_path_c_str);
            auto const plugin_path = FromNullTerminated(plugin_path_c_str);
            ASSERT(path::IsAbsolute(plugin_path));
            ASSERT(IsValidUtf8(plugin_path));

            [[maybe_unused]] ArenaAllocatorWithInlineStorage<2000> arena {PageAllocator::Instance()};
            GlobalInit({
                .current_binary_path = ({
                    String p = plugin_path;
                    // the CLAP spec says that the path is to the bundle on macOS, so we need to append
                    // the subpaths to get the binary path
                    if (IS_MACOS && g_final_binary_type != FinalBinaryType::Standalone) {
                        constexpr String k_subpath = "/Contents/MacOS/Floe"_s;
                        auto binary_path =
                            arena.AllocateExactSizeUninitialised<char>(p.size + k_subpath.size);
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

            LogDebug(k_clap_log_module, "init DSO");
            LogInfo(k_global_log_module, "Floe version: " FLOE_VERSION_STRING);
            LogInfo(k_global_log_module, "OS: {}", GetOsInfo().name);
            LogDebug(k_global_log_module, "Path: {}", plugin_path);

            return true;
        } catch (PanicException) {
            return false;
        }
    },

    .deinit =
        []() {
            if (PanicOccurred()) return;

            try {
                if (!Exchange(g_init, false)) return; // already deinitialised

                LogDebug(k_clap_log_module, "deinit");

                GlobalDeinit({
                    .shutdown_error_reporting = false,
                });
            } catch (PanicException) {
            }
        },

    .get_factory = [](char const* factory_id) -> void const* {
        if (!factory_id) return nullptr;
        if (PanicOccurred()) return nullptr;
        LogDebug(k_clap_log_module, "get_factory");
        if (NullTermStringsEqual(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &factory;
        return nullptr;
    },
};
