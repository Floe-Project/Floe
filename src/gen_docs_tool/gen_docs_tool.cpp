// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lua.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

#include "config.h"

// From public domain https://github.com/reactos/reactos/blob/master/sdk/include/psdk/sdkddkver.h
#define NTDDI_WIN7         0x06010000 // Windows 7
#define NTDDI_WIN8         0x06020000 // Windows 8
#define NTDDI_WINBLUE      0x06030000 // Windows 8.1
#define NTDDI_WINTHRESHOLD 0x0A000000 // Windows 10.0.10240 / 1507 / Threshold 1
#define NTDDI_WIN10        0x0A000000
#define NTDDI_WIN10_TH2    0x0A000001 // Windows 10.0.10586 / 1511 / Threshold 2
#define NTDDI_WIN10_RS1    0x0A000002 // Windows 10.0.14393 / 1607 / Redstone 1
#define NTDDI_WIN10_RS2    0x0A000003 // Windows 10.0.15063 / 1703 / Redstone 2
#define NTDDI_WIN10_RS3    0x0A000004 // Windows 10.0.16299 / 1709 / Redstone 3
#define NTDDI_WIN10_RS4    0x0A000005 // Windows 10.0.17134 / 1803 / Redstone 4
#define NTDDI_WIN10_RS5    0x0A000006 // Windows 10.0.17763 / 1809 / Redstone 5
#define NTDDI_WIN10_19H1                                                                                     \
    0x0A000007 // Windows 10.0.18362 / 1903 / 19H1 "Titanium"
               //         10.0.18363 / Vanadium
#define NTDDI_WIN10_VB 0x0A000008 // Windows 10.0.19041 / 2004 / Vibranium
#define NTDDI_WIN10_MN 0x0A000009 // Windows 10.0.19042 / 20H2 / Manganese
#define NTDDI_WIN10_FE 0x0A00000A // Windows 10.0.19043 / 21H1 / Ferrum
#define NTDDI_WIN10_CO 0x0A00000B // Windows 10.0.19044 / 21H2 / Cobalt
#define NTDDI_WIN11_CO NTDDI_WIN10_CO // Windows 10.0.22000 / 21H2 / Cobalt
#define NTDDI_WIN11    NTDDI_WIN11_CO
#define NTDDI_WIN10_NI 0x0A00000C // Windows 10.0.22621 / 22H2 / Nickel
#define NTDDI_WIN11_NI NTDDI_WIN10_NI
#define NTDDI_WIN10_CU 0x0A00000D // Windows 10.0.22621 / 22H2 / Copper

ErrorCodeOr<void> Main(String destination_folder) {
    ArenaAllocator arena {PageAllocator::Instance()};

    {
        auto const lua_path = path::Join(arena, Array {destination_folder, "sample-library-example.lua"_s});
        g_cli_out.Info({}, "Generating {}", lua_path);
        auto file = TRY(OpenFile(lua_path, FileMode::Write));
        auto writer = file.Writer();
        TRY(sample_lib::WriteDocumentedLuaExample(writer, true));
    }

    {
        auto const lua_path =
            path::Join(arena, Array {destination_folder, "sample-library-example-no-comments.lua"_s});
        g_cli_out.Info({}, "Generating {}", lua_path);
        auto file = TRY(OpenFile(lua_path, FileMode::Write));
        auto writer = file.Writer();
        TRY(sample_lib::WriteDocumentedLuaExample(writer, false));
    }

    {
        auto const mdbook_config_path = path::Join(arena, Array {destination_folder, "mdbook_config.txt"_s});
        g_cli_out.Info({}, "Generating {}", mdbook_config_path);
        auto file = TRY(OpenFile(mdbook_config_path, FileMode::Write));
        auto writer = file.Writer();

        // Generate in a format so that in mdbook we can {{#include file-name.txt:anchor-name}}
        auto write_value = [&](String key, String value) -> ErrorCodeOr<void> {
            TRY(fmt::FormatToWriter(writer, "ANCHOR: {}\n", key));
            TRY(fmt::FormatToWriter(writer, "{}\n", value));
            TRY(fmt::FormatToWriter(writer, "ANCHOR_END: {}\n", key));
            return k_success;
        };

        TRY(write_value("lua-version", LUA_VERSION_MAJOR "." LUA_VERSION_MINOR));

        {
            String windows_version = {};
            switch (MIN_WINDOWS_NTDDI_VERSION) {
                case NTDDI_WIN10: windows_version = "Windows 10"; break;
                case NTDDI_WIN10_TH2: windows_version = "Windows 10 (Build 10586)"; break;
                case NTDDI_WIN10_RS1: windows_version = "Windows 10 (Build 14393)"; break;
                case NTDDI_WIN10_RS2: windows_version = "Windows 10 (Build 15063)"; break;
                case NTDDI_WIN10_RS3: windows_version = "Windows 10 (Build 16299)"; break;
                case NTDDI_WIN10_RS4: windows_version = "Windows 10 (Build 17134)"; break;
                case NTDDI_WIN10_RS5: windows_version = "Windows 10 (Build 17763)"; break;
                case NTDDI_WIN10_19H1: windows_version = "Windows 10 (Build 18362)"; break;
                case NTDDI_WIN10_VB: windows_version = "Windows 10 (Build 19041)"; break;
                case NTDDI_WIN10_MN: windows_version = "Windows 10 (Build 19042)"; break;
                case NTDDI_WIN10_FE: windows_version = "Windows 10 (Build 19043)"; break;
                case NTDDI_WIN10_CO: windows_version = "Windows 11"; break;
                case NTDDI_WIN10_NI: windows_version = "Windows 11 (Build 22621)"; break;
                case NTDDI_WIN10_CU: windows_version = "Windows 11 (Build 22621)"; break;
                default: PanicIfReached();
            }

            TRY(write_value("min-windows-version", windows_version));
        }

        {
            auto const macos_version = ParseVersionString(MIN_MACOS_VERSION).Value();
            DynamicArrayBounded<char, 64> macos_version_str {"macOS "};
            ASSERT(macos_version.major != 0);
            fmt::Append(macos_version_str, "{}", macos_version.major);
            if (macos_version.minor != 0) fmt::Append(macos_version_str, ".{}", macos_version.minor);
            if (macos_version.patch != 0) fmt::Append(macos_version_str, ".{}", macos_version.patch);

            String release_name = {};
            switch (macos_version.major) {
                case 11: release_name = "Big Sur"; break;
                case 12: release_name = "Monterey"; break;
                case 13: release_name = "Ventura"; break;
                case 14: release_name = "Sonoma"; break;
                case 15: release_name = "Sequoia"; break;
                default: PanicIfReached();
            }
            fmt::Append(macos_version_str, " ({})", release_name);

            TRY(write_value("min-macos-version", macos_version_str));
        }
    }

    return k_success;
}

static ErrorCodeOr<int> Main(ArgsCstr args) {
    enum class CommandLineArgId : u32 {
        OutFolder,
        Count,
    };

    auto constexpr k_cli_arg_defs = MakeCommandLineArgDefs<CommandLineArgId>({
        {
            .id = (u32)CommandLineArgId::OutFolder,
            .key = "out-folder",
            .description = "Destination folder for generated files",
            .value_type = "path",
            .required = true,
            .num_values = 1,
        },
    });

    ArenaAllocator arena {PageAllocator::Instance()};

    auto const cli_args = TRY(ParseCommandLineArgsStandard(arena,
                                                           args,
                                                           k_cli_arg_defs,
                                                           {
                                                               .handle_help_option = true,
                                                               .print_usage_on_error = true,
                                                           }));

    auto const destination_folder = cli_args[ToInt(CommandLineArgId::OutFolder)].values[0];

    auto result = Main(destination_folder);
    if (result.HasError()) {
        g_cli_out.Error({}, "Error: {}", result.Error());
        return 1;
    }

    return 0;
}

int main(int argc, char** argv) {
    SetThreadName("main");
    auto result = Main({argc, argv});
    if (result.HasError()) {
        g_cli_out.Error({}, "Error: {}", result.Error());
        return 1;
    }
    return result.Value();
}
