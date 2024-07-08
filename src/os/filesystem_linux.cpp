// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <ftw.h>
#include <link.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "foundation/foundation.hpp"
#include "utils/debug/debug.hpp"

#include "filesystem.hpp"

ErrorCodeOr<Optional<MutableString>> FilesystemDialog(DialogOptions options) {
    DynamicArrayInline<char, 3000> command {};
    dyn::AppendSpan(command, "zenity --file-selection "_s);
    fmt::Append(command, "--title=\"{}\" ", options.title);
    if (options.default_path) fmt::Append(command, "--filename=\"{}\" ", *options.default_path);
    for (auto f : options.filters)
        fmt::Append(command, "--file-filter=\"{}|{}\" ", f.description, f.wildcard_filter);

    switch (options.type) {
        case DialogOptions::Type::SelectFolder: {
            dyn::AppendSpan(command, "--directory "_s);
            break;
        }
        case DialogOptions::Type::OpenFile: {
            break;
        }
        case DialogOptions::Type::SaveFile: {
            dyn::AppendSpan(command, "--save "_s);
            break;
        }
    }

    FILE* f = popen(dyn::NullTerminated(command), "r");
    if (f) {
        char filename[4000];
        auto _ = fgets(filename, ArraySize(filename), f);
        pclose(f);
        auto result = WhitespaceStripped(FromNullTerminated(filename));
        if (path::IsAbsolute(result, path::Format::Posix)) return result.Clone(options.allocator);
    } else {
        return FilesystemErrnoErrorCode(errno);
    }

    return nullopt;
}

ErrorCodeOr<void> MoveFile(String from, String to, ExistingDestinationHandling existing) {
    PathArena temp_path_allocator;
    auto to_nt = NullTerminated(to, temp_path_allocator);

    if (auto const exists = access(to_nt, F_OK) == 0) {
        switch (existing) {
            case ExistingDestinationHandling::Fail: return ErrorCode(FilesystemError::PathAlreadyExists);
            case ExistingDestinationHandling::Skip: return k_success;
            case ExistingDestinationHandling::Overwrite: break;
        }
    }

    auto from_nt = NullTerminated(from, temp_path_allocator);
    auto result = ::rename(from_nt, to_nt);

    if (result == -1 && errno == EXDEV) {
        // Handle moving the file across different drives
        int input;
        int output;
        if ((input = open(from_nt, O_RDONLY)) == -1) return FilesystemErrnoErrorCode(errno, "open");
        DEFER { close(input); };

        if ((output = creat(to_nt, 0660)) == -1) return FilesystemErrnoErrorCode(errno, "open");
        DEFER { close(output); };

        off_t bytes_copied = 0;
        struct stat fileinfo = {};
        fstat(input, &fileinfo);
        if (sendfile(output, input, &bytes_copied, (usize)fileinfo.st_size) == -1)
            return FilesystemErrnoErrorCode(errno, "open");

        if (remove(from_nt) == -1) return FilesystemErrnoErrorCode(errno, "remove");
    } else if (result != 0) {
        return FilesystemErrnoErrorCode(errno, "rename");
    }
    return k_success;
}

static ErrorCodeOr<ssize> CopyFile(char const* source, char const* destination) {
    int input;
    int output;
    if ((input = open(source, O_RDONLY)) == -1) return FilesystemErrnoErrorCode(errno);
    DEFER { close(input); };
    if ((output = creat(destination, 0660)) == -1) return FilesystemErrnoErrorCode(errno);
    DEFER { close(output); };

    off_t bytes_copied = 0;
    struct stat fileinfo = {};
    if (fstat(input, &fileinfo) != 0) return FilesystemErrnoErrorCode(errno);
    ssize result = sendfile(output, input, &bytes_copied, CheckedCast<usize>(fileinfo.st_size));
    if (result == -1) return FilesystemErrnoErrorCode(errno);

    return result;
}

ErrorCodeOr<void> CopyFile(String from, String to, ExistingDestinationHandling existing) {
    PathArena temp_path_allocator;
    auto from_nt = NullTerminated(from, temp_path_allocator);
    auto to_nt = NullTerminated(to, temp_path_allocator);

    if (auto const exists = access(to_nt, F_OK) == 0) {
        switch (existing) {
            case ExistingDestinationHandling::Fail: return ErrorCode(FilesystemError::PathAlreadyExists);
            case ExistingDestinationHandling::Skip: return k_success;
            case ExistingDestinationHandling::Overwrite: break;
        }
    }

    TRY(CopyFile(from_nt, to_nt));
    return k_success;
}

ErrorCodeOr<MutableString> ConvertToAbsolutePath(Allocator& a, String path) {
    ASSERT(path.size);

    PathArena temp_path_allocator;
    DynamicArray<char> path_nt(path, temp_path_allocator);

    if (StartsWith(path_nt, '~')) {
        char const* home = secure_getenv("HOME");
        if (home == nullptr) return ErrorCode(FilesystemError::PathDoesNotExist);
        dyn::Remove(path_nt, 0, 1);
        dyn::PrependSpan(path_nt, FromNullTerminated(home));
    }

    char result[PATH_MAX] {};
    auto _ = realpath(dyn::NullTerminated(path_nt), result);
    auto const result_path = FromNullTerminated(result);
    if (path::IsAbsolute(result_path)) return result_path.Clone(a);

    return FilesystemErrnoErrorCode(errno);
}

ErrorCodeOr<MutableString> ResolveSymlinks(Allocator& a, String path) {
    ASSERT(path.size);
    PathArena temp_path_allocator;

    char result[PATH_MAX] {};
    auto _ = realpath(NullTerminated(path, temp_path_allocator), result);
    return FromNullTerminated(result).Clone(a);
}

ErrorCodeOr<void> Delete(String path, DeleteOptions options) {
    PathArena temp_path_allocator;
    auto const path_ptr = NullTerminated(path, temp_path_allocator);

    if (remove(path_ptr) == 0)
        return k_success;
    else {
        if (errno == ENOENT && !options.fail_if_not_exists) return k_success;
        if ((errno == EEXIST || errno == ENOTEMPTY) &&
            (options.type == DeleteOptions::Type::Any ||
             options.type == DeleteOptions::Type::DirectoryRecursively)) {

            char* files[] = {(char*)path_ptr, nullptr};
            auto ftsp = fts_open(files, FTS_NOCHDIR | FTS_PHYSICAL, nullptr);
            if (!ftsp) return FilesystemErrnoErrorCode(errno);
            DEFER { fts_close(ftsp); };

            while (auto curr = fts_read(ftsp)) {
                switch (curr->fts_info) {
                    // IMPROVE: handle more error cases
                    case FTS_DP:
                    case FTS_F:
                    case FTS_SL:
                    case FTS_SLNONE:
                    case FTS_DEFAULT:
                        if (remove(curr->fts_accpath) != 0) return FilesystemErrnoErrorCode(errno);
                        break;
                }
            }
            return k_success;
        }

        return FilesystemErrnoErrorCode(errno);
    }
}

ErrorCodeOr<void> CreateDirectory(String path, CreateDirectoryOptions options) {
    PathArena temp_path_allocator;
    DynamicArray<char> buffer {path, temp_path_allocator};
    Optional<usize> cursor = 0u;

    if (mkdir(dyn::NullTerminated(buffer), 0700) == 0) {
        return k_success;
    } else {
        if (errno == EEXIST && !options.fail_if_exists) return k_success;
        if (errno == ENOENT && options.create_intermediate_directories) {
            dyn::Clear(buffer);
            while (cursor) {
                auto part = SplitWithIterator(path, cursor, '/');
                if (part.size) {
                    dyn::Append(buffer, '/');
                    dyn::AppendSpan(buffer, part);
                    if (mkdir(dyn::NullTerminated(buffer), 0700) != 0) {
                        if (errno == EEXIST)
                            continue;
                        else
                            return FilesystemErrnoErrorCode(errno);
                    }
                }
            }
            return k_success;
        }

        return FilesystemErrnoErrorCode(errno);
    }
}

ErrorCodeOr<MutableString> KnownDirectory(Allocator& a, KnownDirectories type) {
    String rel_path;
    switch (type) {
        case KnownDirectories::Temporary: return a.Clone("/tmp"_s);
        case KnownDirectories::AllUsersSettings:
        case KnownDirectories::PluginSettings: rel_path = "~/.config"; break;
        case KnownDirectories::AllUsersData:
        case KnownDirectories::Documents: rel_path = "~/Documents"; break;
        case KnownDirectories::Downloads: rel_path = "~/Downloads"; break;
        case KnownDirectories::Prefs: rel_path = "~/.config"; break;
        case KnownDirectories::Data: rel_path = "~/.local/share"; break;
        case KnownDirectories::Logs: rel_path = "~/.local/state"; break;
        case KnownDirectories::ClapPlugin: rel_path = "~/.clap"; break;
        case KnownDirectories::Vst3Plugin: rel_path = "~/.vst3"; break;
        case KnownDirectories::Count: PanicIfReached();
    }

    return TRY(ConvertToAbsolutePath(a, rel_path));
}

ErrorCodeOr<DynamicArrayInline<char, 200>> NameOfRunningExecutableOrLibrary() { return "unknown"_s; }

ErrorCodeOr<MutableString> CurrentExecutablePath(Allocator& a) {
    char buffer[8000];
    auto const size = readlink("/proc/self/exe", buffer, ArraySize(buffer));
    if (size == -1) return FilesystemErrnoErrorCode(errno, "readlink");
    return a.Clone(MutableString {buffer, (usize)size});
}

// Makes string allocations reusable within an arena. The lifetime is managed by the arena, but you can
// (optionally) Clone/Free individual strings within that lifetime to avoid lots of reallocations. It avoids
// having make everything RAII ready (std C++ style).
struct PathPool {
    struct Path {
        MutableString buffer;
        Path* next {};
    };

    String Clone(String p, ArenaAllocator& arena) {
        {
            Path* prev_free = nullptr;
            for (auto free = free_list; free != nullptr; free = free->next) {
                // if we find a free path that is big enough, use it
                if (free->buffer.size >= p.size) {
                    SinglyLinkedListRemove(free_list, free, prev_free);
                    SinglyLinkedListPrepend(used_list, free);

                    // copy and return
                    CopyMemory(free->buffer.data, p.data, p.size);
                    return {free->buffer.data, p.size};
                }
                prev_free = free;
            }
        }

        auto new_path = arena.NewUninitialised<Path>();
        new_path->buffer = arena.AllocateExactSizeUninitialised<char>(Min(p.size, 64uz));
        CopyMemory(new_path->buffer.data, p.data, p.size);
        SinglyLinkedListPrepend(used_list, new_path);
        return {new_path->buffer.data, p.size};
    }

    void Free(String p) {
        Path* prev = nullptr;
        for (auto i = used_list; i != nullptr; i = i->next) {
            if (i->buffer.data == p.data) {
                SinglyLinkedListRemove(used_list, i, prev);
                SinglyLinkedListPrepend(free_list, i);
                return;
            }
            prev = i;
        }
    }

    Path* used_list {};
    Path* free_list {};
};

// TODO: better name for these 'native' types vs the common types - currently it's the same name
struct WatchedDirectory {
    struct SubDir {
        int watch_id;
        String subpath;
        bool watch_id_invalidated;
    };

    int watch_id;
    // TODO: we need to update this if the children directories change
    ArenaList<SubDir, false> subdirs;
    PathPool path_pool;
};

static ErrorCodeOr<int> InotifyWatch(int inotify_id, char const* path, ArenaAllocator& scratch_arena) {
    auto const scratch_cursor = scratch_arena.TotalUsed();
    DEFER { scratch_arena.TryShrinkTotalUsed(scratch_cursor); };
    ZoneScoped;
    auto const watch_id = inotify_add_watch(inotify_id,
                                            path,
                                            IN_EXCL_UNLINK | IN_ONLYDIR | IN_MODIFY | IN_CREATE | IN_DELETE |
                                                IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM);
    if (watch_id == -1) return FilesystemErrnoErrorCode(errno);
    return watch_id;
}

static void InotifyUnwatch(int inotify_id, int watch_id) {
    auto const rc = inotify_rm_watch(inotify_id, watch_id);
    ASSERT(rc == 0);
}

static void UnwatchDirectory(int inotify_id, DirectoryWatcher::WatchedDirectory& d) {
    auto native_data = (WatchedDirectory*)d.native_data.pointer;
    if (!native_data) return;
    auto const rc = inotify_rm_watch(inotify_id, native_data->watch_id);
    ASSERT(rc == 0);
    d.arena.Delete(native_data);
}

ErrorCodeOr<DirectoryWatcher> CreateDirectoryWatcher(Allocator& a) {
    ZoneScoped;
    DirectoryWatcher result {
        .allocator = a,
        .watched_dirs = {a},
    };
    auto h = inotify_init1(IN_NONBLOCK);
    if (h == -1) return FilesystemErrnoErrorCode(errno);
    result.native_data.int_id = h;
    return result;
}

void DestoryDirectoryWatcher(DirectoryWatcher& watcher) {
    ZoneScoped;
    ArenaAllocatorWithInlineStorage<1000> scratch_arena;

    for (auto& dir : watcher.watched_dirs) {
        if (dir.native_data.pointer != nullptr) {
            auto& native_dir = *(WatchedDirectory*)dir.native_data.pointer;
            for (auto& subdir : native_dir.subdirs)
                InotifyUnwatch(watcher.native_data.int_id, subdir.watch_id);
            UnwatchDirectory(watcher.native_data.int_id, dir);
        }
    }

    watcher.watched_dirs.Clear();

    close(watcher.native_data.int_id);
}

static ErrorCodeOr<WatchedDirectory*> WatchDirectory(DirectoryWatcher::WatchedDirectory& dir,
                                                     int inotify_id,
                                                     String path,
                                                     bool recursive,
                                                     Allocator& allocator,
                                                     ArenaAllocator& scratch_arena) {
    bool success = false;
    auto watch_id = TRY(InotifyWatch(inotify_id, NullTerminated(path, scratch_arena), scratch_arena));
    DEFER {
        if (!success) InotifyUnwatch(inotify_id, watch_id);
    };

    ArenaList<WatchedDirectory::SubDir, false> subdirs {dir.arena};
    PathPool path_pool {};
    if (recursive) {
        auto const try_watch_subdirs = [&]() -> ErrorCodeOr<void> {
            auto it = TRY(RecursiveDirectoryIterator::Create(scratch_arena, dir.path, "*"));
            DynamicArray<char> full_subpath {dir.path, scratch_arena};
            while (it.HasMoreFiles()) {
                auto const& entry = it.Get();

                if (entry.type == FileType::Directory) {
                    String subpath = entry.path;
                    subpath = TrimStartIfMatches(subpath, dir.path);
                    subpath = path::TrimDirectorySeparatorsStart(subpath);

                    dyn::Resize(full_subpath, dir.path.size);
                    path::JoinAppend(full_subpath, subpath);

                    auto subdir = subdirs.PrependUninitialised();
                    PLACEMENT_NEW(subdir)
                    WatchedDirectory::SubDir {
                        .watch_id =
                            TRY(InotifyWatch(inotify_id, dyn::NullTerminated(full_subpath), scratch_arena)),
                        .subpath = path_pool.Clone(subpath, dir.arena),
                        .watch_id_invalidated = false,
                    };
                }

                TRY(it.Increment());
            }
            return k_success;
        };

        auto const outcome = try_watch_subdirs();
        if (outcome.HasError()) {
            for (auto const& subdir : subdirs)
                InotifyUnwatch(inotify_id, subdir.watch_id);
            return outcome.Error();
        }
    }

    auto result = allocator.New<WatchedDirectory>(watch_id, Move(subdirs), path_pool);
    success = true;
    return result;
}

constexpr bool k_debug_inotify = false;

ErrorCodeOr<void> ReadDirectoryChanges(DirectoryWatcher& watcher,
                                       Span<DirectoryToWatch const> dirs_to_watch,
                                       ArenaAllocator& scratch_arena,
                                       DirectoryWatcher::Callback callback) {
    watcher.HandleWatchedDirChanges(dirs_to_watch, scratch_arena);

    for (auto& dir : watcher.watched_dirs) {
        if (dir.native_data.pointer != nullptr) {
            auto& native_dir = *(WatchedDirectory*)dir.native_data.pointer;
            native_dir.subdirs.RemoveIf([&](auto& subdir) {
                if (subdir.watch_id_invalidated) {
                    native_dir.path_pool.Free(subdir.subpath);
                    return true;
                }
                return false;
            });
        }

        switch (dir.state) {
            case DirectoryWatcher::WatchedDirectory::State::NotWatching: break; // no change
            case DirectoryWatcher::WatchedDirectory::State::Watching: break; // no change
            case DirectoryWatcher::WatchedDirectory::State::WatchingFailed: break; // no change
            case DirectoryWatcher::WatchedDirectory::State::NeedsWatching: {
                auto const outcome = WatchDirectory(dir,
                                                    watcher.native_data.int_id,
                                                    dir.path,
                                                    dir.recursive,
                                                    dir.arena,
                                                    scratch_arena);
                if (outcome.HasError()) {
                    dir.state = DirectoryWatcher::WatchedDirectory::State::WatchingFailed;
                    ASSERT(dir.native_data.pointer == nullptr);
                    callback(dir.path, outcome.Error());
                }
                dir.state = DirectoryWatcher::WatchedDirectory::State::Watching;
                dir.native_data.pointer = outcome.Value();
                break;
            }
            case DirectoryWatcher::WatchedDirectory::State::NeedsUnwatching: {
                auto& native_dir = *(WatchedDirectory*)dir.native_data.pointer;
                for (auto& child : native_dir.subdirs) {
                    ASSERT(child.watch_id_invalidated == false);
                    InotifyUnwatch(watcher.native_data.int_id, child.watch_id);
                }
                UnwatchDirectory(watcher.native_data.int_id, dir);
                dir.state = DirectoryWatcher::WatchedDirectory::State::NotWatching;
                dir.native_data.pointer = nullptr;
                break;
            }
        }
    }

    alignas(struct inotify_event) char buf[4096];
    while (true) {
        ZoneNamedN(read_trace, "inotify read", true);
        auto const bytes_read = read(watcher.native_data.int_id, buf, sizeof(buf));
        if (bytes_read == -1 && errno != EAGAIN) return FilesystemErrnoErrorCode(errno);

        if (bytes_read <= 0) break;

        const struct inotify_event* event;
        for (char* ptr = buf; ptr < buf + bytes_read; ptr += sizeof(struct inotify_event) + event->len) {
            event = CheckedPointerCast<const struct inotify_event*>(ptr);

            // event queue overflowed, we might be missing any number of events. event->wd is -1.
            // TODO: we should report 'manual rescan needed' for all dirs for this.
            if (event->mask & IN_Q_OVERFLOW) {
                DebugLn("ERROR: inotify queue overflowed");
                continue;
            }

            // Find what directory this event is for
            struct ThisEvent {
                String RootDirPath() const { return dir.path; }
                String SubDirPath() const { return subdir ? subdir->subpath : ""; }
                auto& NativeObj() { return *(WatchedDirectory*)dir.native_data.pointer; }
                bool IsForRoot() const { return subdir == nullptr; }
                DirectoryWatcher::WatchedDirectory& dir;
                WatchedDirectory::SubDir* subdir; // if null, then it's for the root directory
            };
            ThisEvent this_event = ({
                Optional<ThisEvent> e {};
                for (auto& watch : watcher.watched_dirs) {
                    if (watch.state != DirectoryWatcher::WatchedDirectory::State::Watching) continue;

                    auto& native_data = *(WatchedDirectory*)watch.native_data.pointer;
                    if (native_data.watch_id == event->wd) {
                        e.Emplace(watch, nullptr);
                        break;
                    } else {
                        for (auto& child : native_data.subdirs) {
                            if (child.watch_id == event->wd) {
                                e.Emplace(watch, &child);
                                break;
                            }
                        }
                        if (e.HasValue()) break;
                    }
                }
                if (!e.HasValue()) {
                    if constexpr (k_debug_inotify) {
                        DebugLn("ERROR: inotify event for unknown watch id: {}, name_len: {}, name: {}",
                                event->wd,
                                event->len,
                                FromNullTerminated(event->len ? event->name : ""));
                        DebugLn("Available watch ids:");
                        bool found_ids = false;
                        for (auto& watch : watcher.watched_dirs) {
                            if (watch.state != DirectoryWatcher::WatchedDirectory::State::Watching) continue;
                            found_ids = true;
                            auto& native_data = *(WatchedDirectory*)watch.native_data.pointer;
                            DebugLn("  {}: {}", native_data.watch_id, watch.path);
                            for (auto& child : native_data.subdirs)
                                DebugLn("    {}: {}", child.watch_id, child.subpath);
                        }
                        if (!found_ids) DebugLn("  none");
                    }
                    continue;
                }
                e.Value();
            });

            if constexpr (k_debug_inotify) {
                DynamicArrayInline<char, 2000> printout;
                auto _ = [&]() -> ErrorCodeOr<void> {
                    auto writer = dyn::WriterFor(printout);
                    TRY(fmt::AppendLine(writer, "{{"));
                    TRY(fmt::AppendLine(writer, "  .wd = {}", event->wd));

                    {
                        TRY(fmt::FormatToWriter(writer, "  .mask = "));
                        auto const sz = printout.size;
                        if (event->mask & IN_ACCESS) dyn::AppendSpan(printout, "ACCESS, "_s);
                        if (event->mask & IN_ATTRIB) dyn::AppendSpan(printout, "ATTRIB, "_s);
                        if (event->mask & IN_CLOSE_WRITE) dyn::AppendSpan(printout, "CLOSE_WRITE, "_s);
                        if (event->mask & IN_CLOSE_NOWRITE) dyn::AppendSpan(printout, "CLOSE_NOWRITE, "_s);
                        if (event->mask & IN_CREATE) dyn::AppendSpan(printout, "CREATE, "_s);
                        if (event->mask & IN_DELETE) dyn::AppendSpan(printout, "DELETE, "_s);
                        if (event->mask & IN_DELETE_SELF) dyn::AppendSpan(printout, "DELETE_SELF, "_s);
                        if (event->mask & IN_MODIFY) dyn::AppendSpan(printout, "MODIFY, "_s);
                        if (event->mask & IN_MOVE_SELF) dyn::AppendSpan(printout, "MOVE_SELF, "_s);
                        if (event->mask & IN_MOVED_FROM) dyn::AppendSpan(printout, "MOVED_FROM, "_s);
                        if (event->mask & IN_MOVED_TO) dyn::AppendSpan(printout, "MOVED_TO, "_s);
                        if (event->mask & IN_OPEN) dyn::AppendSpan(printout, "OPEN, "_s);
                        if (event->mask & IN_IGNORED) dyn::AppendSpan(printout, "IGNORED, "_s);
                        if (event->mask & IN_ISDIR) dyn::AppendSpan(printout, "ISDIR, "_s);
                        if (sz != printout.size) printout.size -= 2;
                        dyn::Append(printout, '\n');
                    }

                    TRY(fmt::AppendLine(writer,
                                        "  .path = \"{}\" => \"{}\" => \"{}\"",
                                        this_event.RootDirPath(),
                                        this_event.SubDirPath(),
                                        FromNullTerminated(event->len ? event->name : "")));
                    TRY(fmt::AppendLine(writer, "}}"));
                    return k_success;
                }();
                StdPrint(StdStream::Err, printout);
            }

            // "Watch was removed explicitly (inotify_rm_watch()) or automatically (file was deleted, or
            // filesystem was unmounted)"
            // This can be given BEFORE other events for this watch_id so we mustn't invalidate it here.
            if (event->mask & IN_IGNORED) {
                if (this_event.IsForRoot()) {
                    this_event.dir.state = DirectoryWatcher::WatchedDirectory::State::NotWatching;
                    this_event.dir.native_data.pointer = nullptr;
                } else {
                    this_event.subdir->watch_id_invalidated = true;
                }
                continue;
            }

            if (event->mask & IN_ISDIR && event->mask & IN_CREATE) {
                DynamicArray<char> subpath {scratch_arena};
                if (!this_event.IsForRoot()) path::JoinAppend(subpath, this_event.SubDirPath());
                if (event->len) path::JoinAppend(subpath, FromNullTerminated(event->name));

                DynamicArray<char> full_path {scratch_arena};
                path::JoinAppend(full_path, Array {this_event.RootDirPath(), subpath});

                auto subdir = this_event.NativeObj().subdirs.PrependUninitialised();
                PLACEMENT_NEW(subdir)
                WatchedDirectory::SubDir {
                    .watch_id = TRY(InotifyWatch(watcher.native_data.int_id,
                                                 dyn::NullTerminated(full_path),
                                                 scratch_arena)),
                    .subpath = this_event.NativeObj().path_pool.Clone(subpath, this_event.dir.arena),
                };
            }

            if (event->mask & IN_MOVE_SELF) {
                // IMPROVE: do we need to handle renaming root directories in this case?
            }

            Optional<DirectoryWatcher::FileChange::Change> action {};
            if (event->mask & IN_MODIFY || event->mask & IN_CLOSE_WRITE)
                action = DirectoryWatcher::FileChange::Change::Modified;
            else if (event->mask & IN_MOVED_TO)
                action = DirectoryWatcher::FileChange::Change::RenamedNewName;
            else if (event->mask & IN_MOVED_FROM)
                action = DirectoryWatcher::FileChange::Change::RenamedOldName;
            else if (event->mask & IN_DELETE || (event->mask & IN_DELETE_SELF && this_event.IsForRoot()))
                action = DirectoryWatcher::FileChange::Change::Deleted;
            else if (event->mask & IN_CREATE)
                action = DirectoryWatcher::FileChange::Change::Added;
            if (!action) continue;

            auto filepath = event->len ? FromNullTerminated(event->name) : String {};
            if (!this_event.IsForRoot())
                filepath = path::Join(scratch_arena, Array {this_event.SubDirPath(), filepath});

            callback(this_event.RootDirPath(),
                     DirectoryWatcher::FileChange {
                         .changes = Array {*action},
                         .subpath = filepath,
                         .file_type = (event->mask & IN_ISDIR) ? FileType::Directory : FileType::RegularFile,
                     });
        }
    }

    watcher.RemoveAllNotWatching();

    return k_success;
}
