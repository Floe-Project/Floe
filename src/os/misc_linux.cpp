// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdlib.h>
#include <unistd.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"

DynamicArrayInline<char, 64> OperatingSystemName() { return "Linux"_s; }

String GetFileBrowserAppName() { return "File Explorer"; }

SystemStats GetSystemStats() {
    static SystemStats result {};
    if (!result.page_size) {
        result = {.num_logical_cpus = (u32)sysconf(_SC_NPROCESSORS_ONLN),
                  .page_size = (u32)sysconf(_SC_PAGESIZE)};
    }
    return result;
}

void OpenFolderInFileBrowser(String path) {
    PathArena path_allocator;
    // IMPROVE: system is not thread-safe?
    auto _ =
        ::system(fmt::Format(path_allocator, "xdg-open {}\0", path).data); // NOLINT(concurrency-mt-unsafe)
}

void OpenUrlInBrowser(String url) {
    ArenaAllocatorWithInlineStorage<200> arena;
    // IMPROVE: system is not thread-safe?
    auto _ = ::system(fmt::Format(arena, "xdg-open {}\0", url).data); // NOLINT(concurrency-mt-unsafe)
}
