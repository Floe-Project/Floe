// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <miniz.h>

#include "foundation/foundation.hpp"
#include "foundation/utils/path.hpp"
#include "os/filesystem.hpp"
#include "utils/debug/debug.hpp"

#include "checksum_crc32_file.hpp"
#include "miniz_zip.h"
#include "sample_library/sample_library.hpp"

namespace package {

constexpr String k_libraries_subdir = "Libraries";
constexpr String k_presets_subdir = "Presets";
constexpr auto k_folders = Array {k_libraries_subdir, k_presets_subdir};
constexpr String k_file_extension = ".floe.zip"_s;
constexpr String k_checksums_file = "Floe-Details/checksums.crc32"_s;
enum class SubfolderType : u8 { Libraries, Presets, Count };

PUBLIC bool IsPathPackageFile(String path) { return EndsWithSpan(path, k_file_extension); }

enum class PackageError {
    FileCorrupted,
    NotFloePackage,
    InvalidLibrary,
    AccessDenied,
    FilesystemError,
    NotEmpty,
};
PUBLIC ErrorCodeCategory const& PackageErrorCodeType() {
    static constexpr ErrorCodeCategory const k_cat {
        .category_id = "PK",
        .message =
            [](Writer const& writer, ErrorCode e) {
                return writer.WriteChars(({
                    String s {};
                    switch ((PackageError)e.code) {
                        case PackageError::FileCorrupted: s = "Package file is corrupted"_s; break;
                        case PackageError::NotFloePackage: s = "Not a valid Floe package"_s; break;
                        case PackageError::InvalidLibrary: s = "Library is invalid"_s; break;
                        case PackageError::AccessDenied: s = "Access denied"_s; break;
                        case PackageError::FilesystemError: s = "Filesystem error"_s; break;
                        case PackageError::NotEmpty: s = "Directory not empty"_s; break;
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
    auto it = TRY(dir_iterator::RecursiveCreate(scratch_arena,
                                                folder,
                                                {
                                                    .wildcard = "*",
                                                    .get_file_size = false,
                                                    .skip_dot_files = true,
                                                }));
    DEFER { dir_iterator::Destroy(it); };
    ArenaAllocator inner_arena {PageAllocator::Instance()};
    while (auto entry = TRY(dir_iterator::Next(it, scratch_arena))) {
        inner_arena.ResetCursorAndConsolidateRegions();

        DynamicArray<char> archive_path {inner_arena};
        path::JoinAppend(archive_path, subdirs_in_zip);
        path::JoinAppend(archive_path, entry->subpath);
        if (entry->type == FileType::File) {
            auto const file_data =
                TRY(ReadEntireFile(dir_iterator::FullPath(it, *entry, inner_arena), inner_arena))
                    .ToByteSpan();
            WriterAddFile(zip, archive_path, file_data);
        }
    }

    return k_success;
}

static Optional<String> RelativePathIfInFolder(String path, String folder) {
    folder = TrimEndIfMatches(folder, '/');
    if (path.size <= folder.size) return k_nullopt;
    if (path[folder.size] != '/') return k_nullopt;
    if (!StartsWithSpan(path, folder)) return k_nullopt;
    auto result = path.SubSpan(folder.size + 1);
    result = TrimEndIfMatches(result, '/');
    return result;
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

struct PackageReader {
    Reader& zip_file_reader;
    mz_zip_archive zip {};
    u64 seed = SeedFromTime();
    Optional<ErrorCode> zip_file_read_error {};
};

namespace detail {

ErrorCode ZipReadError(PackageReader const& package) {
    if (package.zip_file_read_error) return *package.zip_file_read_error;
    return ErrorCode {PackageError::FileCorrupted};
}

static ErrorCodeOr<mz_zip_archive_file_stat> FileStat(PackageReader& package, mz_uint file_index) {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&package.zip, file_index, &file_stat)) return ZipReadError(package);
    return file_stat;
}

static String PathWithoutTrailingSlash(char const* path) {
    return TrimEndIfMatches(FromNullTerminated(path), '/');
}

static ErrorCodeOr<Optional<mz_zip_archive_file_stat>> FindFloeLuaInZipInLibrary(PackageReader& package,
                                                                                 String library_dir_in_zip) {
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat = TRY(FileStat(package, file_index));
        if (file_stat.m_is_directory) continue;
        auto const path = PathWithoutTrailingSlash(file_stat.m_filename);
        if (!sample_lib::FilenameIsFloeLuaFile(path::Filename(path, path::Format::Posix))) continue;
        auto const parent = path::Directory(path, path::Format::Posix);
        if (!parent) continue;
        if (*parent != library_dir_in_zip) continue;

        return Optional<mz_zip_archive_file_stat>(file_stat);
    }
    return Optional<mz_zip_archive_file_stat>(k_nullopt);
}

static ErrorCodeOr<Span<u8 const>>
ExtractFileToMem(PackageReader& package, mz_zip_archive_file_stat const& file_stat, ArenaAllocator& arena) {
    auto const data = arena.AllocateExactSizeUninitialised<u8>(file_stat.m_uncomp_size);
    if (!mz_zip_reader_extract_to_mem(&package.zip, file_stat.m_file_index, data.data, data.size, 0))
        return ZipReadError(package);
    return data;
}

static ErrorCodeOr<void>
ExtractFileToFile(PackageReader& package, mz_zip_archive_file_stat const& file_stat, File& out_file) {
    struct Context {
        File& out_file;
        ErrorCodeOr<void> result = k_success;
    };
    Context context {out_file};
    if (!mz_zip_reader_extract_to_callback(
            &package.zip,
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
        return ZipReadError(package);
    }
    return k_success;
}

static ErrorCodeOr<sample_lib::Library*>
ReaderReadLibraryLua(PackageReader& package, String library_dir_in_zip, ArenaAllocator& arena) {
    auto const floe_lua_stat = TRY(detail::FindFloeLuaInZipInLibrary(package, library_dir_in_zip));
    if (!floe_lua_stat) return (sample_lib::Library*)nullptr;
    auto const floe_lua_data = TRY(detail::ExtractFileToMem(package, *floe_lua_stat, arena));

    auto lua_reader = ::Reader::FromMemory(floe_lua_data);
    auto const lib_outcome =
        sample_lib::ReadLua(lua_reader, PathWithoutTrailingSlash(floe_lua_stat->m_filename), arena, arena);
    if (lib_outcome.HasError()) return ErrorCode {PackageError::InvalidLibrary};
    return lib_outcome.ReleaseValue();
}

static ErrorCodeOr<HashTable<String, ChecksumValues>>
ReaderChecksumValuesForDir(PackageReader& package, String dir_in_zip, ArenaAllocator& arena) {
    DynamicHashTable<String, ChecksumValues> table {arena};
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat = TRY(detail::FileStat(package, file_index));
        if (file_stat.m_is_directory) continue;
        auto const path = PathWithoutTrailingSlash(file_stat.m_filename);
        auto const relative_path = detail::RelativePathIfInFolder(path, dir_in_zip);
        if (!relative_path) continue;
        if (*relative_path == k_checksums_file) continue;
        table.Insert(arena.Clone(*relative_path),
                     ChecksumValues {(u32)file_stat.m_crc32, file_stat.m_uncomp_size});
    }
    return table.ToOwnedTable();
}

static ErrorCodeOr<void> ExtractFolder(PackageReader& package,
                                       String dir_in_zip,
                                       String destination_folder,
                                       ArenaAllocator& scratch_arena,
                                       HashTable<String, ChecksumValues> destination_checksums) {
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat = TRY(detail::FileStat(package, file_index));
        if (file_stat.m_is_directory) continue;
        auto const path = PathWithoutTrailingSlash(file_stat.m_filename);
        auto const relative_path = detail::RelativePathIfInFolder(path, dir_in_zip);
        if (!relative_path) continue;

        auto const out_path = path::Join(scratch_arena, Array {destination_folder, *relative_path});
        DEFER { scratch_arena.Free(out_path.ToByteSpan()); };
        TRY(CreateDirectory(*path::Directory(out_path),
                            {
                                .create_intermediate_directories = true,
                                .fail_if_exists = false,
                            }));
        auto out_file = TRY(OpenFile(out_path, FileMode::WriteNoOverwrite));
        TRY(detail::ExtractFileToFile(package, file_stat, out_file));
    }

    {
        auto const checksum_file_path =
            path::Join(scratch_arena, Array {destination_folder, k_checksums_file});
        TRY(CreateDirectory(*path::Directory(checksum_file_path),
                            {
                                .create_intermediate_directories = true,
                                .fail_if_exists = false,
                            }));
        TRY(WriteChecksumsValuesToFile(checksum_file_path,
                                       destination_checksums,
                                       scratch_arena,
                                       "Generated by Floe"));
    }

    return k_success;
}

template <typename... Args>
static PackageError CreatePackageError(Logger& error_log, ErrorCode error, Args const&... args) {
    auto const package_error = ({
        PackageError e;
        if (error.category == &PackageErrorCodeType())
            e = (PackageError)error.code;
        else if (error == FilesystemError::AccessDenied)
            e = PackageError::AccessDenied;
        else if (error == FilesystemError::NotEmpty)
            e = PackageError::NotEmpty;
        else
            e = PackageError::FilesystemError;
        e;
    });

    DynamicArrayBounded<char, 1000> error_buffer;
    fmt::Append(error_buffer, "{u}", ErrorCode {package_error});
    if (error != package_error) fmt::Append(error_buffer, ". {u}", error);
    if constexpr (sizeof...(args) > 0) {
        dyn::AppendSpan(error_buffer, ": ");
        fmt::Append(error_buffer, args...);
        dyn::Append(error_buffer, '.');
    } else
        fmt::Append(error_buffer, ".");

    String possible_fix {};
    switch (package_error) {
        case PackageError::FileCorrupted: possible_fix = "Try redownloading the package"; break;
        case PackageError::NotFloePackage: possible_fix = "Make sure the file is a Floe package"; break;
        case PackageError::InvalidLibrary: possible_fix = "Contact the developer"; break;
        case PackageError::AccessDenied: possible_fix = "Install the package manually"; break;
        case PackageError::FilesystemError: possible_fix = "Try again"; break;
        case PackageError::NotEmpty: possible_fix = {}; break;
    }
    if (possible_fix.size) fmt::Append(error_buffer, " {}.", possible_fix);

    error_log.Error({}, error_buffer);

    return package_error;
}

static ErrorCodeOr<String> ResolvePossibleFilenameConflicts(String path, ArenaAllocator& arena) {
    if (auto const o = GetFileType(path); o.HasError() && o.Error() == FilesystemError::PathDoesNotExist)
        return path;

    constexpr usize k_max_suffix_number = 999;
    constexpr usize k_max_suffix_str_size = " (999)"_s.size;

    auto buffer = arena.AllocateExactSizeUninitialised<char>(path.size + k_max_suffix_str_size);
    usize pos = 0;
    WriteAndIncrement(pos, buffer, path);
    WriteAndIncrement(pos, buffer, " ("_s);

    for (usize suffix_num = 1; suffix_num <= k_max_suffix_number; ++suffix_num) {
        usize initial_pos = pos;
        DEFER { pos = initial_pos; };
        pos +=
            fmt::IntToString(suffix_num, buffer.data + pos, {.base = fmt::IntToStringOptions::Base::Decimal});
        WriteAndIncrement(pos, buffer, ')');

        if (auto const o = GetFileType({buffer.data, pos});
            o.HasError() && o.Error() == FilesystemError::PathDoesNotExist)
            return arena.ResizeType(buffer, pos, pos);
    }

    return ErrorCode {FilesystemError::FolderContainsTooManyFiles};
}

} // namespace detail

PUBLIC VoidOrError<PackageError> ReaderInit(PackageReader& package, Logger& error_log) {
    mz_zip_zero_struct(&package.zip);
    package.zip.m_pRead =
        [](void* io_opaque_ptr, mz_uint64 file_offset, void* buffer, usize buffer_size) -> usize {
        auto& package = *(PackageReader*)io_opaque_ptr;
        package.zip_file_reader.pos = file_offset;
        auto const o = package.zip_file_reader.Read(buffer, buffer_size);
        if (o.HasError()) {
            package.zip_file_read_error = o.Error();
            return 0;
        }
        return o.Value();
    };
    package.zip.m_pIO_opaque = &package;

    if (!mz_zip_reader_init(&package.zip, package.zip_file_reader.size, 0))
        return detail::CreatePackageError(error_log, detail::ZipReadError(package));

    usize known_subdirs = 0;
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat = ({
            auto const o = detail::FileStat(package, file_index);
            if (o.HasError()) return detail::CreatePackageError(error_log, o.Error());
            o.Value();
        });
        auto const path = detail::PathWithoutTrailingSlash(file_stat.m_filename);
        for (auto const known_subdir : Array {k_libraries_subdir, k_presets_subdir}) {
            if (path == known_subdir || detail::RelativePathIfInFolder(path, known_subdir)) {
                ++known_subdirs;
                break;
            }
        }
    }

    if (!known_subdirs) {
        mz_zip_reader_end(&package.zip);
        return detail::CreatePackageError(error_log,
                                          ErrorCode {PackageError::NotFloePackage},
                                          "it doesn't contain Libraries or Presets subfolders");
    }

    return k_success;
}

PUBLIC void ReaderDeinit(PackageReader& package) { mz_zip_reader_end(&package.zip); }

struct PackageFolder {
    String path; // path in the zip
    SubfolderType type;
    sample_lib::Library* library; // can be null
    HashTable<String, ChecksumValues> checksum_values;
};

// init to 0
using PackageFolderIteratorIndex = mz_uint;

// Call this repeatedly until it returns nullopt
PUBLIC ValueOrError<Optional<PackageFolder>, PackageError>
IteratePackageFolders(PackageReader& package,
                      PackageFolderIteratorIndex& file_index,
                      ArenaAllocator& arena,
                      Logger& error_log) {
    DEFER { ++file_index; };
    for (; file_index < mz_zip_reader_get_num_files(&package.zip); ++file_index) {
        auto const file_stat = ({
            auto const o = detail::FileStat(package, file_index);
            if (o.HasError()) return detail::CreatePackageError(error_log, o.Error());
            o.Value();
        });
        auto const path = detail::PathWithoutTrailingSlash(file_stat.m_filename);
        for (auto const folder : k_folders) {
            auto const relative_path = detail::RelativePathIfInFolder(path, folder);
            if (!relative_path) continue;
            if (relative_path->size == 0) continue;
            if (Contains(*relative_path, '/')) continue;

            return Optional<PackageFolder> {PackageFolder {
                .path = path.Clone(arena),
                .type = ({
                    SubfolderType t;
                    if (folder == k_libraries_subdir)
                        t = SubfolderType::Libraries;
                    else if (folder == k_presets_subdir)
                        t = SubfolderType::Presets;
                    else
                        PanicIfReached();
                    t;
                }),
                .library = ({
                    sample_lib::Library* lib = nullptr;
                    if (folder == k_libraries_subdir) {
                        lib = ({
                            auto const o = detail::ReaderReadLibraryLua(package, path, arena);
                            if (o.HasError()) return detail::CreatePackageError(error_log, o.Error());
                            o.Value();
                        });
                    }
                    lib;
                }),
                .checksum_values = ({
                    auto const o = detail::ReaderChecksumValuesForDir(package, path, arena);
                    if (o.HasError()) return detail::CreatePackageError(error_log, o.Error());
                    o.Value();
                }),
            }};
        }
    }
    return Optional<PackageFolder> {k_nullopt};
}

enum class DestinationType {
    FullPath, // install all files inside the PackageFolder into a specific folder
    DefaultFolderWithSubfolderFromPackage,
};

using Destination = TaggedUnion<DestinationType, TypeAndTag<String, DestinationType::FullPath>>;

struct ExtractOptions {
    Destination destination = DestinationType::DefaultFolderWithSubfolderFromPackage;
    bool overwrite_existing_files;
    bool resolve_install_folder_name_conflicts;
};

// Destination folder is the folder where the package will be installed. e.g. /home/me/Libraries
// The final folder name is determined by options.destination.
// Extracts to a temp folder than then renames to the final location. This ensures we either fail or succeed,
// with no in-between cases where the folder is partially extracted. Additionally, it doesn't generate lots of
// filesystem-change notifications which Floe might try to process and fail on.
PUBLIC VoidOrError<PackageError> ReaderExtractFolder(PackageReader& package,
                                                     PackageFolder const& folder,
                                                     String default_destination_folder,
                                                     ArenaAllocator& scratch_arena,
                                                     Logger& error_log,
                                                     ExtractOptions options) {
    auto const destination_folder = ({
        String f {};
        switch (options.destination.tag) {
            case DestinationType::FullPath: f = options.destination.Get<String>(); break;
            case DestinationType::DefaultFolderWithSubfolderFromPackage: {
                f = path::Join(scratch_arena,
                               Array {default_destination_folder, path::Filename(folder.path)});
                break;
            }
        }

        if (options.resolve_install_folder_name_conflicts) {
            auto const o = detail::ResolvePossibleFilenameConflicts(f, scratch_arena);
            if (o.HasError())
                return detail::CreatePackageError(error_log,
                                                  o.Error(),
                                                  "couldn't access destination folder: {}",
                                                  f);
            f = o.Value();
        }

        f;
    });

    // Try to get a folder on the same filesystem so that we can atomic-rename and therefore reduce the chance
    // of leaving partially extracted files and generating lots of filesystem-change events.
    auto const temp_folder = ({
        ASSERT(GetFileType(default_destination_folder).HasValue());
        // try getting an 'official' temp folder
        auto o = TemporaryDirectoryOnSameFilesystemAs(default_destination_folder, scratch_arena);
        if (o.HasError()) {
            // fallback to a folder within the destination folder
            o = TemporaryDirectoryWithinFolder(default_destination_folder, scratch_arena, package.seed);
            if (o.HasError())
                return detail::CreatePackageError(error_log,
                                                  o.Error(),
                                                  "couldn't access destination folder: {}",
                                                  default_destination_folder);
        }
        String(o.Value());
    });
    DEFER {
        auto _ = Delete(temp_folder,
                        {
                            .type = DeleteOptions::Type::DirectoryRecursively,
                            .fail_if_not_exists = false,
                        });
    };

    if (auto const o =
            detail::ExtractFolder(package, folder.path, temp_folder, scratch_arena, folder.checksum_values);
        o.HasError()) {
        return detail::CreatePackageError(error_log, o.Error(), "in folder: {}", temp_folder);
    }

    if (auto const rename_o = Rename(temp_folder, destination_folder); rename_o.HasError()) {
        if (options.overwrite_existing_files && rename_o.Error() == FilesystemError::NotEmpty) {
            for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
                auto const file_stat = ({
                    auto const o = detail::FileStat(package, file_index);
                    if (o.HasError()) return detail::CreatePackageError(error_log, o.Error());
                    o.Value();
                });
                if (file_stat.m_is_directory) continue;
                auto const path = detail::PathWithoutTrailingSlash(file_stat.m_filename);
                auto const relative_path = detail::RelativePathIfInFolder(path, folder.path);
                if (!relative_path) continue;

                auto const to_path = path::Join(scratch_arena, Array {destination_folder, *relative_path});
                DEFER { scratch_arena.Free(to_path.ToByteSpan()); };
                if (auto const o = CreateDirectory(*path::Directory(to_path),
                                                   {
                                                       .create_intermediate_directories = true,
                                                       .fail_if_exists = false,
                                                   });
                    o.HasError()) {
                    return detail::CreatePackageError(
                        error_log,
                        o.Error(),
                        "could'nt create directory(s) in your install folder: {}",
                        to_path);
                }

                auto const from_path = path::Join(scratch_arena, Array {temp_folder, *relative_path});
                DEFER { scratch_arena.Free(from_path.ToByteSpan()); };
                if (auto const copy_o = Rename(from_path, to_path); copy_o.HasError()) {
                    return detail::CreatePackageError(error_log,
                                                      copy_o.Error(),
                                                      "couldn't install file to your install folder: {}",
                                                      to_path);
                }
            }
        } else {
            return detail::CreatePackageError(error_log,
                                              rename_o.Error(),
                                              "couldn't install files to your install folder: {}",
                                              destination_folder);
        }
    }

    // remove hidden
    if (auto const o = WindowsSetFileAttributes(destination_folder, k_nullopt); o.HasError()) {
        return detail::CreatePackageError(error_log,
                                          o.Error(),
                                          "failed to make the folder visible: {}",
                                          destination_folder);
    }

    return k_success;
}

struct ExistingInstalltionInfo {
    enum class ModifiedSinceInstalled {
        Unmodified,
        Modified,
        Unknown,
    };
    enum class VersionComparison {
        Equal,
        PackageIsNewer,
        PackageIsOlder,
    };

    ModifiedSinceInstalled modified_since_installed;
    VersionComparison version_comparison;
};

enum class InstallationStatusType {
    NotInstalled,
    AlreadyInstalled,
    InstalledButDifferent,
};

using InstallationStatus =
    TaggedUnion<InstallationStatusType,
                TypeAndTag<ExistingInstalltionInfo, InstallationStatusType::InstalledButDifferent>>;

enum class RecommendedAction {
    Install,
    InstallAndOverwriteWithoutAsking,
    DoNothing,
    AskUser,
};

struct FolderCheckResult {
    InstallationStatus installation_status;
    RecommendedAction recommended_action;
    ExtractOptions extract_options;
};

PUBLIC ValueOrError<FolderCheckResult, PackageError>
LibraryCheckExistingInstallation(PackageFolder const& folder,
                                 sample_lib::Library const* existing_matching_library,
                                 ArenaAllocator& scratch_arena,
                                 Logger& error_log) {
    ASSERT(folder.type == SubfolderType::Libraries);
    ASSERT(folder.library);

    if (!existing_matching_library)
        return FolderCheckResult {
            .installation_status = InstallationStatusType::NotInstalled,
            .recommended_action = RecommendedAction::Install,
            .extract_options =
                {
                    .destination = DestinationType::DefaultFolderWithSubfolderFromPackage,
                    .overwrite_existing_files = false,
                    .resolve_install_folder_name_conflicts = true,
                },
        };

    auto const existing_folder = *path::Directory(existing_matching_library->path);
    ASSERT(existing_matching_library->Id() == folder.library->Id());

    auto const actual_checksums = ({
        auto const o = ChecksumsForFolder(existing_folder, scratch_arena, scratch_arena);
        if (o.HasError())
            return detail::CreatePackageError(error_log, o.Error(), "folder: {}", existing_folder);
        o.Value();
    });

    if (!ChecksumsDiffer(folder.checksum_values, actual_checksums, nullptr))
        return FolderCheckResult {
            .installation_status = InstallationStatusType::AlreadyInstalled,
            .recommended_action = RecommendedAction::DoNothing,
            .extract_options = {},
        };

    // The installed version DIFFERS from the package version.
    // HOW it differs will effect the recommendation we give to the user.

    ExistingInstalltionInfo existing {};
    auto const checksum_file_path = path::Join(scratch_arena, Array {existing_folder, k_checksums_file});
    if (auto const o = ReadEntireFile(checksum_file_path, scratch_arena); !o.HasError()) {
        auto const stored_checksums = ParseChecksumFile(o.Value(), scratch_arena);
        if (stored_checksums.HasValue() &&
            !ChecksumsDiffer(stored_checksums.Value(), actual_checksums, &error_log)) {
            existing.modified_since_installed = ExistingInstalltionInfo::ModifiedSinceInstalled::Unmodified;
        } else {
            // The library has been modified since it was installed. OR the checksums file is badly formatted,
            // which presumably means it was modified.
            existing.modified_since_installed = ExistingInstalltionInfo::ModifiedSinceInstalled::Modified;
        }
    } else {
        existing.modified_since_installed = ExistingInstalltionInfo::ModifiedSinceInstalled::Unknown;
    }

    if (folder.library->minor_version > existing_matching_library->minor_version)
        existing.version_comparison = ExistingInstalltionInfo::VersionComparison::PackageIsNewer;
    else if (folder.library->minor_version < existing_matching_library->minor_version)
        existing.version_comparison = ExistingInstalltionInfo::VersionComparison::PackageIsOlder;
    else
        existing.version_comparison = ExistingInstalltionInfo::VersionComparison::Equal;

    RecommendedAction recommended_action;
    switch (existing.modified_since_installed) {
        case ExistingInstalltionInfo::ModifiedSinceInstalled::Unmodified: {
            if (existing.version_comparison == ExistingInstalltionInfo::VersionComparison::PackageIsNewer)
                recommended_action = RecommendedAction::InstallAndOverwriteWithoutAsking; // safe update
            else
                recommended_action = RecommendedAction::DoNothing;
            break;
        }
        case ExistingInstalltionInfo::ModifiedSinceInstalled::Modified: {
            recommended_action = RecommendedAction::AskUser;
            break;
        }
        case ExistingInstalltionInfo::ModifiedSinceInstalled::Unknown: {
            recommended_action = RecommendedAction::AskUser;
            break;
        }
    }

    return FolderCheckResult {
        .installation_status = existing,
        .recommended_action = recommended_action,
        .extract_options =
            {
                .destination = existing_folder,
                .overwrite_existing_files = true,
                .resolve_install_folder_name_conflicts = false,
            },
    };
}

PUBLIC ValueOrError<FolderCheckResult, PackageError>
PresetsCheckExistingInstallation(PackageFolder const& package_folder,
                                 Span<String const> presets_folders,
                                 ArenaAllocator& scratch_arena,
                                 Logger& error_log) {
    for (auto const folder : presets_folders) {
        auto const entries = ({
            auto const o = AllEntriesRecursive(scratch_arena,
                                               folder,
                                               k_nullopt,
                                               {
                                                   .wildcard = "*",
                                                   .get_file_size = true,
                                                   .skip_dot_files = true,
                                               });
            if (o.HasError()) return detail::CreatePackageError(error_log, o.Error(), "folder: {}", folder);
            o.Value();
        });

        if constexpr (IS_WINDOWS)
            for (auto& entry : entries)
                Replace(entry.subpath, '\\', '/');

        for (auto const dir_entry : entries) {
            if (dir_entry.type != FileType::Directory) continue;

            bool dir_contains_all_expected_files = true;
            for (auto const [expected_path, checksum] : package_folder.checksum_values) {
                bool found_expected = false;
                for (auto const file_entry : entries) {
                    if (file_entry.type != FileType::File) continue;
                    auto const relative =
                        detail::RelativePathIfInFolder(file_entry.subpath, dir_entry.subpath);
                    if (!relative) continue;
                    if (path::Equal(*relative, expected_path)) {
                        found_expected = true;
                        break;
                    }
                }
                if (!found_expected) {
                    dir_contains_all_expected_files = false;
                    break;
                }
            }

            if (dir_contains_all_expected_files) {
                bool matches_exactly = true;

                // check the checksums of all files
                for (auto const [expected_path, checksum] : package_folder.checksum_values) {
                    auto const cursor = scratch_arena.TotalUsed();
                    DEFER { scratch_arena.TryShrinkTotalUsed(cursor); };

                    auto const full_path =
                        path::Join(scratch_arena, Array {folder, dir_entry.subpath, expected_path});

                    auto const matches_file = ({
                        auto const o = FileMatchesChecksum(full_path, *checksum, scratch_arena);
                        if (o.HasError())
                            return detail::CreatePackageError(error_log, o.Error(), "file: {}", full_path);
                        o.Value();
                    });

                    if (!matches_file) {
                        matches_exactly = false;
                        break;
                    }
                }

                if (matches_exactly)
                    return FolderCheckResult {
                        .installation_status = InstallationStatusType::AlreadyInstalled,
                        .recommended_action = RecommendedAction::DoNothing,
                        .extract_options = {},
                    };
            }
        }
    }

    return FolderCheckResult {
        .installation_status = InstallationStatusType::NotInstalled,
        .recommended_action = RecommendedAction::Install,
        .extract_options =
            {
                .destination = DestinationType::DefaultFolderWithSubfolderFromPackage,
                .overwrite_existing_files = false,
                .resolve_install_folder_name_conflicts = true,
            },
    };
}

} // namespace package
