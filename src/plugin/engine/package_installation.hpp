// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/package_format.hpp"

#include "sample_lib_server/sample_library_server.hpp"
#include "settings/settings_file.hpp"

// This is a higher-level API on top of package_format.hpp. It provides an API for multi-threaded code to
// install packages. It references other parts of the codebase such as the sample library server in order to
// make the best decisions.

namespace package {

struct InstallJob {
    enum class State {
        Installing, // worker owns all data
        AwaitingUserInput, // worker thread is not running, user input needed
        DoneSuccess, // worker thread is not running, packages install completed
        DoneError, // worker thread is not running, packages install failed
    };

    enum class UserDecision {
        Unknown,
        Overwrite,
        Skip,
    };

    enum class FolderInstallationOutcome : u8 {
        Skipped,
        Overwritten,
        Installed,
        Updated,
        DoneNothingAlreadyInstalled,
        DoneNothingNewerVersionAlreadyInstalled,
        Count,
    };

    struct InitOptions {
        String zip_path;
        String libraries_install_folder;
        String presets_install_folder;
        sample_lib_server::Server& server;
        Span<String> preset_folders;
    };

    InstallJob(InitOptions opts)
        : path(arena.Clone(opts.zip_path))
        , libraries_install_folder(arena.Clone(opts.libraries_install_folder))
        , presets_install_folder(arena.Clone(opts.presets_install_folder))
        , sample_lib_server(opts.server)
        , preset_folders(arena.Clone(opts.preset_folders, CloneType::Deep)) {}

    ~InstallJob() {
        if (reader) package::ReaderDeinit(*reader);
    }

    Atomic<State> state {State::Installing};
    Atomic<bool> abort {false};
    ArenaAllocator arena {PageAllocator::Instance()}; // never freed
    String const path; // always valid
    String const libraries_install_folder; // always valid
    String const presets_install_folder; // always valid
    sample_lib_server::Server& sample_lib_server;
    Span<String> const preset_folders;

    Optional<Reader> file_reader {};
    Optional<PackageReader> reader {};
    BufferLogger error_log {arena};

    struct Folder {
        PackageFolder folder;
        ExistingInstallationStatus existing_installation_status {};
        UserDecision user_decision {UserDecision::Unknown};
        FolderInstallationOutcome outcome {FolderInstallationOutcome::Skipped};
    };
    ArenaList<Folder, false> folders {arena};
};

PUBLIC void CompleteJob(InstallJob& job);

namespace detail {

struct TryHelpersToState {
    static auto IsError(auto const& o) { return o.HasError(); }
    static auto ExtractError(auto const&) { return InstallJob::State::DoneError; }
    static auto ExtractValue(auto& o) { return o.ReleaseValue(); }
};

PUBLIC InstallJob::State StartJobInternal(InstallJob& job) {
    using H = package::detail::TryHelpersToState;

    job.file_reader = ({
        auto o = Reader::FromFile(job.path);
        if (o.HasError()) {
            job.error_log.Error({}, "couldn't read file {}: {}", path::Filename(job.path), o.Error());
            return InstallJob::State::DoneError;
        }
        o.ReleaseValue();
    });

    job.reader = PackageReader {.zip_file_reader = *job.file_reader};

    TRY_H(ReaderInit(*job.reader, job.error_log));

    PackageFolderIteratorIndex it {};
    bool user_input_needed = false;
    u32 num_folders = 0;
    constexpr u32 k_max_folders = 4000;
    for (; num_folders < k_max_folders; ++num_folders) {
        if (job.abort.Load(LoadMemoryOrder::Acquire)) {
            job.error_log.Error({}, "aborted");
            return InstallJob::State::DoneError;
        }

        auto const folder = TRY_H(IteratePackageFolders(*job.reader, it, job.arena, job.error_log));
        if (!folder) {
            // end of folders
            break;
        }

        auto const existing_check = ({
            ExistingInstallationStatus r;
            switch (folder->type) {
                case package::SubfolderType::Libraries: {
                    sample_lib_server::RequestScanningOfUnscannedFolders(job.sample_lib_server);

                    u32 wait_ms = 0;
                    while (sample_lib_server::IsScanningSampleLibraries(job.sample_lib_server)) {
                        constexpr u32 k_sleep_ms = 100;
                        SleepThisThread(k_sleep_ms);
                        wait_ms += k_sleep_ms;

                        constexpr u32 k_timeout_ms = 30 * 1000;
                        if (wait_ms >= k_timeout_ms) {
                            job.error_log.Error({}, "timed out waiting for sample libraries to be scanned");
                            return InstallJob::State::DoneError;
                        }
                    }

                    auto existing_lib =
                        sample_lib_server::FindLibraryRetained(job.sample_lib_server, folder->library->Id());
                    DEFER { existing_lib.Release(); };

                    r = TRY_H(LibraryCheckExistingInstallation(*folder,
                                                               existing_lib ? &*existing_lib : nullptr,
                                                               job.arena,
                                                               job.error_log));
                    break;
                }
                case package::SubfolderType::Presets: {
                    r = TRY_H(PresetsCheckExistingInstallation(*folder,
                                                               job.preset_folders,
                                                               job.arena,
                                                               job.error_log));
                    break;
                }
                case package::SubfolderType::Count: PanicIfReached();
            }
            r;
        });

        if (UserInputIsRequired(existing_check)) user_input_needed = true;

        auto* f = job.folders.PrependUninitialised();
        PLACEMENT_NEW(f)
        InstallJob::Folder {
            .folder = *folder,
            .existing_installation_status = existing_check,
            .user_decision = InstallJob::UserDecision::Unknown,
        };
    }

    if (num_folders == k_max_folders) {
        job.error_log.Error({}, "too many folders in package");
        return InstallJob::State::DoneError;
    }

    if (user_input_needed) return InstallJob::State::AwaitingUserInput;

    return InstallJob::State::Installing;
}

PUBLIC InstallJob::State CompleteJobInternal(InstallJob& job) {
    using H = package::detail::TryHelpersToState;

    for (auto& folder : job.folders) {
        if (job.abort.Load(LoadMemoryOrder::Acquire)) {
            job.error_log.Error({}, "aborted");
            return InstallJob::State::DoneError;
        }

        if (NoInstallationRequired(folder.existing_installation_status)) continue;

        if (UserInputIsRequired(folder.existing_installation_status))
            ASSERT(folder.user_decision != InstallJob::UserDecision::Unknown);

        ExtractOptions options {
            .destination = ({
                Destination d {DestinationType::DefaultFolderWithSubfolderFromPackage};
                if (folder.folder.library) d = *path::Directory(folder.folder.library->path);
                d;
            }),
            .overwrite_existing_files =
                folder.folder.type == SubfolderType::Libraries &&
                folder.existing_installation_status & ExistingInstallationStatus::Installed &&
                folder.user_decision == InstallJob::UserDecision::Overwrite,
            .resolve_install_folder_name_conflicts = folder.folder.type == SubfolderType::Presets,
        };

        String destination_folder {};
        switch (folder.folder.type) {
            case package::SubfolderType::Libraries: destination_folder = job.libraries_install_folder; break;
            case package::SubfolderType::Presets: destination_folder = job.presets_install_folder; break;
            case package::SubfolderType::Count: PanicIfReached();
        }

        TRY_H(ReaderExtractFolder(*job.reader,
                                  folder.folder,
                                  destination_folder,
                                  job.arena,
                                  job.error_log,
                                  options));
    }

    return InstallJob::State::DoneSuccess;
}

} // namespace detail

// [worker thread]
PUBLIC void StartJob(InstallJob& job) {
    ASSERT(job.state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::Installing);
    auto const result = detail::StartJobInternal(job);
    if (result != InstallJob::State::Installing) {
        job.state.Store(result, StoreMemoryOrder::Release);
        return;
    }

    CompleteJob(job);
}

// [worker thread]
PUBLIC void CompleteJob(InstallJob& job) {
    ASSERT(job.state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::Installing);
    auto const result = detail::CompleteJobInternal(job);
    job.state.Store(result, StoreMemoryOrder::Release);
}

// [main thread]
PUBLIC void AllUserInputReceived(InstallJob& job, ThreadPool& thread_pool) {
    ASSERT(job.state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::AwaitingUserInput);
    for (auto& folder : job.folders)
        if (UserInputIsRequired(folder.existing_installation_status))
            ASSERT(folder.user_decision != InstallJob::UserDecision::Unknown);

    job.state.Store(InstallJob::State::Installing, StoreMemoryOrder::Release);
    thread_pool.AddJob([&job]() { package::CompleteJob(job); });
}

// The state variable dictates who is allowed access to a job's data at any particular time: whether that's
// the main thread or a worker thread. We use a data structure that does not reallocate memory, so that we can
// safely push more jobs onto the list from the main thread, and give the worker thread a reference to the
// job.
using InstallJobs = BoundedList<InstallJob, 16>;

// [main thread]
PUBLIC void AddJob(InstallJobs& jobs,
                   String zip_path,
                   SettingsFile& settings,
                   ThreadPool& thread_pool,
                   ArenaAllocator& scratch_arena,
                   sample_lib_server::Server& sample_library_server) {
    ASSERT(!jobs.Full());
    ASSERT(path::IsAbsolute(zip_path));
    ASSERT(CheckThreadName("main"));

    auto job = jobs.AppendUninitialised();
    PLACEMENT_NEW(job)
    package::InstallJob({
        .zip_path = zip_path,
        .libraries_install_folder = settings.paths.always_scanned_folder[ToInt(ScanFolderType::Libraries)],
        .presets_install_folder = settings.paths.always_scanned_folder[ToInt(ScanFolderType::Presets)],
        .server = sample_library_server,
        .preset_folders = CombineStringArrays(
            scratch_arena,
            settings.settings.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)],
            Array {settings.paths.always_scanned_folder[ToInt(ScanFolderType::Presets)]}),
    });
    thread_pool.AddJob([job]() { package::StartJob(*job); });
}

// [main thread]
PUBLIC InstallJobs::Iterator RemoveJob(InstallJobs& jobs, InstallJobs::Iterator it) {
    ASSERT(CheckThreadName("main"));
    ASSERT(it->state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::DoneError ||
           it->state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::DoneSuccess);

    return jobs.Remove(it);
}

// Stalls until all jobs are done.
// [main thread]
PUBLIC void ShutdownJobs(InstallJobs& jobs) {
    ASSERT(CheckThreadName("main"));
    if (jobs.Empty()) return;

    for (auto& j : jobs)
        j.abort.Store(true, StoreMemoryOrder::Release);

    u32 wait_ms = 0;
    constexpr u32 k_sleep_ms = 100;
    constexpr u32 k_timeout_ms = 30 * 1000;

    for (; wait_ms < k_timeout_ms; wait_ms += k_sleep_ms) {
        bool jobs_are_installing = false;
        for (auto& j : jobs) {
            if (j.state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::Installing) {
                jobs_are_installing = true;
                break;
            }
        }

        if (!jobs_are_installing) break;

        SleepThisThread(k_sleep_ms);
    }

    ASSERT(wait_ms < k_timeout_ms);

    jobs.RemoveAll();
}

// [threadsafe]
PUBLIC InstallJob::FolderInstallationOutcome Outcome(ExistingInstallationStatus existing_installation_status,
                                                     InstallJob::UserDecision user_decision) {
    using enum InstallJob::FolderInstallationOutcome;

    if (existing_installation_status == ExistingInstallationStatus::NotInstalled) return Installed;

    if (UserInputIsRequired(existing_installation_status)) {
        switch (user_decision) {
            case InstallJob::UserDecision::Unknown: PanicIfReached();
            case InstallJob::UserDecision::Overwrite: {
                if (existing_installation_status & ExistingInstallationStatus::InstalledIsOlder)
                    return Updated;
                else
                    return Overwritten;
            }
            case InstallJob::UserDecision::Skip: return Skipped;
        }
    }

    if (NoInstallationRequired(existing_installation_status)) {
        if (existing_installation_status & ExistingInstallationStatus::InstalledIsNewer) {
            return DoneNothingNewerVersionAlreadyInstalled;
        } else {
            ASSERT(existing_installation_status == ExistingInstallationStatus::Installed);
            return DoneNothingAlreadyInstalled;
        }
    }

    PanicIfReached();
}

PUBLIC String OutcomeString(InstallJob::FolderInstallationOutcome outcome) {
    switch (outcome) {
        case package::InstallJob::FolderInstallationOutcome::Overwritten: return "overwritten";
        case package::InstallJob::FolderInstallationOutcome::Skipped: return "skipped";
        case package::InstallJob::FolderInstallationOutcome::Installed: return "installed";
        case package::InstallJob::FolderInstallationOutcome::DoneNothingAlreadyInstalled:
            return "already installed";
        case package::InstallJob::FolderInstallationOutcome::DoneNothingNewerVersionAlreadyInstalled:
            return "newer version already installed";
        case package::InstallJob::FolderInstallationOutcome::Updated: return "updated";
        case package::InstallJob::FolderInstallationOutcome::Count: PanicIfReached();
    }
    return {};
}

// [main-thread]
PUBLIC InstallJob::FolderInstallationOutcome Outcome(InstallJob::Folder const& folder) {
    return Outcome(folder.existing_installation_status, folder.user_decision);
}

} // namespace package
