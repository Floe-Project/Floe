// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package_format.hpp"

#include "tests/framework.hpp"

static ErrorCodeOr<sample_lib::Library*> TestLib(tests::Tester& tester, String test_files_folder) {
    auto const test_floe_lua_path = (String)path::Join(
        tester.scratch_arena,
        Array {test_files_folder, k_repo_subdirs_floe_test_libraries, "Test-Lib-1", "floe.lua"});
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

static ErrorCodeOr<Span<u8 const>> TestPackage(tests::Tester& tester) {
    auto writer = package::WriterCreate();
    DEFER { package::WriterDestroy(writer); };

    auto const test_files_folder = TestFilesFolder(tester);

    TRY(package::WriterAddLibrary(writer,
                                  *TRY(TestLib(tester, test_files_folder)),
                                  tester.scratch_arena,
                                  "tester"));

    TRY(package::WriterAddPresetsFolder(
        writer,
        path::Join(tester.scratch_arena, Array {test_files_folder, k_repo_subdirs_floe_test_presets}),
        tester.scratch_arena,
        "tester"));

    return package::WriterFinalise(writer, tester.scratch_arena);
}

TEST_CASE(TestPackageFormat) {
    auto const zip_data = TRY(TestPackage(tester));
    CHECK_NEQ(zip_data.size, 0uz);

    auto io_reader = Reader::FromMemory(zip_data);
    auto outcome = package::ReaderCreate(io_reader);
    if (outcome.HasError()) {
        tester.log.Error({}, "Failed to create package reader: {}", outcome.Error().message);
        return outcome.Error().code;
    }
    auto reader = outcome.ReleaseValue();
    DEFER { package::ReaderDestroy(reader); };

    return k_success;
}

TEST_REGISTRATION(RegisterPackageFormatTests) { REGISTER_TEST(TestPackageFormat); }
