// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/array.hpp"
#include "foundation/container/span.hpp"
#include "foundation/error/error_code.hpp"
#include "foundation/universal_defs.hpp"
#include "foundation/utils/memory.hpp"

struct Writer {
    template <typename ObjectType>
    constexpr void Set(ObjectType& obj,
                       ErrorCodeOr<void> (*write_bytes)(ObjectType& obj, Span<u8 const> bytes)) {
        object = (void*)&obj;
        write_bytes_function_ptr = (void*)write_bytes;
        invoke_write_bytes = [](void* func_ptr, void* ob, Span<u8 const> bytes) -> ErrorCodeOr<void> {
            return ((decltype(write_bytes))func_ptr)(*(ObjectType*)ob, bytes);
        };
    }

    template <typename UserData>
    constexpr void SetContained(UserData user_data,
                                ErrorCodeOr<void> (*write_bytes)(UserData user_data, Span<u8 const> bytes)) {
        object = (void*)(uintptr)user_data;
        write_bytes_function_ptr = (void*)write_bytes;
        invoke_write_bytes = [](void* func_ptr, void* ob, Span<u8 const> bytes) -> ErrorCodeOr<void> {
            return ((decltype(write_bytes))func_ptr)((UserData)(uintptr)ob, bytes);
        };
    }

    ErrorCodeOr<void> WriteByte(u8 byte) const { return WriteBytes({&byte, 1}); }
    ErrorCodeOr<void> WriteBytes(Span<u8 const> bytes) const {
        return invoke_write_bytes(write_bytes_function_ptr, object, bytes);
    }
    ErrorCodeOr<void> WriteChar(char c) const { return WriteByte((u8)c); }
    ErrorCodeOr<void> WriteChars(Span<char const> cs) const { return WriteBytes(cs.ToByteSpan()); }

    ErrorCodeOr<void> WriteCharRepeated(char c, usize count) const {
        Array<u8, 32> bytes = {};
        FillMemory(bytes, (u8)c);

        usize remaining = count;
        while (remaining > 0) {
            auto const to_write = Min(remaining, bytes.size);
            TRY(WriteBytes(Span<u8 const> {bytes}.SubSpan(0, to_write)));
            remaining -= to_write;
        }
        return k_success;
    }

    ErrorCodeOr<void> (*invoke_write_bytes)(void* func_ptr, void* object, Span<u8 const> bytes) = {};
    void* write_bytes_function_ptr = {};
    void* object = {};
    usize bytes_written = {};
};

struct BufferedData {
    ::Writer Writer() {
        ::Writer result;
        result.Set<BufferedData>(*this, [](BufferedData& self, Span<u8 const> bytes) -> ErrorCodeOr<void> {
            // if we can't fit the bytes in the buffer, flush the buffer and try again
            if (bytes.size > self.buffer.size - self.pos) {
                auto const to_write = self.buffer.SubSpan(0, self.pos);
                TRY(self.sub_writer.WriteBytes(to_write));
                self.pos = 0;
            }

            // if the bytes are still too big, write them directly
            if (bytes.size > self.buffer.size) return self.sub_writer.WriteBytes(bytes);

            // otherwise, write them to the buffer
            auto const to_write = bytes.SubSpan(0, self.buffer.size - self.pos);
            CopyMemory(self.buffer.SubSpan(self.pos), to_write);

            return k_success;
        });
        return result;
    }

    ErrorCodeOr<void> Flush() {
        if (pos == 0) return k_success;
        auto const to_write = buffer.SubSpan(0, pos);
        TRY(sub_writer.WriteBytes(to_write));
        pos = 0;
        return k_success;
    }

    ::Writer sub_writer;
    Span<u8> buffer;
    usize pos = 0;
};
