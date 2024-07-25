// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#if __linux__
#include <sys/stat.h>
#endif
#include <errno.h>
#include <stdio.h>

#include "foundation/foundation.hpp"

#include "config.h"
#include "filesystem.hpp"

ErrorCodeOr<FileType> GetFileType(String path) {
    PathArena temp_path_allocator;
    struct ::stat info;
    auto r = ::stat(NullTerminated(path, temp_path_allocator), &info);
    if (r != 0) return FilesystemErrnoErrorCode(errno);

    if ((info.st_mode & S_IFMT) == S_IFDIR) return FileType::Directory;
    return FileType::File;
}

ErrorCodeOr<s64> LastWriteTime(String path) {
    PathArena temp_path_allocator;
    struct ::stat info;
    auto r = ::stat(NullTerminated(path, temp_path_allocator), &info);
    if (r != 0) return FilesystemErrnoErrorCode(errno);
        // Is milliseconds what we want here?

#if __APPLE__
    auto timespec = info.st_mtimespec;
#else
    auto timespec = info.st_mtim;
#endif

    return timespec.tv_sec * 1000 + timespec.tv_nsec / 1000;
}

ErrorCodeOr<DirectoryIterator>
DirectoryIterator::Create(Allocator& allocator, String path, DirectoryIteratorOptions options) {
    PathArena temp_path_allocator;
    auto handle = ::opendir(NullTerminated(path, temp_path_allocator));
    if (!handle) return FilesystemErrnoErrorCode(errno, "opendir");

    DirectoryIterator result {path, allocator};
    result.m_handle = handle;
    result.m_base_path_size = result.m_e.path.size;
    dyn::Assign(result.m_wildcard, options.wildcard);
    result.m_get_file_size = options.get_file_size;
    result.m_skip_dot_files = options.skip_dot_files;

    TRY(result.Increment());
    return result;
}

DirectoryIterator::~DirectoryIterator() {
    if (m_handle) ::closedir((DIR*)m_handle);
}

ErrorCodeOr<void> DirectoryIterator::Increment() {
    if (m_reached_end) return k_success;
    ASSERT(m_handle);
    bool skip;
    do {
        skip = false;
        errno = 0;
        // "modern implementations (including the glibc implementation), concurrent calls to readdir() that
        // specify different directory streams are thread-safe"
        auto entry = readdir((DIR*)m_handle); // NOLINT(concurrency-mt-unsafe)
        if (entry) {
            auto entry_name = FromNullTerminated(entry->d_name);
            if (!MatchWildcard(m_wildcard, entry_name) || entry_name == "."_s || entry_name == ".."_s ||
                (m_skip_dot_files && entry_name.size && entry_name[0] == '.')) {
                skip = true;
            } else {
                dyn::Resize(m_e.path, m_base_path_size);
                path::JoinAppend(m_e.path, FromNullTerminated(entry->d_name));
                m_e.type = entry->d_type == DT_DIR ? FileType::Directory : FileType::File;
                if (m_get_file_size) {
                    struct ::stat info;
                    auto r = ::stat(dyn::NullTerminated(m_e.path), &info);
                    if (r != 0) return FilesystemErrnoErrorCode(errno);

                    m_e.file_size = (u64)info.st_size;
                }
            }
        } else {
            m_reached_end = true;
            if (errno) return FilesystemErrnoErrorCode(errno);
            break;
        }
    } while (skip);

    return k_success;
}

void File::CloseFile() {
    if (m_file) fclose((FILE*)m_file);
}

ErrorCodeOr<void> File::Flush() {
    auto result = fflush((FILE*)m_file);
    if (result == 0) return k_success;
    return FilesystemErrnoErrorCode(errno, "fflush");
}

static_assert(sizeof(off_t) == 8, "you must #define _FILE_OFFSET_BITS 64");

ErrorCodeOr<u64> File::CurrentPosition() {
    auto const result = (s64)ftello((FILE*)m_file);
    if (result == k_ftell_error) return FilesystemErrnoErrorCode(errno, "ftell");
    ASSERT(result >= 0);
    if (result < 0) return 0ull;
    return (u64)result;
}

ErrorCodeOr<void> File::Seek(s64 const offset, SeekOrigin origin) {
    int const origin_flag = ({
        int r {SEEK_CUR};
        switch (origin) {
            case SeekOrigin::Start: r = SEEK_SET; break;
            case SeekOrigin::End: r = SEEK_END; break;
            case SeekOrigin::Current: SEEK_CUR; break;
        }
        r;
    });
    auto const result = fseeko((FILE*)m_file, offset, origin_flag);
    if (result != k_fseek_success) return FilesystemErrnoErrorCode(errno, "fseek");
    return k_success;
}

ErrorCodeOr<usize> File::Write(Span<u8 const> data) {
    clearerr((FILE*)m_file);
    auto const num_written = ::fwrite(data.data, 1, data.size, (FILE*)m_file);
    if (auto ec = ferror((FILE*)m_file)) return FilesystemErrnoErrorCode(ec, "fwrite");
    return num_written;
}

ErrorCodeOr<usize> File::Read(void* data, usize num_bytes) {
    clearerr((FILE*)m_file);
    auto const num_read = ::fread(data, 1, num_bytes, (FILE*)m_file);
    if (auto ec = ferror((FILE*)m_file)) return FilesystemErrnoErrorCode(ec, "fread");
    return num_read;
}

ErrorCodeOr<u64> File::FileSize() {
    TRY(Seek(0, SeekOrigin::End));
    auto const size = CurrentPosition();
    TRY(Seek(0, SeekOrigin::Start));
    return size.Value();
}

ErrorCodeOr<File> OpenFile(String filename, FileMode mode) {
    PathArena temp_allocator;

    FILE* file;
    auto nullterm_filename = NullTerminated(filename, temp_allocator);

    char const* mode_str = ({
        char const* w;
        switch (mode) {
            case FileMode::Read: w = "rb"; break;
            case FileMode::Write: w = "wb"; break;
            case FileMode::Append: w = "ab"; break;
        }
        w;
    });

    file = ::fopen(nullterm_filename, mode_str);
    if (file == nullptr) return FilesystemErrnoErrorCode(errno, "fopen");
    ASSERT(file != nullptr);
    return File(file);
}
