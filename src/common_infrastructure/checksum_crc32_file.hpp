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

    template <typename OptT>
    static ErrorCodeOr<typename OptT::ValueType> UnwrapOrError(OptT opt) {
        if (!opt) return ErrorCode {CommonError::InvalidFileFormat};
        return opt.Value();
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

        return nullopt;
    }

    String const file_data;
    Optional<usize> cursor = 0uz;
};
