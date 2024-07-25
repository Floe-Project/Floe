// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filesystem.hpp"

#include <errno.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/debug/debug.hpp"

static constexpr ErrorCodeCategory k_fp_error_category {
    .category_id = "FP",
    .message = [](Writer const& writer, ErrorCode e) -> ErrorCodeOr<void> {
        auto const get_str = [code = e.code]() -> String {
            switch ((FilesystemError)code) {
                case FilesystemError::PathDoesNotExist: return "File or folder does not exist";
                case FilesystemError::TooManyFilesOpen: return "Too many files open";
                case FilesystemError::FolderContainsTooManyFiles: return "Folder is too large";
                case FilesystemError::AccessDenied:
                    return "Access is denied to this file or folder. It could be that it's only accessible to a certain user or administrator.";
                case FilesystemError::PathIsAFile: return "Path is a file";
                case FilesystemError::PathIsAsDirectory: return "Path is a folder";
                case FilesystemError::PathAlreadyExists: return "Path already exists";
                case FilesystemError::FileWatcherCreationFailed: return "File watcher creation failed";
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
        case EACCES:
        case EPERM: {
            // POSIX defines EACCES as "an attempt was made to access a file in a way forbidden by its file
            // access permissions" and EPERM as "an attempt was made to perform an operation limited to
            // processes with appropriate privileges or to the owner of a file or other resource". These are
            // so similar that I think we will just consider them the same.
            return FilesystemError::AccessDenied;
        }
    }
    return {};
}

ErrorCode FilesystemErrnoErrorCode(s64 error_code, char const* extra_debug_info, SourceLocation loc) {
    if (auto code = TranslateErrnoCode(error_code))
        return ErrorCode {ErrorCategoryForEnum(FilesystemError {}), (s64)code.Value(), extra_debug_info, loc};
    return ErrnoErrorCode(error_code, extra_debug_info, loc);
}

ErrorCodeOr<MutableString>
KnownDirectoryWithSubdirectories(Allocator& a, KnownDirectories type, Span<String const> subdirectories) {
    ASSERT(subdirectories.size);
    auto path = DynamicArray<char>::FromOwnedSpan(TRY(KnownDirectory(a, type)), a);
    path::JoinAppend(path, subdirectories);
    TRY(CreateDirectory(path, {.create_intermediate_directories = true, .fail_if_exists = false}));
    return path.ToOwnedSpan();
}

ErrorCodeOr<void>
MoveFileOrDirIntoFolder(String file_or_folder, String target_folder, ExistingDestinationHandling existing) {
    auto const file_type = TRY(GetFileType(file_or_folder));

    PathArena path_allocator;
    auto new_name = path::Join(path_allocator, Array {target_folder, path::Filename(file_or_folder)});

    switch (file_type) {
        case FileType::Directory: {
            TRY(CreateDirectory(new_name,
                                {.create_intermediate_directories = true, .fail_if_exists = false}));
            return MoveDirectoryContents(file_or_folder, new_name, existing);
        }
        case FileType::File: {
            return MoveFile(file_or_folder, new_name, existing);
        }
    }
    PanicIfReached();
    return {};
}

ErrorCodeOr<void> MoveFileOrDirectoryContentsIntoFolder(String file_or_dir,
                                                        String destination_dir,
                                                        ExistingDestinationHandling existing) {
    auto const file_type = TRY(GetFileType(file_or_dir));

    switch (file_type) {
        case FileType::Directory: {
            return MoveDirectoryContents(file_or_dir, destination_dir, existing);
        }
        case FileType::File: {
            PathArena path_allocator;
            auto to = path::Join(path_allocator, Array {destination_dir, path::Filename(file_or_dir)});
            return MoveFile(file_or_dir, to, existing);
        }
    }
    PanicIfReached();
    return {};
}

ErrorCodeOr<void>
MoveDirectoryContents(String source_dir, String destination_directory, ExistingDestinationHandling existing) {
    auto dest_file_type = GetFileType(destination_directory);
    if (dest_file_type.HasError() && dest_file_type.Error() != FilesystemError::PathDoesNotExist)
        return dest_file_type.Error();

    if (dest_file_type.Value() == FileType::File) return ErrorCode(FilesystemError::PathIsAFile);

    if (existing == ExistingDestinationHandling::Fail) {
        // Do a dry-run and see if there would be any conflicts

        ArenaAllocatorWithInlineStorage<4000> temp_allocator;
        auto it = TRY(RecursiveDirectoryIterator::Create(temp_allocator,
                                                         source_dir,
                                                         {
                                                             .wildcard = "*",
                                                             .get_file_size = false,
                                                         }));
        while (it.HasMoreFiles()) {
            auto const& entry = it.Get();

            auto relative_path =
                path::TrimDirectorySeparatorsEnd(entry.path.Items().SubSpan(source_dir.size));
            if (relative_path.size) {
                if (entry.type == FileType::File) {
                    PathArena path_allocator;
                    auto const o =
                        GetFileType(path::Join(path_allocator, Array {destination_directory, relative_path}));
                    if (o.HasValue()) return ErrorCode(FilesystemError::PathAlreadyExists);
                }
            }

            TRY(it.Increment());
        }
    }

    {
        ArenaAllocatorWithInlineStorage<4000> temp_allocator;
        auto it = TRY(RecursiveDirectoryIterator::Create(temp_allocator,
                                                         source_dir,
                                                         {
                                                             .wildcard = "*",
                                                             .get_file_size = false,
                                                         }));
        while (it.HasMoreFiles()) {
            auto const& entry = it.Get();

            auto relative_path =
                path::TrimDirectorySeparatorsEnd(entry.path.Items().SubSpan(source_dir.size));
            if (relative_path.size) {
                PathArena path_allocator;
                auto dest = path::Join(path_allocator, Array {destination_directory, relative_path});

                if (entry.type == FileType::Directory) {
                    auto created_dir_outcome =
                        CreateDirectory(dest,
                                        {.create_intermediate_directories = true, .fail_if_exists = true});
                    if (created_dir_outcome == ErrorCode {FilesystemError::PathAlreadyExists}) {
                        if (existing == ExistingDestinationHandling::Overwrite) {
                            // On Mac it's not allowed to modify the contents of a signed bundle.
                            // Therefore we try to delete the whole bundle and try again.
                            // https://stackoverflow.com/questions/3439408/nsfilemanager-creating-folder-cocoa-error-513
                            auto const deleted_mac_bundle = TRY(DeleteDirectoryIfMacBundle(dest));
                            if (deleted_mac_bundle)
                                TRY(CreateDirectory(dest, {.create_intermediate_directories = true}));
                        }
                    } else {
                        TRY(created_dir_outcome);
                    }
                } else {
                    TRY(MoveFile(entry.path, dest, existing));
                }
            }

            TRY(it.Increment());
        }
    }

    // A hack to delete the directory contents
    TRY(Delete(source_dir, {}));
    TRY(CreateDirectory(source_dir, {.create_intermediate_directories = true, .fail_if_exists = false}));

    return k_success;
}

static ErrorCodeOr<void>
AppendFilesForFolder(ArenaAllocator& a, DynamicArray<MutableString>& output, String folder, String wildcard) {
    ArenaAllocatorWithInlineStorage<1000> temp_allocator;
    {
        auto it = TRY(DirectoryIterator::Create(temp_allocator, folder));
        while (it.HasMoreFiles()) {
            auto const& entry = it.Get();
            if (entry.type == FileType::Directory) TRY(AppendFilesForFolder(a, output, entry.path, wildcard));
            TRY(it.Increment());
        }
    }

    {
        auto it = TRY(DirectoryIterator::Create(temp_allocator,
                                                folder,
                                                {
                                                    .wildcard = wildcard,
                                                    .get_file_size = false,
                                                }));
        while (it.HasMoreFiles()) {
            auto const& entry = it.Get();
            dyn::Append(output, MutableString(entry.path).Clone(a));
            TRY(it.Increment());
        }
    }

    return k_success;
}

ErrorCodeOr<Span<MutableString>> GetFilesRecursive(ArenaAllocator& a, String folder, String wildcard) {
    DynamicArray<MutableString> result {a};
    TRY(AppendFilesForFolder(a, result, folder, wildcard));
    return result.ToOwnedSpan();
}

DirectoryIterator::DirectoryIterator(DirectoryIterator&& other)
    : m_e(Move(other.m_e))
    , m_wildcard(Move(other.m_wildcard)) {
    m_reached_end = other.m_reached_end;
    m_handle = other.m_handle;
    m_base_path_size = other.m_base_path_size;

    other.m_handle = nullptr;
    other.m_reached_end = true;
}

DirectoryIterator& DirectoryIterator::operator=(DirectoryIterator&& other) {
    m_reached_end = other.m_reached_end;
    m_e = Move(other.m_e);
    m_handle = other.m_handle;
    m_base_path_size = other.m_base_path_size;
    m_wildcard = Move(other.m_wildcard);

    other.m_handle = nullptr;
    other.m_reached_end = true;

    return *this;
}

static ErrorCodeOr<void>
IncrementToNextThatMatchesPattern(DirectoryIterator& it, String wildcard, bool always_increment) {
    if (always_increment) TRY(it.Increment());
    while (it.HasMoreFiles() && it.Get().type == FileType::File &&
           !MatchWildcard(wildcard, path::Filename(it.Get().path))) {
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
    TRY(IncrementToNextThatMatchesPattern(it, options.wildcard, false));

    RecursiveDirectoryIterator result {allocator};
    dyn::Assign(result.m_wildcard, options.wildcard);
    result.m_get_file_size = options.get_file_size;
    result.m_skip_dot_files = options.skip_dot_files;
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
            TRY(IncrementToNextThatMatchesPattern(sub_it, m_wildcard, false));
            if (sub_it.HasMoreFiles()) dyn::Append(m_stack, Move(sub_it));
        }
    }

    auto& it = m_stack[it_index];

    TRY(IncrementToNextThatMatchesPattern(it, m_wildcard, true));
    if (!it.HasMoreFiles()) {
        auto const index = &it - m_stack.data;
        dyn::Remove(m_stack, (usize)index);
    }

    return k_success;
}

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

    return nullopt;
}
