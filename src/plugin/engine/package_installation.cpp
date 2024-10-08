// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package_installation.hpp"

#include "tests/framework.hpp"

namespace package {

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

static ErrorCodeOr<Span<u8 const>> CreateValidTestPackage(tests::Tester& tester) {
    DynamicArray<u8> zip_data {tester.scratch_arena};
    auto writer = dyn::WriterFor(zip_data);
    auto package = WriterCreate(writer);
    DEFER { WriterDestroy(package); };

    auto lib = TRY(LoadTestLibrary(tester));
    TRY(WriterAddLibrary(package, *lib, tester.scratch_arena, "tester"));

    TRY(WriterAddPresetsFolder(package, TestPresetsFolder(tester), tester.scratch_arena, "tester"));

    WriterFinalise(package);
    return zip_data.ToOwnedSpan();
}

static ErrorCodeOr<void> ReadTestPackage(tests::Tester& tester, String zip_path) {
    auto const temp_folder = tests::TempFilename(tester);
    TRY(CreateDirectory(temp_folder, {.create_intermediate_directories = false, .fail_if_exists = false}));

    ThreadPool thread_pool;
    thread_pool.Init("pkginstall", {});
    ThreadsafeErrorNotifications error_notif;
    sample_lib_server::Server server {thread_pool, temp_folder, error_notif};

    auto job = CreateInstallJob(tester.scratch_arena,
                                {
                                    .zip_path = zip_path,
                                    .libraries_install_folder = temp_folder,
                                    .presets_install_folder = temp_folder,
                                    .server = server,
                                    .preset_folders = {},
                                });
    DEFER { DestroyInstallJob(job); };

    StartJob(*job);

    auto const state = job->state.Load(LoadMemoryOrder::Acquire);
    CHECK_EQ(state, InstallJob::State::DoneSuccess);

    for (auto& comp : job->components)
        tester.log.Debug({}, "action taken for {}: {}", comp.component.path, TypeOfActionTaken(comp));

    tester.log.Debug({}, "error log: {}", job->error_log.buffer);

    // TODO: lots more tests (differing checksums, different folders, overwrite, user-input, etc)

#if 0

    PackageReader package {reader};
    auto outcome = ReaderInit(package, error_log);
    if (outcome.HasError()) {
        TEST_FAILED("Failed to create package reader: {}. error_log: {}",
                    ErrorCode {outcome.Error()},
                    error_log.buffer);
    }
    DEFER { ReaderDeinit(package); };
    CHECK(error_log.buffer.size == 0);

    PackageComponentIndex iterator = 0;

    usize components_found = 0;
    while (true) {
        auto const component = ({
            auto const o =
                IteratePackageComponents(package, iterator, tester.scratch_arena, error_log);
            if (o.HasError()) {
                TEST_FAILED("Failed to read package component: {}, error_log: {}",
                            ErrorCode {o.Error()},
                            error_log.buffer);
            }
            o.ReleaseValue();
        });
        if (!component) break;
        CHECK(error_log.buffer.size == 0);

        ++components_found;
        switch (component->type) {
            case ComponentType::Library: {
                REQUIRE(component->library);
                CHECK_EQ(component->library->name, "Test Lua"_s);

                auto const dest_dir =
                    String(path::Join(tester.scratch_arena,
                                      Array {tests::TempFolder(tester), "package_format_test"}));
                auto _ = Delete(dest_dir, {.type = DeleteOptions::Type::DirectoryRecursively});
                TRY(CreateDirectory(dest_dir, {.create_intermediate_directories = false}));
                DEFER { auto _ = Delete(dest_dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

#if 0
                // Check not installed if we don't provide a library
                {
                    auto const o = LibraryCheckExistingInstallation(*component,
                                                                             nullptr,
                                                                             tester.scratch_arena,
                                                                             error_log);
                    if (o.HasError()) return ErrorCode {o.Error()};
                    auto const status = o.ReleaseValue();
                    CHECK(!status.installed);
                }

                // Check installed if we provide a library
                {
                    auto const lib = TRY(LoadTestLibrary(tester));
                    auto const o = LibraryCheckExistingInstallation(*component,
                                                                             lib,
                                                                             tester.scratch_arena,
                                                                             error_log);
                    if (o.HasError()) return ErrorCode {o.Error()};
                    auto const status = o.ReleaseValue();
                    CHECK(status.installed);
                    CHECK(status.modified_since_installed == ExistingInstallationStatus::Unmodified);
                    CHECK(status.version_difference == ExistingInstallationStatus::Equal);
                }

                // Initially should be empty
                {
                    auto checksums =
                        TRY(ChecksumsForFolder(dest_dir, tester.scratch_arena, tester.scratch_arena));
                    CHECK_EQ(checksums.size, 0u);
                }

                // Initial extraction
                {
                    auto const extract_result = ReaderInstallComponent(
                        package,
                        *component,
                        dest_dir,
                        tester.scratch_arena,
                        error_log,
                        {
                            .destination = DestinationType::DefaultFolderWithSubfolderFromPackage,
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
                    auto const extract_result = ReaderInstallComponent(
                        package,
                        *component,
                        dest_dir,
                        tester.scratch_arena,
                        error_log,
                        {
                            .destination = DestinationType::DefaultFolderWithSubfolderFromPackage,
                            .overwrite_existing_files = false,
                            .resolve_install_folder_name_conflicts = false,
                        });
                    REQUIRE(extract_result.HasError());
                    CHECK(extract_result.Error() == PackageError::NotEmpty);
                    CHECK(error_log.buffer.size > 0);
                    tester.log.Debug({}, "Expected error log: {}", error_log.buffer);
                    dyn::Clear(error_log.buffer);
                }

                // We should succeed when we allow overwriting
                {
                    auto const extract_result = ReaderInstallComponent(
                        package,
                        *component,
                        dest_dir,
                        tester.scratch_arena,
                        error_log,
                        {
                            .destination = DestinationType::DefaultFolderWithSubfolderFromPackage,
                            .overwrite_existing_files = true,
                            .resolve_install_folder_name_conflicts = false,
                        });
                    if (extract_result.HasError()) return ErrorCode {extract_result.Error()};
                    CHECK(error_log.buffer.size == 0);
                }

                // We should succeed when we allow name conflict resolution
                {
                    auto const extract_result = ReaderInstallComponent(
                        package,
                        *component,
                        dest_dir,
                        tester.scratch_arena,
                        error_log,
                        {
                            .destination = DestinationType::DefaultFolderWithSubfolderFromPackage,
                            .overwrite_existing_files = false,
                            .resolve_install_folder_name_conflicts = true,
                        });
                    if (extract_result.HasError()) return ErrorCode {extract_result.Error()};
                    CHECK(error_log.buffer.size == 0);
                }

                // Check installed
                CHECK_NEQ(TRY(ChecksumsForFolder(dest_dir, tester.scratch_arena, tester.scratch_arena)).size,
                          0u);
#endif

                break;
            }
            case ComponentType::Presets: {
#if 0
                auto const dest_dir =
                    String(path::Join(tester.scratch_arena,
                                      Array {tests::TempFolder(tester), "package_format_test"}));
                auto _ = Delete(dest_dir, {.type = DeleteOptions::Type::DirectoryRecursively});
                TRY(CreateDirectory(dest_dir, {.create_intermediate_directories = false}));
                DEFER { auto _ = Delete(dest_dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

                // Check it should not be installed in an empty dir
                {
                    auto const o = PresetsCheckExistingInstallation(*component,
                                                                             Array {dest_dir},
                                                                             tester.scratch_arena,
                                                                             error_log);
                    if (o.HasError()) return ErrorCode {o.Error()};
                    auto const status = o.ReleaseValue();
                    CHECK(!status.installed);
                }

                // Installation should succeed
                {
                    auto const extract_result = ReaderInstallComponent(
                        package,
                        *component,
                        dest_dir,
                        tester.scratch_arena,
                        error_log,
                        {
                            .destination = DestinationType::DefaultFolderWithSubfolderFromPackage,
                            .overwrite_existing_files = false,
                            .resolve_install_folder_name_conflicts = false,
                        });
                    if (extract_result.HasError()) {
                        tester.log.Error({}, "error log: {}", error_log.buffer);
                        return ErrorCode {extract_result.Error()};
                    }
                    CHECK(error_log.buffer.size == 0);
                }

                // Check it now is installed
                {
                    auto const o = PresetsCheckExistingInstallation(*component,
                                                                             Array {dest_dir},
                                                                             tester.scratch_arena,
                                                                             error_log);
                    if (o.HasError()) return ErrorCode {o.Error()};
                    auto const status = o.ReleaseValue();
                    CHECK(status.installed);
                    CHECK(status.modified_since_installed == ExistingInstallationStatus::Unmodified);
                    CHECK(status.version_difference == ExistingInstallationStatus::Equal);
                }
#endif

                break;
            }
            case ComponentType::Count: PanicIfReached();
        }
    }

    CHECK_EQ(components_found, 2u);
#endif

    return k_success;
}

} // namespace package

TEST_CASE(TestPackageInstallation) {
    auto const zip_data = TRY(package::CreateValidTestPackage(tester));
    CHECK_NEQ(zip_data.size, 0uz);
    auto const package_path = tests::TempFilename(tester);
    TRY(WriteFile(package_path, zip_data));
    TRY(package::ReadTestPackage(tester, package_path));

    return k_success;
}

TEST_REGISTRATION(RegisterPackageInstallationTests) { REGISTER_TEST(TestPackageInstallation); }
