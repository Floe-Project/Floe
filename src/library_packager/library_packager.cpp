// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <miniz.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/package_format.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

// TODO: rename this, it's not just packaging libraries, also presets

// Library packager CLI tool
// - Generates an 'About' HTML file for a Floe library
// - Ensures there's a license file
// - Validates that the Lua file is correct

struct Paths {
    String lua;
    String license;
};

ErrorCodeOr<Paths> ScanLibraryFolder(ArenaAllocator& arena, String library_folder) {
    constexpr auto k_license_filenames = Array {
        "License.html"_s,
        "License.txt",
        "License.pdf",
        "LICENSE",
        "Licence.html", // british spelling
        "Licence.txt",
        "Licence.pdf",
        "LICENCE",
    };

    Paths result {};

    auto it = TRY(dir_iterator::Create(arena,
                                       library_folder,
                                       {
                                           .wildcard = "*",
                                           .get_file_size = false,
                                       }));
    DEFER { dir_iterator::Destroy(it); };
    while (auto const entry = TRY(dir_iterator::Next(it, arena)))
        if (sample_lib::FilenameIsFloeLuaFile(entry->subpath))
            result.lua = dir_iterator::FullPath(it, *entry, arena);
        else if (Contains(k_license_filenames, path::Filename(entry->subpath)))
            result.license = dir_iterator::FullPath(it, *entry, arena);

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

static ErrorCodeOr<MutableString> FileDataFromBuildResources(ArenaAllocator& arena, String filename) {
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

    auto const html_path = path::Join(arena, Array {html_dir.Value(), filename});
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
                return ErrorCode {CommonError::InvalidFileFormat};
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
                    return ErrorCode {CommonError::InvalidFileFormat};
                }

                value = ini.SubSpan(*cursor, *end - *cursor);
                cursor = *end;
                SplitWithIterator(ini, cursor, '\n');
            }

            return KeyVal {key, value};
        }

        return k_nullopt;
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
            return ErrorCode {CommonError::InvalidFileFormat};
        }
    }

    return result;
}

static ErrorCodeOr<void> WriteAboutLibraryHtml(sample_lib::Library const& lib,
                                               ArenaAllocator& arena,
                                               Paths paths,
                                               String library_folder) {
    auto const html_template = TRY(FileDataFromBuildResources(arena, "about_library_template.html"_s));

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
    OutputPackageFolder,
    PackageName,
    Count,
};

auto constexpr k_command_line_args_defs = MakeCommandLineArgDefs<CliArgId>({
    {
        .id = (u32)CliArgId::LibraryFolder,
        .key = "library-folders",
        .description = "Path to the library folder",
        .value_type = "path",
        .required = false,
        .num_values = -1,
    },
    {
        .id = (u32)CliArgId::PresetFolder,
        .key = "presets-folders",
        .description = "Path to the presets folder",
        .value_type = "path",
        .required = false,
        .num_values = -1,
    },
    {
        .id = (u32)CliArgId::OutputPackageFolder,
        .key = "output-package-folder",
        .description = "Folder to write the created package to",
        .value_type = "path",
        .required = false,
        .num_values = 1,
    },
    {
        .id = (u32)CliArgId::PackageName,
        .key = "package-name",
        .description = "Package name - inferred from library name if not provided",
        .value_type = "name",
        .required = false,
        .num_values = 1,
    },
});

static ErrorCodeOr<void> CheckNeededPackageCliArgs(Span<CommandLineArg const> args) {
    if (!args[ToInt(CliArgId::OutputPackageFolder)].was_provided) return k_success;

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

static String
PackageName(ArenaAllocator& arena, sample_lib::Library const* lib, CommandLineArg const& package_name_arg) {
    if (package_name_arg.was_provided)
        return fmt::Format(arena, "{}{}", package_name_arg.values[0], package::k_file_extension);
    ASSERT(lib);
    return fmt::Format(arena, "{} - {}{}", lib->author, lib->name, package::k_file_extension);
}

static ErrorCodeOr<void> Main(ArgsCstr args) {
    ArenaAllocator arena {PageAllocator::Instance()};
    auto const program_name = path::Filename(FromNullTerminated(args.args[0]));

    auto const cli_args = TRY(ParseCommandLineArgs(StdWriter(g_cli_out.stream),
                                                   arena,
                                                   args,
                                                   k_command_line_args_defs,
                                                   {
                                                       .handle_help_option = true,
                                                       .print_usage_on_error = true,
                                                   }));
    TRY(CheckNeededPackageCliArgs(cli_args));

    DynamicArray<u8> zip_data {arena};
    auto writer = dyn::WriterFor(zip_data);
    auto package = package::WriterCreate(writer);
    DEFER { package::WriterDestroy(package); };

    auto const create_package = cli_args[ToInt(CliArgId::OutputPackageFolder)].was_provided;

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

        if (create_package) TRY(package::WriterAddLibrary(package, *lib, arena, program_name));
    }

    if (create_package)
        for (auto const preset_folder : cli_args[ToInt(CliArgId::PresetFolder)].values)
            TRY(package::WriterAddPresetsFolder(package, preset_folder, arena, program_name));

    if (create_package) {
        auto const html_template = TRY(FileDataFromBuildResources(arena, "how_to_install_template.html"_s));
        auto const result_html = fmt::FormatStringReplace(arena,
                                                          html_template,
                                                          ArrayT<fmt::StringReplacement>({
                                                              {"__FLOE_MANUAL_URL__", FLOE_MANUAL_URL},
                                                          }));
        package::WriterAddFile(package, "How to Install.html"_s, result_html.ToByteSpan());

        auto const package_path = path::Join(
            arena,
            Array {cli_args[ToInt(CliArgId::OutputPackageFolder)].values[0],
                   PackageName(arena, lib_for_package_name, cli_args[ToInt(CliArgId::PackageName)])});
        package::WriterFinalise(package);
        TRY(WriteFile(package_path, zip_data));
        g_cli_out.Info({}, "Created package file: {}", package_path);
    } else {
        g_cli_out.Info({}, "No output packge folder provided, not creating a package file");
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
