// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/entry.h>

#include "os/misc.hpp"
#include "utils/debug/debug.hpp"

#include "clap/factory/plugin-factory.h"
#include "plugin.hpp"

static clap_plugin_factory_t const factory = {
    .get_plugin_count = [](clap_plugin_factory_t const*) -> uint32_t { return 1; },
    .get_plugin_descriptor = [](clap_plugin_factory_t const*, uint32_t) { return &k_plugin_info; },
    .create_plugin = [](clap_plugin_factory const*,
                        clap_host_t const* host,
                        char const* plugin_id) -> clap_plugin_t const* {
        g_logger.DebugLn("create_plugin");
        if (NullTermStringsEqual(plugin_id, k_plugin_info.id)) {
            auto& p = CreatePlugin(host);
            return &p;
        }
        return nullptr;
    },
};

bool g_clap_entry_init = false;

__attribute__((destructor)) static void Init() { g_logger.DebugLn("__attribute__((destructor))"); }

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = [](char const*) -> bool {
        g_logger.DebugLn("init");
        g_panic_handler = [](char const* message, SourceLocation loc) {
            g_logger.ErrorLn("{}: {}", loc, message);
            DefaultPanicHandler(message, loc);
        };
        StartupCrashHandler();
        g_clap_entry_init = true;
        return true;
    },
    .deinit =
        []() {
            if (g_clap_entry_init) {
                g_logger.DebugLn("deinit");
                ShutdownCrashHandler();
                g_clap_entry_init = false;
            }
        },
    .get_factory = [](char const* factory_id) -> void const* {
        g_logger.DebugLn("get_factory");
        if (NullTermStringsEqual(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &factory;
        return nullptr;
    },
};
