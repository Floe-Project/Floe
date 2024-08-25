// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foundation/foundation.hpp"
#include "os/threading.hpp"
#include "tests/framework.hpp"

#include "tracy/TracyC.h"

//
#include "foundation_tests.hpp"
#include "hosting_tests.hpp"
#include "os_tests.hpp"
#include "utils_tests.hpp"

#define TEST_REGISTER_FUNCTIONS                                                                              \
    X(RegisterFoundationTests)                                                                               \
    X(RegisterOsTests)                                                                                       \
    X(RegisterUtilsTests)                                                                                    \
    X(RegisterHostingTests)                                                                                  \
    X(RegisterAudioUtilsTests)                                                                               \
    X(RegisterVolumeFadeTests)                                                                               \
    X(RegisterStateCodingTests)                                                                              \
    X(RegisterAudioFileTests)                                                                                \
    X(RegisterPresetTests)                                                                                   \
    X(RegisterLibraryLuaTests)                                                                               \
    X(RegisterLibraryMdataTests)                                                                             \
    X(RegisterSampleLibraryLoaderTests)                                                                      \
    X(RegisterParamInfoTests)                                                                                \
    X(RegisterSettingsFileTests)

#define WINDOWS_FP_TEST_REGISTER_FUNCTIONS X(RegisterWindowsSpecificTests)

// Declare the test functions
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
    DEFER { ___tracy_shutdown_profiler(); };
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
                        g_cli_out.Error({}, "Unknown log level: {}", *value);
                        return 1;
                    }
                } else {
                    g_cli_out.Error({}, "Unknown option: {}", key);
                    return 1;
                }
            }
        }

        // Register the test functions
#define X(fn) fn(tester);
        TEST_REGISTER_FUNCTIONS
#if _WIN32
        WINDOWS_FP_TEST_REGISTER_FUNCTIONS
#endif
#undef X

        result = RunAllTests(tester, filter_pattern);
    }

    return result;
}
