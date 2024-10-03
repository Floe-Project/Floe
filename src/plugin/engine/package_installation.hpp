// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/package_format.hpp"

#include "sample_lib_server/sample_library_server.hpp"

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
        ResultNotified, // main thread
    };

    enum class UserDecision {
        Unknown,
        Overwrite,
        Skip,
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

    Atomic<State> state {State::Installing};
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
        FolderCheckResult check_result;
        UserDecision user_decision {};
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
    while (true) {
        auto const folder = TRY_H(IteratePackageFolders(*job.reader, it, job.arena, job.error_log));
        if (!folder) break;

        auto const check_result = ({
            FolderCheckResult r;
            switch (folder->type) {
                case package::SubfolderType::Libraries: {
                    sample_lib_server::RequestScanningOfUnscannedFolders(job.sample_lib_server);

                    u32 wait_ms = 0;
                    while (sample_lib_server::IsScanningSampleLibraries(job.sample_lib_server)) {
                        SleepThisThread(100);
                        wait_ms += 100;

                        constexpr u32 k_max_wait_ms = 2 * 60 * 1000;
                        if (wait_ms >= k_max_wait_ms) {
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

        if (check_result.recommended_action == RecommendedAction::AskUser) user_input_needed = true;

        auto* f = job.folders.PrependUninitialised();
        PLACEMENT_NEW(f)
        InstallJob::Folder {
            .folder = *folder,
            .check_result = check_result,
            .user_decision = InstallJob::UserDecision::Unknown,
        };
    }

    if (user_input_needed) return InstallJob::State::AwaitingUserInput;

    return InstallJob::State::Installing;
}

PUBLIC InstallJob::State CompleteJobInternal(InstallJob& job) {
    using H = package::detail::TryHelpersToState;

    for (auto& folder : job.folders) {
        if (folder.check_result.recommended_action == RecommendedAction::AskUser)
            ASSERT(folder.user_decision != InstallJob::UserDecision::Unknown);

        if (folder.user_decision == InstallJob::UserDecision::Skip)
            continue;
        else if (folder.user_decision == InstallJob::UserDecision::Overwrite)
            folder.check_result.extract_options.overwrite_existing_files = true;

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
                                  folder.check_result.extract_options));
    }

    return InstallJob::State::DoneSuccess;
}

} // namespace detail

PUBLIC void StartJob(InstallJob& job) {
    ASSERT(job.state.Load(LoadMemoryOrder::Relaxed) == InstallJob::State::Installing);
    auto const result = detail::StartJobInternal(job);
    if (result != InstallJob::State::Installing) {
        job.state.Store(result, StoreMemoryOrder::Release);
        return;
    }

    CompleteJob(job);
}

PUBLIC void CompleteJob(InstallJob& job) {
    ASSERT(job.state.Load(LoadMemoryOrder::Relaxed) == InstallJob::State::Installing);
    auto const result = detail::CompleteJobInternal(job);
    job.state.Store(result, StoreMemoryOrder::Release);
}

} // namespace package
