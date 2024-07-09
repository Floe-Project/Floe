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
    ErrorCodeOr<String>
    ReadSectionOfFile(usize const bytes_offset_from_file_start, usize const size_in_bytes, Allocator& a);
    ErrorCodeOr<String> ReadWholeFile(Allocator& a);

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
ErrorCodeOr<String> ReadEntireFile(String filename, Allocator& a);
ErrorCodeOr<String> ReadSectionOfFile(String filename,
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

enum class FileType { RegularFile, Directory };

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

const auto it = TRY(DirectoryIterator::Create(folder, "*"));
while (it.HasMoreFiles()) {
    const auto &entry = it.Get();
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

class DirectoryIterator {
  public:
    NON_COPYABLE(DirectoryIterator);

    DirectoryIterator(DirectoryIterator&& other);
    DirectoryIterator& operator=(DirectoryIterator&& other);

    ~DirectoryIterator();

    static ErrorCodeOr<DirectoryIterator>
    Create(Allocator& a, String path, String wildcard = "*", bool get_file_size = false);

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
};

class RecursiveDirectoryIterator {
  public:
    static ErrorCodeOr<RecursiveDirectoryIterator>
    Create(Allocator& allocator, String path, String filename_wildcard = "*", bool get_file_size = false);

    DirectoryEntry const& Get() const { return Last(m_stack).Get(); }
    bool HasMoreFiles() const { return m_stack.size; }
    ErrorCodeOr<void> Increment();

  private:
    RecursiveDirectoryIterator(Allocator& a) : m_a(a), m_wildcard(a), m_stack(a) {}

    Allocator& m_a;
    DynamicArray<char> m_wildcard;
    DynamicArray<DirectoryIterator> m_stack;
    bool m_get_file_size {};
};

// File system watcher
// =======================================================================================================
// - inotify on Linux, ReadDirectoryChangesW on Windows, FSEvents on macOS
// - Super simple poll-like API, just create, poll, destroy -  all from one thread
// - Recursive or non-recursive
// - Events are grouped to each directory you request watching for
// - Full error handling
//
// NOTE: there's no fallback if the file system watcher fails to initialize. We could eventually add a system
// that tracks changes by repeatedly scanning the directories.
//
// These are some of reasons why this API is the way it is:
//
// The use-case that we designed this for was for an event/worker thread. The thread is already regularly
// polling for events from other systems. So for file changes it's convenient to have the same poll-like
// API. The alternative API that file watchers often have is a callback-based API where you receive events in
// a separate thread. For our use-case that would just mean having to do lots of extra thread-safety work.
//
// Per-directory events:
// We chose to group events to each directory that you request watching for rather than receiving an stream of
// events from all directories. This is convenient for our use-case but it does loose a bit of information: we
// no longer have the order of events _across_ directories. Each directory _does_ get an ordered list of
// changes for itself, but we have no way of knowing if a change happened in watched_dir_A before or after a
// change in watched_dir_B. We could add a way to retain this information if it's needed in the future.
//
// Single call for multiple directories:
// The underlying APIs sometimes work as a single stream that watches multiple directories at once so we
// follow that pattern rather than fighting it to allow separate calls for each directory.
//
// TODO: should we limit the number of errors that can be returned so that regularly failing operations aren't
// repeated unnecessarily?
//
// TODO: do a final tidy-up pass over the 3 backends.

struct DirectoryToWatch {
    String path;
    bool recursive;
    // TODO: allow a user-pointer here?
};

struct DirectoryWatcher {
    union NativeData {
        void* pointer;
        int int_id;
    };

    enum class ChangeType : u16 { Added, Deleted, Modified, RenamedOldName, RenamedNewName, Count };

    struct SubpathChangeSet {
        // if true, ignore all changes and recursively rescan this directory
        bool manual_rescan_needed;

        // ordered sequence of changes, loop over this to know the full history or just look at the Last()
        ArenaStack<ChangeType> changes;

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
            ChangeType change; // ignored if manual_rescan_needed is true
            bool manual_rescan_needed;
        };

        // private
        void Add(Change change, ArenaAllocator& arena) {
            // try finding the subpath+file_type and add the change to it
            for (auto& subpath_changeset : subpath_changesets)
                // We check both subpath and file_type because a file can be deleted and then created as a
                // different type. We shouldn't coalesce in this case.
                if (path::Equal(subpath_changeset.subpath, change.subpath) &&
                    subpath_changeset.file_type == change.file_type) {
                    if (change.manual_rescan_needed)
                        subpath_changeset.manual_rescan_needed = true;
                    else if (subpath_changeset.changes.Last() !=
                             change.change) // don't add the same change twice
                        subpath_changeset.changes.Append(change.change, arena);
                    return;
                }

            // else, we create a new one
            SubpathChangeSet new_changeset {
                .manual_rescan_needed = change.manual_rescan_needed,
                .changes = {},
                .subpath = change.subpath,
                .file_type = change.file_type,
            };
            if (!change.manual_rescan_needed) new_changeset.changes.Append(change.change, arena);
            subpath_changesets.Append(new_changeset, arena);
        }

        // A pointer to the directory that you requested watching for. Allows you to more easily associate the
        // changes with a directory.
        DirectoryToWatch const* linked_dir_to_watch {};

        // An error occurred, events could be incomplete. What to do is probably dependent on the type of
        // error.
        Optional<ErrorCode> error;

        // Changesets for each subpath that had changes. This list is unordered, but the changes
        // contained within each changset _are_ ordered. You will also get one of these with an empty
        // 'subpath' if the watched directory itself changed.
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
    bool HandleWatchedDirChanges(Span<DirectoryToWatch const> dirs_to_watch) {
        for (auto& dir : watched_dirs)
            dir.is_desired = false;

        bool any_states_changed = false;

        for (auto const [index, dir_to_watch] : Enumerate(dirs_to_watch)) {
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

        for (auto [index, dir] : Enumerate(watched_dirs))
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

ErrorCodeOr<Span<DirectoryWatcher::DirectoryChanges const>>
ReadDirectoryChanges(DirectoryWatcher& w,
                     Span<DirectoryToWatch const> directories,
                     ArenaAllocator& result_arena,
                     ArenaAllocator& scratch_arena);
