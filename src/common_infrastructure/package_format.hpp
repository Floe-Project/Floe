// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <miniz.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/debug/debug.hpp"
#include "utils/error_notifications.hpp"

#include "sample_library/sample_library.hpp"

namespace package {

constexpr String k_libraries_subdir = "Libraries";
constexpr String k_presets_subdir = "Presets";
constexpr String k_file_extension = ".floe.zip"_s;

PUBLIC bool IsPathPackageFile(String path) { return EndsWithSpan(path, k_file_extension); }

PUBLIC mz_zip_archive WriterCreate() {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_heap(&zip, 0, Mb(100))) {
        PanicF(SourceLocation::Current(),
               "Failed to initialize zip writer: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    return zip;
}

PUBLIC void WriterDestroy(mz_zip_archive& zip) { mz_zip_writer_end(&zip); }

PUBLIC void WriterAddFile(mz_zip_archive& zip, String path, Span<u8 const> data) {
    ArenaAllocatorWithInlineStorage<200> scratch_arena;
    auto const archived_path = NullTerminated(path, scratch_arena);

    // archive paths use posix separators
    if constexpr (IS_WINDOWS) {
        for (auto p = archived_path; *p; ++p)
            if (*p == '\\') *p = '/';
    }

    if (!mz_zip_writer_add_mem(
            &zip,
            archived_path,
            data.data,
            data.size,
            (mz_uint)(path::Extension(path) != ".flac" ? MZ_DEFAULT_COMPRESSION : MZ_NO_COMPRESSION))) {
        PanicF(SourceLocation::Current(),
               "Failed to add file to zip: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
}

// builds the package file data
PUBLIC Span<u8 const> WriterFinalise(mz_zip_archive& zip) {
    void* zip_data = nullptr;
    usize zip_size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &zip_data, &zip_size)) {
        PanicF(SourceLocation::Current(),
               "Failed to finalize zip archive: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    return Span {(u8 const*)zip_data, zip_size};
}

namespace detail {

// IMPROVE: this is not robust, we need to consider the canonical form that a path can take
static String RelativePath(String from, String to) {
    if (to.size < from.size + 1)
        PanicF(SourceLocation::Current(),
               "Bug with calculating relative paths: {} is shorter than {}",
               from,
               to);

    return to.SubSpan(from.size + 1);
}

static ErrorCodeOr<void> WriterAddAllFiles(mz_zip_archive& zip,
                                           String folder,
                                           ArenaAllocator& scratch_arena,
                                           Span<String const> subdirs_in_zip) {
    auto it = TRY(RecursiveDirectoryIterator::Create(scratch_arena,
                                                     folder,
                                                     {
                                                         .wildcard = "*",
                                                         .get_file_size = false,
                                                         .skip_dot_files = true,
                                                     }));
    ArenaAllocator inner_arena {PageAllocator::Instance()};
    while (it.HasMoreFiles()) {
        inner_arena.ResetCursorAndConsolidateRegions();

        auto const& entry = it.Get();
        if (entry.type == FileType::File) {
            auto const file_data = TRY(ReadEntireFile(entry.path, inner_arena)).ToByteSpan();

            DynamicArray<char> archive_path {inner_arena};
            path::JoinAppend(archive_path, subdirs_in_zip);
            path::JoinAppend(archive_path, RelativePath(folder, entry.path));
            WriterAddFile(zip, archive_path, file_data);
        }

        TRY(it.Increment());
    }

    return k_success;
}

} // namespace detail

PUBLIC ErrorCodeOr<void>
WriterAddLibrary(mz_zip_archive& zip, sample_lib::Library const& lib, ArenaAllocator& scratch_arena) {
    return detail::WriterAddAllFiles(zip,
                                     *path::Directory(lib.path),
                                     scratch_arena,
                                     Array {k_libraries_subdir, lib.name});
}

PUBLIC ErrorCodeOr<void>
WriterAddPresetsFolder(mz_zip_archive& zip, String folder, ArenaAllocator& scratch_arena) {
    return detail::WriterAddAllFiles(zip,
                                     folder,
                                     scratch_arena,
                                     Array {k_presets_subdir, path::Filename(folder)});
}

enum class PackageError {
    FileCorrupted,
    NotFloePackage,
};
PUBLIC ErrorCodeCategory const& PackageErrorCodeType() {
    static constexpr ErrorCodeCategory const k_cat {
        .category_id = "PK",
        .message =
            [](Writer const& writer, ErrorCode e) {
                return writer.WriteChars(({
                    String s {};
                    switch ((PackageError)e.code) {
                        case PackageError::FileCorrupted: s = "File is corrupted"_s; break;
                        case PackageError::NotFloePackage: s = "Not a valid Floe package"_s; break;
                    }
                    s;
                }));
            },
    };
    return k_cat;
}
PUBLIC ErrorCodeCategory const& ErrorCategoryForEnum(PackageError) { return PackageErrorCodeType(); }

static bool ErrorExtracting(String path,
                            Optional<ErrorCode> error_code,
                            ThreadsafeErrorNotifications& error_notifications) {
    auto item = error_notifications.NewError();
    item->value = {
        .title = "Package error"_s,
        .error_code = error_code,
        .id = ThreadsafeErrorNotifications::Id("pkg ", path),
    };
    fmt::Assign(item->value.message, "Package file name: {}", path::Filename(path));
    error_notifications.AddOrUpdateError(item);
    return false;
}

static bool ExtractLibrary(mz_zip_archive& zip,
                           String package_path,
                           String library_dir_in_zip,
                           String destination_folder,
                           ThreadsafeErrorNotifications& error_notifications) {
    (void)destination_folder;
    for (auto file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat))
            return ErrorExtracting(package_path,
                                   ErrorCode {PackageError::FileCorrupted},
                                   error_notifications);

        if (file_stat.m_is_directory) continue;

        auto const file_name = FromNullTerminated(file_stat.m_filename);
        if (StartsWithSpan(file_name, library_dir_in_zip)) {
            // TODO
        }
    }

    return true;
}

PUBLIC bool InstallPackage(String package_path,
                           String libraries_path,
                           String presets_path,
                           ThreadsafeErrorNotifications& error_notifications) {
    (void)presets_path;
    ASSERT(IsPathPackageFile(package_path));

    auto package_file = ({
        auto o = OpenFile(package_path, FileMode::Read);
        if (o.HasError()) return ErrorExtracting(package_path, o.Error(), error_notifications);
        o.ReleaseValue();
    });

    auto const package_file_size = ({
        auto const o = package_file.FileSize();
        if (o.HasError()) return ErrorExtracting(package_path, o.Error(), error_notifications);
        o.Value();
    });

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    zip.m_pRead = [](void* io_opaque_ptr, mz_uint64 file_offset, void* buffer, usize buffer_size) -> usize {
        auto& file = *(File*)io_opaque_ptr;
        auto const seek_outcome = file.Seek((s64)file_offset, File::SeekOrigin::Start);
        if (seek_outcome.HasError()) return 0;

        auto const read_outcome = file.Read(buffer, buffer_size);
        if (read_outcome.HasError()) return 0;
        return read_outcome.Value();
    };
    zip.m_pIO_opaque = &package_file;

    if (!mz_zip_reader_init(&zip, package_file_size, 0))
        return ErrorExtracting(package_path, ErrorCode {PackageError::FileCorrupted}, error_notifications);
    DEFER { mz_zip_reader_end(&zip); };

    auto const has_libraries = mz_zip_reader_locate_file(&zip, k_libraries_subdir.data, nullptr, 0) != -1;
    auto const has_presets = mz_zip_reader_locate_file(&zip, k_presets_subdir.data, nullptr, 0) != -1;
    if (!has_libraries && !has_presets)
        return ErrorExtracting(package_path, ErrorCode {PackageError::NotFloePackage}, error_notifications);

    // validate checksum
    if (!mz_zip_validate_archive(&zip, 0))
        return ErrorExtracting(package_path, ErrorCode {PackageError::FileCorrupted}, error_notifications);

    for (auto file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat))
            return ErrorExtracting(package_path,
                                   ErrorCode {PackageError::FileCorrupted},
                                   error_notifications);

        auto const file_name = FromNullTerminated(file_stat.m_filename);

        if (file_stat.m_is_directory) {
            auto const parent = path::Directory(file_name, path::Format::Posix);
            if (parent == k_libraries_subdir) {
                if (!ExtractLibrary(zip, package_path, file_name, libraries_path, error_notifications))
                    return false;
            } else if (parent == k_presets_subdir) {
                // IMPROVE: extract presets
            }
        }
    }

    return false;
}

} // namespace package
