// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sample_library_server.hpp"

#include <xxhash.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/threading.hpp"
#include "tests/framework.hpp"
#include "utils/debug/debug.hpp"
#include "utils/reader.hpp"

#include "build_resources/embedded_files.h"
#include "common/common_errors.hpp"
#include "sample_library/audio_file.hpp"
#include "sample_library/sample_library.hpp"

namespace sample_lib_server {

using namespace detail;
constexpr String k_trace_category = "SLS";
constexpr u32 k_trace_colour = 0xfcba03;

// ==========================================================================================================
// Library loading

struct PendingLibraryJobs {
    struct Job {
        enum class Type { ReadLibrary, ScanFolder };

        struct ReadLibrary {
            struct Args {
                PathOrMemory path_or_memory;
                sample_lib::FileFormat format;
                LibrariesList& libraries;
            };
            struct Result {
                ArenaAllocator arena {PageAllocator::Instance()};
                Optional<sample_lib::LibraryPtrOrError> result {};
            };

            Args args;
            Result result {};
        };

        struct ScanFolder {
            struct Args {
                ScanFolderList::Node* folder;
            };
            struct Result {
                ErrorCodeOr<void> outcome {};
            };

            Args args;
            Result result {};
        };

        using DataUnion = TaggedUnion<Type,
                                      TypeAndTag<ReadLibrary*, Type::ReadLibrary>,
                                      TypeAndTag<ScanFolder*, Type::ScanFolder>>;

        DataUnion data;
        Atomic<Job*> next {nullptr};
        Atomic<bool> completed {false};
        bool handled {};
    };

    u64 server_thread_id;
    ThreadPool& thread_pool;
    WorkSignaller& work_signaller;

    Mutex job_mutex;
    ArenaAllocator job_arena {PageAllocator::Instance()};
    Atomic<Job*> jobs {};
    Atomic<u32> num_uncompleted_jobs {0};
};

static void ReadLibraryAsync(PendingLibraryJobs& pending_library_jobs,
                             LibrariesList& lib_list,
                             PathOrMemory path_or_memory,
                             sample_lib::FileFormat format);

static void DoReadLibraryJob(PendingLibraryJobs::Job::ReadLibrary& job, ArenaAllocator& scratch_arena) {
    ZoneScopedN("read library");

    auto const& args = job.args;
    String const path = args.path_or_memory.Is<String>() ? args.path_or_memory.Get<String>() : ":memory:";
    ZoneText(path.data, path.size);

    auto const try_read = [&]() -> Optional<sample_lib::LibraryPtrOrError> {
        using H = sample_lib::TryHelpersOutcomeToError;
        auto path_or_memory = args.path_or_memory;
        if (args.format == sample_lib::FileFormat::Lua && args.path_or_memory.Is<String>()) {
            // it will be more efficient to just load the whole lua into memory
            path_or_memory =
                TRY_H(ReadEntireFile(args.path_or_memory.Get<String>(), scratch_arena)).ToConstByteSpan();
        }

        auto reader = TRY_H(Reader::FromPathOrMemory(path_or_memory));
        auto const file_hash = TRY_H(sample_lib::Hash(reader, args.format));

        for (auto& node : args.libraries) {
            if (auto l = node.TryScoped()) {
                if (l->lib->file_hash == file_hash) return nullopt;
            }
        }

        auto lib = TRY(sample_lib::Read(reader, args.format, path, job.result.arena, scratch_arena));
        lib->file_hash = file_hash;
        return lib;
    };

    job.result.result = try_read();
}

static void DoScanFolderJob(PendingLibraryJobs::Job::ScanFolder& job,
                            ArenaAllocator& scratch_arena,
                            PendingLibraryJobs& pending_library_jobs,
                            LibrariesList& lib_list) {
    auto folder = job.args.folder->TryScoped();
    if (!folder) job.result.outcome = k_success;

    auto const& path = folder->path;
    ZoneScoped;
    ZoneText(path.data, path.size);

    auto const try_job = [&]() -> ErrorCodeOr<void> {
        auto it = TRY(RecursiveDirectoryIterator::Create(scratch_arena,
                                                         path,
                                                         {
                                                             .wildcard = "*",
                                                             .get_file_size = false,
                                                         }));
        while (it.HasMoreFiles()) {
            auto const& entry = it.Get();
            if (path::Extension(entry.path) == ".mdata") {
                ReadLibraryAsync(pending_library_jobs,
                                 lib_list,
                                 String(entry.path),
                                 sample_lib::FileFormat::Mdata);
            } else if (sample_lib::FilenameIsFloeLuaFile(path::Filename(entry.path))) {
                ReadLibraryAsync(pending_library_jobs,
                                 lib_list,
                                 String(entry.path),
                                 sample_lib::FileFormat::Lua);
            }
            TRY(it.Increment());
        }
        return k_success;
    };

    job.result.outcome = try_job();
}

// threadsafe
static void AddAsyncJob(PendingLibraryJobs& pending_library_jobs,
                        LibrariesList& lib_list,
                        PendingLibraryJobs::Job::DataUnion data) {
    ZoneNamed(add_job, true);
    PendingLibraryJobs::Job* job;
    {
        pending_library_jobs.job_mutex.Lock();
        DEFER { pending_library_jobs.job_mutex.Unlock(); };

        job = pending_library_jobs.job_arena.NewUninitialised<PendingLibraryJobs::Job>();
        PLACEMENT_NEW(job)
        PendingLibraryJobs::Job {
            .data = data,
            .next = pending_library_jobs.jobs.Load(MemoryOrder::Relaxed),
            .handled = false,
        };
        pending_library_jobs.jobs.Store(job, MemoryOrder::Release);
    }

    pending_library_jobs.num_uncompleted_jobs.FetchAdd(1, MemoryOrder::AcquireRelease);

    pending_library_jobs.thread_pool.AddJob([&pending_library_jobs, &job = *job, &lib_list]() {
        ZoneNamed(do_job, true);
        ArenaAllocator scratch_arena {PageAllocator::Instance()};
        switch (job.data.tag) {
            case PendingLibraryJobs::Job::Type::ReadLibrary: {
                DoReadLibraryJob(*job.data.Get<PendingLibraryJobs::Job::ReadLibrary*>(), scratch_arena);
                break;
            }
            case PendingLibraryJobs::Job::Type::ScanFolder: {
                DoScanFolderJob(*job.data.Get<PendingLibraryJobs::Job::ScanFolder*>(),
                                scratch_arena,
                                pending_library_jobs,
                                lib_list);
                break;
            }
        }

        job.completed.Store(true, MemoryOrder::SequentiallyConsistent);
        pending_library_jobs.work_signaller.Signal();
    });
}

// threadsafe
static void ReadLibraryAsync(PendingLibraryJobs& pending_library_jobs,
                             LibrariesList& lib_list,
                             PathOrMemory path_or_memory,
                             sample_lib::FileFormat format) {
    auto read_job = ({
        pending_library_jobs.job_mutex.Lock();
        DEFER { pending_library_jobs.job_mutex.Unlock(); };
        auto j = pending_library_jobs.job_arena.NewUninitialised<PendingLibraryJobs::Job::ReadLibrary>();
        PLACEMENT_NEW(j)
        PendingLibraryJobs::Job::ReadLibrary {
            .args =
                {
                    .path_or_memory = path_or_memory.Is<String>()
                                          ? PathOrMemory(String(pending_library_jobs.job_arena.Clone(
                                                path_or_memory.Get<String>())))
                                          : path_or_memory,
                    .format = format,
                    .libraries = lib_list,
                },
            .result = {},
        };
        j;
    });

    AddAsyncJob(pending_library_jobs, lib_list, read_job);
}

// threadsafe
static void RereadLibraryAsync(PendingLibraryJobs& pending_library_jobs,
                               LibrariesList& lib_list,
                               LibrariesList::Node* lib_node) {
    ReadLibraryAsync(pending_library_jobs,
                     lib_list,
                     lib_node->value.lib->path,
                     lib_node->value.lib->file_format_specifics.tag);
}

// threadsafe
static bool RequestLibraryFolderScanIfNeeded(ScanFolderList& scan_folders) {
    bool any_rescan_requested = false;
    for (auto& n : scan_folders)
        if (auto f = n.TryScoped()) {
            auto expected = ScanFolder::State::NotScanned;
            if (f->state.CompareExchangeStrong(expected, ScanFolder::State::RescanRequested))
                any_rescan_requested = true;
        }
    return any_rescan_requested;
}

// server-thread
static bool UpdateLibraryJobs(Server& server,
                              PendingLibraryJobs& pending_library_jobs,
                              ArenaAllocator& scratch_arena,
                              Optional<DirectoryWatcher>& watcher) {
    ASSERT(CurrentThreadID() == pending_library_jobs.server_thread_id);
    ZoneNamed(outer, true);

    // trigger folder scanning if any are marked as 'rescan-requested'
    for (auto& node : server.scan_folders) {
        if (auto f = node.TryScoped()) {
            auto expected = ScanFolder::State::RescanRequested;
            auto const exchanged = f->state.CompareExchangeStrong(expected, ScanFolder::State::Scanning);
            if (!exchanged) continue;
        }

        PendingLibraryJobs::Job::ScanFolder* scan_job;
        {
            pending_library_jobs.job_mutex.Lock();
            DEFER { pending_library_jobs.job_mutex.Unlock(); };
            scan_job = pending_library_jobs.job_arena.NewUninitialised<PendingLibraryJobs::Job::ScanFolder>();
            PLACEMENT_NEW(scan_job)
            PendingLibraryJobs::Job::ScanFolder {
                .args =
                    {
                        .folder = &node,
                    },
                .result = {},
            };
        }

        AddAsyncJob(pending_library_jobs, server.libraries, scan_job);
    }

    // handle async jobs that have completed
    for (auto node = pending_library_jobs.jobs.Load(MemoryOrder::Acquire); node != nullptr;
         node = node->next.Load(MemoryOrder::Relaxed)) {
        if (node->handled) continue;
        if (!node->completed.Load(MemoryOrder::Acquire)) continue;

        DEFER {
            node->handled = true;
            pending_library_jobs.num_uncompleted_jobs.FetchSub(1, MemoryOrder::AcquireRelease);
        };
        auto const& job = *node;
        switch (job.data.tag) {
            case PendingLibraryJobs::Job::Type::ReadLibrary: {
                auto& j = *job.data.Get<PendingLibraryJobs::Job::ReadLibrary*>();
                auto const& args = j.args;
                String const path =
                    args.path_or_memory.Is<String>() ? args.path_or_memory.Get<String>() : ":memory:";
                ZoneScopedN("job completed: library read");
                ZoneText(path.data, path.size);
                if (!j.result.result) {
                    TracyMessageEx({k_trace_category, k_trace_colour, {}},
                                   "skipping {}, it already exists",
                                   path::Filename(path));
                    continue;
                }
                auto const& outcome = *j.result.result;

                auto const error_id = ThreadsafeErrorNotifications::Id("libs", path);
                switch (outcome.tag) {
                    case ResultType::Value: {
                        auto lib = outcome.GetFromTag<ResultType::Value>();
                        TracyMessageEx({k_trace_category, k_trace_colour, {}},
                                       "adding new library {}",
                                       path::Filename(path));

                        // only allow one with the same name or path, and only if it isn't already present
                        bool already_exists = false;
                        for (auto it = server.libraries.begin(); it != server.libraries.end();) {
                            if (it->value.lib->file_hash == lib->file_hash) already_exists = true;
                            if (it->value.lib->name == lib->name ||
                                path::Equal(it->value.lib->path, lib->path))
                                it = server.libraries.Remove(it);
                            else
                                ++it;
                        }
                        if (already_exists) break;

                        auto new_node = server.libraries.AllocateUninitialised();
                        PLACEMENT_NEW(&new_node->value)
                        ListedLibrary {.arena = Move(j.result.arena), .lib = lib};
                        server.libraries.Insert(new_node);

                        server.error_notifications.RemoveError(error_id);
                        break;
                    }
                    case ResultType::Error: {
                        auto const error = outcome.GetFromTag<ResultType::Error>();
                        if (error.code == FilesystemError::PathDoesNotExist) continue;

                        auto const err = server.error_notifications.NewError();
                        err->value = {
                            .title = "Failed to read library"_s,
                            .message = {},
                            .error_code = error.code,
                            .id = error_id,
                        };
                        if (j.args.path_or_memory.Is<String>())
                            fmt::Append(err->value.message, "{}\n", j.args.path_or_memory.Get<String>());
                        if (error.message.size) fmt::Append(err->value.message, "{}\n", error.message);
                        server.error_notifications.AddOrUpdateError(err);
                        break;
                    }
                }

                break;
            }
            case PendingLibraryJobs::Job::Type::ScanFolder: {
                auto const& j = *job.data.Get<PendingLibraryJobs::Job::ScanFolder*>();
                if (auto folder = j.args.folder->TryScoped()) {
                    auto const& path = folder->path;
                    ZoneScopedN("job completed: folder scanned");
                    ZoneText(path.data, path.size);

                    auto const folder_error_id = ThreadsafeErrorNotifications::Id("libs", path);

                    if (!j.result.outcome.HasError()) {
                        server.error_notifications.RemoveError(folder_error_id);
                        folder->state.Store(ScanFolder::State::ScannedSuccessfully, MemoryOrder::Release);
                    } else {
                        auto const is_always_scanned_folder =
                            folder->source == ScanFolder::Source::AlwaysScannedFolder;
                        if (!(is_always_scanned_folder &&
                              j.result.outcome.Error() == FilesystemError::PathDoesNotExist)) {
                            auto const err = server.error_notifications.NewError();
                            err->value = {
                                .title = "Failed to scan library folder"_s,
                                .message = String(path),
                                .error_code = j.result.outcome.Error(),
                                .id = folder_error_id,
                            };
                            server.error_notifications.AddOrUpdateError(err);
                        }
                        folder->state.Store(ScanFolder::State::ScanFailed, MemoryOrder::Release);
                    }
                }
                break;
            }
        }
    }

    // check if the scan-folders have changed
    if (watcher) {
        ZoneNamedN(fs_watch, "fs watch", true);

        auto const dirs_to_watch = ({
            DynamicArray<DirectoryToWatch> dirs {scratch_arena};
            for (auto& node : server.scan_folders) {
                if (auto f = node.TryRetain()) {
                    if (f->state.Load(MemoryOrder::Relaxed) == ScanFolder::State::ScannedSuccessfully)
                        dyn::Append(dirs,
                                    {
                                        .path = f->path,
                                        .recursive = true,
                                        .user_data = &node,
                                    });
                    else
                        node.Release();
                }
            }
            dirs.ToOwnedSpan();
        });
        DEFER {
            for (auto& d : dirs_to_watch)
                ((ScanFolderList::Node*)d.user_data)->Release();
        };

        if (auto const outcome = PollDirectoryChanges(*watcher,
                                                      {
                                                          .dirs_to_watch = dirs_to_watch,
                                                          .retry_failed_directories = false,
                                                          .result_arena = scratch_arena,
                                                          .scratch_arena = scratch_arena,
                                                      });
            outcome.HasError()) {
            // IMPROVE: handle error
            DebugLn("Reading directory changes failed: {}", outcome.Error());
        } else {
            auto const dir_changes_span = outcome.Value();
            for (auto const& dir_changes : dir_changes_span) {
                auto& scan_folder =
                    ((ScanFolderList::Node*)dir_changes.linked_dir_to_watch->user_data)->value;

                if (dir_changes.error) {
                    // IMPROVE: handle this
                    DebugLn("Reading directory changes failed for {}: {}",
                            scan_folder.path,
                            dir_changes.error);
                    continue;
                }

                for (auto const& subpath_changeset : dir_changes.subpath_changesets) {
                    if (subpath_changeset.changes & DirectoryWatcher::ChangeType::ManualRescanNeeded) {
                        scan_folder.state.Store(ScanFolder::State::RescanRequested);
                        continue;
                    }

                    // changes to the watched directory itself
                    if (subpath_changeset.subpath.size == 0) continue;

                    DebugLn("Scan-folder change: {} {} in {}",
                            subpath_changeset.subpath,
                            DirectoryWatcher::ChangeType::ToString(subpath_changeset.changes),
                            scan_folder.path);

                    auto const full_path =
                        path::Join(scratch_arena,
                                   Array {(String)scan_folder.path, subpath_changeset.subpath});

                    if (path::Depth(subpath_changeset.subpath) == 0) {
                        bool modified_existing_lib = false;
                        if (subpath_changeset.changes & DirectoryWatcher::ChangeType::Modified)
                            for (auto& lib_node : server.libraries) {
                                auto const& lib = *lib_node.value.lib;
                                if (path::Equal(lib.path, full_path)) {
                                    DebugLn("  Rereading library: {}", lib.name);
                                    RereadLibraryAsync(pending_library_jobs, server.libraries, &lib_node);
                                    modified_existing_lib = true;
                                    break;
                                }
                            }
                        if (!modified_existing_lib) {
                            DebugLn("  Rescanning folder: {}", scan_folder.path);
                            scan_folder.state.Store(ScanFolder::State::RescanRequested);
                        }
                    } else {
                        for (auto& lib_node : server.libraries) {
                            auto const& lib = *lib_node.value.lib;
                            if (lib.file_format_specifics.tag == sample_lib::FileFormat::Lua) {
                                // get the directory of the library (the directory of the floe.lua)
                                auto const dir = path::Directory(lib.path);
                                if (dir && path::IsWithinDirectory(full_path, *dir)) {
                                    DebugLn("  Rereading library: {}", lib.name);
                                    RereadLibraryAsync(pending_library_jobs, server.libraries, &lib_node);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // TODO(1.0): if a library/instrument has changed, trigger a reload for all clients of this loader so it
    // feels totally seamless

    // remove libraries that are not in any active scan-folders
    for (auto it = server.libraries.begin(); it != server.libraries.end();) {
        auto const& lib = *it->value.lib;

        bool within_any_folder = false;
        if (lib.name == k_builtin_library_name)
            within_any_folder = true;
        else
            for (auto& sn : server.scan_folders) {
                if (auto folder = sn.TryScoped()) {
                    if (path::IsWithinDirectory(lib.path, folder->path)) {
                        within_any_folder = true;
                        break;
                    }
                }
            }

        if (!within_any_folder)
            it = server.libraries.Remove(it);
        else
            ++it;
    }

    // update libraries_by_name
    {
        ZoneNamedN(rebuild_htab, "rehash", true);
        server.libraries_by_name_mutex.Lock();
        DEFER { server.libraries_by_name_mutex.Unlock(); };
        auto& libs_by_name = server.libraries_by_name;
        libs_by_name.DeleteAll();
        for (auto& n : server.libraries) {
            auto const& lib = *n.value.lib;
            auto const inserted = libs_by_name.Insert(lib.name, &n);
            ASSERT(inserted);
        }
    }

    // remove scan-folders that are no longer used
    {
        server.scan_folders_writer_mutex.Lock();
        DEFER { server.scan_folders_writer_mutex.Unlock(); };
        server.scan_folders.DeleteRemovedAndUnreferenced();
    }

    auto const library_work_still_pending =
        pending_library_jobs.num_uncompleted_jobs.Load(MemoryOrder::AcquireRelease) != 0;
    return library_work_still_pending;
}

static Optional<DirectoryWatcher> CreateDirectoryWatcher(ThreadsafeErrorNotifications& error_notifications) {
    Optional<DirectoryWatcher> watcher;
    auto watcher_outcome = CreateDirectoryWatcher(PageAllocator::Instance());
    auto const error_id = U64FromChars("libwatch");
    if (!watcher_outcome.HasError()) {
        error_notifications.RemoveError(error_id);
        watcher.Emplace(watcher_outcome.ReleaseValue());
    } else {
        DebugLn("Failed to create directory watcher: {}", watcher_outcome.Error());
        auto const err = error_notifications.NewError();
        err->value = {
            .title = "Warning: unable to monitor library folders"_s,
            .message = {},
            .error_code = watcher_outcome.Error(),
            .id = error_id,
        };
        error_notifications.AddOrUpdateError(err);
    }
    return watcher;
}

// ==========================================================================================================
// Library resource loading

using AudioDataAllocator = PageAllocator;

ListedAudioData::~ListedAudioData() {
    ZoneScoped;
    auto const s = state.Load();
    ASSERT(s == FileLoadingState::CompletedCancelled || s == FileLoadingState::CompletedWithError ||
           s == FileLoadingState::CompletedSucessfully);
    if (audio_data.interleaved_samples.size)
        AudioDataAllocator::Instance().Free(audio_data.interleaved_samples.ToByteSpan());
    library_ref_count.FetchSub(1);
}

ListedInstrument::~ListedInstrument() {
    ZoneScoped;
    for (auto a : audio_data_set)
        a->ref_count.FetchSub(1);
}

ListedImpulseResponse::~ListedImpulseResponse() { audio_data->ref_count.FetchSub(1); }

// Just a little helper that we pass around when working with the thread pool.
struct ThreadPoolArgs {
    ThreadPool& pool;
    AtomicCountdown& num_thread_pool_jobs;
    WorkSignaller& completed_signaller;
};

static void
LoadAudioAsync(ListedAudioData& audio_data, sample_lib::Library const& lib, ThreadPoolArgs thread_pool_args) {
    thread_pool_args.num_thread_pool_jobs.Increase();
    thread_pool_args.pool.AddJob([&, thread_pool_args]() {
        ZoneScoped;
        DEFER {
            thread_pool_args.num_thread_pool_jobs.CountDown();

            // TODO: This is not right. It's possible that completed_signaller will be destroyed at this point
            // because as soon as num_thread_pool_jobs equals 0 the server could shut down. It's very unlikely
            // because there's a lot of other things that happen before that point and so this thread, in all
            // liklihoods, runs first, but we shouldn't count on it. The 2 methods of signalling completion
            // need to be unified somehow.
            thread_pool_args.completed_signaller.Signal();
        };

        {
            auto state = audio_data.state.Load();
            FileLoadingState new_state;
            do {
                if (state == FileLoadingState::PendingLoad)
                    new_state = FileLoadingState::Loading;
                else if (state == FileLoadingState::PendingCancel)
                    new_state = FileLoadingState::CompletedCancelled;
                else
                    PanicIfReached();
            } while (!audio_data.state.CompareExchangeWeak(state, new_state));

            if (new_state == FileLoadingState::CompletedCancelled) return;
        }

        ASSERT(audio_data.state.Load() == FileLoadingState::Loading);

        auto const outcome = [&audio_data, &lib]() -> ErrorCodeOr<AudioData> {
            auto reader = TRY(lib.create_file_reader(lib, audio_data.path));
            return DecodeAudioFile(reader, audio_data.path, AudioDataAllocator::Instance());
        }();

        FileLoadingState result;
        if (outcome.HasValue()) {
            audio_data.audio_data = outcome.Value();
            result = FileLoadingState::CompletedSucessfully;
        } else {
            audio_data.error = outcome.Error();
            result = FileLoadingState::CompletedWithError;
        }
        audio_data.state.Store(result);
    });
}

// if the audio load is cancelled, or pending-cancel, then queue up a load again
static void TriggerReloadIfAudioIsCancelled(ListedAudioData& audio_data,
                                            sample_lib::Library const& lib,
                                            ThreadPoolArgs thread_pool_args,
                                            u32 debug_inst_id) {
    auto expected = FileLoadingState::PendingCancel;
    if (!audio_data.state.CompareExchangeStrong(expected, FileLoadingState::PendingLoad)) {
        if (expected == FileLoadingState::CompletedCancelled) {
            audio_data.state.Store(FileLoadingState::PendingLoad);
            TracyMessageEx({k_trace_category, k_trace_colour, -1u},
                           "instID:{}, reloading CompletedCancelled audio",
                           debug_inst_id);
            LoadAudioAsync(audio_data, lib, thread_pool_args);
        } else {
            TracyMessageEx({k_trace_category, k_trace_colour, -1u},
                           "instID:{}, reusing audio which is in state: {}",
                           debug_inst_id,
                           EnumToString(expected));
        }
    } else {
        TracyMessageEx({k_trace_category, k_trace_colour, -1u},
                       "instID:{}, audio swapped PendingCancel with PendingLoad",
                       debug_inst_id);
    }

    ASSERT(audio_data.state.Load() != FileLoadingState::CompletedCancelled &&
           audio_data.state.Load() != FileLoadingState::PendingCancel);
}

static ListedAudioData* FetchOrCreateAudioData(LibrariesList::Node& lib_node,
                                               String path,
                                               ThreadPoolArgs thread_pool_args,
                                               u32 debug_inst_id) {
    auto const& lib = *lib_node.value.lib;
    for (auto& d : lib_node.value.audio_datas) {
        if (lib.name == d.library_name && d.path == path) {
            TriggerReloadIfAudioIsCancelled(d, lib, thread_pool_args, debug_inst_id);
            return &d;
        }
    }

    auto audio_data = lib_node.value.audio_datas.PrependUninitialised();
    PLACEMENT_NEW(audio_data)
    ListedAudioData {
        .library_name = lib.name,
        .path = path,
        .audio_data = {},
        .ref_count = 0u,
        .library_ref_count = lib_node.reader_uses,
        .state = FileLoadingState::PendingLoad,
        .error = {},
    };
    lib_node.reader_uses.FetchAdd(1);

    LoadAudioAsync(*audio_data, lib, thread_pool_args);
    return audio_data;
}

static ListedInstrument* FetchOrCreateInstrument(LibrariesList::Node& lib_node,
                                                 sample_lib::Instrument const& inst,
                                                 ThreadPoolArgs thread_pool_args) {
    auto& lib = lib_node.value;
    ASSERT(&inst.library == lib.lib);

    for (auto& i : lib.instruments)
        if (i.inst.instrument.name == inst.name) {
            for (auto d : i.audio_data_set)
                TriggerReloadIfAudioIsCancelled(*d, *lib.lib, thread_pool_args, i.debug_id);
            return &i;
        }

    static u32 g_inst_debug_id {};

    auto new_inst = lib.instruments.PrependUninitialised();
    PLACEMENT_NEW(new_inst)
    ListedInstrument {
        .debug_id = g_inst_debug_id++,
        .inst = {inst},
        .ref_count = 0u,
    };

    DynamicArray<ListedAudioData*> audio_data_set {new_inst->arena};

    new_inst->inst.audio_datas =
        new_inst->arena.AllocateExactSizeUninitialised<AudioData const*>(inst.regions.size);
    for (auto region_index : Range(inst.regions.size)) {
        auto& region_info = inst.regions[region_index];
        auto& audio_data = new_inst->inst.audio_datas[region_index];

        auto ref_audio_data =
            FetchOrCreateAudioData(lib_node, region_info.file.path, thread_pool_args, new_inst->debug_id);
        audio_data = &ref_audio_data->audio_data;

        dyn::AppendIfNotAlreadyThere(audio_data_set, ref_audio_data);

        if (inst.audio_file_path_for_waveform == region_info.file.path)
            new_inst->inst.file_for_gui_waveform = &ref_audio_data->audio_data;
    }

    for (auto d : audio_data_set)
        d->ref_count.FetchAdd(1);

    ASSERT(audio_data_set.size);
    new_inst->audio_data_set = audio_data_set.ToOwnedSpan();

    return new_inst;
}

static ListedImpulseResponse* FetchOrCreateImpulseResponse(LibrariesList::Node& lib_node,
                                                           sample_lib::ImpulseResponse const& ir,
                                                           ThreadPoolArgs thread_pool_args) {
    auto audio_data = FetchOrCreateAudioData(lib_node, ir.path, thread_pool_args, 999999);
    audio_data->ref_count.FetchAdd(1);

    auto new_ir = lib_node.value.irs.PrependUninitialised();
    PLACEMENT_NEW(new_ir)
    ListedImpulseResponse {
        .ir = {ir, &audio_data->audio_data},
        .audio_data = audio_data,
        .ref_count = 0u,
    };
    return new_ir;
}

static void CancelLoadingAudioForInstrumentIfPossible(ListedInstrument const* i, uintptr_t trace_id) {
    ASSERT(i);
    ZoneScoped;
    TracyMessageEx({k_trace_category, k_trace_colour, trace_id},
                   "cancel instID:{}, num audio: {}",
                   i->debug_id,
                   i->audio_data_set.size);

    usize num_attempted_cancel = 0;
    for (auto audio_data : i->audio_data_set) {
        ASSERT(audio_data->ref_count.Load() != 0);
        if (audio_data->ref_count.Load() == 1) {
            auto expected = FileLoadingState::PendingLoad;
            audio_data->state.CompareExchangeStrong(expected, FileLoadingState::PendingCancel);

            TracyMessageEx({k_trace_category, k_trace_colour, trace_id},
                           "instID:{} cancel attempt audio from state: {}",
                           i->debug_id,
                           EnumToString(expected));

            ++num_attempted_cancel;
        }
    }

    TracyMessageEx({k_trace_category, k_trace_colour, trace_id},
                   "instID:{} num audio attempted cancel: {}",
                   i->debug_id,
                   num_attempted_cancel);
}

struct PendingResource {
    enum class State {
        AwaitingLibrary,
        AwaitingAudio,
        Cancelled,
        Failed,
        CompletedSuccessfully,
    };

    using ListedPointer = TaggedUnion<LoadRequestType,
                                      TypeAndTag<ListedInstrument*, LoadRequestType::Instrument>,
                                      TypeAndTag<ListedImpulseResponse*, LoadRequestType::Ir>>;

    using StateUnion = TaggedUnion<State,
                                   TypeAndTag<ListedPointer, State::AwaitingAudio>,
                                   TypeAndTag<ErrorCode, State::Failed>,
                                   TypeAndTag<Resource, State::CompletedSuccessfully>>;

    u32 LayerIndex() const {
        if (auto i = request.request.TryGet<LoadRequestInstrumentIdWithLayer>()) return i->layer_index;
        PanicIfReached();
        return 0;
    }
    bool IsDesired() const {
        return state.Get<ListedPointer>().Get<ListedInstrument*>() ==
               request.async_comms_channel.desired_inst[LayerIndex()];
    }
    auto& LoadingPercent() { return request.async_comms_channel.instrument_loading_percents[LayerIndex()]; }

    StateUnion state {State::AwaitingLibrary};
    QueuedRequest request;
    uintptr_t debug_id;

    PendingResource* next = nullptr;
};

struct PendingResources {
    u64 server_thread_id;
    IntrusiveSinglyLinkedList<PendingResource> list {};
    AtomicCountdown thread_pool_jobs {0};
};

static void DumpPendingResourcesDebugInfo(PendingResources& pending_resources) {
    ASSERT(CurrentThreadID() == pending_resources.server_thread_id);
    DebugLn("Thread pool jobs: {}", pending_resources.thread_pool_jobs.counter.Load());
    DebugLn("\nPending results:");
    for (auto& pending_resource : pending_resources.list) {
        DebugLn("  Pending result: {}", pending_resource.debug_id);
        switch (pending_resource.state.tag) {
            case PendingResource::State::AwaitingLibrary: DebugLn("    Awaiting library"); break;
            case PendingResource::State::AwaitingAudio: {
                auto& resource = pending_resource.state.Get<PendingResource::ListedPointer>();
                switch (resource.tag) {
                    case LoadRequestType::Instrument: {
                        auto inst = resource.Get<ListedInstrument*>();
                        DebugLn("    Awaiting audio for instrument {}", inst->inst.instrument.name);
                        for (auto& audio_data : inst->audio_data_set) {
                            DebugLn("      Audio data: {}, {}",
                                    audio_data->audio_data.hash,
                                    EnumToString(audio_data->state.Load()));
                        }
                        break;
                    }
                    case LoadRequestType::Ir: {
                        auto ir = resource.Get<ListedImpulseResponse*>();
                        DebugLn("    Awaiting audio for IR {}", ir->ir.ir.path);
                        DebugLn("      Audio data: {}, {}",
                                ir->audio_data->audio_data.hash,
                                EnumToString(ir->audio_data->state.Load()));
                        break;
                    }
                }
                break;
            }
            case PendingResource::State::Cancelled: DebugLn("    Cancelled"); break;
            case PendingResource::State::Failed: DebugLn("    Failed"); break;
            case PendingResource::State::CompletedSuccessfully: DebugLn("    Completed successfully"); break;
        }
    }
}

static bool ConsumeResourceRequests(PendingResources& pending_resources,
                                    ArenaAllocator& arena,
                                    ThreadsafeQueue<QueuedRequest>& request_queue) {
    ASSERT(CurrentThreadID() == pending_resources.server_thread_id);
    bool any_requests = false;
    while (auto queued_request = request_queue.TryPop()) {
        ZoneNamedN(req, "request", true);

        if (!queued_request->async_comms_channel.used.Load(MemoryOrder::Relaxed)) continue;

        static uintptr debug_result_id = 0;
        auto pending_resource = arena.NewUninitialised<PendingResource>();
        PLACEMENT_NEW(pending_resource)
        PendingResource {
            .state = PendingResource::State::AwaitingLibrary,
            .request = *queued_request,
            .debug_id = debug_result_id++,
        };
        SinglyLinkedListPrepend(pending_resources.list.first, pending_resource);
        any_requests = true;

        TracyMessageEx({k_trace_category, k_trace_colour, pending_resource->debug_id},
                       "pending result added");
    }
    return any_requests;
}

static bool UpdatePendingResources(PendingResources& pending_resources,
                                   Server& server,
                                   bool libraries_are_still_loading) {
    ASSERT(CurrentThreadID() == server.server_thread_id);

    if (pending_resources.list.Empty()) return false;

    ThreadPoolArgs thread_pool_args {
        .pool = server.thread_pool,
        .num_thread_pool_jobs = pending_resources.thread_pool_jobs,
        .completed_signaller = server.work_signaller,
    };

    // Fill in library
    for (auto& pending_resource : pending_resources.list) {
        if (pending_resource.state != PendingResource::State::AwaitingLibrary) continue;

        auto const library_name = ({
            String n {};
            switch (pending_resource.request.request.tag) {
                case LoadRequestType::Instrument:
                    n = pending_resource.request.request.Get<LoadRequestInstrumentIdWithLayer>()
                            .id.library_name;
                    break;
                case LoadRequestType::Ir:
                    n = pending_resource.request.request.Get<sample_lib::IrId>().library_name;
                    break;
            }
            n;
        });
        ASSERT(library_name.size != 0);

        LibrariesList::Node* lib {};
        if (auto l_ptr = server.libraries_by_name.Find(library_name)) lib = *l_ptr;

        if (!lib) {
            // If libraries are still loading, then we just wait to see if the library we're missing is
            // about to be loaded. If not, then it's an error.
            if (!libraries_are_still_loading) {
                auto const err = pending_resource.request.async_comms_channel.error_notifications.NewError();
                err->value = {
                    .title = {},
                    .message = {},
                    .error_code = CommonError::NotFound,
                    .id = ThreadsafeErrorNotifications::Id("lib ", library_name),
                };
                fmt::Append(err->value.title, "{} not found", library_name);
                pending_resource.request.async_comms_channel.error_notifications.AddOrUpdateError(err);
                pending_resource.state = ErrorCode {CommonError::NotFound};
            }
        } else {
            switch (pending_resource.request.request.tag) {
                case LoadRequestType::Instrument: {
                    auto const& load_inst =
                        pending_resource.request.request.Get<LoadRequestInstrumentIdWithLayer>();
                    auto const inst_name = load_inst.id.inst_name;

                    ASSERT(inst_name.size != 0);

                    if (auto const i = lib->value.lib->insts_by_name.Find(inst_name)) {
                        pending_resource.request.async_comms_channel
                            .instrument_loading_percents[load_inst.layer_index]
                            .Store(0);

                        auto inst = FetchOrCreateInstrument(*lib, **i, thread_pool_args);
                        ASSERT(inst);

                        pending_resource.request.async_comms_channel.desired_inst[load_inst.layer_index] =
                            inst;
                        pending_resource.state = PendingResource::ListedPointer {inst};

                        TracyMessageEx({k_trace_category, k_trace_colour, pending_resource.debug_id},
                                       "option: instID:{} load Sampler inst[{}], {}, {}, {}",
                                       inst->debug_id,
                                       load_inst.layer_index,
                                       (void const*)inst,
                                       lib->value.lib->name,
                                       inst_name);
                    } else {
                        auto const err =
                            pending_resource.request.async_comms_channel.error_notifications.NewError();
                        err->value = {
                            .title = {},
                            .message = {},
                            .error_code = CommonError::NotFound,
                            .id = ThreadsafeErrorNotifications::Id("inst", inst_name),
                        };
                        fmt::Append(err->value.title, "Cannot find instrument \"{}\"", inst_name);
                        pending_resource.request.async_comms_channel.error_notifications.AddOrUpdateError(
                            err);
                        pending_resource.state = *err->value.error_code;
                    }
                    break;
                }
                case LoadRequestType::Ir: {
                    auto const ir_id = pending_resource.request.request.Get<sample_lib::IrId>();
                    auto const ir = lib->value.lib->irs_by_name.Find(ir_id.ir_name);

                    if (ir) {
                        auto listed_ir = FetchOrCreateImpulseResponse(*lib, **ir, thread_pool_args);

                        pending_resource.state = PendingResource::ListedPointer {listed_ir};

                        TracyMessageEx({k_trace_category, k_trace_colour, pending_resource.debug_id},
                                       "option: load IR, {}, {}",
                                       ir_id.library_name,
                                       ir_id.ir_name);
                    } else {
                        auto const err =
                            pending_resource.request.async_comms_channel.error_notifications.NewError();
                        err->value = {
                            .title = "Failed to find IR"_s,
                            .message = {},
                            .error_code = CommonError::NotFound,
                            .id = {},
                        };
                        fmt::Assign(err->value.message,
                                    "Could not find reverb impulse response: {}, in library: {}",
                                    ir_id.ir_name,
                                    library_name);
                        err->value.id = ThreadsafeErrorNotifications::Id("ir  ", err->value.message),
                        pending_resource.request.async_comms_channel.error_notifications.AddOrUpdateError(
                            err);
                        pending_resource.state = *err->value.error_code;
                    }
                    break;
                }
            }
        }
    }

    // For each inst, check for errors
    for (auto& pending_resource : pending_resources.list) {
        if (pending_resource.state.tag != PendingResource::State::AwaitingAudio) continue;

        auto const& listed_inst = *({
            auto i = pending_resource.state.Get<PendingResource::ListedPointer>().TryGet<ListedInstrument*>();
            if (!i) continue;
            *i;
        });

        ASSERT(listed_inst.audio_data_set.size);

        Optional<ErrorCode> error {};
        for (auto a : listed_inst.audio_data_set) {
            if (a->state.Load() == FileLoadingState::CompletedWithError) {
                error = a->error;
                break;
            }
        }

        if (error) {
            auto const err = pending_resource.request.async_comms_channel.error_notifications.NewError();
            err->value = {
                .title = "Failed to load audio"_s,
                .message = listed_inst.inst.instrument.name,
                .error_code = *error,
                .id = ThreadsafeErrorNotifications::Id("audi", listed_inst.inst.instrument.name),
            };
            pending_resource.request.async_comms_channel.error_notifications.AddOrUpdateError(err);

            CancelLoadingAudioForInstrumentIfPossible(&listed_inst, pending_resource.debug_id);
            if (pending_resource.IsDesired()) pending_resource.LoadingPercent().Store(-1);
            pending_resource.state = *error;
        }
    }

    // For each inst, check if it's still needed, and cancel if not. And update percent markers
    for (auto& pending_resource : pending_resources.list) {
        if (pending_resource.state.tag != PendingResource::State::AwaitingAudio) continue;

        auto i_ptr = pending_resource.state.Get<PendingResource::ListedPointer>().TryGet<ListedInstrument*>();
        if (!i_ptr) continue;
        auto i = *i_ptr;

        if (pending_resource.IsDesired()) {
            auto const num_completed = ({
                u32 n = 0;
                for (auto& a : i->audio_data_set) {
                    auto const state = a->state.Load();
                    if (state == FileLoadingState::CompletedSucessfully) ++n;
                }
                n;
            });
            if (num_completed == i->audio_data_set.size) {
                pending_resource.LoadingPercent().Store(-1);
                pending_resource.state = Resource {
                    RefCounted<sample_lib::LoadedInstrument> {i->inst, i->ref_count, &server.work_signaller}};
            } else {
                f32 const percent = 100.0f * ((f32)num_completed / (f32)i->audio_data_set.size);
                pending_resource.LoadingPercent().Store(RoundPositiveFloat(percent));
            }
        } else {
            // If it's not desired by any others it can be cancelled
            bool const is_desired_by_another = ({
                bool desired = false;
                for (auto& other_pending_resource : pending_resources.list) {
                    for (auto other_desired :
                         other_pending_resource.request.async_comms_channel.desired_inst) {
                        if (other_desired == i) {
                            desired = true;
                            break;
                        }
                    }
                    if (desired) break;
                }
                desired;
            });
            if (!is_desired_by_another)
                CancelLoadingAudioForInstrumentIfPossible(i, pending_resource.debug_id);

            pending_resource.state = PendingResource::State::Cancelled;
        }
    }

    // Store the result of the IR load in the result, if needed
    for (auto& pending_resource : pending_resources.list) {
        if (pending_resource.state.tag != PendingResource::State::AwaitingAudio) continue;

        auto ir_ptr_ptr =
            pending_resource.state.Get<PendingResource::ListedPointer>().TryGet<ListedImpulseResponse*>();
        if (!ir_ptr_ptr) continue;
        auto ir_ptr = *ir_ptr_ptr;

        auto const& ir = *ir_ptr;
        switch (ir.audio_data->state.Load()) {
            case FileLoadingState::CompletedSucessfully: {
                pending_resource.state = Resource {
                    RefCounted<sample_lib::LoadedIr> {
                        ir_ptr->ir,
                        ir_ptr->ref_count,
                        &server.work_signaller,
                    },
                };
                break;
            }
            case FileLoadingState::CompletedWithError: {
                auto const ir_index = pending_resource.request.request.Get<sample_lib::IrId>();
                {
                    auto const err =
                        pending_resource.request.async_comms_channel.error_notifications.NewError();
                    err->value = {
                        .title = "Failed to load IR"_s,
                        .message = {},
                        .error_code = *ir.audio_data->error,
                        .id = Hash("ir  "_s) + Hash(ir_index.library_name.Items()) +
                              Hash(ir_index.ir_name.Items()),
                    };
                    fmt::Assign(err->value.message,
                                "File '{}', in library {} failed to load. Check your Lua file: {}",
                                ir_ptr->ir.ir.path,
                                ir_index.library_name,
                                ir_ptr->ir.ir.library.path);
                    pending_resource.request.async_comms_channel.error_notifications.AddOrUpdateError(err);
                }
                pending_resource.state = *ir.audio_data->error;
                break;
            }
            case FileLoadingState::PendingLoad:
            case FileLoadingState::Loading: break;
            case FileLoadingState::PendingCancel:
            case FileLoadingState::CompletedCancelled: PanicIfReached(); break;
            case FileLoadingState::Count: PanicIfReached(); break;
        }
    }

    // For each result, check if all loading has completed and if so, dispatch the result
    // and remove it from the pending list
    SinglyLinkedListRemoveIf(
        pending_resources.list.first,
        [&](PendingResource const& pending_resource) {
            switch (pending_resource.state.tag) {
                case PendingResource::State::AwaitingLibrary:
                case PendingResource::State::AwaitingAudio: return false;
                case PendingResource::State::Cancelled:
                case PendingResource::State::Failed:
                case PendingResource::State::CompletedSuccessfully: break;
            }

            LoadResult result {
                .id = pending_resource.request.id,
                .result = ({
                    LoadResult::Result r {LoadResult::ResultType::Cancelled};
                    switch (pending_resource.state.tag) {
                        case PendingResource::State::AwaitingLibrary:
                        case PendingResource::State::AwaitingAudio: PanicIfReached(); break;
                        case PendingResource::State::Cancelled: break;
                        case PendingResource::State::Failed:
                            r = pending_resource.state.Get<ErrorCode>();
                            break;
                        case PendingResource::State::CompletedSuccessfully:
                            r = pending_resource.state.Get<Resource>();
                            break;
                    }
                    r;
                }),
            };

            server.channels.Use([&](auto&) {
                if (pending_resource.request.async_comms_channel.used.Load(MemoryOrder::Relaxed)) {
                    result.Retain();
                    pending_resource.request.async_comms_channel.results.Push(result);
                    pending_resource.request.async_comms_channel.result_added_callback();
                }
            });
            return true;
        },
        [](PendingResource*) {
            // delete function
        });

    return !pending_resources.list.Empty();
}

// ==========================================================================================================
// Server thread

static void ServerThreadUpdateMetrics(Server& server) {
    ASSERT(CurrentThreadID() == server.server_thread_id);
    u32 num_insts_loaded = 0;
    u32 num_samples_loaded = 0;
    u64 total_bytes_used = 0;
    for (auto& i : server.libraries) {
        for (auto& _ : i.value.instruments)
            ++num_insts_loaded;
        for (auto const& audio : i.value.audio_datas) {
            ++num_samples_loaded;
            if (audio.state.Load() == FileLoadingState::CompletedSucessfully)
                total_bytes_used += audio.audio_data.RamUsageBytes();
        }
    }

    server.num_insts_loaded.Store(num_insts_loaded);
    server.num_samples_loaded.Store(num_samples_loaded);
    server.total_bytes_used_by_samples.Store(total_bytes_used);
}

static void RemoveUnreferencedObjects(Server& server) {
    ZoneScoped;
    ASSERT(CurrentThreadID() == server.server_thread_id);

    server.channels.Use([](auto& channels) {
        channels.RemoveIf([](AsyncCommsChannel const& h) { return !h.used.Load(MemoryOrder::Relaxed); });
    });

    auto remove_unreferenced_in_lib = [](auto& lib) {
        auto remove_unreferenced = [](auto& list) {
            list.RemoveIf([](auto const& n) { return n.ref_count.Load() == 0; });
        };
        remove_unreferenced(lib.instruments);
        remove_unreferenced(lib.irs);
        remove_unreferenced(lib.audio_datas);
    };

    for (auto& l : server.libraries)
        remove_unreferenced_in_lib(l.value);
    for (auto n = server.libraries.dead_list; n != nullptr; n = n->writer_next)
        remove_unreferenced_in_lib(n->value);

    server.libraries.DeleteRemovedAndUnreferenced();
}

static void ServerThreadProc(Server& server) {
    ZoneScoped;

    server.server_thread_id = CurrentThreadID();

    ArenaAllocator scratch_arena {PageAllocator::Instance(), Kb(128)};
    auto watcher = CreateDirectoryWatcher(server.error_notifications);
    DEFER {
        if (watcher) DestoryDirectoryWatcher(*watcher);
    };

    while (!server.end_thread.Load()) {
        PendingResources pending_resources {
            .server_thread_id = server.server_thread_id,
        };
        PendingLibraryJobs libs_async_ctx {
            .server_thread_id = server.server_thread_id,
            .thread_pool = server.thread_pool,
            .work_signaller = server.work_signaller,
        };

        while (true) {
            server.work_signaller.WaitUntilSignalledOrSpurious(250u);

            if (server.request_debug_dump_current_state.Exchange(false)) {
                ZoneNamedN(dump, "dump", true);
                DebugLn("Dumping current state of loading thread");
                DebugLn("Libraries currently loading: {}", libs_async_ctx.num_uncompleted_jobs.Load());
                DumpPendingResourcesDebugInfo(pending_resources);
                DebugLn("\nAvailable Libraries:");
                for (auto& lib : server.libraries) {
                    DebugLn("  Library: {}", lib.value.lib->name);
                    for (auto& inst : lib.value.instruments)
                        DebugLn("    Instrument: {}", inst.inst.instrument.name);
                }
            }

            ZoneNamedN(working, "working", true);

            TracyMessageEx({k_trace_category, k_trace_colour, {}},
                           "poll, thread_pool_jobs: {}",
                           pending_resources.thread_pool_jobs.counter.Load());

            if (ConsumeResourceRequests(pending_resources, scratch_arena, server.request_queue)) {
                // For quick initialisation, we load libraries only when there's been a request.
                RequestLibraryFolderScanIfNeeded(server.scan_folders);
            }

            // There's 2 separate systems here. The library loading, and then the audio loading (which
            // includes Instruments and IRs). Before we can fulfill a request for an instrument or IR, we need
            // to have a loaded library. The library contains the information needed to locate the audio.

            auto const libraries_are_still_loading =
                UpdateLibraryJobs(server, libs_async_ctx, scratch_arena, watcher);

            auto const resources_are_still_loading =
                UpdatePendingResources(pending_resources, server, libraries_are_still_loading);

            ServerThreadUpdateMetrics(server);

            if (!resources_are_still_loading && !libraries_are_still_loading) break;
        }

        ZoneNamedN(post_inner, "post inner", true);

        TracyMessageEx({k_trace_category, k_trace_colour, -1u}, "poll completed");

        // We have completed all of the loading requests, but there might still be audio data that is in the
        // thread pool. We need for them to finish before we potentially delete the memory that they rely on.
        pending_resources.thread_pool_jobs.WaitUntilZero();

        RemoveUnreferencedObjects(server);
        scratch_arena.ResetCursorAndConsolidateRegions();
    }

    // It's necessary to do this at the end of this function because it is not guaranteed to be called in the
    // loop; the 'end' boolean can be changed at a point where the loop ends before calling this.
    RemoveUnreferencedObjects(server);

    server.libraries.RemoveAll();
    server.libraries.DeleteRemovedAndUnreferenced();
    server.libraries_by_name.DeleteAll();
}

inline String ToString(EmbeddedString s) { return {s.data, s.size}; }

// not threadsafe
static sample_lib::Library* BuiltinLibrary() {
    static sample_lib::Library builtin_library {
        .name = k_builtin_library_name,
        .tagline = "Built-in library",
        .url = FLOE_HOMEPAGE_URL,
        .author = FLOE_VENDOR,
        .minor_version = 1,
        .background_image_path = nullopt,
        .icon_image_path = nullopt,
        .insts_by_name = {},
        .irs_by_name = {},
        .path = ":memory:",
        .file_hash = 100,
        .create_file_reader = [](sample_lib::Library const&, String path) -> ErrorCodeOr<Reader> {
            auto const embedded_irs = EmbeddedIrs();
            for (auto& ir : embedded_irs.irs)
                if (ToString(ir.filename) == path) return Reader::FromMemory({ir.data, ir.size});
            return ErrorCode(FilesystemError::PathDoesNotExist);
        },
        .file_format_specifics = sample_lib::LuaSpecifics {}, // unused
    };

    static bool init = false;
    if (!Exchange(init, true)) {
        static UninitialisedArray<sample_lib::ImpulseResponse, EmbeddedIr_Count> irs;
        for (auto const i : Range(ToInt(EmbeddedIr_Count))) {
            auto const& embedded = EmbeddedIrs().irs[i];
            PLACEMENT_NEW(&irs[i])
            sample_lib::ImpulseResponse {
                .library = builtin_library,
                .name = ToString(embedded.name),
                .path = ToString(embedded.filename),
            };
        }

        static FixedSizeAllocator<1000> alloc;
        builtin_library.irs_by_name =
            decltype(builtin_library.irs_by_name)::Create(alloc, ToInt(EmbeddedIr_Count));

        for (auto& ir : irs)
            builtin_library.irs_by_name.InsertWithoutGrowing(ir.name, &ir);
    }

    return &builtin_library;
}

Server::Server(ThreadPool& pool,
               Span<String const> always_scanned_folders,
               ThreadsafeErrorNotifications& error_notifications)
    : error_notifications(error_notifications)
    , thread_pool(pool) {
    for (auto e : always_scanned_folders) {
        auto node = scan_folders.AllocateUninitialised();
        PLACEMENT_NEW(&node->value) ScanFolder();
        dyn::Assign(node->value.path, e);
        node->value.source = ScanFolder::Source::AlwaysScannedFolder;
        node->value.state.raw = ScanFolder::State::NotScanned;
        scan_folders.Insert(node);
    }

    {
        auto node = libraries.AllocateUninitialised();
        PLACEMENT_NEW(&node->value)
        ListedLibrary {.arena = PageAllocator::Instance(), .lib = BuiltinLibrary()};
        libraries.Insert(node);

        libraries_by_name.Insert(BuiltinLibrary()->name, node);
    }

    thread.Start([this]() { ServerThreadProc(*this); }, "Sample lib loading");
}

Server::~Server() {
    end_thread.Store(true);
    work_signaller.Signal();
    thread.Join();
    ASSERT(channels.Use([](auto& h) { return h.Empty(); }), "missing channel close");

    scan_folders.RemoveAll();
    scan_folders.DeleteRemovedAndUnreferenced();
}

AsyncCommsChannel& OpenAsyncCommsChannel(Server& server,
                                         ThreadsafeErrorNotifications& error_notifications,
                                         AsyncCommsChannel::ResultAddedCallback&& callback) {
    return server.channels.Use([&](auto& channels) -> AsyncCommsChannel& {
        auto channel = channels.PrependUninitialised();
        PLACEMENT_NEW(channel)
        AsyncCommsChannel {
            .error_notifications = error_notifications,
            .result_added_callback = Move(callback),
            .used = true,
        };
        for (auto& p : channel->instrument_loading_percents)
            p.raw = -1;
        return *channel;
    });
}

void CloseAsyncCommsChannel(Server& server, AsyncCommsChannel& channel) {
    server.channels.Use([&channel](auto& channels) {
        (void)channels;
        channel.used.Store(false, MemoryOrder::Relaxed);
        while (auto r = channel.results.TryPop())
            r->Release();
    });
}

RequestId SendAsyncLoadRequest(Server& server, AsyncCommsChannel& channel, LoadRequest const& request) {
    QueuedRequest const queued_request {
        .id = server.request_id_counter.FetchAdd(1),
        .request = request,
        .async_comms_channel = channel,
    };
    server.request_queue.Push(queued_request);
    server.work_signaller.Signal();
    return queued_request.id;
}

void SetExtraScanFolders(Server& server, Span<String const> extra_folders) {
    server.scan_folders_writer_mutex.Lock();
    DEFER { server.scan_folders_writer_mutex.Unlock(); };

    for (auto it = server.scan_folders.begin(); it != server.scan_folders.end();)
        if (it->value.source == ScanFolder::Source::ExtraFolder && !Find(extra_folders, it->value.path))
            it = server.scan_folders.Remove(it);
        else
            ++it;

    for (auto e : extra_folders) {
        bool already_present = false;
        for (auto& l : server.scan_folders)
            if (l.value.path == e) already_present = true;
        if (already_present) continue;

        auto node = server.scan_folders.AllocateUninitialised();
        PLACEMENT_NEW(&node->value) ScanFolder();
        dyn::Assign(node->value.path, e);
        node->value.source = ScanFolder::Source::ExtraFolder;
        node->value.state.raw = ScanFolder::State::NotScanned;
        server.scan_folders.Insert(node);
    }
}

Span<RefCounted<sample_lib::Library>> AllLibrariesRetained(Server& server, ArenaAllocator& arena) {
    // IMPROVE: is this slow to do at every request for a library?
    if (RequestLibraryFolderScanIfNeeded(server.scan_folders)) server.work_signaller.Signal();

    DynamicArray<RefCounted<sample_lib::Library>> result(arena);
    for (auto& i : server.libraries) {
        if (i.TryRetain()) {
            auto ref = RefCounted<sample_lib::Library>(*i.value.lib, i.reader_uses, nullptr);
            dyn::Append(result, ref);
        }
    }
    return result.ToOwnedSpan();
}

RefCounted<sample_lib::Library> FindLibraryRetained(Server& server, String name) {
    // IMPROVE: is this slow to do at every request for a library?
    if (RequestLibraryFolderScanIfNeeded(server.scan_folders)) server.work_signaller.Signal();

    server.libraries_by_name_mutex.Lock();
    DEFER { server.libraries_by_name_mutex.Unlock(); };
    auto l = server.libraries_by_name.Find(name);
    if (!l) return {};
    auto& node = **l;
    if (!node.TryRetain()) return {};
    return RefCounted<sample_lib::Library>(*node.value.lib, node.reader_uses, nullptr);
}

void LoadResult::ChangeRefCount(RefCountChange t) const {
    if (auto resource_union = result.TryGet<Resource>()) {
        switch (resource_union->tag) {
            case LoadRequestType::Instrument:
                resource_union->Get<RefCounted<sample_lib::LoadedInstrument>>().ChangeRefCount(t);
                break;
            case LoadRequestType::Ir:
                break;
                resource_union->Get<RefCounted<sample_lib::LoadedIr>>().ChangeRefCount(t);
                break;
        }
    }
}

//=================================================
//  _______        _
// |__   __|      | |
//    | | ___  ___| |_ ___
//    | |/ _ \/ __| __/ __|
//    | |  __/\__ \ |_\__ \
//    |_|\___||___/\__|___/
//
//=================================================

template <typename Type>
static Type& ExtractSuccess(tests::Tester& tester, LoadResult const& result, LoadRequest const& request) {
    switch (request.tag) {
        case LoadRequestType::Instrument: {
            auto inst = request.Get<LoadRequestInstrumentIdWithLayer>();
            tester.log.DebugLn("Instrument: {} - {}", inst.id.library_name, inst.id.inst_name);
            break;
        }
        case LoadRequestType::Ir: {
            auto ir = request.Get<sample_lib::IrId>();
            tester.log.DebugLn("Ir: {} - {}", ir.library_name, ir.ir_name);
            break;
        }
    }

    if (auto err = result.result.TryGet<ErrorCode>()) DebugLn("Error: {}", *err);
    REQUIRE_EQ(result.result.tag, LoadResult::ResultType::Success);
    auto opt_r = result.result.Get<Resource>().TryGetMut<Type>();
    REQUIRE(opt_r);
    return *opt_r;
}

TEST_CASE(TestSampleLibraryLoader) {
    struct Fixture {
        Fixture(tests::Tester&) { thread_pool.Init("Thread Pool", 8u); }
        bool initialised = false;
        ArenaAllocatorWithInlineStorage<2000> arena;
        String test_lib_path;
        ThreadPool thread_pool;
        ThreadsafeErrorNotifications error_notif {};
        DynamicArrayInline<String, 2> scan_folders;
    };

    auto& fixture = CreateOrFetchFixtureObject<Fixture>(tester);
    if (!fixture.initialised) {
        fixture.initialised = true;

        auto const lib_dir = (String)path::Join(tester.scratch_arena,
                                                Array {
                                                    tests::TempFolder(tester),
                                                    "floe libraries",
                                                });
        // We copy the test library files to a temp directory so that we can modify them without messing up
        // our test data. And also on Windows WSL, we can watch for directory changes - which doesn't work on
        // the WSL filesystem.
        auto _ =
            Delete(lib_dir, {.type = DeleteOptions::Type::DirectoryRecursively, .fail_if_not_exists = false});
        {
            auto const source = (String)path::Join(
                tester.scratch_arena,
                ConcatArrays(Array {TestFilesFolder(tester)}, k_repo_subdirs_floe_test_libraries));

            auto it = TRY(RecursiveDirectoryIterator::Create(tester.scratch_arena, source));
            while (it.HasMoreFiles()) {
                auto const& entry = it.Get();

                auto const relative_path =
                    path::TrimDirectorySeparatorsEnd(entry.path.Items().SubSpan(source.size));
                auto const dest_file = path::Join(tester.scratch_arena, Array {lib_dir, relative_path});
                if (entry.type == FileType::File) {
                    if (auto const dir = path::Directory(dest_file)) {
                        TRY(CreateDirectory(
                            *dir,
                            {.create_intermediate_directories = true, .fail_if_exists = false}));
                    }
                    TRY(CopyFile(entry.path, dest_file, ExistingDestinationHandling::Overwrite));
                } else {
                    TRY(CreateDirectory(dest_file,
                                        {.create_intermediate_directories = true, .fail_if_exists = false}));
                }

                TRY(it.Increment());
            }
        }

        fixture.test_lib_path = path::Join(fixture.arena, Array {lib_dir, "shared_files_test_lib.mdata"_s});

        DynamicArrayInline<String, 2> scan_folders;
        dyn::Append(scan_folders, fixture.arena.Clone(lib_dir));
        if (auto dir = tests::BuildResourcesFolder(tester))
            dyn::Append(scan_folders, fixture.arena.Clone(*dir));

        fixture.scan_folders = scan_folders;
    }

    auto& scratch_arena = tester.scratch_arena;
    Server server {fixture.thread_pool, {}, fixture.error_notif};
    SetExtraScanFolders(server, fixture.scan_folders);

    SUBCASE("single channel") {
        auto& channel = OpenAsyncCommsChannel(server, fixture.error_notif, []() {});
        CloseAsyncCommsChannel(server, channel);
    }

    SUBCASE("multiple channels") {
        auto& channel1 = OpenAsyncCommsChannel(server, fixture.error_notif, []() {});
        auto& channel2 = OpenAsyncCommsChannel(server, fixture.error_notif, []() {});
        CloseAsyncCommsChannel(server, channel1);
        CloseAsyncCommsChannel(server, channel2);
    }

    SUBCASE("registering again after unregistering all") {
        auto& channel1 = OpenAsyncCommsChannel(server, fixture.error_notif, []() {});
        auto& channel2 = OpenAsyncCommsChannel(server, fixture.error_notif, []() {});
        CloseAsyncCommsChannel(server, channel1);
        CloseAsyncCommsChannel(server, channel2);
        auto& channel3 = OpenAsyncCommsChannel(server, fixture.error_notif, []() {});
        CloseAsyncCommsChannel(server, channel3);
    }

    SUBCASE("unregister a channel directly after sending a request") {
        auto& channel = OpenAsyncCommsChannel(server, fixture.error_notif, [&]() {});

        SendAsyncLoadRequest(server,
                             channel,
                             LoadRequestInstrumentIdWithLayer {
                                 .id =
                                     {
                                         .library_name = "Test Lua"_s,
                                         .inst_name = "Auto Mapped Samples"_s,
                                     },
                                 .layer_index = 0,
                             });
        CloseAsyncCommsChannel(server, channel);
    }

    SUBCASE("loading works") {
        struct Request {
            LoadRequest request;
            TrivialFixedSizeFunction<24, void(LoadResult const&, LoadRequest const& request)> check_result;
            RequestId request_id; // filled in later
        };
        DynamicArray<Request> requests {scratch_arena};

        SUBCASE("ir") {
            auto const builtin_ir = EmbeddedIrs().irs[0];
            dyn::Append(
                requests,
                {
                    .request =
                        sample_lib::IrId {.library_name = k_builtin_library_name,
                                          .ir_name = String {builtin_ir.name.data, builtin_ir.name.size}},
                    .check_result =
                        [&](LoadResult const& r, LoadRequest const& request) {
                            auto ir = ExtractSuccess<RefCounted<sample_lib::LoadedIr>>(tester, r, request);
                            CHECK(ir->audio_data->interleaved_samples.size);
                        },
                });
        }

        SUBCASE("library and instrument") {
            dyn::Append(
                requests,
                {
                    .request =
                        LoadRequestInstrumentIdWithLayer {
                            .id =
                                {
                                    .library_name = "SharedFilesMdata"_s,
                                    .inst_name = "Groups And Refs"_s,
                                },
                            .layer_index = 0,
                        },
                    .check_result =
                        [&](LoadResult const& r, LoadRequest const& request) {
                            auto inst =
                                ExtractSuccess<RefCounted<sample_lib::LoadedInstrument>>(tester, r, request);
                            CHECK(inst->audio_datas.size);
                        },
                });
        }

        SUBCASE("library and instrument (lua)") {
            dyn::Append(
                requests,
                {
                    .request =
                        LoadRequestInstrumentIdWithLayer {
                            .id =
                                {
                                    .library_name = "Test Lua"_s,
                                    .inst_name = "Single Sample"_s,
                                },
                            .layer_index = 0,
                        },
                    .check_result =
                        [&](LoadResult const& r, LoadRequest const& request) {
                            auto inst =
                                ExtractSuccess<RefCounted<sample_lib::LoadedInstrument>>(tester, r, request);
                            CHECK(inst->audio_datas.size);
                        },
                });
        }

        SUBCASE("audio file shared across insts") {
            dyn::Append(
                requests,
                {
                    .request =
                        LoadRequestInstrumentIdWithLayer {
                            .id =
                                {
                                    .library_name = "SharedFilesMdata"_s,
                                    .inst_name = "Groups And Refs"_s,
                                },
                            .layer_index = 0,
                        },
                    .check_result =
                        [&](LoadResult const& r, LoadRequest const& request) {
                            auto i =
                                ExtractSuccess<RefCounted<sample_lib::LoadedInstrument>>(tester, r, request);
                            CHECK_EQ(i->instrument.name, "Groups And Refs"_s);
                            CHECK_EQ(i->audio_datas.size, 4u);
                            for (auto& d : i->audio_datas)
                                CHECK_NEQ(d->interleaved_samples.size, 0u);
                        },
                });
            dyn::Append(
                requests,
                {
                    .request =
                        LoadRequestInstrumentIdWithLayer {
                            .id =
                                {
                                    .library_name = "SharedFilesMdata"_s,
                                    .inst_name = "Groups And Refs (copy)"_s,
                                },
                            .layer_index = 1,
                        },
                    .check_result =
                        [&](LoadResult const& r, LoadRequest const& request) {
                            auto i =
                                ExtractSuccess<RefCounted<sample_lib::LoadedInstrument>>(tester, r, request);
                            CHECK_EQ(i->instrument.name, "Groups And Refs (copy)"_s);
                            CHECK_EQ(i->audio_datas.size, 4u);
                            for (auto& d : i->audio_datas)
                                CHECK_NEQ(d->interleaved_samples.size, 0u);
                        },
                });
            dyn::Append(
                requests,
                {
                    .request =
                        LoadRequestInstrumentIdWithLayer {
                            .id =
                                {
                                    .library_name = "SharedFilesMdata"_s,
                                    .inst_name = "Single Sample"_s,
                                },
                            .layer_index = 2,
                        },
                    .check_result =
                        [&](LoadResult const& r, LoadRequest const& request) {
                            auto i =
                                ExtractSuccess<RefCounted<sample_lib::LoadedInstrument>>(tester, r, request);
                            CHECK_EQ(i->instrument.name, "Single Sample"_s);
                            CHECK_EQ(i->audio_datas.size, 1u);
                            for (auto& d : i->audio_datas)
                                CHECK_NEQ(d->interleaved_samples.size, 0u);
                        },
                });
        }

        SUBCASE("audio files shared within inst") {
            dyn::Append(
                requests,
                {
                    .request =
                        LoadRequestInstrumentIdWithLayer {
                            .id =
                                {
                                    .library_name = "SharedFilesMdata"_s,
                                    .inst_name = "Same Sample Twice"_s,
                                },
                            .layer_index = 0,
                        },
                    .check_result =
                        [&](LoadResult const& r, LoadRequest const& request) {
                            auto i =
                                ExtractSuccess<RefCounted<sample_lib::LoadedInstrument>>(tester, r, request);
                            CHECK_EQ(i->instrument.name, "Same Sample Twice"_s);
                            CHECK_EQ(i->audio_datas.size, 2u);
                            for (auto& d : i->audio_datas)
                                CHECK_NEQ(d->interleaved_samples.size, 0u);
                        },
                });
        };

        SUBCASE("core library") {
            dyn::Append(
                requests,
                {
                    .request =
                        LoadRequestInstrumentIdWithLayer {
                            .id =
                                {
                                    .library_name = "Core"_s,
                                    .inst_name = "bar"_s,
                                },
                            .layer_index = 0,
                        },
                    .check_result =
                        [&](LoadResult const& r, LoadRequest const&) {
                            auto err = r.result.TryGet<ErrorCode>();
                            REQUIRE(err);
                            if (*err != CommonError::NotFound)
                                LOG_WARNING(
                                    "Unable to properly test Core library, not expecting error: {}. The test program scans upwards from its executable path for a folder named '{}' and scans that for the core library",
                                    tests::k_build_resources_subdir,
                                    *err);
                            for (auto& n : fixture.error_notif.items)
                                if (auto e = n.TryScoped())
                                    tester.log.DebugLn("Error: {}: {}: {}",
                                                       e->title,
                                                       e->message,
                                                       e->error_code);
                        },
                });
        }

        SUBCASE("invalid lib+path") {
            dyn::Append(requests,
                        {
                            .request =
                                LoadRequestInstrumentIdWithLayer {
                                    .id =
                                        {
                                            .library_name = "foo"_s,
                                            .inst_name = "bar"_s,
                                        },
                                    .layer_index = 0,
                                },
                            .check_result =
                                [&](LoadResult const& r, LoadRequest const&) {
                                    auto err = r.result.TryGet<ErrorCode>();
                                    REQUIRE(err);
                                    REQUIRE(*err == CommonError::NotFound);
                                },
                        });
        }
        SUBCASE("invalid path only") {
            dyn::Append(requests,
                        {
                            .request =
                                LoadRequestInstrumentIdWithLayer {
                                    .id =
                                        {
                                            .library_name = "SharedFilesMdata"_s,
                                            .inst_name = "bar"_s,
                                        },
                                    .layer_index = 0,
                                },
                            .check_result =
                                [&](LoadResult const& r, LoadRequest const&) {
                                    auto err = r.result.TryGet<ErrorCode>();
                                    REQUIRE(err);
                                    REQUIRE(*err == CommonError::NotFound);
                                },
                        });
        }

        AtomicCountdown countdown {(u32)requests.size};
        auto& channel = OpenAsyncCommsChannel(server, fixture.error_notif, [&]() { countdown.CountDown(); });
        DEFER { CloseAsyncCommsChannel(server, channel); };

        if (requests.size) {
            for (auto& j : requests)
                j.request_id = SendAsyncLoadRequest(server, channel, j.request);

            u32 const timeout_secs = 15;
            auto const countdown_result = countdown.WaitUntilZero(timeout_secs * 1000);

            if (countdown_result == WaitResult::TimedOut) {
                tester.log.ErrorLn("Timed out waiting for library resource loading to complete");
                DumpCurrentStackTraceToStderr();
                server.request_debug_dump_current_state.Store(true);
                server.work_signaller.Signal();
                SleepThisThread(1000);
                // We need to hard-exit without cleaning up because the loading thread is probably deadlocked
                __builtin_abort();
            }

            usize num_results = 0;
            while (auto r = channel.results.TryPop()) {
                DEFER { r->Release(); };
                for (auto const& request : requests) {
                    if (r->id == request.request_id) {
                        for (auto& n : fixture.error_notif.items) {
                            if (auto e = n.TryScoped()) {
                                tester.log.DebugLn("Error Notification  {}: {}: {}",
                                                   e->title,
                                                   e->message,
                                                   e->error_code);
                            }
                        }
                        request.check_result(*r, request.request);
                    }
                }
                ++num_results;
            }
            REQUIRE_EQ(num_results, requests.size);
        }
    }

    SUBCASE("randomly send lots of requests") {
        sample_lib::InstrumentId const inst_ids[] {
            {
                .library_name = "SharedFilesMdata"_s,
                .inst_name = "Groups And Refs"_s,
            },
            {
                .library_name = "SharedFilesMdata"_s,
                .inst_name = "Groups And Refs (copy)"_s,
            },
            {
                .library_name = "SharedFilesMdata"_s,
                .inst_name = "Single Sample"_s,
            },
            {
                .library_name = "Test Lua"_s,
                .inst_name = "Auto Mapped Samples"_s,
            },
        };
        auto const builtin_irs = EmbeddedIrs();

        constexpr u32 k_num_calls = 200;
        u64 random_seed = SeedFromTime();
        AtomicCountdown countdown {k_num_calls};

        auto& channel = OpenAsyncCommsChannel(server, fixture.error_notif, [&]() { countdown.CountDown(); });
        DEFER { CloseAsyncCommsChannel(server, channel); };

        // We sporadically rename the library file to test the error handling of the loading thread
        DynamicArray<char> temp_rename {fixture.test_lib_path, scratch_arena};
        dyn::AppendSpan(temp_rename, ".foo");
        bool is_renamed = false;

        for (auto _ : Range(k_num_calls)) {
            SendAsyncLoadRequest(
                server,
                channel,
                (RandomIntInRange(random_seed, 0, 2) == 0)
                    ? LoadRequest {sample_lib::IrId {.library_name = k_builtin_library_name,
                                                     .ir_name = ({
                                                         auto const ele = RandomElement(
                                                             Span<BinaryData const> {builtin_irs.irs},
                                                             random_seed);
                                                         String {ele.name.data, ele.name.size};
                                                     })}}
                    : LoadRequest {LoadRequestInstrumentIdWithLayer {
                          .id = RandomElement(Span<sample_lib::InstrumentId const> {inst_ids}, random_seed),
                          .layer_index = RandomIntInRange<u32>(random_seed, 0, k_num_layers - 1)}});

            SleepThisThread(RandomIntInRange(random_seed, 0, 3));

            // Let's make this a bit more interesting by simulating a file rename mid-move
            if (RandomIntInRange(random_seed, 0, 4) == 0) {
                if (is_renamed)
                    auto _ = MoveFile(temp_rename, fixture.test_lib_path, ExistingDestinationHandling::Fail);
                else
                    auto _ = MoveFile(fixture.test_lib_path, temp_rename, ExistingDestinationHandling::Fail);
                is_renamed = !is_renamed;
            }

            // Additionally, let's release one the results to test ref-counting/reuse
            if (auto r = channel.results.TryPop()) r->Release();
        }

        constexpr u32 k_timeout_secs = 25;
        auto const countdown_result = countdown.WaitUntilZero(k_timeout_secs * 1000);

        if (countdown_result == WaitResult::TimedOut) {
            tester.log.ErrorLn("Timed out waiting for library resource loading to complete");
            DumpCurrentStackTraceToStderr();
            server.request_debug_dump_current_state.Store(true);
            SleepThisThread(1000);
            // We need to hard-exit without cleaning up because the loading thread is probably deadlocked
            __builtin_abort();
        }
    }

    return k_success;
}

} // namespace sample_lib_server

TEST_REGISTRATION(RegisterSampleLibraryLoaderTests) {
    REGISTER_TEST(sample_lib_server::TestSampleLibraryLoader);
}
