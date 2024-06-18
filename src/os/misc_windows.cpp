// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h> // needs to be first
//
#include <dbghelp.h>
#include <errhandlingapi.h>
#include <process.h>
#include <shellapi.h>
#include <versionhelpers.h>

//
#include "os/undef_windows_macros.h"
//

#include "foundation/foundation.hpp"
#include "os/misc_windows.hpp"
#include "os/threading.hpp"
#include "tests/framework.hpp"
#include "utils/debug/debug.hpp"

#include "misc.hpp"
#include "misc_windows.hpp"

// This function is from the JUCE core library
// https://github.com/juce-framework/JUCE/blob/master/modules/juce_foundation/native/juce_SystemStats_windows.cpp
// Copyright (c) 2022 - Raw Material Software Limited
// SPDX-License-Identifier: ISC
DynamicArrayInline<char, 64> OperatingSystemName() {
    auto const windows_version_info = []() -> RTL_OSVERSIONINFOW {
        RTL_OSVERSIONINFOW version_info = {};

        if (auto* module_handle = ::GetModuleHandleW(L"ntdll.dll")) {
            using RtlGetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

            if (auto* rtl_get_version = (RtlGetVersion)::GetProcAddress(module_handle, "RtlGetVersion")) {
                version_info.dwOSVersionInfoSize = sizeof(version_info);
                LONG status_success = 0;

                if (rtl_get_version(&version_info) != status_success) version_info = {};
            }
        }

        return version_info;
    };

    auto const version_info = windows_version_info();
    auto const major = version_info.dwMajorVersion;
    auto const minor = version_info.dwMinorVersion;
    auto const build = version_info.dwBuildNumber;

    ASSERT(major <= 10); // need to add support for new version

    if (major == 10 && build >= 22000) return "Windows 11"_s;
    if (major == 10) return "Windows 10"_s;
    if (major == 6 && minor == 3) return "Windows 8.1"_s;
    if (major == 6 && minor == 2) return "Windows 8"_s;
    if (major == 6 && minor == 1) return "Windows 7"_s;
    if (major == 6 && minor == 0) return "Windows Vista"_s;
    if (major == 5 && minor == 1) return "Windows XP"_s;
    if (major == 5 && minor == 0) return "Windows 2000"_s;

    PanicIfReached();
    return {};
}

void* AllocatePages(usize bytes) {
    auto p = VirtualAlloc(nullptr, (DWORD)bytes, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    TracyAlloc(p, bytes);
    return p;
}

void FreePages(void* ptr, usize bytes) {
    (void)bytes; // VirtualFree requires the size to be 0 when MEM_RELEASE is used
    TracyFree(ptr);
    auto result = VirtualFree(ptr, 0, MEM_RELEASE);
    ASSERT(result != 0, "VirtualFree failed");
}

void TryShrinkPages(void* ptr, usize old_size, usize new_size) {
    (void)ptr;
    (void)old_size;
    (void)new_size;
    // IMPROVE: actually shrink the memory
}

int CurrentProcessId() { return _getpid(); }

static String ExceptionCodeString(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return "EXCEPTION_ACCESS_VIOLATION: The thread tried to read from or write to a virtual address for which it does not have the appropriate access.";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED: The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking.";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "EXCEPTION_DATATYPE_MISALIGNMENT: The thread tried to read or write data that is misaligned on hardware that does not provide alignment. For example, 16-bit values must be aligned on 2-byte boundaries; 32-bit values on 4-byte boundaries, and so on.";
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            return "EXCEPTION_FLT_DENORMAL_OPERAND: One of the operands in a floating-point operation is denormal. A denormal value is one that is too small to represent as a standard floating-point value.";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            return "EXCEPTION_FLT_DIVIDE_BY_ZERO: The thread tried to divide a floating-point value by a floating-point divisor of zero.";
        case EXCEPTION_FLT_INEXACT_RESULT:
            return "EXCEPTION_FLT_INEXACT_RESULT: The result of a floating-point operation cannot be represented exactly as a decimal fraction.";
        case EXCEPTION_FLT_INVALID_OPERATION:
            return "EXCEPTION_FLT_INVALID_OPERATION: This exception represents any floating-point exception not included in this list.";
        case EXCEPTION_FLT_OVERFLOW:
            return "EXCEPTION_FLT_OVERFLOW: The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type.";
        case EXCEPTION_FLT_STACK_CHECK:
            return "EXCEPTION_FLT_STACK_CHECK: The stack overflowed or underflowed as the result of a floating-point operation.";
        case EXCEPTION_FLT_UNDERFLOW:
            return "EXCEPTION_FLT_UNDERFLOW: The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type.";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "EXCEPTION_ILLEGAL_INSTRUCTION: The thread tried to execute an invalid instruction.";
        case EXCEPTION_IN_PAGE_ERROR:
            return "EXCEPTION_IN_PAGE_ERROR: The thread tried to access a page that was not present, and the system was unable to load the page. For example, this exception might occur if a network connection is lost while running a program over the network.";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "EXCEPTION_INT_DIVIDE_BY_ZERO: The thread tried to divide an integer value by an integer divisor of zero.";
        case EXCEPTION_INT_OVERFLOW:
            return "EXCEPTION_INT_OVERFLOW: The result of an integer operation caused a carry out of the most significant bit of the result.";
        case EXCEPTION_INVALID_DISPOSITION:
            return "EXCEPTION_INVALID_DISPOSITION: An exception handler returned an invalid disposition to the exception dispatcher. Programmers using a high-level language such as C should never encounter this exception.";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            return "EXCEPTION_NONCONTINUABLE_EXCEPTION: The thread tried to continue execution after a noncontinuable exception occurred.";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "EXCEPTION_PRIV_INSTRUCTION: The thread tried to execute an instruction whose operation is not allowed in the current machine mode.";
        case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW: The thread used up its stack. ";
    }
    return {};
}

void* g_exception_handler = nullptr;

void StartupCrashHandler() {

    g_exception_handler = AddVectoredExceptionHandler(1, [](PEXCEPTION_POINTERS exception_info) -> LONG {
        // some exceptions are expected and should be ignored; for example lua will trigger exceptions.
        if (auto const msg = ExceptionCodeString(exception_info->ExceptionRecord->ExceptionCode); msg.size) {
            StdPrint(StdStream::Err, "Unhandled exception: ");
            StdPrint(StdStream::Err, msg);
            StdPrint(StdStream::Err, "\n");

#ifdef __x86_64__
            if (exception_info->ExceptionRecord->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION &&
                exception_info->ExceptionRecord->ExceptionAddress) {
                auto* pc =
                    reinterpret_cast<unsigned char const*>(exception_info->ExceptionRecord->ExceptionAddress);
                if (pc[0] == 0x67 && pc[1] == 0x0f && pc[2] == 0xb9 && pc[3] == 0x40)
                    DumpInfoAboutUBSanToStderr();
            }
#endif

            DumpCurrentStackTraceToStderr();
        }
        return EXCEPTION_CONTINUE_SEARCH;
    });
}
void ShutdownCrashHandler() {
    if (g_exception_handler) RemoveVectoredContinueHandler(g_exception_handler);
}

void StdPrint(StdStream stream, String str) {
    DWORD bytes_written;
    auto const result = WriteFile(GetStdHandle(({
                                      DWORD h = STD_OUTPUT_HANDLE;
                                      switch (stream) {
                                          case StdStream::Out: h = STD_OUTPUT_HANDLE; break;
                                          case StdStream::Err: h = STD_ERROR_HANDLE; break;
                                      }
                                      h;
                                  })),
                                  str.data,
                                  (DWORD)str.size,
                                  &bytes_written,
                                  nullptr);
    if (!result) {
        // error
    }
}

// the FILETIME structure represents the number of 100-nanosecond intervals since January 1, 1601 UTC
constexpr s64 k_epoch_offset = 116444736000000000;

s128 NanosecondsSinceEpoch() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER const li {
        .LowPart = ft.dwLowDateTime,
        .HighPart = ft.dwHighDateTime,
    };
    return ((s128)(li.QuadPart) - k_epoch_offset) * 100;
}

DateAndTime LocalTimeFromNanosecondsSinceEpoch(s128 nanoseconds) {
    ULARGE_INTEGER const li {
        .QuadPart = (ULONGLONG)(nanoseconds / 100) + k_epoch_offset,
    };
    FILETIME const gmt {
        .dwLowDateTime = li.LowPart,
        .dwHighDateTime = li.HighPart,
    };

    FILETIME local;
    FileTimeToLocalFileTime(&gmt, &local);

    SYSTEMTIME st;
    FileTimeToSystemTime(&local, &st);
    return {.year = (s16)st.wYear,
            .months_since_jan = (s8)(st.wMonth - 1),
            .day_of_month = (s8)st.wDay,
            .hour = (s8)st.wHour,
            .minute = (s8)st.wMinute,
            .second = (s8)st.wSecond,
            .nanosecond = (s32)st.wMilliseconds * 1000000};
}

TimePoint TimePoint::Now() {
    s64 result;
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    result = counter.QuadPart;
    return TimePoint(result);
}

static f64 CountsPerSecond() {
    static f64 result = []() {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        return (f64)frequency.QuadPart;
    }();
    return result;
}

f64 operator-(TimePoint lhs, TimePoint rhs) { return (f64)(lhs.m_time - rhs.m_time) / CountsPerSecond(); }

TimePoint operator+(TimePoint t, f64 s) {
    t.m_time += (s64)(s * CountsPerSecond());
    return t;
}

String GetFileBrowserAppName() { return "File Explorer"; }

SystemStats GetSystemStats() {
    static SystemStats result {};
    if (!result.page_size) {
        SYSTEM_INFO system_info;
        GetNativeSystemInfo(&system_info);
        result = {.num_logical_cpus = (u32)system_info.dwNumberOfProcessors,
                  .page_size = (u32)system_info.dwPageSize};
    }
    return result;
}

static void WindowsShellExecute(String arg) {
    DynamicArray<wchar_t> wide_arg {Malloc::Instance()};
    if (WidenAppend(wide_arg, arg)) {
        dyn::Append(wide_arg, L'\0');
        // We do this in a separate thread because ShellExecuteW is really slow
        Thread thread;
        thread.Start(
            [wide_arg = wide_arg.ToOwnedSpan()]() mutable {
                DEFER { Malloc::Instance().Free(wide_arg.ToByteSpan()); };
                if (auto scoped_com = ScopedWin32ComUsage::Create(); scoped_com.HasValue())
                    ShellExecuteW(nullptr, L"open", wide_arg.data, nullptr, nullptr, SW_SHOW);
            },
            "WindowsShellExecute");
        thread.Detach();
    }
}

void OpenFolderInFileBrowser(String path) { WindowsShellExecute(path); }
void OpenUrlInBrowser(String url) { WindowsShellExecute(url); }

static constexpr ErrorCodeCategory k_error_category {
    .category_id = "WIN",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        WCHAR buf[200];
        auto const num_chars_written =
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr,
                           (DWORD)code.code,
                           MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK),
                           buf,
                           (DWORD)ArraySize(buf),
                           nullptr);
        if (num_chars_written) {
            WString error_text_wide {(const WCHAR*)buf, num_chars_written};
            while (error_text_wide.size && (Last(error_text_wide) == L'\n' || Last(error_text_wide) == L'\r'))
                error_text_wide.size--;
            ArenaAllocatorWithInlineStorage<sizeof(buf) * 4> temp_allocator;
            if (auto o = Narrow(temp_allocator, error_text_wide); o.HasValue())
                return writer.WriteChars(o.Value());
        }

        return fmt::FormatToWriter(writer, "FormatMessage failed: {}", GetLastError());
    }};

ErrorCode Win32ErrorCode(DWORD win32_code, char const* extra_debug_info, SourceLocation loc) {
    return ErrorCode {k_error_category, (s64)win32_code, extra_debug_info, loc};
}

//=================================================
//  _______        _
// |__   __|      | |
//    | | ___  ___| |_ ___
//    | |/ _ \/ __| __/ __|
//    | |  __/\__ \ |_\__ \
//    |_|\___||___/\__|___/
//
//=================================================

TEST_CASE(TestWindowsErrors) {
    auto& a = tester.scratch_arena;

    SUBCASE("Win32") {
        auto e = Win32ErrorCode(ERROR_TOO_MANY_OPEN_FILES, "ERROR_TOO_MANY_OPEN_FILES");
        auto message = fmt::Format(a, "{}", e);
        REQUIRE(message.size);
        tester.log.DebugLn("{}", e);
    }

    SUBCASE("Win32 HRESULT") {
        auto e = HresultErrorCode(E_OUTOFMEMORY, "E_OUTOFMEMORY");
        auto message = fmt::Format(a, "{}", e);
        REQUIRE(message.size);
        tester.log.DebugLn("{}", e);
    }

    return k_success;
}

TEST_CASE(TestScopedWin32ComUsage) {
    auto com_lib1 = ScopedWin32ComUsage::Create();
    { auto com_lib2 = ScopedWin32ComUsage::Create(); }
    return k_success;
}

TEST_REGISTRATION(RegisterWindowsPlatformTests) {
    REGISTER_TEST(TestScopedWin32ComUsage);
    REGISTER_TEST(TestWindowsErrors);
}
