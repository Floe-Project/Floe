// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>

#include "utils/logger/logger.hpp"

#if __linux__
#include <sys/stat.h>
#endif

#include "foundation/foundation.hpp"

#include "filesystem.hpp"

ErrorCodeOr<void> WindowsSetFileAttributes(String, Optional<WindowsFileAttributes>) { return k_success; }

ErrorCodeOr<void> Rename(String from, String to) {
    PathArena temp_path_allocator;
    auto const result =
        rename(NullTerminated(from, temp_path_allocator), NullTerminated(to, temp_path_allocator));
    if (result != 0) {
        switch (result) {
            case EINVAL:
            case EFAULT: PanicIfReached();
        }
        return FilesystemErrnoErrorCode(errno, "rename");
    }
    return k_success;
}

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

namespace dir_iterator {

ErrorCodeOr<Iterator> Create(ArenaAllocator& arena, String path, Options options) {
    auto result = TRY(Iterator::InternalCreate(arena, path, options));

    ArenaAllocatorWithInlineStorage<1024> scratch_arena;
    auto handle = opendir(NullTerminated(result.base_path, scratch_arena));
    if (!handle) return FilesystemErrnoErrorCode(errno, "opendir");
    result.handle = handle;

    return result;
}

void Destroy(Iterator& it) {
    if (it.handle) closedir((DIR*)it.handle);
}

ErrorCodeOr<Optional<Entry>> Next(Iterator& it, ArenaAllocator& result_arena) {
    ASSERT(it.handle);
    if (it.reached_end) return Optional<Entry> {};
    bool skip;
    do {
        skip = false;
        errno = 0;
        // "modern implementations (including the glibc implementation), concurrent calls to readdir() that
        // specify different directory streams are thread-safe"
        auto entry = readdir((DIR*)it.handle); // NOLINT(concurrency-mt-unsafe)
        if (entry) {
            auto const entry_name = FromNullTerminated(entry->d_name);
            if (!MatchWildcard(it.options.wildcard, entry_name) || entry_name == "."_s ||
                entry_name == ".."_s ||
                (it.options.skip_dot_files && entry_name.size && entry_name[0] == '.')) {
                skip = true;
            } else {
                Entry result {
                    .subpath = result_arena.Clone(entry_name),
                    .type = entry->d_type == DT_DIR ? FileType::Directory : FileType::File,
                    .file_size = ({
                        u64 s = 0;
                        if (it.options.get_file_size) {
                            PathArena temp_path_allocator;
                            auto const full_path = CombineStrings(temp_path_allocator,
                                                                  Array {
                                                                      it.base_path,
                                                                      "/"_s,
                                                                      entry_name,
                                                                      "\0"_s,
                                                                  });
                            struct stat info;
                            if (stat(full_path.data, &info) != 0) return FilesystemErrnoErrorCode(errno);
                            s = (u64)info.st_size;
                        }
                        s;
                    }),
                };
                return result;
            }
        } else {
            it.reached_end = true;
            if (errno) return FilesystemErrnoErrorCode(errno);
            break;
        }
    } while (skip);
    return Optional<Entry> {};
}

} // namespace dir_iterator

ErrorCodeOr<void> File::Lock(FileLockType type) {
    int const operation = ({
        int r {LOCK_UN};
        switch (type) {
            case FileLockType::Shared: r = LOCK_SH; break;
            case FileLockType::Exclusive: r = LOCK_EX; break;
        }
        r;
    });
    auto const result = flock(fileno((FILE*)m_file), operation);
    if (result != 0) return FilesystemErrnoErrorCode(errno, "flock");
    return k_success;
}

ErrorCodeOr<void> File::Unlock() {
    auto const result = flock(fileno((FILE*)m_file), LOCK_UN);
    if (result != 0) return FilesystemErrnoErrorCode(errno, "flock");
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
            case FileMode::WriteNoOverwrite: w = "wxb"; break;
        }
        w;
    });

    file = ::fopen(nullterm_filename, mode_str);
    if (file == nullptr) return FilesystemErrnoErrorCode(errno, "fopen");
    ASSERT(file != nullptr);
    return File(file);
}
