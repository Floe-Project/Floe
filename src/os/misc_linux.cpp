// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdlib.h>
#include <sys/random.h>
#include <unistd.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"

bool FillDistributionInfo(OsInfo& info, String filename) {
    auto const file_data = TRY_OR(ReadEntireFile(filename, PageAllocator::Instance()), return false);
    DEFER { PageAllocator::Instance().Free(file_data.ToByteSpan()); };

    for (auto const line : StringSplitIterator {file_data, '\n'}) {
        auto const equals_pos = Find(line, '=');
        if (!equals_pos) continue;

        auto const key = WhitespaceStripped(line.SubSpan(0, *equals_pos));
        auto value = WhitespaceStripped(line.SubSpan(*equals_pos + 1));
        value = TrimEndIfMatches(value, '"');
        value = TrimStartIfMatches(value, '"');

        if (key == "PRETTY_NAME")
            info.distribution_pretty_name = value;
        else if (key == "ID")
            info.distribution_name = value;
        else if (key == "VERSION_ID")
            info.distribution_version = value;
    }

    return true;
}

// This code is based on Sentry's Native SDK
// Copyright (c) 2019 Sentry (https://sentry.io) and individual contributors.
// SPDX-License-Identifier: MIT
OsInfo GetOsInfo() {
    OsInfo result {};

    if (!FillDistributionInfo(result, "/etc/os-release")) FillDistributionInfo(result, "/usr/lib/os-release");

    struct utsname uts {};
    if (uname(&uts) == 0) {
        char const* release = uts.release;
        size_t num_dots = 0;
        for (; release[0] != '\0'; release++) {
            char c = release[0];
            if (c == '.') num_dots += 1;
            if (!(c >= '0' && c <= '9') && (c != '.' || num_dots > 2)) break;
        }
        auto release_start = release;
        if (release[0] == '-' || release[0] == '.') release_start++;

        if (release_start[0] != '\0') result.build = FromNullTerminated(release_start);

        result.name = FromNullTerminated((char const*)uts.sysname);
        result.version = FromNullTerminated((char const*)uts.release);
    }

    if (!result.name.size) result.name = "Linux"_s;

    return result;
}

String GetFileBrowserAppName() { return "File Explorer"; }

SystemStats GetSystemStats() {
    static SystemStats result {};
    if (!result.page_size) {
        result = {.num_logical_cpus = (u32)sysconf(_SC_NPROCESSORS_ONLN),
                  .page_size = (u32)sysconf(_SC_PAGESIZE)};
    }
    return result;
}

u64 RandomSeed() {
    u64 seed = 0;
    auto _ = getrandom(&seed, sizeof(seed), 0);
    return seed;
}

void OpenFolderInFileBrowser(String path) {
    PathArena path_allocator {Malloc::Instance()};
    // IMPROVE: system is not thread-safe?
    auto _ =
        ::system(fmt::Format(path_allocator, "xdg-open {}\0", path).data); // NOLINT(concurrency-mt-unsafe)
}

void OpenUrlInBrowser(String url) {
    ArenaAllocatorWithInlineStorage<200> arena {Malloc::Instance()};
    // IMPROVE: system is not thread-safe?
    auto _ = ::system(fmt::Format(arena, "xdg-open {}\0", url).data); // NOLINT(concurrency-mt-unsafe)
}
