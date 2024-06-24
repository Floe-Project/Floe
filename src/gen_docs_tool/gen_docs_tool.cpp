#include <foundation/foundation.hpp>
#include <os/misc.hpp>

#include "utils/logger/logger.hpp"

#include "plugin/sample_library/sample_library.hpp"

#include "config.h"

// From public domain https://github.com/reactos/reactos/blob/master/sdk/include/psdk/sdkddkver.h
#define __NTDDI_WIN7         0x06010000 // Windows 7
#define __NTDDI_WIN8         0x06020000 // Windows 8
#define __NTDDI_WINBLUE      0x06030000 // Windows 8.1
#define __NTDDI_WINTHRESHOLD 0x0A000000 // Windows 10.0.10240 / 1507 / Threshold 1
#define __NTDDI_WIN10        0x0A000000
#define __NTDDI_WIN10_TH2    0x0A000001 // Windows 10.0.10586 / 1511 / Threshold 2
#define __NTDDI_WIN10_RS1    0x0A000002 // Windows 10.0.14393 / 1607 / Redstone 1
#define __NTDDI_WIN10_RS2    0x0A000003 // Windows 10.0.15063 / 1703 / Redstone 2
#define __NTDDI_WIN10_RS3    0x0A000004 // Windows 10.0.16299 / 1709 / Redstone 3
#define __NTDDI_WIN10_RS4    0x0A000005 // Windows 10.0.17134 / 1803 / Redstone 4
#define __NTDDI_WIN10_RS5    0x0A000006 // Windows 10.0.17763 / 1809 / Redstone 5
#define __NTDDI_WIN10_19H1                                                                                   \
    0x0A000007 // Windows 10.0.18362 / 1903 / 19H1 "Titanium"
               //         10.0.18363 / Vanadium
#define __NTDDI_WIN10_VB 0x0A000008 // Windows 10.0.19041 / 2004 / Vibranium
#define __NTDDI_WIN10_MN 0x0A000009 // Windows 10.0.19042 / 20H2 / Manganese
#define __NTDDI_WIN10_FE 0x0A00000A // Windows 10.0.19043 / 21H1 / Ferrum
#define __NTDDI_WIN10_CO 0x0A00000B // Windows 10.0.19044 / 21H2 / Cobalt
#define __NTDDI_WIN11_CO NTDDI_WIN10_CO // Windows 10.0.22000 / 21H2 / Cobalt
#define __NTDDI_WIN11    NTDDI_WIN11_CO
#define __NTDDI_WIN10_NI 0x0A00000C // Windows 10.0.22621 / 22H2 / Nickel
#define __NTDDI_WIN11_NI NTDDI_WIN10_NI
#define __NTDDI_WIN10_CU 0x0A00000D // Windows 10.0.22621 / 22H2 / Copper

ErrorCodeOr<void> Main(String destination_folder) {
    ArenaAllocator arena {PageAllocator::Instance()};

    {
        auto const lua_path = path::Join(arena, Array {destination_folder, "sample-library-example.lua"_s});
        stdout_log.InfoLn("Generating {}", lua_path);
        auto file = TRY(OpenFile(lua_path, FileMode::Write));
        auto writer = file.Writer();
        TRY(sample_lib::WriteDocumentedLuaExample(writer));
    }

    {
        auto const mdbook_config_path = path::Join(arena, Array {destination_folder, "mdbook_config.txt"_s});
        stdout_log.InfoLn("Generating {}", mdbook_config_path);
        auto file = TRY(OpenFile(mdbook_config_path, FileMode::Write));
        auto writer = file.Writer();

        // Generate in a format so that in mdbook we can {{#include file-name.txt:anchor-name}}
        auto write_value = [&](String key, String value) -> ErrorCodeOr<void> {
            TRY(fmt::FormatToWriter(writer, "ANCHOR: {}\n", key));
            TRY(fmt::FormatToWriter(writer, "{}\n", value));
            TRY(fmt::FormatToWriter(writer, "ANCHOR_END: {}\n", key));
            return k_success;
        };

        String windows_version = {};
        switch (MIN_WINDOWS_NTDDI_VERSION) {
            case __NTDDI_WIN10: windows_version = "Windows 10"; break;
            case __NTDDI_WIN10_TH2: windows_version = "Windows 10 (Build 10586)"; break;
            case __NTDDI_WIN10_RS1: windows_version = "Windows 10 (Build 14393)"; break;
            case __NTDDI_WIN10_RS2: windows_version = "Windows 10 (Build 15063)"; break;
            case __NTDDI_WIN10_RS3: windows_version = "Windows 10 (Build 16299)"; break;
            case __NTDDI_WIN10_RS4: windows_version = "Windows 10 (Build 17134)"; break;
            case __NTDDI_WIN10_RS5: windows_version = "Windows 10 (Build 17763)"; break;
            case __NTDDI_WIN10_19H1: windows_version = "Windows 10 (Build 18362)"; break;
            case __NTDDI_WIN10_VB: windows_version = "Windows 10 (Build 19041)"; break;
            case __NTDDI_WIN10_MN: windows_version = "Windows 10 (Build 19042)"; break;
            case __NTDDI_WIN10_FE: windows_version = "Windows 10 (Build 19043)"; break;
            case __NTDDI_WIN10_CO: windows_version = "Windows 11"; break;
            case __NTDDI_WIN10_NI: windows_version = "Windows 11 (Build 22621)"; break;
            case __NTDDI_WIN10_CU: windows_version = "Windows 11 (Build 22621)"; break;
            default: PanicIfReached();
        }

        TRY(write_value("min-windows-version", windows_version));
        TRY(write_value("min-macos-version", MIN_MACOS_VERSION));
    }

    return k_success;
}

int main(int argc, char** argv) {
    ArenaAllocator arena {PageAllocator::Instance()};

    if (argc != 2) {
        stdout_log.ErrorLn("Usage: {} <destination_folder>", argv[0]);
        return 1;
    }

    auto const destination_folder = FromNullTerminated(argv[1]);

    auto result = Main(destination_folder);
    if (result.HasError()) {
        stdout_log.ErrorLn("Error: {}", result.Error());
        return 1;
    }

    return 0;
}
