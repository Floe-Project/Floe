#pragma once
#include "foundation/foundation.hpp"

#include "common_errors.hpp"

struct ChecksumLine {
    String path;
    u32 crc32;
    usize file_size;
};

PUBLIC void AppendChecksumLine(DynamicArray<char>& buffer, ChecksumLine line) {
    fmt::Append(buffer, "{08x} {} {}\n", line.crc32, line.file_size, line.path);
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

            usize crc_num_chars = 0;
            auto const opt_crc = ParseInt(line, ParseIntBase::Hexadecimal, &crc_num_chars);
            if (!opt_crc) return ErrorCode {CommonError::InvalidFileFormat};

            line.RemovePrefix(crc_num_chars);
            if (line.size == 0) return ErrorCode {CommonError::InvalidFileFormat};
            line.RemovePrefix(1); // remove space
            if (line.size == 0) return ErrorCode {CommonError::InvalidFileFormat};

            usize file_size_num_chars = 0;
            auto const opt_file_size = ParseInt(line, ParseIntBase::Decimal, &file_size_num_chars);
            if (!opt_file_size) return ErrorCode {CommonError::InvalidFileFormat};

            line.RemovePrefix(file_size_num_chars);
            if (line.size == 0) return ErrorCode {CommonError::InvalidFileFormat};
            line.RemovePrefix(1); // remove space
            if (line.size == 0) return ErrorCode {CommonError::InvalidFileFormat};

            return ChecksumLine {
                .path = line,
                .crc32 = (u32)*opt_crc,
                .file_size = (usize)*opt_file_size,
            };
        }

        return nullopt;
    }

    String const file_data;
    Optional<usize> cursor = 0uz;
};
