#pragma once
#include <shlobj.h>
#include <windows.h>
//
#include "os/misc_windows.hpp"
#include "os/undef_windows_macros.h"
//

#include "foundation/foundation.hpp"

#include "common_infrastructure/error_reporting.hpp"

// Never change this.
constexpr auto k_uninstall_key =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{1395024D-2B55-4B81-88CA-26DF09D175B1}";

PUBLIC Optional<String> UninstallerPath(ArenaAllocator& arena, bool create) {
    PWSTR wide_dir {};
    auto const hr = SHGetKnownFolderPath(FOLDERID_ProgramFiles,
                                         create ? KF_FLAG_CREATE : KF_FLAG_DEFAULT,
                                         nullptr,
                                         &wide_dir);
    DEFER { CoTaskMemFree(wide_dir); };
    if (hr != S_OK) {
        ReportError(ErrorLevel::Warning, k_nullopt, "Failed to get Program Files directory: {}", hr);
        return k_nullopt;
    }

    auto path = *Narrow(arena, FromNullTerminated(wide_dir));
    path = path::JoinAppendResizeAllocation(arena, path, Array {"Floe"_s});

    if (create) {
        TRY_OR(CreateDirectory(path, {.create_intermediate_directories = false, .fail_if_exists = false}), {
            ReportError(ErrorLevel::Warning, k_nullopt, "Failed to create directory '{}': {}", path, error);
            return k_nullopt;
        });
    }

    path = path::JoinAppendResizeAllocation(arena, path, Array {"Floe-Uninstaller.exe"_s});

    return path;
}

PUBLIC void CreateUninstallRegistryKey(ArenaAllocator& arena, String uninstaller_exe_path) {
    // Create the uninstall registry key
    HKEY h_key;
    if (auto const rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                                        k_uninstall_key,
                                        0,
                                        nullptr,
                                        REG_OPTION_NON_VOLATILE,
                                        KEY_WRITE,
                                        nullptr,
                                        &h_key,
                                        nullptr);
        rc != ERROR_SUCCESS) {
        ReportError(ErrorLevel::Warning,
                    k_nullopt,
                    "Failed to create registry key for uninstaller: {}",
                    Win32ErrorCode((DWORD)rc));
        return;
    }
    DEFER { RegCloseKey(h_key); };

    // Set the uninstall string (path to the uninstaller).
    // wide_path has a null terminator but it's not included in the size.
    auto const wide_path = *WidenAllocNullTerm(arena, uninstaller_exe_path);
    if (auto const rc = RegSetValueExW(h_key,
                                       L"UninstallString",
                                       0,
                                       REG_SZ,
                                       (const BYTE*)wide_path.data,
                                       (DWORD)(wide_path.size + 1) * sizeof(wchar_t));
        rc != ERROR_SUCCESS) {
        ReportError(ErrorLevel::Warning,
                    k_nullopt,
                    "Failed to set uninstall string in registry: {}",
                    Win32ErrorCode((DWORD)rc));
        return;
    }

    WString constexpr k_display_name = L"Floe Audio Plugin"_s;
    if (auto const rc = RegSetValueExW(h_key,
                                       L"DisplayName",
                                       0,
                                       REG_SZ,
                                       (const BYTE*)k_display_name.data,
                                       (DWORD)(k_display_name.size + 1) * sizeof(wchar_t));
        rc != ERROR_SUCCESS) {
        ReportError(ErrorLevel::Warning,
                    k_nullopt,
                    "Failed to set display name in registry: {}",
                    Win32ErrorCode((DWORD)rc));
        return;
    }

    // We can use the uninstaller as the icon source
    RegSetValueExW(h_key,
                   L"DisplayIcon",
                   0,
                   REG_SZ,
                   (const BYTE*)wide_path.data,
                   (DWORD)(wide_path.size + 1) * sizeof(wchar_t));

    {
        DWORD const no_modify = 1;
        RegSetValueExW(h_key, L"NoModify", 0, REG_DWORD, (const BYTE*)&no_modify, sizeof(no_modify));
    }

    {
        DWORD const no_repair = 1;
        RegSetValueExW(h_key, L"NoRepair", 0, REG_DWORD, (const BYTE*)&no_repair, sizeof(no_repair));
    }
}

PUBLIC void RemoveUninstallRegistryKey() {
    if (auto const rc = RegDeleteKeyW(HKEY_LOCAL_MACHINE, k_uninstall_key); rc != ERROR_SUCCESS)
        ReportError(ErrorLevel::Warning,
                    k_nullopt,
                    "Failed to delete uninstall registry key: {}",
                    Win32ErrorCode((DWORD)rc));
}

PUBLIC void RemoveFileOnReboot(String path, ArenaAllocator& arena) {
    auto const wide_path = *WidenAllocNullTerm(arena, path);
    if (!MoveFileExW(wide_path.data, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
        ReportError(ErrorLevel::Warning,
                    k_nullopt,
                    "Failed to schedule file for deletion on reboot: {}: {}",
                    path,
                    Win32ErrorCode(GetLastError()));
}
