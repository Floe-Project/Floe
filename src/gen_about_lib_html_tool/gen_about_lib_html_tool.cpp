// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <foundation/foundation.hpp>

#include "os/misc.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

#include "plugin/sample_library/sample_library.hpp"

#include "common/common_errors.hpp"

// This tool generates an 'About' HTML file for a Floe library. This file is intended to be packaged with the
// library to give context to the library's files and to help get users started using it. Additionally, this
// tool validates that the Lua file is correct and that there's a license file.

struct Paths {
    String lua;
    String license;
};

ErrorCodeOr<Paths> ScanLibraryFolder(ArenaAllocator& arena, String library_folder) {
    constexpr auto k_license_filenames = Array {"License.html"_s, "License.txt"};

    Paths result {};

    auto it = TRY(DirectoryIterator::Create(arena,
                                            library_folder,
                                            {
                                                .wildcard = "*",
                                                .get_file_size = false,
                                            }));
    while (it.HasMoreFiles()) {
        auto const& entry = it.Get();
        if (sample_lib::FilenameIsFloeLuaFile(entry.path))
            result.lua = arena.Clone(entry.path);
        else if (Contains(k_license_filenames, path::Filename(entry.path)))
            result.license = arena.Clone(entry.path);
        TRY(it.Increment());
    }

    if (!result.lua.size) {
        g_cli_out.Error({}, "No Floe Lua file found in {}", library_folder);
        return ErrorCode {CommonError::NotFound};
    }

    if (!result.license.size) {
        g_cli_out.Error({}, "No license file found in {}", library_folder);
        g_cli_out.Error({}, "Expected one of the following:");
        for (auto const& filename : k_license_filenames)
            g_cli_out.Error({}, "  {}", filename);
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
        g_cli_out.Error({},
                        "Error reading {}: {}, {}",
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

constexpr String k_metadata_ini_filename = ".metadata.ini"_s;

static ErrorCodeOr<String> MetadataIni(String library_folder, ArenaAllocator& arena) {
    auto const metadata_ini_path = path::Join(arena, Array {library_folder, k_metadata_ini_filename});
    auto const outcome = ReadEntireFile(metadata_ini_path, arena);
    if (outcome.HasError()) {
        g_cli_out.Error({}, "ERROR {}: {}", metadata_ini_path, outcome.Error());
        return outcome.Error();
    }
    return outcome.Value();
}

struct Metadata {
    // NOTE: empty at the moment
};

// INI-like file format:
// - Key = Value
// - Lines starting with ';' are comments
// - Multiline values are supported with triple quotes
struct MetadataParser {
    struct KeyVal {
        String key, value;
    };

    ErrorCodeOr<Optional<KeyVal>> ReadLine() {
        while (cursor) {
            auto const line = WhitespaceStripped(SplitWithIterator(ini, cursor, '\n'));

            if (line.size == 0) continue;
            if (StartsWith(line, ';')) continue;

            auto const equals_pos = Find(line, '=');
            if (!equals_pos) {
                g_cli_out.Error({}, "Invalid line in {}: {}", k_metadata_ini_filename, line);
                return ErrorCode {CommonError::FileFormatIsInvalid};
            }

            auto const key = WhitespaceStrippedEnd(line.SubSpan(0, *equals_pos));
            auto value = WhitespaceStrippedStart(line.SubSpan(*equals_pos + 1));

            if (StartsWithSpan(value, k_multiline_delim)) {
                cursor = (usize)(value.data - ini.data) + k_multiline_delim.size;
                auto const end = FindSpan(ini, k_multiline_delim, *cursor);
                if (!end) {
                    g_cli_out.Error({},
                                    "Unterminated multiline value in {}: {}",
                                    k_metadata_ini_filename,
                                    key);
                    return ErrorCode {CommonError::FileFormatIsInvalid};
                }

                value = ini.SubSpan(*cursor, *end - *cursor);
                cursor = *end;
                SplitWithIterator(ini, cursor, '\n');
            }

            return KeyVal {key, value};
        }

        return nullopt;
    }

    static constexpr String k_multiline_delim = "\"\"\"";

    String const ini;
    Optional<usize> cursor = 0uz;
};

static ErrorCodeOr<Metadata> ReadMetadata(String library_folder, ArenaAllocator& arena) {
    Metadata result {};
    MetadataParser parser {TRY(MetadataIni(library_folder, arena))};

    while (auto const opt_line = TRY(parser.ReadLine())) {
        auto const& [key, value] = *opt_line;

        if (key.size) {
            g_cli_out.Error({}, "Unknown key in {}: {}", k_metadata_ini_filename, key);
            return ErrorCode {CommonError::FileFormatIsInvalid};
        }
    }

    return result;
}

static ErrorCodeOr<void> Main(String library_folder) {
    ArenaAllocator arena {PageAllocator::Instance()};

    auto const paths = TRY(ScanLibraryFolder(arena, library_folder));
    auto const lib = TRY(ReadLua(paths.lua, arena));
    if (!sample_lib::CheckAllReferencedFilesExist(*lib, g_cli_out)) return ErrorCode {CommonError::NotFound};
    auto const html_template = TRY(HtmlTemplate(arena));
    auto const metadata = TRY(ReadMetadata(library_folder, arena));
    (void)metadata; // NOTE: unused at the moment

    String description_html = {};
    if (lib->description) description_html = fmt::Format(arena, "<p>{}</p>", *lib->description);

    auto const result_html =
        fmt::FormatStringReplace(arena,
                                 html_template,
                                 ArrayT<fmt::StringReplacement>({
                                     {"__LIBRARY_NAME__", lib->name},
                                     {"__LUA_FILENAME__", path::Filename(paths.lua)},
                                     {"__LICENSE_FILENAME__", path::Filename(paths.license)},
                                     {"__FLOE_HOMEPAGE_URL__", FLOE_HOMEPAGE_URL},
                                     {"__FLOE_MANUAL_URL__", FLOE_MANUAL_URL},
                                     {"__FLOE_DOWNLOAD_URL__", FLOE_DOWNLOAD_URL},
                                     {"__LIBRARY_DESCRIPTION_HTML__", description_html},
                                 }));

    auto const output_path =
        path::Join(arena, Array {library_folder, fmt::Format(arena, "About {}.html"_s, lib->name)});
    TRY(WriteFile(output_path, result_html));
    g_cli_out.Info({}, "Successfully wrote '{}'", output_path);

    return k_success;
}

int main(int argc, char** argv) {
    ArenaAllocator arena {PageAllocator::Instance()};

    if (argc != 2) {
        g_cli_out.Error({}, "Usage: {} <library-folder>", argv[0]);
        return 1;
    }

    auto const library_folder = FromNullTerminated(argv[1]);

    auto result = Main(library_folder);
    if (result.HasError()) {
        g_cli_out.Error({}, "Error: {}", result.Error());
        return 1;
    }

    return 0;
}
