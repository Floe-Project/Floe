// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <miniz.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

#include "plugin/sample_library/sample_library.hpp"

#include "common/common_errors.hpp"

// Library packager CLI tool
// - Generates an 'About' HTML file for a Floe library
// - Ensures there's a license file
// - Validates that the Lua file is correct

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
        g_cli_out.Info({}, "Expected one of the following:");
        for (auto const& filename : k_license_filenames)
            g_cli_out.Info({}, "  {}", filename);
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

static ErrorCodeOr<MutableString> AboutLibraryHtmlTemplate(ArenaAllocator& arena) {
    auto const exe_path = String(TRY(CurrentExecutablePath(arena)));

    auto const exe_dir = path::Directory(exe_path);
    if (!exe_dir) {
        g_cli_out.Error({}, "Could not executable's path");
        return ErrorCode {CommonError::NotFound};
    }

    auto const html_dir = SearchForExistingFolderUpwards(*exe_dir, "build_resources", arena);
    if (!html_dir) {
        g_cli_out.Error({}, "Could not find 'build_resources' folder upwards from '{}'", exe_path);
        return ErrorCode {CommonError::NotFound};
    }

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

static ErrorCodeOr<void> WriteAboutLibraryHtml(sample_lib::Library const& lib,
                                               ArenaAllocator& arena,
                                               Paths paths,
                                               String library_folder) {
    auto const html_template = TRY(AboutLibraryHtmlTemplate(arena));

    String description_html = {};
    if (lib.description) description_html = fmt::Format(arena, "<p>{}</p>", *lib.description);

    auto const result_html =
        fmt::FormatStringReplace(arena,
                                 html_template,
                                 ArrayT<fmt::StringReplacement>({
                                     {"__LIBRARY_NAME__", lib.name},
                                     {"__LUA_FILENAME__", path::Filename(paths.lua)},
                                     {"__LICENSE_FILENAME__", path::Filename(paths.license)},
                                     {"__FLOE_HOMEPAGE_URL__", FLOE_HOMEPAGE_URL},
                                     {"__FLOE_MANUAL_URL__", FLOE_MANUAL_URL},
                                     {"__FLOE_DOWNLOAD_URL__", FLOE_DOWNLOAD_URL},
                                     {"__LIBRARY_DESCRIPTION_HTML__", description_html},
                                 }));

    auto const output_path =
        path::Join(arena, Array {library_folder, fmt::Format(arena, "About {}.html"_s, lib.name)});
    TRY(WriteFile(output_path, result_html));

    g_cli_out.Info({}, "Successfully wrote '{}'", output_path);
    return k_success;
}

enum class CliArgId : u32 {
    LibraryFolder,
    PresetFolder,
    OutputZipFolder,
    PackageName,
    Count,
};

auto constexpr k_command_line_args_defs = MakeCommandLineArgDefs<CliArgId>({
    {
        .id = (u32)CliArgId::LibraryFolder,
        .key = "library-folders",
        .description = "Path to the library folder",
        .required = false,
        .num_values = -1,
    },
    {
        .id = (u32)CliArgId::PresetFolder,
        .key = "presets-folders",
        .description = "Path to the presets folder",
        .required = false,
        .num_values = -1,
    },
    {
        .id = (u32)CliArgId::OutputZipFolder,
        .key = "output-zip-folder",
        .description = "Path to the output zip folder",
        .required = false,
        .num_values = 1,
    },
    {
        .id = (u32)CliArgId::PackageName,
        .key = "package-name",
        .description = "Package name - inferred from library name if not provided",
        .required = false,
        .num_values = 1,
    },
});

// IMPROVE: this is not robust, we need to consider the canonical form that a path can take
static String RelativePath(String from, String to) {
    if (to.size < from.size + 1)
        PanicF(SourceLocation::Current(),
               "Bug with calculating relative paths: {} is shorter than {}",
               from,
               to);

    return to.SubSpan(from.size + 1);
}

static String ReplaceSpacesWithDashes(ArenaAllocator& arena, String str) {
    auto result = arena.Clone(str);
    for (auto& c : result)
        if (c == ' ') c = '-';
    return result;
}

static ErrorCodeOr<void> CheckNeededZipCliArgs(Span<CommandLineArg const> args) {
    if (!args[ToInt(CliArgId::OutputZipFolder)].was_provided) return k_success;

    auto const library_folders_arg = args[ToInt(CliArgId::LibraryFolder)];
    auto const presets_folders_arg = args[ToInt(CliArgId::PresetFolder)];

    if (!library_folders_arg.values.size && !presets_folders_arg.values.size) {
        g_cli_out.Error({},
                        "Either --{} or --{} must be provided",
                        library_folders_arg.info.key,
                        presets_folders_arg.info.key);
        return ErrorCode {CliError::InvalidArguments};
    }

    auto const package_name_arg = args[ToInt(CliArgId::PackageName)];
    if (library_folders_arg.values.size != 1 && !package_name_arg.was_provided) {
        g_cli_out.Error({},
                        "If --{} is not set to 1 folder, --{} must be",
                        library_folders_arg.info.key,
                        package_name_arg.info.key);
        return ErrorCode {CliError::InvalidArguments};
    }

    return k_success;
}

static mz_zip_archive ZipCreate() {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_heap(&zip, 0, Mb(100))) {
        PanicF(SourceLocation::Current(),
               "Failed to initialize zip writer: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    return zip;
}

static void ZipDestroy(mz_zip_archive& zip) { mz_zip_writer_end(&zip); }

static void ZipAddFile(mz_zip_archive& zip, String path, Span<u8 const> data) {
    ArenaAllocatorWithInlineStorage<200> scratch_arena;
    auto const archived_path = NullTerminated(path, scratch_arena);

    // archive paths use posix separators
    if constexpr (IS_WINDOWS) {
        for (auto p = archived_path; *p; ++p)
            if (*p == '\\') *p = '/';
    }

    g_cli_out.Info({}, "Adding file to zip: {}", archived_path);

    if (!mz_zip_writer_add_mem(
            &zip,
            archived_path,
            data.data,
            data.size,
            (mz_uint)(path::Extension(path) != ".flac" ? MZ_DEFAULT_COMPRESSION : MZ_NO_COMPRESSION))) {
        PanicF(SourceLocation::Current(),
               "Failed to add file to zip: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
}

static Span<u8 const> ZipFinalise(mz_zip_archive& zip) {
    void* zip_data = nullptr;
    usize zip_size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &zip_data, &zip_size)) {
        PanicF(SourceLocation::Current(),
               "Failed to finalize zip archive: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    return Span {(u8 const*)zip_data, zip_size};
}

static ErrorCodeOr<void> AddAllFilesToZip(mz_zip_archive& zip,
                                          String folder,
                                          ArenaAllocator& arena,
                                          Span<String const> subdirs_in_zip) {
    auto it = TRY(RecursiveDirectoryIterator::Create(arena,
                                                     folder,
                                                     {
                                                         .wildcard = "*",
                                                         .get_file_size = false,
                                                         .skip_dot_files = true,
                                                     }));
    ArenaAllocator inner_arena {PageAllocator::Instance()};
    while (it.HasMoreFiles()) {
        inner_arena.ResetCursorAndConsolidateRegions();

        auto const& entry = it.Get();
        if (entry.type == FileType::File) {
            auto const file_data = TRY(ReadEntireFile(entry.path, inner_arena)).ToByteSpan();

            DynamicArray<char> archive_path {inner_arena};
            path::JoinAppend(archive_path, subdirs_in_zip);
            path::JoinAppend(archive_path, RelativePath(folder, entry.path));
            ZipAddFile(zip, archive_path, file_data);
        }

        TRY(it.Increment());
    }

    return k_success;
}

static ErrorCodeOr<void> Main(ArgsCstr args) {
    ArenaAllocator arena {PageAllocator::Instance()};

    auto const cli_args = TRY(ParseCommandLineArgs(StdWriter(g_cli_out.stream),
                                                   arena,
                                                   args,
                                                   k_command_line_args_defs,
                                                   {
                                                       .handle_help_option = true,
                                                       .print_usage_on_error = true,
                                                   }));
    TRY(CheckNeededZipCliArgs(cli_args));

    auto zip = ZipCreate();
    DEFER { ZipDestroy(zip); };

    sample_lib::Library* lib_for_package_name = nullptr;

    for (auto const library_folder : cli_args[ToInt(CliArgId::LibraryFolder)].values) {
        auto const paths = TRY(ScanLibraryFolder(arena, library_folder));

        auto lib = TRY(ReadLua(paths.lua, arena));
        lib_for_package_name = lib;
        if (!sample_lib::CheckAllReferencedFilesExist(*lib, g_cli_out))
            return ErrorCode {CommonError::NotFound};

        auto const metadata_outcome = ReadMetadata(library_folder, arena);
        (void)metadata_outcome; // NOTE: unused at the moment

        TRY(WriteAboutLibraryHtml(*lib, arena, paths, library_folder));

        if (cli_args[ToInt(CliArgId::OutputZipFolder)].was_provided)
            TRY(AddAllFilesToZip(zip, library_folder, arena, Array {"Libraries"_s, lib->name}));
    }

    if (cli_args[ToInt(CliArgId::OutputZipFolder)].was_provided)
        for (auto const preset_folder : cli_args[ToInt(CliArgId::PresetFolder)].values)
            TRY(AddAllFilesToZip(zip,
                                 preset_folder,
                                 arena,
                                 Array {"Presets"_s, path::Filename(preset_folder)}));

    if (cli_args[ToInt(CliArgId::OutputZipFolder)].was_provided) {
        auto const zip_path =
            path::Join(arena,
                       Array {cli_args[ToInt(CliArgId::OutputZipFolder)].values[0],
                              fmt::Format(arena,
                                          "Package-{}.floe.zip"_s,
                                          cli_args[ToInt(CliArgId::PackageName)].was_provided
                                              ? cli_args[ToInt(CliArgId::PackageName)].values[0]
                                              : ReplaceSpacesWithDashes(arena, lib_for_package_name->name))});
        TRY(WriteFile(zip_path, ZipFinalise(zip)));
        g_cli_out.Info({}, "Created zip file: {}", zip_path);
    } else {
        g_cli_out.Info({}, "No output zip folder provided, not creating a zip file");
    }

    return k_success;
}

int main(int argc, char** argv) {
    SetThreadName("main");
    auto result = Main({argc, argv});
    if (result.HasError()) {
        if (result.Error() == CliError::HelpRequested) return 0;
        g_cli_out.Error({}, "Error: {}", result.Error());
        return 1;
    }
    return 0;
}
