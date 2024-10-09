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

static ErrorCodeOr<String> FindFileWithExt(tests::Tester& tester, String folder, String ext) {
    ASSERT(ext.size);
    ASSERT(ext[0] == '.');

    auto& arena = tester.scratch_arena;

    auto it = TRY(dir_iterator::RecursiveCreate(arena, folder, {.wildcard = fmt::Format(arena, "*{}", ext)}));
    DEFER { dir_iterator::Destroy(it); };
    auto const entry = TRY(dir_iterator::Next(it, arena));
    REQUIRE(entry);
    REQUIRE(path::Extension(entry->subpath) == ext);

    return dir_iterator::FullPath(it, *entry, arena);
}

static ErrorCodeOr<void> RenameAnyFileWithExt(tests::Tester& tester, String folder, String ext) {
    auto const path = TRY(FindFileWithExt(tester, folder, ext));
    auto const new_path =
        fmt::Format(tester.scratch_arena, "{}-renamed{}", path.SubSpan(0, path.size - ext.size), ext);
    tester.log.Debug({}, "Renaming {} to {}", path, new_path);
    return Rename(path, new_path);
}

TEST_CASE(TestPackageInstallation) {
    struct Fixture {
        Fixture(tests::Tester& tester) : destination_folder {tests::TempFilename(tester)} {
            REQUIRE(!CreateDirectory(destination_folder,
                                     {.create_intermediate_directories = false, .fail_if_exists = false})
                         .HasError());
            thread_pool.Init("pkg-install", {});

            auto const zip_data = ({
                auto o = package::CreateValidTestPackage(tester);
                REQUIRE(!o.HasError());
                o.ReleaseValue();
            });
            CHECK_NEQ(zip_data.size, 0uz);

            zip_path = tests::TempFilename(tester);
            REQUIRE(!WriteFile(zip_path, zip_data).HasError());
        }
        DynamicArrayBounded<char, path::k_max> const destination_folder;
        DynamicArrayBounded<char, path::k_max> zip_path;
        ThreadPool thread_pool;
        ThreadsafeErrorNotifications error_notif;
        sample_lib_server::Server server {thread_pool, destination_folder, error_notif};
    };

    // We use a fixture just for better errors. The SUBCASEs here depend on their predecessors, they're not
    // isolated.
    auto& fixture = tests::CreateOrFetchFixtureObject<Fixture>(tester);

    CreateJobOptions const job_options {
        .zip_path = fixture.zip_path,
        .libraries_install_folder = fixture.destination_folder,
        .presets_install_folder = fixture.destination_folder,
        .server = fixture.server,
        .preset_folders = Array {String(fixture.destination_folder)},
    };

    // Initially we're expecting success without any user input because the package is valid, it's not
    // installed anywhere else, and the destination folder is empty.
    SUBCASE("installing package for the 1st time succeeds") {
        auto job = CreateInstallJob(tester.scratch_arena, job_options);
        DEFER { DestroyInstallJob(job); };

        StartJob(*job);

        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);

        for (auto& comp : job->components)
            CHECK_EQ(TypeOfActionTaken(comp), "installed"_s);

        CHECK(job->error_log.buffer.size == 0);
        if (job->error_log.buffer.size > 0) tester.log.Debug({}, "Error log: {}", job->error_log.buffer);
    }

    // If we try to install the exact same package again, it should notice that and do nothing.
    SUBCASE("installing the same package again does nothing") {
        auto job = CreateInstallJob(tester.scratch_arena, job_options);
        DEFER { DestroyInstallJob(job); };

        StartJob(*job);

        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);

        for (auto& comp : job->components)
            CHECK_EQ(TypeOfActionTaken(comp), "already installed"_s);

        CHECK(job->error_log.buffer.size == 0);
        if (job->error_log.buffer.size > 0) tester.log.Debug({}, "Error log: {}", job->error_log.buffer);
    }

    SUBCASE("libraries that are modified should prompt user input") {
        TRY(RenameAnyFileWithExt(tester, fixture.destination_folder, ".lua"));
        TRY(RenameAnyFileWithExt(tester, fixture.destination_folder, ".floe-preset"));

        auto job = CreateInstallJob(tester.scratch_arena, job_options);
        DEFER { DestroyInstallJob(job); };

        StartJob(*job);

        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::AwaitingUserInput);

        SUBCASE("skip") {
            for (auto& comp : job->components)
                if (UserInputIsRequired(comp.existing_installation_status)) {
                    CHECK(comp.component.type == ComponentType::Library);
                    comp.user_decision = InstallJob::UserDecision::Skip;
                }

            job->state.Store(InstallJob::State::Installing, StoreMemoryOrder::Release);
            CompleteJob(*job);

            for (auto& comp : job->components)
                if (comp.component.type == ComponentType::Library)
                    CHECK_EQ(TypeOfActionTaken(comp), "skipped"_s);
                else
                    // presets never require user input, they're always installed or skipped automatically
                    CHECK_EQ(TypeOfActionTaken(comp), "installed"_s);
        }

        SUBCASE("overwrite") {
            for (auto& comp : job->components) {
                if (UserInputIsRequired(comp.existing_installation_status)) {
                    CHECK(comp.component.type == ComponentType::Library);
                    comp.user_decision = InstallJob::UserDecision::Overwrite;
                }
            }

            job->state.Store(InstallJob::State::Installing, StoreMemoryOrder::Release);
            CompleteJob(*job);

            for (auto& comp : job->components) {
                if (comp.component.type == ComponentType::Library) {
                    CHECK_EQ(TypeOfActionTaken(comp), "overwritten"_s);
                } else {
                    // presets never require user input, they're always installed or skipped automatically
                    CHECK_EQ(TypeOfActionTaken(comp), "installed"_s);
                }
            }
        }

        CHECK(job->error_log.buffer.size == 0);
        if (job->error_log.buffer.size > 0) tester.log.Debug({}, "Error log: {}", job->error_log.buffer);
    }

    // IMPROVE: test error conditions and different conditions for destination folders

    return k_success;
}

} // namespace package

TEST_REGISTRATION(RegisterPackageInstallationTests) { REGISTER_TEST(package::TestPackageInstallation); }
