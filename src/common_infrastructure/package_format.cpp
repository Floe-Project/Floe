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

    package::PackageFolderIterator iterator {
        .reader = package,
    };

    usize folders_found = 0;
    while (auto const folder = TRY(iterator.Next(tester.scratch_arena))) {
        ++folders_found;
        switch (folder->type) {
            case package::SubfolderType::Libraries: {
                REQUIRE(folder->library);
                auto const& lib = *folder->library;
                CHECK_EQ(lib.name, "Test Lua"_s);

                auto const differs = TRY(FolderDiffersFromChecksumValues(TestLibFolder(tester),
                                                                         folder->checksum_values,
                                                                         tester.log,
                                                                         tester.scratch_arena));
                CHECK(!differs);

                // test extraction
                {
                    auto const dest_dir =
                        String(path::Join(tester.scratch_arena,
                                          Array {tests::TempFolder(tester), "PackageExtract test"}));
                    TRY(CreateDirectory(dest_dir, {.create_intermediate_directories = false}));
                    DEFER { auto _ = Delete(dest_dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

                    BufferLogger error_log {tester.scratch_arena};

                    {
                        auto const extract_result = package::ReaderExtractFolder(package,
                                                                                 folder->path,
                                                                                 dest_dir,
                                                                                 tester.scratch_arena,
                                                                                 error_log,
                                                                                 false);
                        if (extract_result.HasError()) return ErrorCode {extract_result.Error()};
                        CHECK(error_log.buffer.size == 0);
                    }

                    // We should fail when the folder is not empty
                    {
                        auto const extract_result = package::ReaderExtractFolder(package,
                                                                                 folder->path,
                                                                                 dest_dir,
                                                                                 tester.scratch_arena,
                                                                                 error_log,
                                                                                 false);
                        REQUIRE(extract_result.HasError());
                        CHECK(extract_result.Error() == package::PackageError::NotEmpty);
                        CHECK(error_log.buffer.size > 0);
                        tester.log.Info({}, "Expected error log: {}", error_log.buffer);
                        dyn::Clear(error_log.buffer);
                    }

                    // we should succeed when we allow overwriting
                    {
                        auto const extract_result = package::ReaderExtractFolder(package,
                                                                                 folder->path,
                                                                                 dest_dir,
                                                                                 tester.scratch_arena,
                                                                                 error_log,
                                                                                 true);
                        if (extract_result.HasError()) return ErrorCode {extract_result.Error()};
                        CHECK(error_log.buffer.size == 0);
                    }

                    // check that the installed checksums are correct
                    auto const final_name =
                        path::Join(tester.scratch_arena, Array {dest_dir, path::Filename(folder->path)});
                    auto const final_differs = TRY(FolderDiffersFromChecksumValues(final_name,
                                                                                   folder->checksum_values,
                                                                                   error_log,
                                                                                   tester.scratch_arena));
                    CHECK(!final_differs);
                }

                break;
            }
            case package::SubfolderType::Presets: {
                break;
            }
            case package::SubfolderType::Count: PanicIfReached();
        }
    }

    CHECK_EQ(folders_found, 2u);

    return k_success;
}

TEST_CASE(TestPackageFormat) {
    auto const zip_data = TRY(WriteTestPackage(tester));
    CHECK_NEQ(zip_data.size, 0uz);

    TRY(ReadTestPackage(tester, zip_data));

    return k_success;
}

TEST_REGISTRATION(RegisterPackageFormatTests) { REGISTER_TEST(TestPackageFormat); }
