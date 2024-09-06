// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filesystem.hpp"

#include <cerrno>
#include <errno.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

static constexpr ErrorCodeCategory k_fp_error_category {
    .category_id = "FS",
    .message = [](Writer const& writer, ErrorCode e) -> ErrorCodeOr<void> {
        auto const get_str = [code = e.code]() -> String {
            switch ((FilesystemError)code) {
                case FilesystemError::PathDoesNotExist: return "File or folder does not exist";
                case FilesystemError::TooManyFilesOpen: return "Too many files open";
                case FilesystemError::FolderContainsTooManyFiles: return "Folder is too large";
                case FilesystemError::AccessDenied: return "Access is denied to this file or folder";
                case FilesystemError::PathIsAFile: return "Path is a file";
                case FilesystemError::PathIsAsDirectory: return "Path is a folder";
                case FilesystemError::PathAlreadyExists: return "Path already exists";
                case FilesystemError::FileWatcherCreationFailed: return "File watcher creation failed";
                case FilesystemError::FilesystemBusy: return "Filesystem is busy";
                case FilesystemError::DiskFull: return "Disk is full";
                case FilesystemError::DifferentFilesystems: return "Paths are on different filesystems";
                case FilesystemError::NotEmpty: return "Folder is not empty";
                case FilesystemError::Count: break;
            }
            return "";
        };
        return writer.WriteChars(get_str());
    },
};

ErrorCodeCategory const& ErrorCategoryForEnum(FilesystemError) { return k_fp_error_category; }

static constexpr Optional<FilesystemError> TranslateErrnoCode(s64 ec) {
    switch (ec) {
        case ENOENT: return FilesystemError::PathDoesNotExist;
        case ENFILE: return FilesystemError::TooManyFilesOpen;
        case EROFS: // read-only
        case EACCES:
        case EPERM: {
            // POSIX defines EACCES as "an attempt was made to access a file in a way forbidden by its file
            // access permissions" and EPERM as "an attempt was made to perform an operation limited to
            // processes with appropriate privileges or to the owner of a file or other resource". These are
            // so similar that I think we will just consider them the same.
            return FilesystemError::AccessDenied;
        }
        case EBUSY: return FilesystemError::FilesystemBusy;
#ifdef EDQUOT
        case EDQUOT: return FilesystemError::DiskFull;
#endif
        case ENOSPC: return FilesystemError::DiskFull;
        case EXDEV: return FilesystemError::DifferentFilesystems;
        case ENOTEMPTY: return FilesystemError::NotEmpty;
    }
    return {};
}

ErrorCode FilesystemErrnoErrorCode(s64 error_code, char const* extra_debug_info, SourceLocation loc) {
    if (auto code = TranslateErrnoCode(error_code))
        return ErrorCode {ErrorCategoryForEnum(FilesystemError {}), (s64)code.Value(), extra_debug_info, loc};
    return ErrnoErrorCode(error_code, extra_debug_info, loc);
}

ErrorCodeOr<MutableString>
KnownDirectoryWithSubdirectories(Allocator& a, KnownDirectoryType type, Span<String const> subdirectories) {
    ASSERT(subdirectories.size);
    auto path = DynamicArray<char>::FromOwnedSpan(TRY(KnownDirectory(a, type, true)), a);
    path::JoinAppend(path, subdirectories);
    TRY(CreateDirectory(path, {.create_intermediate_directories = true, .fail_if_exists = false}));
    return path.ToOwnedSpan();
}

ErrorCodeOr<MutableString> FloeKnownDirectory(Allocator& a, FloeKnownDirectoryType type) {
    switch (type) {
        case FloeKnownDirectoryType::Logs:
            return KnownDirectoryWithSubdirectories(a, KnownDirectoryType::Logs, Array {"Floe"_s});
    }
    PanicIfReached();
    return {};
}

// uses Rename() to move a file or folder into a given destination folder
ErrorCodeOr<void> MoveIntoFolder(String from, String destination_folder) {
    PathArena path_allocator;
    auto new_name = path::Join(path_allocator, Array {destination_folder, path::Filename(from)});
    return Rename(from, new_name);
}

ErrorCodeOr<Span<MutableString>> GetFilesRecursive(ArenaAllocator& a,
                                                   String folder,
                                                   Optional<FileType> only_file_type,
                                                   DirectoryIteratorOptions options) {
    DynamicArray<MutableString> result {a};

    auto it = TRY(RecursiveDirectoryIterator::Create(a, folder, options));
    while (it.HasMoreFiles()) {
        auto const& entry = it.Get();
        if (!only_file_type || *only_file_type == entry.type)
            dyn::Append(result, MutableString(entry.path).Clone(a));
        TRY(it.Increment());
    }

    return result.ToOwnedSpan();
}

DirectoryIterator::DirectoryIterator(DirectoryIterator&& other)
    : m_e(Move(other.m_e))
    , m_wildcard(Move(other.m_wildcard)) {
    m_reached_end = other.m_reached_end;
    m_handle = other.m_handle;
    m_base_path_size = other.m_base_path_size;
    m_get_file_size = other.m_get_file_size;
    m_skip_dot_files = other.m_skip_dot_files;

    other.m_handle = nullptr;
    other.m_reached_end = true;
}

DirectoryIterator& DirectoryIterator::operator=(DirectoryIterator&& other) {
    m_reached_end = other.m_reached_end;
    m_e = Move(other.m_e);
    m_handle = other.m_handle;
    m_base_path_size = other.m_base_path_size;
    m_wildcard = Move(other.m_wildcard);
    m_get_file_size = other.m_get_file_size;
    m_skip_dot_files = other.m_skip_dot_files;

    other.m_handle = nullptr;
    other.m_reached_end = true;

    return *this;
}

static ErrorCodeOr<void> IncrementToNextThatMatchesPattern(DirectoryIterator& it,
                                                           String wildcard,
                                                           bool always_increment,
                                                           bool skip_dot_files) {
    if (always_increment) TRY(it.Increment());
    while (it.HasMoreFiles() && it.Get().type == FileType::File &&
           (!MatchWildcard(wildcard, path::Filename(it.Get().path)) ||
            (skip_dot_files && it.Get().path.size && it.Get().path[0] == '.'))) {
        TRY(it.Increment());
    };
    return k_success;
}

ErrorCodeOr<RecursiveDirectoryIterator>
RecursiveDirectoryIterator::Create(Allocator& allocator, String path, DirectoryIteratorOptions options) {
    auto inner_options = options;
    // We do not pass the wildcard into the sub iterators because we need to get the folders, not just paths
    // that match the pattern.
    inner_options.wildcard = "*";
    auto it = TRY(DirectoryIterator::Create(allocator, path, inner_options));
    TRY(IncrementToNextThatMatchesPattern(it, options.wildcard, false, options.skip_dot_files));

    RecursiveDirectoryIterator result {allocator};
    dyn::Assign(result.m_wildcard, options.wildcard);
    result.m_get_file_size = options.get_file_size;
    result.m_skip_dot_files = options.skip_dot_files;
    dyn::Assign(result.m_canonical_base_path, it.CanonicalBasePath());
    if (it.HasMoreFiles()) dyn::Append(result.m_stack, Move(it));

    return result;
}

ErrorCodeOr<void> RecursiveDirectoryIterator::Increment() {
    ASSERT(m_stack.size != 0);

    auto const it_index = m_stack.size - 1;

    {
        auto& it = Last(m_stack);

        if (it.Get().type == FileType::Directory) {
            // We do not pass the wildcard into the sub iterators because we need to get the folders, not just
            // paths that match the pattern.
            auto const inner_options = DirectoryIteratorOptions {
                .wildcard = "*",
                .get_file_size = m_get_file_size,
                .skip_dot_files = m_skip_dot_files,
            };
            auto sub_it = TRY(DirectoryIterator::Create(m_a, it.Get().path, inner_options));
            TRY(IncrementToNextThatMatchesPattern(sub_it, m_wildcard, false, m_skip_dot_files));
            if (sub_it.HasMoreFiles()) dyn::Append(m_stack, Move(sub_it));
        }
    }

    auto& it = m_stack[it_index];

    TRY(IncrementToNextThatMatchesPattern(it, m_wildcard, true, m_skip_dot_files));
    if (!it.HasMoreFiles()) {
        auto const index = &it - m_stack.data;
        dyn::Remove(m_stack, (usize)index);
    }

    return k_success;
}

namespace dir_iterator {

static ErrorCodeOr<Iterator> CreateSubIterator(ArenaAllocator& a, String path, Options options) {
    // We do not pass the wildcard into the sub iterators because we need to get the folders, not just paths
    // that match the pattern.
    options.wildcard = "*";
    return Create(a, path, options);
}

ErrorCodeOr<RecursiveIterator> RecursiveCreate(ArenaAllocator& a, String path, Options options) {
    auto it = TRY(CreateSubIterator(a, path, options));
    RecursiveIterator result {
        .stack = {a},
        .dir_path_to_iterate = {a},
        .canonical_base_path = a.Clone(it.canonical_base_path),
        .options = options,
    };
    result.stack.Prepend(it);
    result.options.wildcard = a.Clone(options.wildcard);
    result.dir_path_to_iterate.Reserve(240);
    return result;
}

void Destroy(RecursiveIterator& it) {
    for (auto& i : it.stack)
        Destroy(i);
}

ErrorCodeOr<Optional<Entry>> Next(RecursiveIterator& it, ArenaAllocator& result_arena) {
    do {
        if (it.dir_path_to_iterate.size) {
            it.stack.Prepend(TRY(CreateSubIterator(result_arena, it.dir_path_to_iterate, it.options)));
            dyn::Clear(it.dir_path_to_iterate);
        }

        while (!it.stack.Empty()) {
            // Break to outer loop because we need to add another iterator to the stack. If we don't break, we
            // might overwrite dir_path_to_iterate (since we just use a single string rather than a queue).
            if (it.dir_path_to_iterate.size) break;

            auto& first = *it.stack.begin();

            auto entry_outcome = Next(first, result_arena);
            if (entry_outcome.HasValue()) {
                auto& opt_entry = entry_outcome.Value();
                if (opt_entry) {
                    auto& entry = *opt_entry;

                    // If it's a directory we will queue it up to be iterated next time. We don't do this here
                    // because if creating the subiterator fails, we have lost this current entry.
                    if (entry.type == FileType::Directory) {
                        dyn::Assign(it.dir_path_to_iterate, first.canonical_base_path);
                        ASSERT(!EndsWith(it.dir_path_to_iterate, path::k_dir_separator));
                        dyn::Append(it.dir_path_to_iterate, path::k_dir_separator);
                        dyn::AppendSpan(it.dir_path_to_iterate, entry.subpath);
                    }

                    if (!MatchWildcard(it.options.wildcard, path::Filename(entry.subpath)) ||
                        (it.options.skip_dot_files && entry.subpath.size && entry.subpath[0] == '.')) {
                        continue;
                    }

                    // Each entry's subpath is relative to the base path of the iterator that created it. We
                    // need convert the subpath relative from each iterator to the base path of this recursive
                    // iterator.
                    if (auto subiterator_path_delta =
                            first.canonical_base_path.SubSpan(it.canonical_base_path.size);
                        subiterator_path_delta.size) {
                        subiterator_path_delta.RemovePrefix(1); // remove the '/'

                        auto subpath = result_arena.AllocateExactSizeUninitialised<char>(
                            subiterator_path_delta.size + 1 + entry.subpath.size);
                        usize write_pos = 0;
                        WriteAndIncrement(write_pos, subpath, subiterator_path_delta);
                        WriteAndIncrement(write_pos, subpath, path::k_dir_separator);
                        WriteAndIncrement(write_pos, subpath, entry.subpath);
                        entry.subpath = subpath;
                    }

                    return entry;
                } else {
                    ASSERT(first.reached_end == true);
                    Destroy(first);
                    it.stack.RemoveFirst();
                    continue;
                }
            } else {
                Destroy(first);
                it.stack.RemoveFirst();
                return entry_outcome.Error();
            }
        }
    } while (it.dir_path_to_iterate.size);

    ASSERT(it.stack.Empty());
    return k_nullopt;
}

} // namespace dir_iterator

#ifndef __APPLE__
ErrorCodeOr<bool> DeleteDirectoryIfMacBundle(String) { return false; }
#endif

void* File::NativeFileHandle() { return m_file; }

File::~File() {
    CloseFile();
    m_file = nullptr;
}

ErrorCodeOr<usize> WriteFile(String filename, Span<u8 const> data) {
    auto file = OpenFile(filename, FileMode::Write);
    if (file.HasError()) return file.Error();
    return file.Value().Write(data);
}

ErrorCodeOr<usize> AppendFile(String filename, Span<u8 const> data) {
    auto file = OpenFile(filename, FileMode::Append);
    if (file.HasError()) return file.Error();
    return file.Value().Write(data);
}

ErrorCodeOr<MutableString> ReadEntireFile(String filename, Allocator& a) {
    auto file = TRY(OpenFile(filename, FileMode::Read));
    return file.ReadWholeFile(a);
}

ErrorCodeOr<MutableString> ReadSectionOfFile(String filename,
                                             usize const bytes_offset_from_file_start,
                                             usize const size_in_bytes,
                                             Allocator& a) {
    auto file = TRY(OpenFile(filename, FileMode::Read));
    return file.ReadSectionOfFile(bytes_offset_from_file_start, size_in_bytes, a);
}

ErrorCodeOr<u64> FileSize(String filename) {
    auto file = OpenFile(filename, FileMode::Read);
    if (file.HasError()) return file.Error();
    return file.Value().FileSize();
}

ErrorCodeOr<MutableString>
File::ReadSectionOfFile(usize const bytes_offset_from_file_start, usize const size_in_bytes, Allocator& a) {
    TRY(Seek((s64)bytes_offset_from_file_start, SeekOrigin::Start));
    auto result = a.AllocateExactSizeUninitialised<u8>(size_in_bytes);
    auto const num_read = TRY(Read(result.data, size_in_bytes));
    if (num_read != size_in_bytes)
        result = a.Resize({.allocation = result.ToByteSpan(), .new_size = num_read});
    return MutableString {(char*)result.data, result.size};
}

ErrorCodeOr<MutableString> File::ReadWholeFile(Allocator& a) {
    auto const file_size = TRY(FileSize());
    return ReadSectionOfFile(0, (usize)file_size, a);
}

ErrorCodeOr<void> ReadSectionOfFileAndWriteToOtherFile(File& file_to_read_from,
                                                       usize const section_start,
                                                       usize const section_size,
                                                       String const filename_to_write_to) {
    ASSERT(section_size);

    auto out_file = TRY(OpenFile(filename_to_write_to, FileMode::Write));
    TRY(file_to_read_from.Seek((s64)section_start, File::SeekOrigin::Start));

    constexpr usize k_four_mb = Mb(4);
    usize const buffer_size = Min(section_size, k_four_mb);
    auto const buffer = PageAllocator::Instance().AllocateBytesForTypeOversizeAllowed<u8>(buffer_size);
    DEFER { PageAllocator::Instance().Free(buffer); };
    usize size_remaining = section_size;
    while (size_remaining != 0) {
        usize const chunk = Min(size_remaining, k_four_mb);
        auto const buffer_span = Span<u8> {buffer.data, chunk};
        TRY(file_to_read_from.Read(buffer_span.data, buffer_span.size));
        TRY(out_file.Write(buffer_span));
        size_remaining -= chunk;
    }
    return k_success;
}

Optional<String>
SearchForExistingFolderUpwards(String dir, String folder_name_to_find, Allocator& allocator) {
    ArenaAllocatorWithInlineStorage<4000> scratch_arena;
    DynamicArray<char> buf {dir, scratch_arena};
    dyn::AppendSpan(buf, "/.");

    constexpr usize k_max_folder_heirarchy = 20;
    for (auto _ : Range(k_max_folder_heirarchy)) {
        auto const opt_dir = path::Directory(dir);
        if (!opt_dir.HasValue()) break;
        ASSERT(dir.size != opt_dir->size);
        dir = *opt_dir;

        dyn::Resize(buf, dir.size);
        path::JoinAppend(buf, folder_name_to_find);
        if (auto const o = GetFileType(buf); o.HasValue() && o.Value() == FileType::Directory)
            return Optional<String> {allocator.Clone(buf)};
    }

    return k_nullopt;
}
