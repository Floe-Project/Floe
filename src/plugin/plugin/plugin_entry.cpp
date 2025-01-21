// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/entry.h>

#include "utils/debug/debug.hpp"
#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/crash_hook.hpp"

#include "clap/factory/plugin-factory.h"
#include "plugin.hpp"

static clap_plugin_factory const factory = {
    .get_plugin_count = [](clap_plugin_factory const*) -> uint32_t { return 1; },
    .get_plugin_descriptor = [](clap_plugin_factory const*, uint32_t) { return &k_plugin_info; },
    .create_plugin = [](clap_plugin_factory const*,
                        clap_host_t const* host,
                        char const* plugin_id) -> clap_plugin_t const* {
        if (NullTermStringsEqual(plugin_id, k_plugin_info.id)) {
            return CreateFloeInstance(host);
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
    g_log.Debug(k_global_log_module, "ZigBugWorkaround");
    __cxa_finalize(__dso_handle);
}
// NOLINTEND
#endif

static void StartupTracy() {
#ifdef TRACY_ENABLE
    ___tracy_startup_profiler();
#endif
}

static void ShutdownTracy() {
#ifdef TRACY_ENABLE
    ___tracy_shutdown_profiler();
#endif
}

static bool g_init = false;

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry = {
    .clap_version = CLAP_VERSION,

    // init and deinit are never called at the same time as any other clap function, including itself.
    // Might be called more than once. See the clap docs for full details.
    .init = [](char const*) -> bool {
        if (Exchange(g_init, true)) return true; // already initialised

        // TODO: consolidate panic handling
        g_panic_handler = [](char const* message, SourceLocation loc) {
            g_log.Error(k_global_log_module, "Panic: {}: {}", loc, message);
            DynamicArrayBounded<char, 2000> buffer {};
            WriteCurrentStacktrace(dyn::WriterFor(buffer), {}, 1);
            g_log.Error(k_global_log_module, "Stacktrace:\n{}", buffer);
            DefaultPanicHandler(message, loc);
        };

        StartupTracy();
        FloeBeginCrashDetection(); // after tracy

        g_log.Debug(k_clap_log_module, "init DSO");
        g_log.Info(k_global_log_module, "Floe version: " FLOE_VERSION_STRING);
        g_log.Info(k_global_log_module, "OS: {}", GetOsInfo().name);

        return true;
    },
    .deinit =
        []() {
            if (!Exchange(g_init, false)) return; // already deinitialised

            g_log.Debug(k_clap_log_module, "deinit");

            FloeEndCrashDetection(); // before tracy
            ShutdownTracy();
        },

    .get_factory = [](char const* factory_id) -> void const* {
        g_log.Debug(k_clap_log_module, "get_factory");
        if (NullTermStringsEqual(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &factory;
        return nullptr;
    },
};
