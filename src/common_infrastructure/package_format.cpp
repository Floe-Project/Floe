// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package_format.hpp"

#include "tests/framework.hpp"
#include "utils/logger/logger.hpp"

static String TestLibFolder(tests::Tester& tester) {
    return path::Join(
        tester.scratch_arena,
        Array {tests::TestFilesFolder(tester), k_repo_subdirs_floe_test_libraries, "Test-Lib-1"});
}

static ErrorCodeOr<sample_lib::Library*> LoadTestLibrary(tests::Tester& tester) {
    auto const test_floe_lua_path =
        (String)path::Join(tester.scratch_arena, Array {TestLibFolder(tester), "floe.lua"});
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

static ErrorCodeOr<Span<u8 const>> WriteTestPackage(tests::Tester& tester) {
    DynamicArray<u8> zip_data {tester.scratch_arena};
    auto writer = dyn::WriterFor(zip_data);
    auto package = package::WriterCreate(writer);
    DEFER { package::WriterDestroy(package); };

    auto lib = TRY(LoadTestLibrary(tester));
    TRY(package::WriterAddLibrary(package, *lib, tester.scratch_arena, "tester"));

    TRY(package::WriterAddPresetsFolder(
        package,
        path::Join(tester.scratch_arena,
                   Array {tests::TestFilesFolder(tester), k_repo_subdirs_floe_test_presets}),
        tester.scratch_arena,
        "tester"));

    package::WriterFinalise(package);
    return zip_data.ToOwnedSpan();
}

static ErrorCodeOr<void> ReadTestPackage(tests::Tester& tester, Span<u8 const> zip_data) {
    auto reader = Reader::FromMemory(zip_data);
    auto outcome = package::ReaderCreate(reader);
    if (outcome.HasError()) {
        tester.log.Error({}, "Failed to create package reader: {}", outcome.Error().message);
        return outcome.Error().code;
    }
    auto package = outcome.ReleaseValue();
    DEFER { package::ReaderDestroy(package); };

    auto const lib_dirs = TRY(package::ReaderFindLibraryDirs(package, tester.scratch_arena));
    for (auto const dir : lib_dirs) {
        auto const o = package::ReaderReadLibraryLua(package, dir, tester.scratch_arena);
        if (o.HasError()) {
            tester.log.Error({}, "Failed to read library lua: {}", o.Error().message);
            return o.Error().code;
        }
        auto const lib = o.ReleaseValue();
        CHECK_EQ(lib->name, "Test Lua"_s);

        auto const checksum_values =
            TRY(package::ReaderChecksumValuesForDir(package, dir, tester.scratch_arena));
        auto const differs = TRY(
            FolderDiffersFromChecksumValues(TestLibFolder(tester), checksum_values, tester.scratch_arena));
        CHECK(!differs);

        auto const dest_dir = String(
            path::Join(tester.scratch_arena, Array {tests::TempFolder(tester), "PackageExtract test"}));
        TRY(CreateDirectory(dest_dir, {.create_intermediate_directories = false}));
        DEFER { auto _ = Delete(dest_dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

        TRY(package::ReaderExtractFolder(package, dir, dest_dir, tester.scratch_arena));

        auto const final_name = path::Join(tester.scratch_arena, Array {dest_dir, path::Filename(dir)});
        auto const final_differs =
            TRY(FolderDiffersFromChecksumValues(final_name, checksum_values, tester.scratch_arena));
        CHECK(!final_differs);
    }

    auto const preset_dirs = TRY(package::ReaderFindPresetDirs(package, tester.scratch_arena));
    for (auto const dir : preset_dirs)
        g_debug_log.Debug({}, "Found preset dir: {}", dir);
    return k_success;
}

TEST_CASE(TestPackageFormat) {
    auto const zip_data = TRY(WriteTestPackage(tester));
    CHECK_NEQ(zip_data.size, 0uz);

    TRY(ReadTestPackage(tester, zip_data));

    return k_success;
}

TEST_REGISTRATION(RegisterPackageFormatTests) { REGISTER_TEST(TestPackageFormat); }
