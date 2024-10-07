// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package_format.hpp"

#include "tests/framework.hpp"
#include "utils/logger/logger.hpp"

static String TestLibFolder(tests::Tester& tester) {
    return path::Join(
        tester.scratch_arena,
        Array {tests::TestFilesFolder(tester), tests::k_libraries_test_files_subdir, "Test-Lib-1"});
}

static String TestPresetsFolder(tests::Tester& tester) {
    return path::Join(tester.scratch_arena,
                      Array {tests::TestFilesFolder(tester), tests::k_preset_test_files_subdir});
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

    TRY(package::WriterAddPresetsFolder(package, TestPresetsFolder(tester), tester.scratch_arena, "tester"));

    package::WriterFinalise(package);
    return zip_data.ToOwnedSpan();
}

static ErrorCodeOr<void> ReadTestPackage(tests::Tester& tester, Span<u8 const> zip_data) {
    auto reader = Reader::FromMemory(zip_data);
    BufferLogger error_log {tester.scratch_arena};

    package::PackageReader package {reader};
    auto outcome = package::ReaderInit(package, error_log);
    if (outcome.HasError()) {
        TEST_FAILED("Failed to create package reader: {}. error_log: {}",
                    ErrorCode {outcome.Error()},
                    error_log.buffer);
    }
    DEFER { package::ReaderDeinit(package); };
    CHECK(error_log.buffer.size == 0);

    package::PackageFolderIteratorIndex iterator = 0;

    usize folders_found = 0;
    while (true) {
        auto const folder = ({
            auto const o = package::IteratePackageFolders(package, iterator, tester.scratch_arena, error_log);
            if (o.HasError()) {
                TEST_FAILED("Failed to read package folder: {}, error_log: {}",
                            ErrorCode {o.Error()},
                            error_log.buffer);
            }
            o.ReleaseValue();
        });
        if (!folder) break;
        CHECK(error_log.buffer.size == 0);

        ++folders_found;
        switch (folder->type) {
            case package::SubfolderType::Libraries: {
                REQUIRE(folder->library);
                CHECK_EQ(folder->library->name, "Test Lua"_s);

                // test extraction
                {
                    auto const dest_dir =
                        String(path::Join(tester.scratch_arena,
                                          Array {tests::TempFolder(tester), "PackageExtract test"}));
                    auto _ = Delete(dest_dir, {.type = DeleteOptions::Type::DirectoryRecursively});
                    TRY(CreateDirectory(dest_dir, {.create_intermediate_directories = false}));
                    DEFER { auto _ = Delete(dest_dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

                    // Check not installed if we don't provide a library
                    {
                        auto const o = package::LibraryCheckExistingInstallation(*folder,
                                                                                 nullptr,
                                                                                 tester.scratch_arena,
                                                                                 error_log);
                        if (o.HasError()) return ErrorCode {o.Error()};
                        auto status = o.ReleaseValue();
                        CHECK(!status.installed);
                    }

                    // Check installed if we provide a library
                    {
                        auto lib = TRY(LoadTestLibrary(tester));
                        auto const o = package::LibraryCheckExistingInstallation(*folder,
                                                                                 lib,
                                                                                 tester.scratch_arena,
                                                                                 error_log);
                        if (o.HasError()) return ErrorCode {o.Error()};
                        auto status = o.ReleaseValue();
                        CHECK(status.installed);
                        CHECK(status.modified_since_installed ==
                              package::ExistingInstallationStatus::Unmodified);
                        CHECK(status.version_difference == package::ExistingInstallationStatus::Equal);
                    }

                    // Initially should be empty
                    {
                        auto checksums =
                            TRY(ChecksumsForFolder(dest_dir, tester.scratch_arena, tester.scratch_arena));
                        CHECK_EQ(checksums.size, 0u);
                    }

                    // Initial extraction
                    {
                        auto const extract_result = package::ReaderExtractFolder(
                            package,
                            *folder,
                            dest_dir,
                            tester.scratch_arena,
                            error_log,
                            {
                                .destination =
                                    package::DestinationType::DefaultFolderWithSubfolderFromPackage,
                                .overwrite_existing_files = false,
                                .resolve_install_folder_name_conflicts = false,
                            });
                        if (extract_result.HasError()) {
                            tester.log.Error({}, "error log: {}", error_log.buffer);
                            return ErrorCode {extract_result.Error()};
                        }
                        CHECK(error_log.buffer.size == 0);
                    }

                    // We should fail when the folder is not empty
                    {
                        auto const extract_result = package::ReaderExtractFolder(
                            package,
                            *folder,
                            dest_dir,
                            tester.scratch_arena,
                            error_log,
                            {
                                .destination =
                                    package::DestinationType::DefaultFolderWithSubfolderFromPackage,
                                .overwrite_existing_files = false,
                                .resolve_install_folder_name_conflicts = false,
                            });
                        REQUIRE(extract_result.HasError());
                        CHECK(extract_result.Error() == package::PackageError::NotEmpty);
                        CHECK(error_log.buffer.size > 0);
                        tester.log.Debug({}, "Expected error log: {}", error_log.buffer);
                        dyn::Clear(error_log.buffer);
                    }

                    // We should succeed when we allow overwriting
                    {
                        auto const extract_result = package::ReaderExtractFolder(
                            package,
                            *folder,
                            dest_dir,
                            tester.scratch_arena,
                            error_log,
                            {
                                .destination =
                                    package::DestinationType::DefaultFolderWithSubfolderFromPackage,
                                .overwrite_existing_files = true,
                                .resolve_install_folder_name_conflicts = false,
                            });
                        if (extract_result.HasError()) return ErrorCode {extract_result.Error()};
                        CHECK(error_log.buffer.size == 0);
                    }

                    // We should succeed when we allow name conflict resolution
                    {
                        auto const extract_result = package::ReaderExtractFolder(
                            package,
                            *folder,
                            dest_dir,
                            tester.scratch_arena,
                            error_log,
                            {
                                .destination =
                                    package::DestinationType::DefaultFolderWithSubfolderFromPackage,
                                .overwrite_existing_files = false,
                                .resolve_install_folder_name_conflicts = true,
                            });
                        if (extract_result.HasError()) return ErrorCode {extract_result.Error()};
                        CHECK(error_log.buffer.size == 0);
                    }

                    // Check installed
                    CHECK_NEQ(
                        TRY(ChecksumsForFolder(dest_dir, tester.scratch_arena, tester.scratch_arena)).size,
                        0u);
                }

                break;
            }
            case package::SubfolderType::Presets: {
                auto const o = package::PresetsCheckExistingInstallation(
                    *folder,
                    Array {*path::Directory(TestPresetsFolder(tester))},
                    tester.scratch_arena,
                    error_log);
                if (o.HasError()) return ErrorCode {o.Error()};
                auto const status = o.ReleaseValue();
                CHECK(status.installed);
                break;
            }
            case package::SubfolderType::Count: PanicIfReached();
        }
    }

    CHECK_EQ(folders_found, 2u);

    return k_success;
}

TEST_CASE(TestRelativePathIfInFolder) {
    CHECK_EQ(package::detail::RelativePathIfInFolder("/a/b/c", "/a/b"), "c"_s);
    CHECK_EQ(package::detail::RelativePathIfInFolder("/a/b/c", "/a/b/"), "c"_s);
    CHECK_EQ(package::detail::RelativePathIfInFolder("/a/b/c", "/a"), "b/c"_s);
    CHECK(!package::detail::RelativePathIfInFolder("/aa/b/c", "/a"));
    CHECK(!package::detail::RelativePathIfInFolder("/a/b/c", "/a/d"));
    CHECK(!package::detail::RelativePathIfInFolder("/a/b/c", "/a/b/c"));
    CHECK(!package::detail::RelativePathIfInFolder("", ""));
    CHECK(!package::detail::RelativePathIfInFolder("", "/a"));
    CHECK(!package::detail::RelativePathIfInFolder("/a", ""));
    return k_success;
}

TEST_CASE(TestPackageFormat) {
    auto const zip_data = TRY(WriteTestPackage(tester));
    CHECK_NEQ(zip_data.size, 0uz);

    TRY(ReadTestPackage(tester, zip_data));

    return k_success;
}

TEST_REGISTRATION(RegisterPackageFormatTests) {
    REGISTER_TEST(TestPackageFormat);
    REGISTER_TEST(TestRelativePathIfInFolder);
}
