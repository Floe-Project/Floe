// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "os/misc_mac.hpp"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wcast-align"
#include <AppKit/AppKit.h>
#include <CoreServices/CoreServices.h>
#include <sys/sysctl.h>
#pragma clang diagnostic pop

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/threading.hpp"
#include "tests/framework.hpp"
#include "utils/debug/debug.hpp"

static ErrorCodeOr<void> ConvertToStringWithNSDomain(NSErrorDomain domain, s64 code, Writer const& writer) {
    // We haven't saved the userInfo from the original source (as we converted the error code into a plain
    // integer to fit in our Error struct). However, the documentation says that if there is no userInfo, "a
    // default string is constructed from the domain and code" for the localizedDescription, so we should be
    // ok.
    auto nserr = [NSError errorWithDomain:domain code:(NSInteger)code userInfo:nil];
    auto const error_text = nserr.localizedDescription.UTF8String;
    return writer.WriteChars(FromNullTerminated(error_text));
}

static constexpr ErrorCodeCategory k_cocoa_category {
    .category_id = "CC",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return ConvertToStringWithNSDomain(NSCocoaErrorDomain, code.code, writer);
    }};

static constexpr ErrorCodeCategory k_mach_category {
    .category_id = "MC",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return ConvertToStringWithNSDomain(NSMachErrorDomain, code.code, writer);
    }};

static constexpr ErrorCodeCategory k_os_status_category {
    .category_id = "OS",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return ConvertToStringWithNSDomain(NSOSStatusErrorDomain, code.code, writer);
    }};

static constexpr ErrorCodeCategory k_url_category {
    .category_id = "UR",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return ConvertToStringWithNSDomain(NSURLErrorDomain, code.code, writer);
    }};

static constexpr ErrorCodeCategory k_unknown_category {
    .category_id = "UN",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return fmt::FormatToWriter(writer, "Mac error (no domain): {}", code.code);
    }};

ErrorCode ErrorFromNSError(NSError* error, char const* extra_debug_info, SourceLocation loc) {
    ASSERT(error != nil);
    ErrorCodeCategory const* code_type = nullptr;
    if ([error.domain isEqualToString:NSCocoaErrorDomain])
        code_type = &k_cocoa_category;
    else if ([error.domain isEqualToString:NSPOSIXErrorDomain])
        return ErrnoErrorCode(error.code, extra_debug_info, loc);
    else if ([error.domain isEqualToString:NSOSStatusErrorDomain])
        code_type = &k_os_status_category;
    else if ([error.domain isEqualToString:NSMachErrorDomain])
        code_type = &k_mach_category;
    else if ([error.domain isEqualToString:NSURLErrorDomain])
        code_type = &k_url_category;
    else
        code_type = &k_unknown_category;
    return {*code_type, (s64)error.code, extra_debug_info, loc};
}

DynamicArrayBounded<char, 64> OperatingSystemName() {
    NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];

    return fmt::FormatInline<64>("macOS {}.{}.{}",
                                 version.majorVersion,
                                 version.minorVersion,
                                 version.patchVersion);
}

String GetFileBrowserAppName() { return "Finder"; }

SystemStats GetSystemStats() {
    static SystemStats result {};
    if (!result.page_size) {
        result = {
            .num_logical_cpus = (u32)[[NSProcessInfo processInfo] activeProcessorCount],
            .page_size = (u32)NSPageSize(),
        };
    }
    return result;
}

void OpenFolderInFileBrowser(String path) {
    NSURL* file_url = [NSURL fileURLWithPath:StringToNSString(path)];
    NSURL* folder_url = [file_url URLByDeletingLastPathComponent];
    [[NSWorkspace sharedWorkspace] openURL:folder_url];
}

void OpenUrlInBrowser(String url) {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:StringToNSString(url)]];
}
