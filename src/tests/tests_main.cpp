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

static ErrorCodeOr<void> SetLogLevel(tests::Tester& tester, String log_level) {
    if (!log_level.size) return k_success; // use default

    for (auto const& [level, name] : Array {
             Pair {LogLevel::Debug, "debug"_s},
             Pair {LogLevel::Info, "info"_s},
             Pair {LogLevel::Warning, "warning"_s},
             Pair {LogLevel::Error, "error"_s},
         }) {
        if (IsEqualToCaseInsensitiveAscii(log_level, name)) {
            tester.log.max_level_allowed = level;
            return k_success;
        }
    }

    g_cli_out.Error({}, "Unknown log level: {}", log_level);
    return ErrorCode {CliError::InvalidArguments};
}

ErrorCodeOr<int> Main(ArgsCstr args) {
    SetThreadName("main");
    DebugSetThreadAsMainThread();
#ifdef TRACY_ENABLE
    ___tracy_startup_profiler();
    DEFER { ___tracy_shutdown_profiler(); };
#endif

    StartupCrashHandler();
    DEFER { ShutdownCrashHandler(); };

    ZoneScoped;

    tests::Tester tester;

    enum class CommandLineArgId {
        Filter,
        LogLevel,
    };

    auto constexpr k_cli_arg_defs = ArrayT<CommandLineArgDefinition>({
        {(u32)CommandLineArgId::Filter, "filter", false, true},
        {(u32)CommandLineArgId::LogLevel, "log-level", false, true},
    });
    static_assert(ValidateCommandLineArgDefs(k_cli_arg_defs));

    auto const cli_args = TRY(ParseCommandLineArgs(StdWriter(g_cli_out.stream),
                                                   tester.scratch_arena,
                                                   args,
                                                   k_cli_arg_defs,
                                                   {
                                                       .handle_help_option = true,
                                                       .print_usage_on_error = true,
                                                   }));

    TRY(SetLogLevel(tester, Arg(cli_args, CommandLineArgId::LogLevel)->value));

    auto const filter_pattern = Arg(cli_args, CommandLineArgId::Filter)->OptValue();

    // Register the test functions
#define X(fn) fn(tester);
    TEST_REGISTER_FUNCTIONS
#if _WIN32
    WINDOWS_FP_TEST_REGISTER_FUNCTIONS
#endif
#undef X

    return RunAllTests(tester, filter_pattern);
}

int main(int argc, char** argv) {
    auto result = Main({argc, argv});
    if (result.HasError()) return 1;
    return result.Value();
}
