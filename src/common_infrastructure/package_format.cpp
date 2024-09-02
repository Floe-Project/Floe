// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package_format.hpp"

#include "tests/framework.hpp"
#include "utils/logger/logger.hpp"

static String TestLibFolder(tests::Tester& tester, String test_files_folder) {
    return path::Join(tester.scratch_arena,
                      Array {test_files_folder, k_repo_subdirs_floe_test_libraries, "Test-Lib-1"});
}

static ErrorCodeOr<sample_lib::Library*> TestLib(tests::Tester& tester, String test_files_folder) {
    auto const test_floe_lua_path =
        (String)path::Join(tester.scratch_arena,
                           Array {TestLibFolder(tester, test_files_folder), "floe.lua"});
    auto reader = TRY(Reader::FromFile(test_floe_lua_path));
    auto lib_outcome =
        sample_lib::ReadLua(reader, test_floe_lua_path, tester.scratch_arena, tester.scratch_arena);
    if (lib_outcome.HasError()) {
        tester.log.Error({}, "Failed to read library from test lua file: {}", lib_outcome.Error().message);
        return lib_outcome.Error().code;
    }
    auto lib = lib_outcome.ReleaseValue();
    return lib;
}

static ErrorCodeOr<Span<u8 const>> WriteTestPackage(tests::Tester& tester, String test_files_folder) {
    DynamicArray<u8> zip_data {tester.scratch_arena};
    auto writer = dyn::WriterFor(zip_data);
    auto package = package::WriterCreate(writer);
    DEFER { package::WriterDestroy(package); };

    TRY(package::WriterAddLibrary(package,
                                  *TRY(TestLib(tester, test_files_folder)),
                                  tester.scratch_arena,
                                  "tester"));

    TRY(package::WriterAddPresetsFolder(
        package,
        path::Join(tester.scratch_arena, Array {test_files_folder, k_repo_subdirs_floe_test_presets}),
        tester.scratch_arena,
        "tester"));

    package::WriterFinalise(package);
    return zip_data.ToOwnedSpan();
}

TEST_CASE(TestPackageFormat) {
    auto const test_files_folder = TestFilesFolder(tester);

    auto const zip_data = TRY(WriteTestPackage(tester, test_files_folder));
    CHECK_NEQ(zip_data.size, 0uz);

    auto reader = Reader::FromMemory(zip_data);
    auto outcome = package::ReaderCreate(reader);
    if (outcome.HasError()) {
        tester.log.Error({}, "Failed to create package reader: {}", outcome.Error().message);
        return outcome.Error().code;
    }
    auto package = outcome.ReleaseValue();
    DEFER { package::ReaderDestroy(package); };

    auto lib_dirs = TRY(package::ReaderFindLibraryDirs(package, tester.scratch_arena));
    for (auto dir : lib_dirs) {
        auto o = package::ReaderReadLibraryLua(package, dir, tester.scratch_arena);
        if (o.HasError()) {
            tester.log.Error({}, "Failed to read library lua: {}", o.Error().message);
            return o.Error().code;
        }
        auto lib = o.ReleaseValue();
        CHECK_EQ(lib->name, "Test Lua"_s);

        auto opt_table = TRY(package::ReaderChecksumValuesForDir(package, dir, tester.scratch_arena));
        for (auto [path, values] : opt_table)
            g_debug_log.Debug({}, "Checksum: {08x} {} {}", values->crc32, values->file_size, path);

        auto differs = TRY(FolderDiffersFromChecksumFile(TestLibFolder(tester, test_files_folder),
                                                         opt_table,
                                                         tester.scratch_arena));
        CHECK(!differs);
    }

    auto preset_dirs = TRY(package::ReaderFindPresetDirs(package, tester.scratch_arena));
    for (auto dir : preset_dirs)
        g_debug_log.Debug({}, "Found preset dir: {}", dir);

    return k_success;
}

TEST_REGISTRATION(RegisterPackageFormatTests) { REGISTER_TEST(TestPackageFormat); }
