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
    mz_zip_archive zip;
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

static String UniqueTempFolder(String inside_folder, u64& seed, ArenaAllocator& arena) {
    DynamicArrayBounded<char, 64> name {".Floe-Temp-"};
    auto const chars_added = fmt::IntToString(RandomU64(seed),
                                              name.data + name.size,
                                              {.base = fmt::IntToStringOptions::Base::Hexadecimal});
    name.size += chars_added;
    auto const result = path::Join(arena, Array {inside_folder, name});
    return result;
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

enum class Tri { No, Yes, Unknown };

struct DestinationFolderStatus {
    bool exists_and_contains_files;
    bool exactly_matches_zip;
    Tri changed_since_originally_installed;
};

// Destination folder is the folder where the package will be installed. e.g. /home/me/Libraries
PUBLIC ErrorCodeOr<DestinationFolderStatus> CheckDestinationFolder(String destination_folder,
                                                                   PackageFolder const& folder,
                                                                   ArenaAllocator& scratch_arena,
                                                                   Logger& error_log) {
    auto const destination_folder_path =
        String(path::Join(scratch_arena, Array {destination_folder, path::Filename(folder.path)}));

    DestinationFolderStatus result {};

    auto const actual_checksums = ({
        auto const o = ChecksumsForFolder(destination_folder_path, scratch_arena, scratch_arena);
        if (o.HasError()) {
            if (o.Error() == FilesystemError::PathDoesNotExist) return result;
            error_log.Error({}, "Couldn't read folder: {}", destination_folder_path);
            return o.Error();
        }
        o.Value();
    });

    if (actual_checksums.size == 0) return result;

    result.exists_and_contains_files = true;
    result.exactly_matches_zip = !ChecksumsDiffer(folder.checksum_values, actual_checksums, error_log);
    result.changed_since_originally_installed = Tri::Unknown;

    auto const checksum_file_path =
        path::Join(scratch_arena, Array {destination_folder_path, k_checksums_file});
    if (auto const o = ReadEntireFile(checksum_file_path, scratch_arena); !o.HasError()) {
        auto const file_data = o.Value();
        auto const stored_checksums = ParseChecksumFile(file_data, scratch_arena);
        if (stored_checksums.HasValue()) {
            result.changed_since_originally_installed =
                ChecksumsDiffer(stored_checksums.Value(), actual_checksums, error_log) ? Tri::Yes : Tri::No;
        } else {
            // presumably we wouldn't have installed a badly formatted checksums file, so let's say it's
            // changed and perhaps we will overwrite it
            result.changed_since_originally_installed = Tri::Yes;
        }
    }

    return result;
}

// The resulting directory will be destination_folder/dir_in_zip.
// Extracts to a temp folder than then renames to the final location. This ensures we either fail or succeed,
// with no in-between cases where the folder is partially extracted. Additionally, it doesn't generate lots of
// filesystem-change notifications which Floe might try to process and fail on.
PUBLIC VoidOrError<PackageError> ReaderExtractFolder(PackageReader& package,
                                                     PackageFolder const& folder,
                                                     String destination_folder,
                                                     ArenaAllocator& scratch_arena,
                                                     Logger& error_log,
                                                     bool overwrite_existing_files) {
    // We don't use the system temp folder because we want to do an atomic rename to the final location which
    // only works if 2 folders are on the same filesystem. By using a temp folder in the same location as the
    // destination folder we ensure that.
    auto const temp_folder =
        detail::UniqueTempFolder(*path::Directory(destination_folder), package.seed, scratch_arena);
    if (auto const o = CreateDirectory(temp_folder,
                                       {
                                           .create_intermediate_directories = false,
                                           .fail_if_exists = true,
                                           .win32_hide_dirs_starting_with_dot = true,
                                       });
        o.HasError()) {
        return detail::CreatePackageError(error_log, o.Error(), "folder: {}", temp_folder);
    }
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

    auto const destination_folder_path =
        path::Join(scratch_arena, Array {destination_folder, path::Filename(folder.path)});

    if (auto const rename_o = Rename(temp_folder, destination_folder_path); rename_o.HasError()) {
        if (overwrite_existing_files && rename_o.Error() == FilesystemError::NotEmpty) {
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
                        "cound't create directory(s) in your install folder: {}",
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
                                              destination_folder_path);
        }
    }

    // remove hidden
    if (auto const o = WindowsSetFileAttributes(destination_folder_path, k_nullopt); o.HasError()) {
        return detail::CreatePackageError(error_log,
                                          o.Error(),
                                          "failed to make the folder visible: {}",
                                          destination_folder_path);
    }

    return k_success;
}

} // namespace package
