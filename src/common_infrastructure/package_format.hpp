// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <miniz.h>

#include "foundation/foundation.hpp"
#include "foundation/utils/path.hpp"
#include "os/filesystem.hpp"
#include "utils/debug/debug.hpp"

#include "checksum_crc32_file.hpp"
#include "sample_library/sample_library.hpp"

namespace package {

constexpr String k_libraries_subdir = "Libraries";
constexpr String k_presets_subdir = "Presets";
constexpr String k_file_extension = ".floe.zip"_s;
constexpr String k_checksums_file = "Floe-Details/checksums.crc32"_s;

PUBLIC bool IsPathPackageFile(String path) { return EndsWithSpan(path, k_file_extension); }

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

PUBLIC mz_zip_archive WriterCreate(Writer& writer) {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    zip.m_pWrite =
        [](void* io_opaque_ptr, mz_uint64 file_offset, void const* buffer, usize buffer_size) -> usize {
        // "The output is streamable, i.e. file_ofs in mz_file_write_func always increases only by n"
        (void)file_offset;
        auto& writer = *(Writer*)io_opaque_ptr;
        auto const o = writer.WriteBytes(Span {(u8 const*)buffer, buffer_size});
        if (o.HasError()) return 0;
        return buffer_size;
    };
    zip.m_pIO_opaque = &writer;
    if (!mz_zip_writer_init(&zip, 0)) {
        PanicF(SourceLocation::Current(),
               "Failed to initialize zip writer: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    return zip;
}

PUBLIC void WriterDestroy(mz_zip_archive& zip) { mz_zip_writer_end(&zip); }

namespace detail {

static bool AlreadyExists(mz_zip_archive& zip, String path) {
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat))
            PanicF(SourceLocation::Current(),
                   "Failed to get file stat: {}",
                   mz_zip_get_error_string(mz_zip_get_last_error(&zip)));

        if (FromNullTerminated(file_stat.m_filename) == path) return true;
    }
    return false;
}

} // namespace detail

PUBLIC void WriterAddFolder(mz_zip_archive& zip, String path) {
    ArenaAllocatorWithInlineStorage<200> scratch_arena;
    DynamicArray<char> archived_path {scratch_arena};
    dyn::Assign(archived_path, path);
    if (!EndsWith(archived_path, '/')) dyn::Append(archived_path, '/');

    // archive paths use posix separators
    if constexpr (IS_WINDOWS) {
        for (auto& c : archived_path)
            if (c == '\\') c = '/';
    }

    if (detail::AlreadyExists(zip, archived_path)) return;

    if (!mz_zip_writer_add_mem(&zip, dyn::NullTerminated(archived_path), nullptr, 0, 0)) {
        PanicF(SourceLocation::Current(),
               "Failed to add folder to zip: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
}

PUBLIC void WriterAddParentFolders(mz_zip_archive& zip, String path) {
    auto const parent_path = path::Directory(path, path::Format::Posix);
    if (!parent_path) return;
    WriterAddFolder(zip, *parent_path);
    WriterAddParentFolders(zip, *parent_path);
}

PUBLIC void WriterAddFile(mz_zip_archive& zip, String path, Span<u8 const> data) {
    ArenaAllocatorWithInlineStorage<200> scratch_arena;
    DynamicArray<char> archived_path {scratch_arena};
    dyn::Assign(archived_path, path);

    // archive paths use posix separators
    if constexpr (IS_WINDOWS) {
        for (auto& c : archived_path)
            if (c == '\\') c = '/';
    }

    if (detail::AlreadyExists(zip, archived_path))
        PanicF(SourceLocation::Current(), "File already exists in zip: {}", path);

    WriterAddParentFolders(zip, path);

    if (!mz_zip_writer_add_mem(
            &zip,
            dyn::NullTerminated(archived_path),
            data.data,
            data.size,
            (mz_uint)(path::Extension(path) != ".flac" ? MZ_DEFAULT_COMPRESSION : MZ_NO_COMPRESSION))) {
        PanicF(SourceLocation::Current(),
               "Failed to add file to zip: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
}

PUBLIC void WriterFinalise(mz_zip_archive& zip) {
    if (!mz_zip_writer_finalize_archive(&zip)) {
        PanicF(SourceLocation::Current(),
               "Failed to finalize zip archive: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
}

namespace detail {

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
        DynamicArray<char> archive_path {inner_arena};
        path::JoinAppend(archive_path, subdirs_in_zip);
        path::JoinAppend(archive_path, entry.path.Items().SubSpan(it.CanonicalBasePath().size + 1));
        if (entry.type == FileType::File) {
            auto const file_data = TRY(ReadEntireFile(entry.path, inner_arena)).ToByteSpan();
            WriterAddFile(zip, archive_path, file_data);
        }

        TRY(it.Increment());
    }

    return k_success;
}

static Optional<String> RelativePathIfInFolder(String path, String folder) {
    if (path.size <= folder.size) return k_nullopt;
    if (path[folder.size] != '/') return k_nullopt;
    if (!StartsWithSpan(path, folder)) return k_nullopt;
    return path.SubSpan(folder.size + 1);
}

static void WriterAddChecksumForFolder(mz_zip_archive& zip,
                                       String folder_in_archive,
                                       ArenaAllocator& scratch_arena,
                                       String program_name) {
    DynamicArray<char> checksums {scratch_arena};
    AppendCommentLine(checksums,
                      fmt::Format(scratch_arena,
                                  "Checksums for {}, generated by {}"_s,
                                  path::Filename(folder_in_archive, path::Format::Posix),
                                  program_name));
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat))
            PanicF(SourceLocation::Current(),
                   "Failed to get file stat: {}",
                   mz_zip_get_error_string(mz_zip_get_last_error(&zip)));

        if (file_stat.m_is_directory) continue;

        auto const relative_path =
            RelativePathIfInFolder(FromNullTerminated(file_stat.m_filename), folder_in_archive);
        if (!relative_path) continue;

        AppendChecksumLine(checksums,
                           {
                               .path = *relative_path,
                               .crc32 = (u32)file_stat.m_crc32,
                               .file_size = file_stat.m_uncomp_size,
                           });
    }
    g_debug_log.Debug({}, "Checksums for folder {}: {}", folder_in_archive, checksums.Items());
    WriterAddFile(zip,
                  path::Join(scratch_arena, Array {folder_in_archive, k_checksums_file}, path::Format::Posix),
                  checksums.Items().ToByteSpan());
}

} // namespace detail

PUBLIC ErrorCodeOr<void> WriterAddLibrary(mz_zip_archive& zip,
                                          sample_lib::Library const& lib,
                                          ArenaAllocator& scratch_arena,
                                          String program_name) {
    auto const subdirs =
        Array {k_libraries_subdir, fmt::Format(scratch_arena, "{} - {}", lib.author, lib.name)};

    TRY(detail::WriterAddAllFiles(zip, *path::Directory(lib.path), scratch_arena, subdirs));
    detail::WriterAddChecksumForFolder(zip,
                                       path::Join(scratch_arena, subdirs, path::Format::Posix),
                                       scratch_arena,
                                       program_name);
    return k_success;
}

PUBLIC ErrorCodeOr<void> WriterAddPresetsFolder(mz_zip_archive& zip,
                                                String folder,
                                                ArenaAllocator& scratch_arena,
                                                String program_name) {
    auto const subdirs = Array {k_presets_subdir, path::Filename(folder)};
    TRY(detail::WriterAddAllFiles(zip, folder, scratch_arena, subdirs));
    detail::WriterAddChecksumForFolder(zip,
                                       path::Join(scratch_arena, subdirs, path::Format::Posix),
                                       scratch_arena,
                                       program_name);
    return k_success;
}

// =================================================================================================

/* TODO: the loading a package format
 *
 * There's 3 checksums: the zip file, the checksums.crc32 file, and the actual current filesystem.
 * For a library, we should also consider the version number in the floe.lua file.
 * For writing, we want to embed the checksums.crc32 file in the zip file because it might be installed
 * manually rather than via this extractor.
 *
 *
 * InstallLibrary:
 * - extract the library to a temp folder
 *   - If the extraction completed successfully
 *     - do an atomic rename to the final location
 *     - done, success
 *   - If the extraction failed
 *     - delete the temp folder
 *     - done, show an error
 *   - If the checksums.crc32 file is missing, create it from the zip data
 *
 * IsUnchangedSinceInstalled:
 * - if the library folder doesn't have a .checksums.crc32 file return 'false'
 * - read the .checksums.crc32 file
 * - if any current file's checksum deviates from the stored checksum return 'false'
 * - else return 'true'
 *
 *
 * For each library in the package:
 * - Get the floe.lua file from the package
 * - Read the floe.lua file
 * - If the library author+name is not already installed in any library folder (ask the sample library server)
 *   - InstallPackage
 * - If it is
 *   - Compare the version of the installed library vs the version in the package
 *   - If IsUnchangedSinceInstalled
 *     - If the version in the package is newer or equal
 *       - InstallPackage, it's safe to overwrite: libraries are backwards compatible
 *         If the version is equal then the developer forgot to increment the version number
 *       - return
 *     - Else
 *       - return: nothing to do, it's already installed
 *   - Else
 *     - Ask the user if they want to overwrite, giving information about versions and files that have changed
 *
 */

struct ReaderError {
    ErrorCode code;
    String message;
};

struct TryHelpersReader {
    TRY_HELPER_INHERIT(IsError, TryHelpers)
    TRY_HELPER_INHERIT(ExtractValue, TryHelpers)
    template <typename T>
    static ReaderError ExtractError(ErrorCodeOr<T> const& o) {
        return {o.Error(), ""_s};
    }
};

namespace detail {

static ErrorCodeOr<mz_zip_archive_file_stat> FileStat(mz_zip_archive& zip, mz_uint file_index) {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat))
        return ErrorCode {PackageError::FileCorrupted};
    return file_stat;
}

static ValueOrError<mz_zip_archive_file_stat, ReaderError>
FindFloeLuaInZipInLibrary(mz_zip_archive& zip, String library_dir_in_zip) {
    using H = TryHelpersReader;
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        auto const file_stat = TRY_H(FileStat(zip, file_index));
        if (file_stat.m_is_directory) continue;
        auto const path = FromNullTerminated(file_stat.m_filename);
        if (!sample_lib::FilenameIsFloeLuaFile(path::Filename(path, path::Format::Posix))) continue;
        auto const parent = path::Directory(path, path::Format::Posix);
        if (!parent) continue;
        if (*parent != library_dir_in_zip) continue;

        return file_stat;
    }
    return ReaderError {ErrorCode {PackageError::NotFloePackage}, "No floe.lua file in library"_s};
}

static ErrorCodeOr<Span<u8 const>>
ExtractFileToMem(mz_zip_archive& zip, mz_zip_archive_file_stat const& file_stat, ArenaAllocator& arena) {
    auto const data = arena.AllocateExactSizeUninitialised<u8>(file_stat.m_uncomp_size);
    if (!mz_zip_reader_extract_to_mem(&zip, file_stat.m_file_index, data.data, data.size, 0))
        return ErrorCode {PackageError::FileCorrupted};
    return data;
}

static ErrorCodeOr<void>
ExtractFileToFile(mz_zip_archive& zip, mz_zip_archive_file_stat const& file_stat, File& out_file) {
    struct Context {
        File& out_file;
        ErrorCodeOr<void> result = k_success;
    };
    Context context {out_file};
    if (!mz_zip_reader_extract_to_callback(
            &zip,
            file_stat.m_file_index,
            [](void* user_data, mz_uint64 file_offset, void const* buffer, usize buffer_size) -> usize {
                auto& context = *(Context*)user_data;
                auto const o = context.out_file.WriteAt((s64)file_offset, {(u8 const*)buffer, buffer_size});
                if (o.HasError()) {
                    context.result = o.Error();
                    return 0;
                }
                return o.Value();
            },
            &context,
            0)) {
        if (context.result.HasError()) return context.result.Error();
        return ErrorCode {PackageError::FileCorrupted};
    }
    return k_success;
}

static ErrorCodeOr<Span<String>>
SubfoldersOfFolder(mz_zip_archive& zip, String folder, ArenaAllocator& arena) {
    DynamicArray<String> folders {arena};

    for (auto const file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        auto const file_stat = TRY(FileStat(zip, file_index));
        auto const path = FromNullTerminated(file_stat.m_filename);
        if (StartsWithSpan(path, folder) && path.size > folder.size) {
            auto const relative_path = TrimEndIfMatches(path.SubSpan(folder.size + 1), '/');
            if (relative_path.size == 0) continue;
            if (Contains(relative_path, '/')) continue;
            dyn::Append(folders, arena.Clone(TrimEndIfMatches(path, '/')));
        }
    }

    return folders.ToOwnedSpan();
}

static ErrorCodeOr<String> UniqueTempFolder(String inside_folder, u64& seed, ArenaAllocator& arena) {
    DynamicArrayBounded<char, 64> name {".Floe-Temp-"};
    auto const chars_added = fmt::IntToString(RandomU64(seed),
                                              name.data + name.size,
                                              {.base = fmt::IntToStringOptions::Base::Hexadecimal});
    name.size += chars_added;
    auto const result = path::Join(arena, Array {inside_folder, name});
    TRY(CreateDirectory(result,
                        {
                            .create_intermediate_directories = false,
                            .fail_if_exists = true,
                            .win32_hide_dirs_starting_with_dot = true,
                        }));
    return result;
}

} // namespace detail

struct PackageReader {
    mz_zip_archive zip;
    u64 seed = SeedFromTime();
};

PUBLIC ValueOrError<PackageReader, ReaderError> ReaderCreate(Reader& reader) {
    using H = TryHelpersReader;
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    zip.m_pRead = [](void* io_opaque_ptr, mz_uint64 file_offset, void* buffer, usize buffer_size) -> usize {
        auto& reader = *(Reader*)io_opaque_ptr;
        reader.pos = file_offset;
        auto const o = reader.Read(buffer, buffer_size);
        if (o.HasError()) return 0;
        return o.Value();
    };
    zip.m_pIO_opaque = &reader;

    if (!mz_zip_reader_init(&zip, reader.size, 0))
        return ReaderError {ErrorCode {PackageError::FileCorrupted}, ""_s};

    usize known_subdirs = 0;
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        auto const file_stat = TRY_H(detail::FileStat(zip, file_index));
        auto const path = FromNullTerminated(file_stat.m_filename);
        g_debug_log.Debug({}, "File in zip: {}", path);
        for (auto const known_subdir : Array {k_libraries_subdir, k_presets_subdir}) {
            if (path == known_subdir || detail::RelativePathIfInFolder(path, known_subdir)) {
                ++known_subdirs;
                break;
            }
        }
    }

    if (!known_subdirs) {
        mz_zip_reader_end(&zip);
        return ReaderError {ErrorCode {PackageError::NotFloePackage},
                            "Doesn't contain Libraries or Presets subfolders"_s};
    }

    return PackageReader {zip};
}

PUBLIC void ReaderDestroy(PackageReader& package) { mz_zip_reader_end(&package.zip); }

PUBLIC ErrorCodeOr<Span<String>> ReaderFindLibraryDirs(PackageReader& package, ArenaAllocator& arena) {
    return detail::SubfoldersOfFolder(package.zip, k_libraries_subdir, arena);
}

PUBLIC ErrorCodeOr<Span<String>> ReaderFindPresetDirs(PackageReader& package, ArenaAllocator& arena) {
    return detail::SubfoldersOfFolder(package.zip, k_presets_subdir, arena);
}

PUBLIC ValueOrError<sample_lib::Library*, ReaderError>
ReaderReadLibraryLua(PackageReader& package, String library_dir_in_zip, ArenaAllocator& scratch_arena) {
    using H = TryHelpersReader;
    auto const floe_lua_stat = TRY(detail::FindFloeLuaInZipInLibrary(package.zip, library_dir_in_zip));
    auto const floe_lua_data = TRY_H(detail::ExtractFileToMem(package.zip, floe_lua_stat, scratch_arena));

    auto lua_reader = ::Reader::FromMemory(floe_lua_data);
    auto const lib_outcome = sample_lib::ReadLua(lua_reader,
                                                 FromNullTerminated(floe_lua_stat.m_filename),
                                                 scratch_arena,
                                                 scratch_arena);
    if (lib_outcome.HasError()) return ReaderError {PackageError::NotFloePackage, "floe.lua file is invalid"};
    return lib_outcome.ReleaseValue();
}

PUBLIC ErrorCodeOr<HashTable<String, ChecksumValues>>
ReaderChecksumValuesForDir(PackageReader& package, String dir_in_zip, ArenaAllocator& arena) {
    DynamicHashTable<String, ChecksumValues> table {arena};
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat = TRY(detail::FileStat(package.zip, file_index));
        if (file_stat.m_is_directory) continue;
        auto const path = FromNullTerminated(file_stat.m_filename);
        auto const relative_path = detail::RelativePathIfInFolder(path, dir_in_zip);
        if (!relative_path) continue;
        if (*relative_path == k_checksums_file) continue;
        table.Insert(arena.Clone(*relative_path),
                     ChecksumValues {(u32)file_stat.m_crc32, file_stat.m_uncomp_size});
    }
    return table.ToOwnedTable();
}

namespace detail {

static ErrorCodeOr<void> ExtractFolder(PackageReader& package,
                                       String dir_in_zip,
                                       String destination_folder,
                                       ArenaAllocator& scratch_arena) {
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat = TRY(detail::FileStat(package.zip, file_index));
        if (file_stat.m_is_directory) continue;
        auto const path = FromNullTerminated(file_stat.m_filename);
        auto const relative_path = detail::RelativePathIfInFolder(path, dir_in_zip);
        if (!relative_path) continue;

        auto const out_path = path::Join(scratch_arena, Array {destination_folder, *relative_path});
        DEFER { scratch_arena.Free(out_path.ToByteSpan()); };
        TRY(CreateDirectory(*path::Directory(out_path),
                            {
                                .create_intermediate_directories = true,
                                .fail_if_exists = false,
                            }));
        g_debug_log.Debug({}, "Extracting {} to {}", path, out_path);
        auto out_file = TRY(OpenFile(out_path, FileMode::Write));
        TRY(detail::ExtractFileToFile(package.zip, file_stat, out_file));
    }

    {
        auto const checksum_values = TRY(ReaderChecksumValuesForDir(package, dir_in_zip, scratch_arena));
        auto const checksum_file_path =
            path::Join(scratch_arena, Array {destination_folder, k_checksums_file});
        TRY(CreateDirectory(*path::Directory(checksum_file_path),
                            {
                                .create_intermediate_directories = true,
                                .fail_if_exists = false,
                            }));
        TRY(WriteChecksumsValuesToFile(checksum_file_path,
                                       checksum_values,
                                       scratch_arena,
                                       "Generated by Floe"));
    }

    return k_success;
}

} // namespace detail

// The resulting directory will be destination_folder/dir_in_zip.
// Extracts to a temp folder than then renames to the final location. This ensures we either fail or succeed,
// with no inbetween cases where the folder is partially extracted. Additionally, it doesn't generate lots of
// filesystem-change notifications which Floe might try to process and fail on.
PUBLIC ErrorCodeOr<void> ReaderExtractFolder(PackageReader& package,
                                             String dir_in_zip,
                                             String destination_folder,
                                             ArenaAllocator& scratch_arena) {
    // We don't use the system temp folder because we want to do an atomic rename to the final location which
    // only works if 2 folders are on the same filesystem. By using a temp folder in the same location as the
    // destination folder we ensure that.
    auto const temp_folder =
        TRY(detail::UniqueTempFolder(*path::Directory(destination_folder), package.seed, scratch_arena));
    DEFER {
        auto _ = Delete(temp_folder,
                        {
                            .type = DeleteOptions::Type::DirectoryRecursively,
                            .fail_if_not_exists = false,
                        });
    };
    TRY(detail::ExtractFolder(package, dir_in_zip, temp_folder, scratch_arena));

    auto const destination_folder_path =
        path::Join(scratch_arena, Array {destination_folder, path::Filename(dir_in_zip)});
    TRY(Rename(temp_folder, destination_folder_path));
    TRY(WindowsSetFileAttributes(destination_folder_path, k_nullopt)); // remove hidden

    return k_success;
}

} // namespace package
