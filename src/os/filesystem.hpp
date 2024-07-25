// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

enum class FilesystemError : u32 {
    PathDoesNotExist,
    PathAlreadyExists,
    TooManyFilesOpen,
    FolderContainsTooManyFiles,
    AccessDenied,
    PathIsAFile,
    PathIsAsDirectory,
    FileWatcherCreationFailed,
    Count,
};

ErrorCodeCategory const& ErrorCategoryForEnum(FilesystemError e);

// translated
ErrorCode FilesystemErrnoErrorCode(s64 error_code,
                                   char const* extra_debug_info = nullptr,
                                   SourceLocation loc = SourceLocation::Current());

enum class FileMode {
    Read,
    Write, // overwrites if it already exists
    Append,
};

struct File {
    File(File&& other) {
        m_file = other.m_file;
        other.m_file = nullptr;
    }
    File& operator=(File&& other) {
        CloseFile();
        m_file = other.m_file;
        other.m_file = nullptr;
        return *this;
    }
    File(File const& other) = delete;
    File& operator=(File const& other) = delete;

    ~File();

    ErrorCodeOr<u64> CurrentPosition();
    enum class SeekOrigin { Start, End, Current };
    ErrorCodeOr<void> Seek(s64 const offset, SeekOrigin origin);
    ErrorCodeOr<u64> FileSize();

    ErrorCodeOr<void> Flush();

    void* NativeFileHandle();
    ErrorCodeOr<MutableString>
    ReadSectionOfFile(usize const bytes_offset_from_file_start, usize const size_in_bytes, Allocator& a);
    ErrorCodeOr<MutableString> ReadWholeFile(Allocator& a);

    ErrorCodeOr<usize> Read(void* data, usize num_bytes);

    ::Writer Writer() {
        ::Writer result;
        result.Set<File>(*this, [](File& f, Span<u8 const> bytes) -> ErrorCodeOr<void> {
            TRY(f.Write(bytes));
            return k_success;
        });
        return result;
    }

    ErrorCodeOr<usize> Write(Span<u8 const> data);
    ErrorCodeOr<usize> Write(Span<char const> data) { return Write(data.ToByteSpan()); }

    friend ErrorCodeOr<File> OpenFile(String filename, FileMode mode);

  private:
    File(void* file) : m_file(file) {}

    void CloseFile();
    static constexpr int k_fseek_success = 0;
    static constexpr s64 k_ftell_error = -1;

    void* m_file {};
};

ErrorCodeOr<File> OpenFile(String filename, FileMode mode);
ErrorCodeOr<MutableString> ReadEntireFile(String filename, Allocator& a);
ErrorCodeOr<MutableString> ReadSectionOfFile(String filename,
                                             usize const bytes_offset_from_file_start,
                                             usize const size_in_bytes,
                                             Allocator& a);

ErrorCodeOr<u64> FileSize(String filename);

ErrorCodeOr<usize> WriteFile(String filename, Span<u8 const> data);
inline ErrorCodeOr<usize> WriteFile(String filename, Span<char const> data) {
    return WriteFile(filename, data.ToByteSpan());
}

ErrorCodeOr<usize> AppendFile(String filename, Span<u8 const> data);
inline ErrorCodeOr<usize> AppendFile(String filename, Span<char const> data) {
    return AppendFile(filename, data.ToByteSpan());
}

ErrorCodeOr<void> ReadSectionOfFileAndWriteToOtherFile(File& file_to_read_from,
                                                       usize section_start,
                                                       usize size,
                                                       String filename_to_write_to);

// Returned paths will use whatever the OS's path separator. And they never have a trailing path seporator.

using PathArena = ArenaAllocatorWithInlineStorage<2000>;

enum class KnownDirectories {
    Logs,
    Prefs,
    AllUsersData,
    Documents,
    PluginSettings,
    AllUsersSettings,
    Data,
    Downloads,
    ClapPlugin,
    Vst3Plugin,
    Temporary,
    Count,
};

// Does not create the directory
ErrorCodeOr<MutableString> KnownDirectory(Allocator& a, KnownDirectories type);

// Creates the directory along with the subdirectories
ErrorCodeOr<MutableString>
KnownDirectoryWithSubdirectories(Allocator& a, KnownDirectories type, Span<String const> subdirectories);

enum class ExistingDestinationHandling {
    Skip, // Keep the existing file without reporting an error
    Overwrite, // Overwrite it if it exists
    Fail, // Fail if it exists
};

enum class FileType { File, Directory };

ErrorCodeOr<FileType> GetFileType(String path);

struct CreateDirectoryOptions {
    bool create_intermediate_directories = false;
    bool fail_if_exists = false; // returns FilesystemError::PathAlreadyExists
};
ErrorCodeOr<void> CreateDirectory(String path, CreateDirectoryOptions options = {});

struct DeleteOptions {
    enum class Type { Any, File, DirectoryRecursively, DirectoryOnlyIfEmpty };
    Type type = Type::Any;
    bool fail_if_not_exists = true; // returns FilesystemError::PathDoesNotExist
};
ErrorCodeOr<void> Delete(String path, DeleteOptions options);

// Returns true if there was a bundle and it was successfully deleted
ErrorCodeOr<bool> DeleteDirectoryIfMacBundle(String dir);

// Turns a relative path into an absolute path.
// Windows: if you pass in a path that starts with a slash you will get that same path prefixed with the
// current drive specifier, e.g. C:/my-path
ErrorCodeOr<MutableString> ConvertToAbsolutePath(Allocator& a, String path);

ErrorCodeOr<MutableString> ResolveSymlinks(Allocator& a, String path);

ErrorCodeOr<void> MoveFileOrDirIntoFolder(String from, String to, ExistingDestinationHandling existing);

ErrorCodeOr<void> MoveFile(String from, String to, ExistingDestinationHandling existing);
ErrorCodeOr<void> CopyFile(String from, String to, ExistingDestinationHandling existing);

// Moves everything (recursively) from source_dir into destination_directory
ErrorCodeOr<void>
MoveDirectoryContents(String source_dir, String destination_directory, ExistingDestinationHandling existing);

ErrorCodeOr<void> MoveFileOrDirectoryContentsIntoFolder(String file_or_dir,
                                                        String destination_dir,
                                                        ExistingDestinationHandling existing);

// Returns a counter representing time since unix epoch
ErrorCodeOr<s64> LastWriteTime(String path);

Optional<Version> MacosBundleVersion(String path);

ErrorCodeOr<DynamicArrayInline<char, 200>> NameOfRunningExecutableOrLibrary();
ErrorCodeOr<MutableString> CurrentExecutablePath(Allocator& a);

Optional<String> SearchForExistingFolderUpwards(String dir, String folder_name_to_find, Allocator& allocator);

struct DialogOptions {
    enum class Type { SaveFile, OpenFile, SelectFolder };
    struct FileFilter {
        String description;
        String wildcard_filter;
    };

    Type type;
    Allocator& allocator;
    String title;
    Optional<String> default_path; // folder and file
    Span<FileFilter const> filters;
    void* parent_window;
};

ErrorCodeOr<Optional<MutableString>> FilesystemDialog(DialogOptions options);

ErrorCodeOr<Span<MutableString>>
GetFilesRecursive(ArenaAllocator& a, String directory, String wildcard = "*");

/*
When creating one of these iterators, they will not return an error if no files that match the pattern, but
instead it will return false to HasMoreFiles().

Usage:

auto it = TRY(DirectoryIterator::Create(alloc, folder, "*"));
while (it.HasMoreFiles()) {
    auto const &entry = it.Get();
    // use entry
    TRY(it.Increment());
}
*/

struct DirectoryEntry {
    DirectoryEntry(String p, Allocator& a) : path(p, a) {}
    DynamicArray<char> path;
    u64 file_size {};
    FileType type {FileType::Directory};
};

struct DirectoryIteratorOptions {
    String wildcard = "*";
    bool get_file_size = false;
    bool skip_dot_files = true;
};

// TODO: tidy up the usage of the options - we seem to be needlessly spliting them out into individual fields
// when we could just keep the struct

class DirectoryIterator {
  public:
    NON_COPYABLE(DirectoryIterator);

    DirectoryIterator(DirectoryIterator&& other);
    DirectoryIterator& operator=(DirectoryIterator&& other);

    ~DirectoryIterator();

    static ErrorCodeOr<DirectoryIterator>
    Create(Allocator& a, String path, DirectoryIteratorOptions options = {});

    DirectoryEntry const& Get() const { return m_e; }
    bool HasMoreFiles() const { return !m_reached_end; }
    ErrorCodeOr<void> Increment();

  private:
    DirectoryIterator(String p, Allocator& a) : m_e(p, a), m_wildcard(a) {}

    bool m_reached_end {};
    DirectoryEntry m_e;
    void* m_handle {};
    usize m_base_path_size {};
    DynamicArray<char> m_wildcard;
    bool m_get_file_size {};
    bool m_skip_dot_files {};
};

class RecursiveDirectoryIterator {
  public:
    static ErrorCodeOr<RecursiveDirectoryIterator>
    Create(Allocator& allocator, String path, DirectoryIteratorOptions options = {});

    DirectoryEntry const& Get() const { return Last(m_stack).Get(); }
    bool HasMoreFiles() const { return m_stack.size; }
    ErrorCodeOr<void> Increment();

  private:
    RecursiveDirectoryIterator(Allocator& a) : m_a(a), m_wildcard(a), m_stack(a) {}

    Allocator& m_a;
    DynamicArray<char> m_wildcard;
    DynamicArray<DirectoryIterator> m_stack;
    bool m_get_file_size {};
    bool m_skip_dot_files {};
};

// Directory watcher
// =======================================================================================================
// - inotify on Linux, ReadDirectoryChangesW on Windows, FSEvents on macOS
// - Super simple poll-like API, just create, poll, destroy - all from one thread
// - Recursive or non-recursive
// - Events are grouped to each directory you request watching for
// - Full error handling
// - Failed actions are only retried if you explicitly ask for it, to reduce spam
//
// NOTE(Sam): The use-case that I designed this for was for an event/worker thread. The thread is already
// regularly polling for events from other systems. So for file changes it's convenient to have the same
// poll-like API. The alternative API that file watchers often have is a callback-based API where you receive
// events in a separate thread. For my use-case that would just mean having to do lots of extra thread-safety
// work.
//
// There's no fallback if the file system watcher fails to initialize or produces an error. But if needed, we
// could add a system that tracks changes by regularly scanning the directories.
//
// This directory watcher gives you a coalesced bitset of changes that happend to each sub-path. We don't give
// the order of events. We do this for 2 reasons:
// 1. On macOS (FSEvents), this kind of coalescing already happens to a certain extent, so it's impossible to
//    get the exact order of events.
// 2. Having the exact order isn't normally the important bit. For example knowing that something was modified
//    before being deleted doesn't really help. It's not like we even know what the modification was. As
//    always with the filesystem, you can't trust the state of anything until you've run a filesystem
//    operation. The same goes for receiving filesystem events. You might have been given a 'created' event
//    but the file might have been deleted in the time between the event being generated and you acting on it.
//    Therefore the changes that you receive are prompts to take further actions, not a guarantee of the
//    current state of the filesystem.
//
// This directory watcher API uses a single call for multiple directories rather than allowing for separate
// calls - one for each directory that you want to watch. This is because in some of the backends (Linux and
// macOS), a single 'watching' object is created to watch multiple directories at once. We follow that pattern
// rather than fighting it.
//
// IMPORTANT: you should check if you receive a 'Delete' change for the watched directory itself. If you poll
// for a directory that doesn't exist then you will get a 'file or folder doesn't exist' error.
//
// On macOS:
// - You may receive changes that occurred very shortly BEFORE you created the watcher.
// - You do not get the distinction between 'renamed to' and 'renamed from'. You only get a 'renamed' event,
//   you must work out yourself if it was a rename to or from.
//
// On Windows:
// - The root directory itself is NOT watched. You will not receive events if the root directory is deleted
//   for example.
// - Windows is very sketchy about giving you events for directories. You might not get the events you'd
//   expect for creating a subdirectory for example.

struct DirectoryToWatch {
    String path;
    bool recursive;
    void* user_data;
};

struct DirectoryWatcher {
    union NativeData {
        void* pointer;
        int int_id;
    };

    using ChangeTypeFlags = u32;
    struct ChangeType {
        enum : ChangeTypeFlags {
            Added = 1 << 0,
            Deleted = 1 << 1,
            Modified = 1 << 2,
            RenamedOldName = 1 << 3,
            RenamedNewName = 1 << 4,
            RenamedOldOrNewName = 1 << 5, // (macOS only) we don't know if it was renamed to or from this name

            // if true, ignore all other changes and recursively rescan this directory
            ManualRescanNeeded = 1 << 6,
        };
        static constexpr DynamicArrayInline<char, 200> ToString(ChangeTypeFlags c) {
            DynamicArrayInline<char, 200> result;
            if (c & Added) dyn::AppendSpan(result, "Added, ");
            if (c & Deleted) dyn::AppendSpan(result, "Deleted, ");
            if (c & Modified) dyn::AppendSpan(result, "Modified, ");
            if (c & RenamedOldName) dyn::AppendSpan(result, "RenamedOldName, ");
            if (c & RenamedNewName) dyn::AppendSpan(result, "RenamedNewName, ");
            if (c & RenamedOldOrNewName) dyn::AppendSpan(result, "RenamedOldOrNewName, ");
            if (c & ManualRescanNeeded) dyn::AppendSpan(result, "ManualRescanNeeded, ");
            if (result.size) result.size -= 2;
            return result;
        }
    };

    struct SubpathChangeSet {
        bool IsSingleChange() const { return Popcount(changes) == 1; }

        // bitset
        ChangeTypeFlags changes;

        // relative to the watched directory, empty if the watched directory itself changed
        String subpath;

        // Might not be available. We get it for free on Linux and macOS but not on Windows.
        Optional<FileType> file_type;
    };

    struct DirectoryChanges {
        // private
        void Clear() {
            error = nullopt;
            subpath_changesets.Clear();
        }

        // private
        bool HasContent() const { return error || subpath_changesets.size; }

        // private
        struct Change {
            String subpath;
            Optional<FileType> file_type;
            ChangeTypeFlags changes;
        };

        // private
        void Add(Change change, ArenaAllocator& arena) {
            // try finding the subpath+file_type and add the change to it
            for (auto& subpath_changeset : subpath_changesets)
                // We check both subpath and file_type because a file can be deleted and then created as a
                // different type. We shouldn't coalesce in this case.
                if (path::Equal(subpath_changeset.subpath, change.subpath) &&
                    subpath_changeset.file_type == change.file_type) {
                    subpath_changeset.changes |= change.changes;
                    return;
                }

            // else, we create a new one
            subpath_changesets.Append(
                {
                    .changes = change.changes,
                    .subpath = change.subpath,
                    .file_type = change.file_type,
                },
                arena);
        }

        // A pointer to the directory that you requested watching for. Allows you to more easily associate the
        // changes with a directory.
        DirectoryToWatch const* linked_dir_to_watch {};

        // An error occurred, events could be incomplete. What to do is probably dependent on the type of
        // error.
        Optional<ErrorCode> error;

        // Unordered list of changesets: one for each subpath that had changes. You will also get one of these
        // with an empty 'subpath' if the watched directory itself changed.
        ArenaStack<SubpathChangeSet> subpath_changesets {};
    };

    struct WatchedDirectory {
        enum class State {
            NeedsWatching,
            NeedsUnwatching,
            Watching,
            WatchingFailed,
            NotWatching,
        };

        ArenaAllocator arena;
        State state;
        String path;
        String resolved_path;
        bool recursive;

        DirectoryChanges directory_changes {}; // ephemeral
        bool is_desired {}; // ephemeral

        NativeData native_data;
    };

    // private
    void RemoveAllNotWatching() {
        watched_dirs.RemoveIf(
            [](WatchedDirectory const& dir) { return dir.state == WatchedDirectory::State::NotWatching; });
    }

    // private
    Span<DirectoryChanges const> AllDirectoryChanges(ArenaAllocator& arena) const {
        DynamicArray<DirectoryChanges> result(arena);
        for (auto const& dir : watched_dirs)
            if (dir.directory_changes.HasContent()) dyn::Append(result, dir.directory_changes);
        return result.ToOwnedSpan();
    }

    // private
    bool HandleWatchedDirChanges(Span<DirectoryToWatch const> dirs_to_watch, bool retry_failed_directories) {
        for (auto& dir : watched_dirs)
            dir.is_desired = false;

        bool any_states_changed = false;

        for (auto& dir_to_watch : dirs_to_watch) {
            if (auto dir_ptr = ({
                    DirectoryWatcher::WatchedDirectory* d = nullptr;
                    for (auto& dir : watched_dirs) {
                        if (path::Equal(dir.path, dir_to_watch.path) &&
                            dir.recursive == dir_to_watch.recursive) {
                            d = &dir;
                            dir.directory_changes.linked_dir_to_watch = &dir_to_watch;
                            break;
                        }
                    }
                    d;
                })) {
                dir_ptr->is_desired = true;
                if (retry_failed_directories && dir_ptr->state == WatchedDirectory::State::WatchingFailed) {
                    dir_ptr->state = WatchedDirectory::State::NeedsWatching;
                    any_states_changed = true;
                }
                continue;
            }

            any_states_changed = true;

            auto new_dir = watched_dirs.PrependUninitialised();
            PLACEMENT_NEW(new_dir)
            DirectoryWatcher::WatchedDirectory {
                .arena = {Malloc::Instance(), 0, 256},
                .state = DirectoryWatcher::WatchedDirectory::State::NeedsWatching,
                .recursive = dir_to_watch.recursive,
                .is_desired = true,
            };
            auto const path = new_dir->arena.Clone(dir_to_watch.path);
            new_dir->path = path;
            // some backends (FSEvents) give use events containing paths with resolved symlinks, so we need
            // to resolve it ourselves to be able to correctly compare paths
            new_dir->resolved_path = ResolveSymlinks(new_dir->arena, dir_to_watch.path).ValueOr(path);
            new_dir->directory_changes.linked_dir_to_watch = &dir_to_watch;
        }

        for (auto& dir : watched_dirs)
            if (!dir.is_desired) {
                dir.state = DirectoryWatcher::WatchedDirectory::State::NeedsUnwatching;
                any_states_changed = true;
            }

        return any_states_changed;
    }

    Allocator& allocator;
    ArenaList<WatchedDirectory, true> watched_dirs;
    NativeData native_data;
};

ErrorCodeOr<DirectoryWatcher> CreateDirectoryWatcher(Allocator& a);
void DestoryDirectoryWatcher(DirectoryWatcher& w);

struct PollDirectoryChangesArgs {
    Span<DirectoryToWatch const> dirs_to_watch;
    bool retry_failed_directories = false;
    double coalesce_latency_ms = 10; // macOS only
    ArenaAllocator& result_arena;
    ArenaAllocator& scratch_arena;
};

ErrorCodeOr<Span<DirectoryWatcher::DirectoryChanges const>>
PollDirectoryChanges(DirectoryWatcher& w, PollDirectoryChangesArgs args);
