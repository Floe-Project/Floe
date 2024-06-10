// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foundation/foundation.hpp"
#include "os/threading.hpp"
#include "tests/framework.hpp"

#include "tracy/TracyC.h"

//
#include "foundation_tests.cpp"
#include "os_tests.cpp"
#include "utils_tests.cpp"

// IMPROVE standardise naming scheme
#define TEST_REGISTER_FUNCTIONS                                                                              \
    X(RegisterFoundationTests)                                                                               \
    X(RegisterOsTests)                                                                                       \
    X(RegisterutilsTests)                                                                                    \
    X(RegisterAudioUtilsTests)                                                                               \
    X(RegisterVolumeFadeTests)                                                                               \
    X(FloeStateCodingTests)                                                                                 \
    X(FloeAudioFormatTests)                                                                                 \
    X(FloePresetTests)                                                                                      \
    X(FloeLibraryLuaTests)                                                                                  \
    X(FloeLibraryTests)                                                                                     \
    X(FloeAssetLoaderTests)                                                                                 \
    X(FloePresetDatabaseTests)                                                                              \
    X(FloeParamStringConversionTests)                                                                       \
    X(FloeSettingsFileTests)

#define WINDOWS_FP_TEST_REGISTER_FUNCTIONS X(RegisterWindowsPlatformTests)

#define X(fn) void fn(tests::Tester&);

TEST_REGISTER_FUNCTIONS
#if _WIN32
WINDOWS_FP_TEST_REGISTER_FUNCTIONS
#endif

#undef X

int main(int argc, char** argv) {
    SetThreadName("Main");
    DebugSetThreadAsMainThread();
#ifdef TRACY_ENABLE
    ___tracy_startup_profiler();
    tracy::SetThreadName("Main");
#endif

    StartupCrashHandler();
    DEFER { ShutdownCrashHandler(); };

    int result;
    {
        ZoneScoped;

        tests::Tester tester;

        Optional<String> filter_pattern {};
        {
            auto const args = Args(argc, argv, false);
            auto const opts = ParseCommandLineArgs(tester.scratch_arena, args);
            for (auto [key, value] : opts) {
                if (key == "filter")
                    filter_pattern = *value;
                else if (key == "log-level") {
                    if (IsEqualToCaseInsensitiveAscii(*value, "debug"_s))
                        tester.log.max_level_allowed = LogLevel::Debug;
                    else if (IsEqualToCaseInsensitiveAscii(*value, "info"_s))
                        tester.log.max_level_allowed = LogLevel::Info;
                    else if (IsEqualToCaseInsensitiveAscii(*value, "warning"_s))
                        tester.log.max_level_allowed = LogLevel::Warning;
                    else if (IsEqualToCaseInsensitiveAscii(*value, "error"_s))
                        tester.log.max_level_allowed = LogLevel::Error;
                    else {
                        cli_out.ErrorLn("Unknown log level: {}", *value);
                        return 1;
                    }
                } else {
                    cli_out.ErrorLn("Unknown option: {}", key);
                    return 1;
                }
            }
        }

#define X(fn)                                                                                                \
    if (!filter_pattern || MatchWildcard(*filter_pattern, #fn)) fn(tester);
        TEST_REGISTER_FUNCTIONS
#if _WIN32
        WINDOWS_FP_TEST_REGISTER_FUNCTIONS
#endif
#undef X

        result = RunAllTests(tester);
    }

#ifdef TRACY_ENABLE
    ___tracy_shutdown_profiler();
#endif
    return result;
}
