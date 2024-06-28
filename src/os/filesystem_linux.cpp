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

namespace native {

bool supports_recursive_watch = false;

ErrorCodeOr<void> Initialise(DirectoryWatcher& w) {
    ZoneScoped;
    auto h = inotify_init1(IN_NONBLOCK);
    if (h == -1) return FilesystemErrnoErrorCode(errno);
    w.native_data.int_id = h;
    return k_success;
}

void Deinitialise(DirectoryWatcher& w) {
    ZoneScoped;
    close(w.native_data.int_id);
}

static ErrorCodeOr<int>
WatchDirectory(int inotify_id, String path, bool recursive, ArenaAllocator& scratch_arena) {
    (void)recursive; // we handle this later on
    if (!PRODUCTION_BUILD && StartsWithSpan(path, "/mnt"_s))
        return ErrorCode {
            FilesystemError::FolderContainsTooManyFiles}; // IMPROVE: is there a better way to avoid the slow
                                                          // not-mounted check on my machine?
    auto const scratch_cursor = scratch_arena.TotalUsed();
    DEFER { scratch_arena.TryShrinkTotalUsed(scratch_cursor); };
    ZoneScoped;
    auto const id =
        inotify_add_watch(inotify_id,
                          scratch_arena.CloneNullTerminated(path).data,
                          IN_MODIFY | IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM);
    if (id == -1) return FilesystemErrnoErrorCode(errno);
    return id;
}

static void UnwatchDirectory(int inotify_id, int handle) { inotify_rm_watch(inotify_id, handle); }

ErrorCodeOr<void> ReadDirectoryChanges(DirectoryWatcher& watcher,
                                       bool watched_directories_changed,
                                       ArenaAllocator& scratch_arena,
                                       DirectoryWatcher::Callback callback) {
    (void)watched_directories_changed;
    for (auto& dir : watcher.watched_dirs) {
        auto const unwatch_dir = [&](DirectoryWatcher::WatchedDirectory::State out_state) {
            for (auto& child : dir.children) {
                if (child.state == DirectoryWatcher::WatchedDirectory::State::Watching)
                    UnwatchDirectory(watcher.native_data.int_id, child.native_data.int_id);
                child.state = out_state;
            }
            if (dir.state == DirectoryWatcher::WatchedDirectory::State::Watching)
                UnwatchDirectory(watcher.native_data.int_id, dir.native_data.int_id);
            dir.state = out_state;
        };

        switch (dir.state) {
            case DirectoryWatcher::WatchedDirectory::State::NotWatching: break; // no change
            case DirectoryWatcher::WatchedDirectory::State::Watching: break; // no change
            case DirectoryWatcher::WatchedDirectory::State::WatchingFailed: break; // no change
            case DirectoryWatcher::WatchedDirectory::State::NeedsWatching: {
                auto const try_watching = [&]() -> ErrorCodeOr<void> {
                    dir.native_data.int_id = TRY(
                        WatchDirectory(watcher.native_data.int_id, dir.path, dir.recursive, scratch_arena));
                    dir.state = DirectoryWatcher::WatchedDirectory::State::Watching;

                    // IMPROVE: lots of allocations here
                    for (auto& child : dir.children) {
                        child.native_data.int_id =
                            TRY(WatchDirectory(watcher.native_data.int_id,
                                               path::Join(scratch_arena, Array {dir.path, child.subpath}),
                                               true,
                                               scratch_arena));
                        child.state = DirectoryWatcher::WatchedDirectory::State::Watching;
                    }

                    return k_success;
                };
                auto const outcome = try_watching();
                if (outcome.HasError()) {
                    unwatch_dir(DirectoryWatcher::WatchedDirectory::State::WatchingFailed);
                    callback(dir.path, outcome.Error());
                }
                break;
            }
            case DirectoryWatcher::WatchedDirectory::State::NeedsUnwatching: {
                unwatch_dir(DirectoryWatcher::WatchedDirectory::State::NotWatching);
                break;
            }
        }
    }

    alignas(struct inotify_event) char buf[4096];
    while (true) {
        ZoneNamedN(read_trace, "read (watcher)", true);
        auto const len = read(watcher.native_data.int_id, buf, sizeof(buf));
        if (len == -1 && errno != EAGAIN) return FilesystemErrnoErrorCode(errno);

        if (len <= 0) break;

        const struct inotify_event* event;
        for (char* ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = CheckedPointerCast<const struct inotify_event*>(ptr);

            Optional<DirectoryWatcher::FileChange::Type> action {};
            if (event->mask & IN_MODIFY || event->mask & IN_CLOSE_WRITE)
                action = DirectoryWatcher::FileChange::Type::Modified;
            else if (event->mask & IN_MOVED_TO)
                action = DirectoryWatcher::FileChange::Type::RenamedNewName;
            else if (event->mask & IN_MOVED_FROM)
                action = DirectoryWatcher::FileChange::Type::RenamedOldName;
            else if (event->mask & IN_DELETE)
                action = DirectoryWatcher::FileChange::Type::Deleted;
            else if (event->mask & IN_CREATE)
                action = DirectoryWatcher::FileChange::Type::Added;
            if (!action) continue;

            String dir {};
            String subdirs {};
            for (auto const& watch : watcher.watched_dirs)
                if (watch.native_data.int_id == event->wd) {
                    dir = watch.path;
                    break;
                } else {
                    for (auto const& child : watch.children) {
                        if (child.native_data.int_id == event->wd) {
                            dir = watch.path;
                            subdirs = child.subpath;
                            break;
                        }
                    }
                    if (dir.size) break;
                }
            if (!dir.size) continue;

            auto filepath = event->len ? FromNullTerminated(event->name) : String {};
            if (subdirs.size) filepath = path::Join(scratch_arena, Array {subdirs, filepath});

            callback(dir, DirectoryWatcher::FileChange {*action, filepath});
        }
    }

    return k_success;
}

} // namespace native
