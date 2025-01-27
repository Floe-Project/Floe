// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "packager.hpp"

#include <miniz.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/crash_hooks.hpp"
#include "common_infrastructure/package_format.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"
#include "common_infrastructure/sentry/sentry_worker.hpp"

#include "build_resources/embedded_files.h"

// Library packager CLI tool - see packager.hpp for more info

struct Paths {
    String lua;
    String license;
};

ErrorCodeOr<Paths> ScanLibraryFolder(ArenaAllocator& arena, String library_folder) {
    library_folder = path::TrimDirectorySeparatorsEnd(library_folder);

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
        else if (IsEqualToCaseInsensitiveAscii(path::FilenameWithoutExtension(entry->subpath), "license") ||
                 IsEqualToCaseInsensitiveAscii(path::FilenameWithoutExtension(entry->subpath), "licence"))
            result.license = dir_iterator::FullPath(it, *entry, arena);

    if (!result.lua.size) {
        g_cli_out.Error({}, "No Floe Lua file found in {}", library_folder);
        return ErrorCode {CommonError::NotFound};
    }

    if (!result.license.size) {
        g_cli_out.Error({}, "No license file found in {}", library_folder);
        g_cli_out.Info(
            {},
            "Expected a file called licence (or license) to be present. Any file extension is allowed.");
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

static ErrorCodeOr<void> WriteAboutLibraryHtml(sample_lib::Library const& lib,
                                               ArenaAllocator& arena,
                                               Paths paths,
                                               String library_folder) {
    auto const about_library_doc = ({
        auto data = EmbeddedAboutLibraryTemplateRtf();
        arena.Clone(Span {(char const*)data.data, data.size});
    });

    String description = {};
    if (lib.description) description = *lib.description;

    auto const result_data =
        fmt::FormatStringReplace(arena,
                                 about_library_doc,
                                 ArrayT<fmt::StringReplacement>({
                                     {"__LIBRARY_NAME__", lib.name},
                                     {"__LUA_FILENAME__", path::Filename(paths.lua)},
                                     {"__LICENSE_FILENAME__", path::Filename(paths.license)},
                                     {"__FLOE_HOMEPAGE_URL__", FLOE_HOMEPAGE_URL},
                                     {"__FLOE_MANUAL_URL__", FLOE_MANUAL_URL},
                                     {"__FLOE_DOWNLOAD_URL__", FLOE_DOWNLOAD_URL},
                                     {"__LIBRARY_DESCRIPTION__", description},
                                 }));

    auto const output_path =
        path::Join(arena, Array {library_folder, fmt::Format(arena, "About {}.rtf"_s, lib.name)});
    TRY(WriteFile(output_path, result_data));

    g_cli_out.Info({}, "Successfully wrote '{}'", output_path);
    return k_success;
}

static ErrorCodeOr<void> CheckNeededPackageCliArgs(Span<CommandLineArg const> args) {
    if (!args[ToInt(PackagerCliArgId::OutputPackageFolder)].was_provided) return k_success;

    auto const library_folders_arg = args[ToInt(PackagerCliArgId::LibraryFolder)];
    auto const presets_folders_arg = args[ToInt(PackagerCliArgId::PresetFolder)];

    if (!library_folders_arg.values.size && !presets_folders_arg.values.size) {
        g_cli_out.Error({},
                        "Either --{} or --{} must be provided",
                        library_folders_arg.info.key,
                        presets_folders_arg.info.key);
        return ErrorCode {CliError::InvalidArguments};
    }

    auto const package_name_arg = args[ToInt(PackagerCliArgId::PackageName)];
    if (library_folders_arg.values.size != 1 && !package_name_arg.was_provided) {
        g_cli_out.Error({},
                        "If --{} is not set to 1 folder, --{} must be",
                        library_folders_arg.info.key,
                        package_name_arg.info.key);
        return ErrorCode {CliError::InvalidArguments};
    }

    if (package_name_arg.was_provided) {
        if (EndsWithSpan(package_name_arg.values[0], package::k_file_extension) ||
            EndsWithSpan(package_name_arg.values[0], ".zip"_s)) {
            g_cli_out.Error({}, "Don't include the file extension in the package name");
            return ErrorCode {CliError::InvalidArguments};
        }
    }

    return k_success;
}

static String
PackageName(ArenaAllocator& arena, sample_lib::Library const* lib, CommandLineArg const& package_name_arg) {
    if (package_name_arg.was_provided)
        return fmt::Format(arena, "{} Package{}", package_name_arg.values[0], package::k_file_extension);
    ASSERT(lib);
    return fmt::Format(arena, "{} - {} Package{}", lib->author, lib->name, package::k_file_extension);
}

static ErrorCodeOr<int> Main(ArgsCstr args) {
    ArenaAllocator arena {PageAllocator::Instance()};
    auto const program_name = path::Filename(FromNullTerminated(args.args[0]));

    auto const cli_args = TRY(ParseCommandLineArgsStandard(arena,
                                                           args,
                                                           k_packager_command_line_args_defs,
                                                           {
                                                               .handle_help_option = true,
                                                               .print_usage_on_error = true,
                                                               .description = k_packager_description,
                                                               .version = FLOE_VERSION_STRING,
                                                           }));
    TRY(CheckNeededPackageCliArgs(cli_args));

    DynamicArray<u8> zip_data {arena};
    auto writer = dyn::WriterFor(zip_data);
    auto package = package::WriterCreate(writer);
    DEFER { package::WriterDestroy(package); };

    auto const create_package = cli_args[ToInt(PackagerCliArgId::OutputPackageFolder)].was_provided;

    sample_lib::Library* lib_for_package_name = nullptr;

    for (auto const path : cli_args[ToInt(PackagerCliArgId::LibraryFolder)].values) {
        auto const library_path = TRY(AbsolutePath(arena, path));
        // library_folder can actually be a MDATA file but this is an uncommon legacy case so we don't
        // document it.
        if (path::Extension(library_path) == ".mdata") {
            auto reader = TRY(Reader::FromFile(library_path));
            ArenaAllocator scratch_arena {PageAllocator::Instance()};
            auto outcome = sample_lib::ReadMdata(reader, library_path, arena, scratch_arena);
            if (outcome.HasError()) {
                g_cli_out.Error({},
                                "Error reading {}: {}, {}",
                                library_path,
                                outcome.Error().message,
                                outcome.Error().code);
                return outcome.Error().code;
            }
            auto lib = outcome.Get<sample_lib::Library*>();
            lib_for_package_name = lib;
            if (create_package) TRY(package::WriterAddLibrary(package, *lib, arena, program_name));

            continue;
        }

        auto const paths = TRY(ScanLibraryFolder(arena, library_path));

        auto lib = TRY(ReadLua(paths.lua, arena));
        lib_for_package_name = lib;
        if (!sample_lib::CheckAllReferencedFilesExist(*lib, g_cli_out))
            return ErrorCode {CommonError::NotFound};

        TRY(WriteAboutLibraryHtml(*lib, arena, paths, library_path));

        if (create_package) TRY(package::WriterAddLibrary(package, *lib, arena, program_name));
    }

    if (create_package)
        for (auto const preset_folder : cli_args[ToInt(PackagerCliArgId::PresetFolder)].values)
            TRY(package::WriterAddPresetsFolder(package, preset_folder, arena, program_name));

    if (create_package) {
        auto const how_to_install_doc = ({
            auto data = EmbeddedPackageInstallationRtf();
            arena.Clone(Span {(char const*)data.data, data.size});
        });
        package::WriterAddFile(package, "Installation.rtf"_s, how_to_install_doc.ToByteSpan());

        auto const package_path = path::Join(
            arena,
            Array {cli_args[ToInt(PackagerCliArgId::OutputPackageFolder)].values[0],
                   PackageName(arena, lib_for_package_name, cli_args[ToInt(PackagerCliArgId::PackageName)])});
        package::WriterFinalise(package);
        TRY(WriteFile(package_path, zip_data));
        g_cli_out.Info({}, "Created package file: {}", package_path);
    } else {
        g_cli_out.Info(
            {},
            "No output packge folder provided, not creating a package file\nRun with --help for usage info");
    }

    return 0;
}

int main(int argc, char** argv) {
    g_panic_hook = PanicHook;
    InitLogFolderIfNeeded();

    SetThreadName("main");

    BeginCrashDetection(CrashHookWriteCrashReport);
    DEFER { EndCrashDetection(); };

    auto const result = Main({argc, argv});
    if (result.HasError()) {
        g_cli_out.Error({}, "Error: {}", result.Error());
        return 1;
    }
    return result.Value();
}
