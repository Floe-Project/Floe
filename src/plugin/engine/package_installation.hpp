// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/package_format.hpp"

#include "sample_lib_server/sample_library_server.hpp"
#include "settings/settings_file.hpp"

// This is a higher-level API on top of package_format.hpp.
//
// It provides an API for multi-threaded code to install packages. It brings together other parts of the
// codebase such as the sample library server in order to make the best decisions when installing.

namespace package {

struct ExistingInstalledComponent {
    enum class VersionDifference : u8 { Equal, InstalledIsOlder, InstalledIsNewer };
    enum class ModifiedSinceInstalled : u8 { Unmodified, MaybeModified, Modified };
    using enum VersionDifference;
    using enum ModifiedSinceInstalled;
    bool operator==(ExistingInstalledComponent const& o) const = default;
    bool installed;
    VersionDifference version_difference; // if installed
    ModifiedSinceInstalled modified_since_installed; // if installed
};

PUBLIC bool32 UserInputIsRequired(ExistingInstalledComponent status) {
    return status.installed && status.modified_since_installed != ExistingInstalledComponent::Unmodified;
}

PUBLIC bool32 NoInstallationRequired(ExistingInstalledComponent status) {
    return status.installed && status.modified_since_installed == ExistingInstalledComponent::Unmodified &&
           (status.version_difference == ExistingInstalledComponent::Equal ||
            status.version_difference == ExistingInstalledComponent::InstalledIsNewer);
}

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

    enum class DestinationWriteMode {
        // Create a subfolder based on the package name. Resolve subfolder name conflicts by automatically
        // appending a number.
        CreateUniqueSubfolder,

        // No subfolder. Files are overwritten.
        OverwriteDirectly,
    };

    ArenaAllocator& arena;
    Atomic<State> state {State::Installing};
    Atomic<bool> abort {false};
    String const path;
    String const libraries_install_folder;
    String const presets_install_folder;
    sample_lib_server::Server& sample_lib_server;
    Span<String> const preset_folders;

    Optional<Reader> file_reader {};
    Optional<PackageReader> reader {}; // NOTE: needs uninit
    BufferLogger error_log;

    struct Component {
        package::Component component;
        ExistingInstalledComponent existing_installation_status {};
        UserDecision user_decision {UserDecision::Unknown};
        String destination_folder;
        DestinationWriteMode destination_write_mode;
    };
    ArenaList<Component, false> components;
};

// ==========================================================================================================
//      _      _        _ _
//     | |    | |      (_) |
//   __| | ___| |_ __ _ _| |___
//  / _` |/ _ \ __/ _` | | / __|
// | (_| |  __/ || (_| | | \__ \
//  \__,_|\___|\__\__,_|_|_|___/
//
// ==========================================================================================================

namespace detail {

static ValueOrError<ExistingInstalledComponent, PackageError>
LibraryCheckExistingInstallation(Component const& component,
                                 sample_lib::Library const* existing_matching_library,
                                 ArenaAllocator& scratch_arena,
                                 Logger& error_log) {
    ASSERT(component.type == ComponentType::Library);
    ASSERT(component.library);

    if (!existing_matching_library) return ExistingInstalledComponent {.installed = false};

    auto const existing_folder = *path::Directory(existing_matching_library->path);
    ASSERT(existing_matching_library->Id() == component.library->Id());

    auto const actual_checksums = ({
        auto const o = ChecksumsForFolder(existing_folder, scratch_arena, scratch_arena);
        if (o.HasError())
            return detail::CreatePackageError(error_log,
                                              o.Error(),
                                              "Couldn't read folder \"{}\"",
                                              existing_folder);
        o.Value();
    });

    if (!ChecksumsDiffer(component.checksum_values, actual_checksums, nullptr))
        return ExistingInstalledComponent {
            .installed = true,
            .version_difference = ExistingInstalledComponent::Equal,
            .modified_since_installed = ExistingInstalledComponent::Unmodified,
        };

    ExistingInstalledComponent result = {.installed = true};

    auto const checksum_file_path = path::Join(scratch_arena, Array {existing_folder, k_checksums_file});
    if (auto const o = ReadEntireFile(checksum_file_path, scratch_arena); !o.HasError()) {
        auto const stored_checksums = ParseChecksumFile(o.Value(), scratch_arena);
        if (stored_checksums.HasValue() &&
            !ChecksumsDiffer(stored_checksums.Value(), actual_checksums, nullptr)) {
            result.modified_since_installed = ExistingInstalledComponent::Unmodified;
        } else {
            // The library has been modified since it was installed. OR the checksum file is badly formatted,
            // which presumably means it was modified.
            result.modified_since_installed = ExistingInstalledComponent::Modified;
        }
    } else {
        result.modified_since_installed = ExistingInstalledComponent::MaybeModified;
    }

    if (existing_matching_library->minor_version < component.library->minor_version)
        result.version_difference = ExistingInstalledComponent::InstalledIsOlder;
    else if (existing_matching_library->minor_version > component.library->minor_version)
        result.version_difference = ExistingInstalledComponent::InstalledIsNewer;
    else
        result.version_difference = ExistingInstalledComponent::Equal;

    return result;
}

// We don't actually check the checksums file of a presets folder. All we do is check if the exact files from
// the package are already installed. If there's any discrepancy, we just install the package again to a new
// folder. It means there could be duplicate files, but it's not a problem; preset files are tiny, and our
// preset system will ignore duplicate files by checking their checksums.
//
// We take this approach because there is no reason to overwrite preset files. Preset files are tiny. If
// there's a 'version 2' of a preset pack, then it might as well be installed alongside version 1.
static ValueOrError<ExistingInstalledComponent, PackageError>
PresetsCheckExistingInstallation(Component const& component,
                                 Span<String const> presets_folders,
                                 ArenaAllocator& scratch_arena,
                                 Logger& error_log) {
    for (auto const folder : presets_folders) {
        auto const entries = ({
            auto const o = AllEntriesRecursive(scratch_arena,
                                               folder,
                                               k_nullopt,
                                               {
                                                   .wildcard = "*",
                                                   .get_file_size = true,
                                                   .skip_dot_files = true,
                                               });
            if (o.HasError())
                return detail::CreatePackageError(error_log,
                                                  o.Error(),
                                                  "Couldn't read folder \"{}\"",
                                                  folder);
            o.Value();
        });

        if constexpr (IS_WINDOWS)
            for (auto& entry : entries)
                Replace(entry.subpath, '\\', '/');

        for (auto const dir_entry : entries) {
            if (dir_entry.type != FileType::Directory) continue;

            bool dir_contains_all_expected_files = true;
            for (auto const [expected_path, checksum] : component.checksum_values) {
                bool found_expected = false;
                for (auto const file_entry : entries) {
                    if (file_entry.type != FileType::File) continue;
                    auto const relative =
                        detail::RelativePathIfInFolder(file_entry.subpath, dir_entry.subpath);
                    if (!relative) continue;
                    if (path::Equal(*relative, expected_path)) {
                        found_expected = true;
                        break;
                    }
                }
                if (!found_expected) {
                    dir_contains_all_expected_files = false;
                    break;
                }
            }

            if (dir_contains_all_expected_files) {
                bool matches_exactly = true;

                // check the checksums of all files
                for (auto const [expected_path, checksum] : component.checksum_values) {
                    auto const cursor = scratch_arena.TotalUsed();
                    DEFER {
                        scratch_arena.TryShrinkTotalUsed(cursor);
                        ASSERT(scratch_arena.TotalUsed() >= cursor);
                    };

                    auto const full_path =
                        path::Join(scratch_arena, Array {folder, dir_entry.subpath, expected_path});

                    auto const matches_file = ({
                        auto const o = FileMatchesChecksum(full_path, *checksum, scratch_arena);
                        if (o.HasError())
                            return detail::CreatePackageError(error_log,
                                                              o.Error(),
                                                              "Couldn't read file \"{}\"",
                                                              full_path);
                        o.Value();
                    });

                    if (!matches_file) {
                        matches_exactly = false;
                        break;
                    }
                }

                if (matches_exactly)
                    return ExistingInstalledComponent {
                        .installed = true,
                        .version_difference = ExistingInstalledComponent::Equal,
                        .modified_since_installed = ExistingInstalledComponent::Unmodified,
                    };
            }
        }
    }

    // It may actually be installed, but for presets we take the approach of just installing the package again
    // unless it is already exactly installed.
    return ExistingInstalledComponent {.installed = false};
}

static ErrorCodeOr<String> ResolvePossibleFilenameConflicts(String path, ArenaAllocator& arena) {
    if (auto const o = GetFileType(path); o.HasError() && o.Error() == FilesystemError::PathDoesNotExist)
        return path;

    constexpr usize k_max_suffix_number = 999;
    constexpr usize k_max_suffix_str_size = " (999)"_s.size;

    auto buffer = arena.AllocateExactSizeUninitialised<char>(path.size + k_max_suffix_str_size);
    usize pos = 0;
    WriteAndIncrement(pos, buffer, path);
    WriteAndIncrement(pos, buffer, " ("_s);

    Optional<ErrorCode> error {};

    for (usize suffix_num = 1; suffix_num <= k_max_suffix_number; ++suffix_num) {
        usize initial_pos = pos;
        DEFER { pos = initial_pos; };
        pos +=
            fmt::IntToString(suffix_num, buffer.data + pos, {.base = fmt::IntToStringOptions::Base::Decimal});
        WriteAndIncrement(pos, buffer, ')');

        if (auto const o = GetFileType({buffer.data, pos}); o.HasError()) {
            if (o.Error() == FilesystemError::PathDoesNotExist) return arena.ResizeType(buffer, pos, pos);
            error = o.Error();
        }
    }

    return error ? *error : ErrorCode {FilesystemError::FolderContainsTooManyFiles};
}

static ErrorCodeOr<void> ExtractFolder(PackageReader& package,
                                       String dir_in_zip,
                                       String destination_folder,
                                       ArenaAllocator& scratch_arena,
                                       HashTable<String, ChecksumValues> destination_checksums) {
    g_log.Info(k_log_mod, "Extracting folder: {} to {}", dir_in_zip, destination_folder);
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat = TRY(detail::FileStat(package, file_index));
        if (file_stat.m_is_directory) continue;
        auto const path = PathWithoutTrailingSlash(file_stat.m_filename);
        auto const relative_path = detail::RelativePathIfInFolder(path, dir_in_zip);
        if (!relative_path) continue;

        auto const out_path = path::Join(scratch_arena, Array {destination_folder, *relative_path});
        DEFER { scratch_arena.Free(out_path.ToByteSpan()); };
        TRY(CreateDirectory(*path::Directory(out_path),
                            {
                                .create_intermediate_directories = true,
                                .fail_if_exists = false,
                            }));
        auto out_file = TRY(OpenFile(out_path, FileMode::WriteNoOverwrite));
        TRY(detail::ExtractFileToFile(package, file_stat, out_file));
    }

    {
        auto const checksum_file_path =
            path::Join(scratch_arena, Array {destination_folder, k_checksums_file});
        TRY(CreateDirectory(*path::Directory(checksum_file_path),
                            {
                                .create_intermediate_directories = true,
                                .fail_if_exists = false,
                            }));
        TRY(WriteChecksumsValuesToFile(checksum_file_path,
                                       destination_checksums,
                                       scratch_arena,
                                       "Generated by Floe"));
    }

    return k_success;
}

// Destination folder is the folder where the package will be installed. e.g. /home/me/Libraries
// The final folder name is determined by options.destination.
// Extracts to a temp folder than then renames to the final location. This ensures we either fail or succeed,
// with no in-between cases where the folder is partially extracted. Additionally, it doesn't generate lots of
// filesystem-change notifications which Floe might try to process and fail on.
static VoidOrError<PackageError> ReaderInstallComponent(PackageReader& package,
                                                        Component const& component,
                                                        ArenaAllocator& scratch_arena,
                                                        Logger& error_log,
                                                        String destination_folder,
                                                        InstallJob::DestinationWriteMode write_mode) {
    ASSERT(path::IsAbsolute(destination_folder));

    auto const resolved_destination_folder = ({
        String f = destination_folder;

        if (write_mode == InstallJob::DestinationWriteMode::CreateUniqueSubfolder) {
            f = path::Join(scratch_arena, Array {f, path::Filename(component.path)});

            auto const o = detail::ResolvePossibleFilenameConflicts(f, scratch_arena);
            if (o.HasError())
                return detail::CreatePackageError(error_log,
                                                  o.Error(),
                                                  "Couldn't access destination folder \"{}\"",
                                                  f);
            if (o.Value() != f)
                g_log.Info(k_log_mod, "Resolved folder name conflict: {} -> {}", f, o.Value());
            f = o.Value();
        }

        f;
    });
    ASSERT(path::IsAbsolute(resolved_destination_folder));

    // Try to get a folder on the same filesystem so that we can atomic-rename and therefore reduce the chance
    // of leaving partially extracted files and generating lots of filesystem-change events.
    auto const temp_folder = ({
        ASSERT(GetFileType(destination_folder).HasValue());

        auto const o = TemporaryDirectoryOnSameFilesystemAs(destination_folder, scratch_arena);
        if (o.HasError()) {
            return detail::CreatePackageError(error_log,
                                              o.Error(),
                                              "Unable to access a temporary folder for \"{}\"",
                                              destination_folder);
        }

        String(o.Value());
    });
    DEFER {
        auto _ = Delete(temp_folder,
                        {
                            .type = DeleteOptions::Type::DirectoryRecursively,
                            .fail_if_not_exists = false,
                        });
    };

    if (auto const o = detail::ExtractFolder(package,
                                             component.path,
                                             temp_folder,
                                             scratch_arena,
                                             component.checksum_values);
        o.HasError()) {
        return detail::CreatePackageError(error_log,
                                          o.Error(),
                                          "Couldn't extract to temporary folder \"{}\"",
                                          temp_folder);
    }

    if (auto const rename_o = Rename(temp_folder, resolved_destination_folder); rename_o.HasError()) {
        if (write_mode == InstallJob::DestinationWriteMode::OverwriteDirectly &&
            rename_o.Error() == FilesystemError::NotEmpty) {
            // Rather than overwrite files one-by-one, we delete the whole folder and replace it with the new
            // component. We do this because overwriting files could leave unwanted files behind if the
            // component has fewer files than the existing installation. This is confusing in general, but in
            // particular it's bad for a library because there could be 2 Lua files.

            // Rename the existing folder so that it's got a unique, recognizable name that will be easy to
            // spot in the Trash.
            MutableString new_name {};
            {
                new_name = scratch_arena.AllocateExactSizeUninitialised<char>(
                    resolved_destination_folder.size + " (old-)"_s.size + 13);
                usize pos = 0;
                WriteAndIncrement(pos, new_name, resolved_destination_folder);
                WriteAndIncrement(pos, new_name, " (old-"_s);
                auto const chars_written = fmt::IntToString(RandomU64(package.seed),
                                                            new_name.data + pos,
                                                            {.base = fmt::IntToStringOptions::Base::Base32});
                ASSERT(chars_written <= 13);
                pos += chars_written;
                WriteAndIncrement(pos, new_name, ')');
                new_name.size = pos;

                auto const o = Rename(resolved_destination_folder, new_name);
                if (o.HasError()) {
                    return detail::CreatePackageError(error_log,
                                                      o.Error(),
                                                      "Couldn't install files to your install folder \"{}\"",
                                                      resolved_destination_folder);
                }
            }

            // The old folder is out of the way so we can now install the new component.
            if (auto const rename2_o = Rename(temp_folder, resolved_destination_folder);
                rename2_o.HasError()) {
                // We failed to install the new files, try to restore the old files.
                auto const _ = Rename(new_name, resolved_destination_folder);

                return detail::CreatePackageError(error_log,
                                                  rename2_o.Error(),
                                                  "Couldn't install files to your install folder \"{}\"",
                                                  resolved_destination_folder);
            }

            // The new component is installed, let's try to trash the old folder.
            String folder_in_trash {};
            if (auto const o = TrashFileOrDirectory(new_name, scratch_arena); o.HasValue()) {
                folder_in_trash = o.Value();
            } else {
                // Try to undo the rename
                auto const _ = Rename(new_name, resolved_destination_folder);

                return detail::CreatePackageError(error_log,
                                                  o.Error(),
                                                  "Couldn't send folder \"{}\" to your " TRASH_NAME,
                                                  resolved_destination_folder);
            }

        } else {
            return detail::CreatePackageError(error_log,
                                              rename_o.Error(),
                                              "Couldn't install files to your install folder \"{}\"",
                                              resolved_destination_folder);
        }
    }

    // remove hidden
    if (auto const o = WindowsSetFileAttributes(resolved_destination_folder, k_nullopt); o.HasError()) {
        return detail::CreatePackageError(error_log,
                                          o.Error(),
                                          "Failed to make the folder \"{}\" visible",
                                          resolved_destination_folder);
    }

    return k_success;
}

struct TryHelpersToState {
    static auto IsError(auto const& o) { return o.HasError(); }
    static auto ExtractError(auto const&) { return InstallJob::State::DoneError; }
    static auto ExtractValue(auto& o) { return o.ReleaseValue(); }
};

static InstallJob::State StartJobInternal(InstallJob& job) {
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

    PackageComponentIndex it {};
    bool user_input_needed = false;
    u32 num_components = 0;
    constexpr u32 k_max_components = 4000;
    for (; num_components < k_max_components; ++num_components) {
        if (job.abort.Load(LoadMemoryOrder::Acquire)) {
            job.error_log.Error({}, "aborted");
            return InstallJob::State::DoneError;
        }

        auto const component = TRY_H(IteratePackageComponents(*job.reader, it, job.arena, job.error_log));
        if (!component) {
            // end of folders
            break;
        }

        String destination_folder = {};
        InstallJob::DestinationWriteMode write_mode {};

        auto const existing_check = ({
            ExistingInstalledComponent r;
            switch (component->type) {
                case package::ComponentType::Library: {
                    sample_lib_server::RequestScanningOfUnscannedFolders(job.sample_lib_server);

                    u32 wait_ms = 0;
                    while (sample_lib_server::IsScanningSampleLibraries(job.sample_lib_server)) {
                        constexpr u32 k_sleep_ms = 100;
                        SleepThisThread(k_sleep_ms);
                        wait_ms += k_sleep_ms;

                        constexpr u32 k_timeout_ms = 120 * 1000;
                        if (wait_ms >= k_timeout_ms) {
                            job.error_log.Error({}, "timed out waiting for sample libraries to be scanned");
                            return InstallJob::State::DoneError;
                        }
                    }

                    auto existing_lib = sample_lib_server::FindLibraryRetained(job.sample_lib_server,
                                                                               component->library->Id());
                    DEFER { existing_lib.Release(); };

                    r = TRY_H(
                        detail::LibraryCheckExistingInstallation(*component,
                                                                 existing_lib ? &*existing_lib : nullptr,
                                                                 job.arena,
                                                                 job.error_log));
                    if (existing_lib) {
                        destination_folder = job.arena.Clone(*path::Directory(existing_lib->path));
                        write_mode = InstallJob::DestinationWriteMode::OverwriteDirectly;
                    } else {
                        destination_folder = job.libraries_install_folder;
                        write_mode = InstallJob::DestinationWriteMode::CreateUniqueSubfolder;
                    }

                    break;
                }
                case package::ComponentType::Presets: {
                    r = TRY_H(detail::PresetsCheckExistingInstallation(*component,
                                                                       job.preset_folders,
                                                                       job.arena,
                                                                       job.error_log));
                    destination_folder = job.presets_install_folder;
                    write_mode = InstallJob::DestinationWriteMode::CreateUniqueSubfolder;
                    break;
                }
                case package::ComponentType::Count: PanicIfReached();
            }
            r;
        });

        if (UserInputIsRequired(existing_check)) user_input_needed = true;

        PLACEMENT_NEW(job.components.PrependUninitialised())
        InstallJob::Component {
            .component = *component,
            .existing_installation_status = existing_check,
            .user_decision = InstallJob::UserDecision::Unknown,
            .destination_folder = destination_folder,
            .destination_write_mode = write_mode,
        };
    }

    if (num_components == k_max_components) {
        job.error_log.Error({}, "too many components in package");
        return InstallJob::State::DoneError;
    }

    if (user_input_needed) return InstallJob::State::AwaitingUserInput;

    return InstallJob::State::Installing;
}

static InstallJob::State CompleteJobInternal(InstallJob& job) {
    using H = package::detail::TryHelpersToState;

    for (auto& component : job.components) {
        if (job.abort.Load(LoadMemoryOrder::Acquire)) {
            job.error_log.Error({}, "aborted");
            return InstallJob::State::DoneError;
        }

        if (NoInstallationRequired(component.existing_installation_status)) continue;

        if (UserInputIsRequired(component.existing_installation_status)) {
            ASSERT(component.user_decision != InstallJob::UserDecision::Unknown);
            if (component.user_decision == InstallJob::UserDecision::Skip) continue;
        }

        TRY_H(ReaderInstallComponent(*job.reader,
                                     component.component,
                                     job.arena,
                                     job.error_log,
                                     component.destination_folder,
                                     component.destination_write_mode));

        if (component.component.type == ComponentType::Library) {
            // The sample library server should receive filesystem-events about the move and rescan
            // automatically. But the timing of filesystem events is not reliable. As we already know that the
            // folder has changed, we can issue a rescan immediately. This way, the changes will be reflected
            // sooner.
            sample_lib_server::RescanFolder(job.sample_lib_server, component.destination_folder);
        }
    }

    return InstallJob::State::DoneSuccess;
}

} // namespace detail

// ==========================================================================================================
//
//       _       _                _____ _____
//      | |     | |         /\   |  __ \_   _|
//      | | ___ | |__      /  \  | |__) || |
//  _   | |/ _ \| '_ \    / /\ \ |  ___/ | |
// | |__| | (_) | |_) |  / ____ \| |    _| |_
//  \____/ \___/|_.__/  /_/    \_\_|   |_____|
//
//
// ==========================================================================================================

struct CreateJobOptions {
    String zip_path;
    String libraries_install_folder;
    String presets_install_folder;
    sample_lib_server::Server& server;
    Span<String> preset_folders;
};

// [main thread]
PUBLIC InstallJob* CreateInstallJob(ArenaAllocator& arena, CreateJobOptions opts) {
    ASSERT(path::IsAbsolute(opts.zip_path));
    ASSERT(path::IsAbsolute(opts.libraries_install_folder));
    ASSERT(path::IsAbsolute(opts.presets_install_folder));
    auto j = arena.NewUninitialised<InstallJob>();
    PLACEMENT_NEW(j)
    InstallJob {
        .arena = arena,
        .path = arena.Clone(opts.zip_path),
        .libraries_install_folder = arena.Clone(opts.libraries_install_folder),
        .presets_install_folder = arena.Clone(opts.presets_install_folder),
        .sample_lib_server = opts.server,
        .preset_folders = arena.Clone(opts.preset_folders, CloneType::Deep),
        .error_log = {arena},
        .components = {arena},
    };
    return j;
}

// [main thread]
PUBLIC void DestroyInstallJob(InstallJob* job) {
    ASSERT(job);
    ASSERT(job->state.Load(LoadMemoryOrder::Acquire) != InstallJob::State::Installing);
    if (job->reader) package::ReaderDeinit(*job->reader);
    job->~InstallJob();
}

PUBLIC void CompleteJob(InstallJob& job);

// Run this and then check the 'state' variable. You might need to ask the user a question on the main thread
// and then call OnAllUserInputReceived.
// [worker thread (probably)]
PUBLIC void StartJob(InstallJob& job) {
    ASSERT(job.state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::Installing);
    auto const result = detail::StartJobInternal(job);
    if (result != InstallJob::State::Installing) {
        job.state.Store(result, StoreMemoryOrder::Release);
        return;
    }

    CompleteJob(job);
}

// [worker thread (probably)]
PUBLIC void CompleteJob(InstallJob& job) {
    ASSERT(job.state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::Installing);
    auto const result = detail::CompleteJobInternal(job);
    job.state.Store(result, StoreMemoryOrder::Release);
}

// Complete a job that was started but needed user input.
// [main thread]
PUBLIC void OnAllUserInputReceived(InstallJob& job, ThreadPool& thread_pool) {
    ASSERT(job.state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::AwaitingUserInput);
    for (auto& component : job.components)
        if (UserInputIsRequired(component.existing_installation_status))
            ASSERT(component.user_decision != InstallJob::UserDecision::Unknown);

    job.state.Store(InstallJob::State::Installing, StoreMemoryOrder::Release);
    thread_pool.AddJob([&job]() { package::CompleteJob(job); });
}

// [threadsafe]
PUBLIC String TypeOfActionTaken(ExistingInstalledComponent existing_installation_status,
                                InstallJob::UserDecision user_decision) {
    if (!existing_installation_status.installed) return "installed";

    if (UserInputIsRequired(existing_installation_status)) {
        switch (user_decision) {
            case InstallJob::UserDecision::Unknown: PanicIfReached();
            case InstallJob::UserDecision::Overwrite: {
                if (existing_installation_status.version_difference ==
                    ExistingInstalledComponent::InstalledIsOlder)
                    return "updated";
                else
                    return "overwritten";
            }
            case InstallJob::UserDecision::Skip: return "skipped";
        }
    }

    if (NoInstallationRequired(existing_installation_status)) {
        if (existing_installation_status.version_difference == ExistingInstalledComponent::InstalledIsNewer) {
            return "newer version already installed";
        } else {
            ASSERT(existing_installation_status.installed);
            return "already installed";
        }
    }

    PanicIfReached();
}

// [main-thread]
PUBLIC String TypeOfActionTaken(InstallJob::Component const& component) {
    return TypeOfActionTaken(component.existing_installation_status, component.user_decision);
}

// ==========================================================================================================
//
//       _       _       _      _     _              _____ _____
//      | |     | |     | |    (_)   | |       /\   |  __ \_   _|
//      | | ___ | |__   | |     _ ___| |_     /  \  | |__) || |
//  _   | |/ _ \| '_ \  | |    | / __| __|   / /\ \ |  ___/ | |
// | |__| | (_) | |_) | | |____| \__ \ |_   / ____ \| |    _| |_
//  \____/ \___/|_.__/  |______|_|___/\__| /_/    \_\_|   |_____|
//
// ==========================================================================================================

struct ManagedInstallJob {
    ~ManagedInstallJob() {
        if (job) DestroyInstallJob(job);
    }
    ArenaAllocator arena {PageAllocator::Instance()};
    InstallJob* job {};
};

// The 'state' variable dictates who is allowed access to a job's data at any particular time: whether that's
// the main thread or a worker thread. We use a data structure that does not reallocate memory, so that we can
// safely push more jobs onto the list from the main thread, and give the worker thread a reference to the
// job.
using InstallJobs = BoundedList<ManagedInstallJob, 16>;

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
    PLACEMENT_NEW(job) ManagedInstallJob();
    job->job = CreateInstallJob(
        job->arena,
        CreateJobOptions {
            .zip_path = zip_path,
            .libraries_install_folder =
                settings.paths.always_scanned_folder[ToInt(ScanFolderType::Libraries)],
            .presets_install_folder = settings.paths.always_scanned_folder[ToInt(ScanFolderType::Presets)],
            .server = sample_library_server,
            .preset_folders = CombineStringArrays(
                scratch_arena,
                settings.settings.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)],
                Array {settings.paths.always_scanned_folder[ToInt(ScanFolderType::Presets)]}),
        });
    thread_pool.AddJob([job]() { package::StartJob(*job->job); });
}

// [main thread]
PUBLIC InstallJobs::Iterator RemoveJob(InstallJobs& jobs, InstallJobs::Iterator it) {
    ASSERT(CheckThreadName("main"));
    ASSERT(it->job->state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::DoneError ||
           it->job->state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::DoneSuccess);

    return jobs.Remove(it);
}

// Stalls until all jobs are done.
// [main thread]
PUBLIC void ShutdownJobs(InstallJobs& jobs) {
    ASSERT(CheckThreadName("main"));
    if (jobs.Empty()) return;

    for (auto& j : jobs)
        j.job->abort.Store(true, StoreMemoryOrder::Release);

    u32 wait_ms = 0;
    constexpr u32 k_sleep_ms = 100;
    constexpr u32 k_timeout_ms = 120 * 1000;

    for (; wait_ms < k_timeout_ms; wait_ms += k_sleep_ms) {
        bool jobs_are_installing = false;
        for (auto& j : jobs) {
            if (j.job->state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::Installing) {
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

} // namespace package
