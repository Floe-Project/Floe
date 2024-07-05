// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

enum class FilesystemError {
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

struct DirectoryWatcher {
    union NativeData {
        void* pointer;
        int int_id;
    };

    struct FileChange {
        enum class Type {
            Added,
            Deleted,
            Modified,
            RenamedOldName,
            RenamedNewName,
            UnknownManualRescanNeeded
        };
        static String TypeToString(Type t) {
            switch (t) {
                case Type::Added: return "Added";
                case Type::Deleted: return "Deleted";
                case Type::Modified: return "Modified";
                case Type::RenamedOldName: return "RenamedOldName";
                case Type::RenamedNewName: return "RenamedNewName";
                case Type::UnknownManualRescanNeeded: return "UnknownManualRescanNeeded";
            }
            return "Unknown";
        }
        Type type;
        String subpath;
    };

    using Callback = FunctionRef<void(String watched_dir, ErrorCodeOr<FileChange> change)>;

    struct WatchedDirectory {
        enum class State {
            NeedsWatching,
            NeedsUnwatching,
            Watching,
            WatchingFailed,
            NotWatching,
        };

        struct Child {
            String subpath;
            State state;
            NativeData native_data;
        };

        ArenaAllocator arena {Malloc::Instance(), 0, 256};
        State state {};
        bool is_desired {};
        String path;
        String resolved_path;
        bool recursive;

        // used if recursive an the backend doesn't support recursive normally
        // TODO: this is only needed on Linux, it should move there
        // TODO: we need to update this if the children directories change
        Span<Child> children;

        NativeData native_data;
    };

    Allocator& allocator;
    List<WatchedDirectory> watched_dirs;
    DynamicArrayInline<u64, 25> blacklisted_path_hashes;
    NativeData native_data;
};

namespace native {

ErrorCodeOr<void> Initialise(DirectoryWatcher& w);
void Deinitialise(DirectoryWatcher& w);

ErrorCodeOr<void> ReadDirectoryChanges(DirectoryWatcher& w,
                                       bool watched_directories_changed,
                                       ArenaAllocator& scratch_arena,
                                       DirectoryWatcher::Callback callback);

} // namespace native

// TODO: should we limit the number of errors that can be returned so that regularly failing operations aren't
// repeated unnecessarily?

ErrorCodeOr<DirectoryWatcher> CreateDirectoryWatcher(Allocator& a);
void DestoryDirectoryWatcher(DirectoryWatcher& w);

struct DirectoryToWatch {
    String path;
    bool recursive;
};

ErrorCodeOr<void> ReadDirectoryChanges(DirectoryWatcher& w,
                                       Span<DirectoryToWatch const> directories,
                                       ArenaAllocator& scratch_arena,
                                       DirectoryWatcher::Callback callback);
