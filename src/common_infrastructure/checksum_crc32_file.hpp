// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <miniz.h>

#include "foundation/container/hash_table.hpp"
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/logger/logger.hpp"

#include "common_errors.hpp"

struct ChecksumLine {
    String path;
    u32 crc32;
    usize file_size;
};

// similar format to unix cksum - except cksum uses a different crc algorithm
PUBLIC void AppendChecksumLine(DynamicArray<char>& buffer, ChecksumLine line) {
    if constexpr (IS_WINDOWS)
        for (auto c : line.path)
            ASSERT(c != '\\'); // zip files use forward slashes

    fmt::Append(buffer, "{08x} {} {}\n", line.crc32, line.file_size, line.path);
}

PUBLIC void AppendCommentLine(DynamicArray<char>& buffer, String comment) {
    fmt::Append(buffer, "; {}\n", comment);
}

// A parser for the checksum file format
struct ChecksumFileParser {
    static String CutStart(String& whole, usize size) {
        auto const result = whole.SubSpan(0, size);
        whole = whole.SubSpan(size);
        return result;
    }

    ErrorCodeOr<Optional<ChecksumLine>> ReadLine() {
        while (cursor) {
            auto line = SplitWithIterator(file_data, cursor, '\n');

            if (line.size == 0) continue;
            if (StartsWith(line, ';')) continue;

            auto const crc = TRY_UNWRAP_OPTIONAL(ParseIntTrimString(line, ParseIntBase::Hexadecimal, false),
                                                 ErrorCode {CommonError::InvalidFileFormat});

            if (!StartsWith(line, ' ')) return ErrorCode {CommonError::InvalidFileFormat};
            line.RemovePrefix(1);

            auto const file_size = TRY_UNWRAP_OPTIONAL(ParseIntTrimString(line, ParseIntBase::Decimal, false),
                                                       ErrorCode {CommonError::InvalidFileFormat});

            if (!StartsWith(line, ' ')) return ErrorCode {CommonError::InvalidFileFormat};
            line.RemovePrefix(1);
            auto const path = line;

            return ChecksumLine {
                .path = path,
                .crc32 = (u32)crc,
                .file_size = (usize)file_size,
            };
        }

        return k_nullopt;
    }

    String const file_data;
    Optional<usize> cursor = 0uz;
};

struct ChecksumValues {
    u32 crc32;
    usize file_size;
};

PUBLIC u32 Crc32(Span<u8 const> data) {
    return CheckedCast<u32>(mz_crc32(MZ_CRC32_INIT, data.data, data.size));
}

PUBLIC ErrorCodeOr<HashTable<String, ChecksumValues>> ParseChecksumFile(String checksum_file_data,
                                                                        ArenaAllocator& scratch_arena) {
    DynamicHashTable<String, ChecksumValues> checksum_values(scratch_arena);
    ChecksumFileParser parser {checksum_file_data};
    while (auto line = TRY(parser.ReadLine()))
        checksum_values.Insert(line->path, ChecksumValues {line->crc32, line->file_size});
    return checksum_values.ToOwnedTable();
}

PUBLIC ErrorCodeOr<bool> FolderDiffersFromChecksumValues(String folder_path,
                                                         HashTable<String, ChecksumValues> checksum_values,
                                                         ArenaAllocator& scratch_arena) {
    auto checksum_values_found = Set<String>::Create(scratch_arena, checksum_values.Capacity());

    auto it = TRY(RecursiveDirectoryIterator::Create(scratch_arena,
                                                     folder_path,
                                                     {
                                                         .wildcard = "*",
                                                         .get_file_size = true,
                                                         .skip_dot_files = false,
                                                     }));

    while (it.HasMoreFiles()) {
        auto const& entry = it.Get();

        auto relative_path = entry.path.Items().SubSpan(it.CanonicalBasePath().size + 1);
        if constexpr (IS_WINDOWS) {
            // zip files use forward slashes
            auto archive_path = scratch_arena.Clone(relative_path);
            Replace(archive_path, '\\', '/');
            relative_path = archive_path;
        }

        if (auto element = checksum_values.FindElement(relative_path)) {
            auto last_checksum = &element->data;
            if (last_checksum->file_size != entry.file_size) {
                g_debug_log.Debug({},
                                  "File size mismatch: {} {} {}",
                                  relative_path,
                                  last_checksum->file_size,
                                  entry.file_size);
                return true;
            }
            auto const file_data = TRY(ReadEntireFile(entry.path, scratch_arena)).ToByteSpan();
            if (auto crc = Crc32(file_data); crc != last_checksum->crc32) {
                g_debug_log.Debug({},
                                  "Checksum mismatch: {} {08x} {08x}",
                                  relative_path,
                                  last_checksum->crc32,
                                  crc);
                return true;
            }
            scratch_arena.Free(file_data);
            checksum_values_found.InsertWithoutGrowing(element->key);
        }

        TRY(it.Increment());
    }

    // check if there's any missing files
    for (auto const& [path, checksum] : checksum_values)
        if (!checksum_values_found.Contains(path)) {
            g_debug_log.Debug({}, "Missing file: {}", path);
            return true;
        }

    return false;
}
