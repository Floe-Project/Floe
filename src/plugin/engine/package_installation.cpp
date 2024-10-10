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

static ErrorCodeOr<void> PrintDirectory(tests::Tester& tester, String dir, String heading) {
    auto it = TRY(dir_iterator::RecursiveCreate(tester.scratch_arena, dir, {}));
    DEFER { dir_iterator::Destroy(it); };

    tester.log.Debug({}, "{} Contents of '{}':", heading, dir);
    while (auto const entry = TRY(dir_iterator::Next(it, tester.scratch_arena)))
        tester.log.Debug({}, "  {}", entry->subpath);

    return k_success;
}

TEST_CASE(TestPackageInstallation) {
    auto const destination_folder {tests::TempFilename(tester)};
    REQUIRE(!CreateDirectory(destination_folder,
                             {.create_intermediate_directories = false, .fail_if_exists = false})
                 .HasError());

    ThreadPool thread_pool;
    thread_pool.Init("pkg-install", {});

    ThreadsafeErrorNotifications error_notif;
    sample_lib_server::Server server {thread_pool, destination_folder, error_notif};

    auto const zip_data = ({
        auto o = package::CreateValidTestPackage(tester);
        REQUIRE(!o.HasError());
        o.ReleaseValue();
    });
    CHECK_NEQ(zip_data.size, 0uz);

    auto const zip_path = tests::TempFilename(tester);
    REQUIRE(!WriteFile(zip_path, zip_data).HasError());

    CreateJobOptions const job_options {
        .zip_path = zip_path,
        .libraries_install_folder = destination_folder,
        .presets_install_folder = destination_folder,
        .server = server,
        .preset_folders = Array {String(destination_folder)},
    };

    // Initially we're expecting success without any user input because the package is valid, it's not
    // installed anywhere else, and the destination folder is empty.
    {
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
    {
        auto job = CreateInstallJob(tester.scratch_arena, job_options);
        DEFER { DestroyInstallJob(job); };

        StartJob(*job);

        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);

        for (auto& comp : job->components)
            CHECK_EQ(TypeOfActionTaken(comp), "already installed"_s);

        CHECK(job->error_log.buffer.size == 0);
        if (job->error_log.buffer.size > 0) tester.log.Debug({}, "Error log: {}", job->error_log.buffer);
    }

    TRY(PrintDirectory(tester, destination_folder, "Post-installation"));

    // Modify the installed components. If this fails then it might mean the test files have moved.
    TRY(Rename(path::Join(tester.scratch_arena, Array {destination_folder, "Tester - Test Lua", "floe.lua"}),
               path::Join(tester.scratch_arena,
                          Array {destination_folder, "Tester - Test Lua", "renamed.floe.lua"})));
    TRY(Rename(
        path::Join(tester.scratch_arena, Array {destination_folder, "presets", "sine.floe-preset"}),
        path::Join(tester.scratch_arena, Array {destination_folder, "presets", "renamed-sine.floe-preset"})));

    TRY(PrintDirectory(tester, destination_folder, "Post-rename"));

    // If the components are modified and we set to Skip, it should skip them.
    {
        auto job = CreateInstallJob(tester.scratch_arena, job_options);
        DEFER { DestroyInstallJob(job); };

        StartJob(*job);

        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::AwaitingUserInput);

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

        CHECK(job->error_log.buffer.size == 0);
        if (job->error_log.buffer.size > 0) tester.log.Debug({}, "Error log: {}", job->error_log.buffer);
    }

    TRY(PrintDirectory(tester, destination_folder, "Post-skip-install"));

    // If the components are modified and we set the Overwrite, it should overwrite them.
    {
        auto job = CreateInstallJob(tester.scratch_arena, job_options);
        DEFER { DestroyInstallJob(job); };

        StartJob(*job);

        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::AwaitingUserInput);

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
                // In our previous 'skip' case, the presets we reinstalled. They would be put in a
                // separate folder, name appended with a number. So we expect the system to have found
                // this installation.
                CHECK_EQ(TypeOfActionTaken(comp), "already installed"_s);
            }
        }
        CHECK(job->error_log.buffer.size == 0);
        if (job->error_log.buffer.size > 0) tester.log.Debug({}, "Error log: {}", job->error_log.buffer);
    }

    TRY(PrintDirectory(tester, destination_folder, "Post-overwrite-install"));

    // IMPROVE: test error conditions and different conditions for destination folders

    return k_success;
}

} // namespace package

TEST_REGISTRATION(RegisterPackageInstallationTests) { REGISTER_TEST(package::TestPackageInstallation); }
