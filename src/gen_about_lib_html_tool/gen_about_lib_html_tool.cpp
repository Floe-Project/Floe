// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <foundation/foundation.hpp>

#include "os/misc.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

#include "plugin/sample_library/sample_library.hpp"

#include "common/common_errors.hpp"

// This tool generates an 'About' HTML file for a Floe library. This file is packaged with the library to give
// the library files context and to help get users started. Additionally, this tool validates that the Lua
// file is correct and that a license file is present.

struct Paths {
    String lua;
    String license;
};

ErrorCodeOr<Paths> ScanLibraryFolder(ArenaAllocator& arena, String library_folder) {
    constexpr auto k_license_filenames = Array {"License.html"_s, "License.txt"};

    Paths result {};

    auto it = TRY(DirectoryIterator::Create(arena, library_folder, "*"));
    while (it.HasMoreFiles()) {
        auto const& entry = it.Get();
        if (sample_lib::PathIsFloeLuaFile(entry.path))
            result.lua = arena.Clone(entry.path);
        else if (Contains(k_license_filenames, path::Filename(entry.path)))
            result.license = arena.Clone(entry.path);
        TRY(it.Increment());
    }

    if (!result.lua.size) {
        stdout_log.ErrorLn("No Floe Lua file found in {}", library_folder);
        return ErrorCode {CommonError::NotFound};
    }

    if (!result.license.size) {
        stdout_log.ErrorLn("No license file found in {}", library_folder);
        stdout_log.ErrorLn("Expected one of the following:");
        for (auto const& filename : k_license_filenames)
            stdout_log.ErrorLn("  {}", filename);
        return ErrorCode {CommonError::NotFound};
    }

    return result;
}

static ErrorCodeOr<sample_lib::Library*> ReadLua(String lua_path, ArenaAllocator& arena) {
    auto const lua_data = TRY(ReadEntireFile(lua_path, arena));
    auto reader {Reader::FromMemory(lua_data)};
    ArenaAllocator scratch_arena {PageAllocator::Instance()};
    auto const outcome = sample_lib::ReadLua(reader, lua_path, arena, scratch_arena, {});
    if (outcome.HasError()) {
        stdout_log.ErrorLn("Error reading {}: {}, {}",
                           lua_path,
                           outcome.Error().message,
                           outcome.Error().code);
        return outcome.Error().code;
    }

    return outcome.Get<sample_lib::Library*>();
}

static ErrorCodeOr<MutableString> HtmlTemplate(ArenaAllocator& arena) {
    auto const exe_path = String(TRY(CurrentExecutablePath(arena)));

    auto const exe_dir = path::Directory(exe_path);
    if (!exe_dir) return ErrorCode {CommonError::NotFound};

    auto const html_dir = SearchForExistingFolderUpwards(*exe_dir, "build_resources", arena);
    if (!html_dir) return ErrorCode {CommonError::NotFound};

    auto const html_path = path::Join(arena, Array {html_dir.Value(), "about_library_template.html"_s});
    return TRY(ReadEntireFile(html_path, arena));
}

static ErrorCodeOr<void> Main(String library_folder) {
    ArenaAllocator arena {PageAllocator::Instance()};

    auto const paths = TRY(ScanLibraryFolder(arena, library_folder));
    auto const lib = TRY(ReadLua(paths.lua, arena));
    auto const html_template = TRY(HtmlTemplate(arena));

    auto const result =
        fmt::FormatStringReplace(arena,
                                 html_template,
                                 ArrayT<fmt::StringReplacement>({
                                     {"__LIBRARY_NAME__"_s, lib->name},
                                     {"__LUA_FILENAME__"_s, path::Filename(paths.lua)},
                                     {"__LICENSE_FILENAME__"_s, path::Filename(paths.license)},
                                     {"__FLOE_HOMEPAGE_URL__"_s, FLOE_HOMEPAGE_URL},
                                     {"__FLOE_MANUAL_URL__"_s, FLOE_MANUAL_URL},
                                     {"__FLOE_DOWNLOAD_URL__"_s, FLOE_DOWNLOAD_URL},
                                     {"__DESCRIPTION_HTML__"_s, ""}, // TODO: get this somehow
                                 }));

    DebugLn("{}", result);

    return k_success;
}

int main(int argc, char** argv) {
    ArenaAllocator arena {PageAllocator::Instance()};

    if (argc != 2) {
        stdout_log.ErrorLn("Usage: {} <library-folder>", argv[0]);
        return 1;
    }

    auto const library_folder = FromNullTerminated(argv[1]);

    auto result = Main(library_folder);
    if (result.HasError()) {
        stdout_log.ErrorLn("Error: {}", result.Error());
        return 1;
    }

    return 0;
}
