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
// - Super simple poll-like API, just create, poll, destroy
// - Recursive or non-recursive
// - Events are grouped to each directory you request watching for
// - Full error handling
//
// NOTE: there's no fallback if the file system watcher fails to initialize. We could eventually add a system
// that tracks changes by repeatedly scanning the directories.
//
//
// These are some of reasons why this API is the way it is:
//
// Per-directory events:
// We chose to group events to each directory that you request watching for rather than receiving an stream of
// events from all directories. This is convenient for our use-case but it does loose a bit of information: we
// no longer have the order of events across directories. Each directory gets an ordered list of changes but
// we have no way of knowing if a change happened in watched_dir_A before or after a change in watched_dir_B.
//
// Single call for multiple directories:
// The underlying APIs sometimes work as a single stream that watches multiple directories at once so we
// follow that pattern rather than fighting it to allow separate calls for each directory.
//
//
// TODO: we should ensure coalescing of changes in the backends so that we can clarify this API to say: "You
// will recieve at most one callback for each subpath. The callback will contain all changes that have
// happened to that subpath since the last call, in the order they happened. This way, you only need to look
// at the last event if you want a gross view of the changes rather than dealing with each intermediate
// state. For example you might not care if a file was modified shortly before being deleted."
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

// Little util that allows a simple way to push items to a list and not worry about memory. Also allows
// easy access the last item.
template <typename Type>
struct ArenaStack {
    struct Node {
        Node* next {};
        Type data;
    };

    using Iterator = SinglyLinkedListIterator<Node, Type>;

    ArenaStack() = default;
    ArenaStack(Type t, ArenaAllocator& arena) { Append(t, arena); }

    void Append(Type data, ArenaAllocator& arena) {
        ++size;
        auto node = arena.NewUninitialised<Node>();
        node->data = data;
        node->next = nullptr;
        if (last) {
            last->next = node;
            last = node;
        } else {
            first = node;
            last = node;
        }
    }

    Type Last() const { return last->data; }

    void Clear() {
        first = nullptr;
        last = nullptr;
        size = 0;
    }

    auto begin() const { return Iterator {first}; }
    auto end() const { return Iterator {nullptr}; }

    Node* first {};
    Node* last {};
    u32 size {};
};

struct DirectoryWatcher {
    union NativeData {
        void* pointer;
        int int_id;
    };

    enum class ChangeType : u16 {
        Added,
        Deleted,
        Modified,
        RenamedOldName,
        RenamedNewName,
        UnknownManualRescanNeeded,
        Count
    };

    struct ChangedItem {
        bool manual_rescan_needed; // if true, ignore all changes and recursively rescan this directory
        ArenaStack<ChangeType> changes; // sequence of changes, loop over this or just look at the Last()
        String subpath; // relative to the watched directory, empty if the watched directory itself changed
        Optional<FileType> file_type; // might not be available
    };

    struct ChangeSet {
        // private
        void Clear() {
            error = nullopt;
            manual_rescan_needed = false;
            changes.Clear();
        }

        // private
        bool HasContent() const { return error || manual_rescan_needed || changes.size; }

        // private
        struct AddChangeArgs {
            String subpath;
            Optional<FileType> file_type;
            ChangeType change; // ignored if subpath_needs_manual_rescan is true
            bool subpath_needs_manual_rescan;
        };

        // private
        void Add(AddChangeArgs args, ArenaAllocator& arena) {
            for (auto& c : changes)
                if (path::Equal(c.subpath, args.subpath) && c.file_type == args.file_type) {
                    if (args.subpath_needs_manual_rescan)
                        c.manual_rescan_needed = true;
                    else if (c.changes.Last() != args.change) // don't add the same change twice
                        c.changes.Append(args.change, arena);
                    return;
                }
            changes.Append(
                    args.subpath_needs_manual_rescan
                        ? ChangedItem {
                              .manual_rescan_needed = true,
                              .changes = {},
                              .subpath = args.subpath,
                              .file_type = args.file_type,
                          }
                        :
                ChangedItem {
                    .manual_rescan_needed = false,
                    .changes = {args.change, arena},
                    .subpath = args.subpath,
                    .file_type = args.file_type,
                },
                arena);
        }

        DirectoryToWatch const* linked_dir_to_watch {};
        Optional<ErrorCode> error; // an error occurred, events could be incomplete
        bool manual_rescan_needed {}; // if true, ignore all changes and recursively rescan this directory
        ArenaStack<ChangedItem> changes {}; // loop over this
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

        ChangeSet change_set {}; // ephemeral

        NativeData native_data;
    };

    // private
    void RemoveAllNotWatching() {
        watched_dirs.RemoveIf(
            [](WatchedDirectory const& dir) { return dir.state == WatchedDirectory::State::NotWatching; });
    }

    // private
    Span<ChangeSet const> ActiveChangeSets(ArenaAllocator& arena) const {
        DynamicArray<ChangeSet> result(arena);
        for (auto const& dir : watched_dirs)
            if (dir.change_set.HasContent()) dyn::Append(result, dir.change_set);
        return result.ToOwnedSpan();
    }

    // private
    bool HandleWatchedDirChanges(Span<DirectoryToWatch const> dirs_to_watch, ArenaAllocator& scratch_arena) {
        auto is_desired = scratch_arena.NewMultiple<bool>(dirs_to_watch.size);
        DEFER {
            if (is_desired.size) scratch_arena.Free(is_desired.ToByteSpan());
        };

        bool any_states_changed = false;

        for (auto const [index, dir_to_watch] : Enumerate(dirs_to_watch)) {
            if (auto dir_ptr = ({
                    DirectoryWatcher::WatchedDirectory* d = nullptr;
                    for (auto& dir : watched_dirs) {
                        if (path::Equal(dir.path, dir_to_watch.path) &&
                            dir.recursive == dir_to_watch.recursive) {
                            d = &dir;
                            dir.change_set.linked_dir_to_watch = &dir_to_watch;
                            break;
                        }
                    }
                    d;
                })) {
                is_desired[index] = true;
                continue;
            }

            any_states_changed = true;
            is_desired[index] = true;

            auto new_dir = watched_dirs.PrependUninitialised();
            PLACEMENT_NEW(new_dir)
            DirectoryWatcher::WatchedDirectory {
                .arena = {Malloc::Instance(), 0, 256},
                .state = DirectoryWatcher::WatchedDirectory::State::NeedsWatching,
                .recursive = dir_to_watch.recursive,
            };
            auto const path = new_dir->arena.Clone(dir_to_watch.path);
            new_dir->path = path;
            new_dir->resolved_path = ResolveSymlinks(new_dir->arena, dir_to_watch.path).ValueOr(path);
            new_dir->change_set.linked_dir_to_watch = &dir_to_watch;
        }

        for (auto [index, dir] : Enumerate(watched_dirs))
            if (!is_desired[index]) {
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

ErrorCodeOr<Span<DirectoryWatcher::ChangeSet const>>
ReadDirectoryChanges(DirectoryWatcher& w,
                     Span<DirectoryToWatch const> directories,
                     ArenaAllocator& result_arena,
                     ArenaAllocator& scratch_arena);
