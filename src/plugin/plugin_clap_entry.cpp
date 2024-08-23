// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/entry.h>
#include <stdio.h>

#include "utils/debug/debug.hpp"
#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"

#include "clap/factory/plugin-factory.h"
#include "plugin.hpp"

static clap_plugin_factory const factory = {
    .get_plugin_count = [](clap_plugin_factory const*) -> uint32_t { return 1; },
    .get_plugin_descriptor = [](clap_plugin_factory const*, uint32_t) { return &k_plugin_info; },
    .create_plugin = [](clap_plugin_factory const*,
                        clap_host_t const* host,
                        char const* plugin_id) -> clap_plugin_t const* {
        if (NullTermStringsEqual(plugin_id, k_plugin_info.id)) {
            auto& p = CreatePlugin(host);
            return &p;
        }
        return nullptr;
    },
};

#if __linux__
// NOLINTBEGIN
extern "C" void* __dso_handle;
extern "C" void __cxa_finalize(void*);
__attribute__((destructor)) void ZigBugWorkaround() {
    g_log.DebugLn(k_global_log_cat, "ZigBugWorkaround");
    __cxa_finalize(__dso_handle);
}
// NOLINTEND
#endif

static u16 g_init_count = 0;

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry = {
    .clap_version = CLAP_VERSION,

    // init and deinit are never called at the same time as any other clap function, including itself.
    // Might be called more than once. See the clap docs for full details.
    .init = [](char const*) -> bool {
        if (g_init_count++ == 0) {
            g_panic_handler = [](char const* message, SourceLocation loc) {
                g_log.ErrorLn(k_global_log_cat, "Panic: {}: {}", loc, message);
                DefaultPanicHandler(message, loc);
            };
#ifdef TRACY_ENABLE
            ___tracy_startup_profiler();
#endif
            // after tracy
            StartupCrashHandler();
        }
        g_log.DebugLn(k_clap_log_cat, "init DSO");
        g_log.InfoLn(k_global_log_cat, "Floe version: " FLOE_VERSION_STRING);
        g_log.InfoLn(k_global_log_cat, "OS: {}", OperatingSystemName());

        return true;
    },
    .deinit =
        []() {
            g_log_file.DebugLn(k_clap_log_cat, "deinit");
            if (--g_init_count == 0) {
                ShutdownCrashHandler();
#ifdef TRACY_ENABLE
                ___tracy_shutdown_profiler();
#endif
            }
        },

    .get_factory = [](char const* factory_id) -> void const* {
        g_log_file.DebugLn(k_clap_log_cat, "get_factory");
        if (NullTermStringsEqual(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &factory;
        return nullptr;
    },
};
