// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <miniz.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/debug/debug.hpp"

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

} // namespace package