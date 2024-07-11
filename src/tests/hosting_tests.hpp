// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#if __linux__
#include <dlfcn.h>
#endif

#include "os/filesystem.hpp"
#include "tests/framework.hpp"

TEST_CASE(TestHostingClap) {
#if __linux__
    struct Fixture {
        Fixture(tests::Tester&) {}
        Optional<String> clap_path {};
        bool initialised = false;
    };

    auto& fixture = CreateOrFetchFixtureObject<Fixture>(tester);

    if (!fixture.initialised) {
        fixture.initialised = true;
        auto const exe_path = TRY(CurrentExecutablePath(tester.scratch_arena));
        auto p = exe_path;

        DynamicArray<char> buf {tester.scratch_arena};

        for (auto _ : Range(6)) {
            auto dir = path::Directory(p);
            if (!dir) break;

            dyn::Assign(buf, *dir);
            path::JoinAppend(buf, "Floe.clap"_s);
            if (auto const o = GetFileType(buf); o.HasValue() && o.Value() == FileType::File) {
                fixture.clap_path = buf.ToOwnedSpan();
                break;
            }
        }
    }

    if (!fixture.clap_path) {
        LOG_WARNING("Failed to find Floe.clap");
        return k_success;
    }

    auto test_dlopen = [&](int flags) {
        auto const handle = dlopen(NullTerminated(*fixture.clap_path, tester.scratch_arena), flags);
        if (!handle) TEST_FAILED("Failed to load clap: {}", dlerror()); // NOLINT(concurrency-mt-unsafe)
        DEFER { dlclose(handle); };
    };

    SUBCASE("dlopen RTLD_LOCAL | RTLD_NOW") { test_dlopen(RTLD_LOCAL | RTLD_NOW); }
    SUBCASE("dlopen RTLD_LOCAL | RTLD_DEEPBIND | RTLD_NOW") {
        test_dlopen(RTLD_LOCAL | RTLD_DEEPBIND | RTLD_NOW);
    }
#endif

    return k_success;
}

TEST_REGISTRATION(RegisterHostingTests) { REGISTER_TEST(TestHostingClap); }
