// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/memory/allocators.hpp"
#include "foundation/universal_defs.hpp"

// https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/

template <TriviallyCopyable Type>
struct CircularBuffer {
    NON_COPYABLE(CircularBuffer);

    constexpr CircularBuffer(Allocator& a, u32 initial_capacity = 0) : a(a) { Reserve(initial_capacity); }

    constexpr CircularBuffer(CircularBuffer&& other) : a(other.a) {
        Swap(buffer, other.buffer);
        Swap(read, other.read);
        Swap(write, other.write);
    }
    constexpr CircularBuffer& operator=(CircularBuffer&& other) {
        Clear();

        // IMPROVE: do this more efficiently; it doesn't have to be done one-by-one
        while (!other.Empty())
            Push(other.Pop());

        return *this;
    }

    constexpr ~CircularBuffer() {
        if (buffer.size) a.Free(buffer.ToByteSpan());
    }

    constexpr void Reserve(u32 size) {
        if (size <= buffer.size) return;
        ASSERT(Full() || Empty());

        auto const initial_size = Size();
        auto const next_size = NextPowerOf2(Max(size, 8u));
        auto const initial_read = Mask(read);

        auto data = a.Reallocate<Type>(next_size, buffer.ToByteSpan(), initial_size, false);
        buffer = {(Type*)(void*)data.data, next_size};

        if (initial_read) __builtin_memcpy(&buffer[initial_size], &buffer[0], initial_size * sizeof(Type));

        read = initial_read;
        write = initial_read + initial_size;

        ASSERT(write < buffer.size);
    }

    constexpr bool Empty() const { return read == write; }
    constexpr bool Full() const { return Size() == buffer.size; }
    constexpr u32 Size() const { return write - read; }
    constexpr void Push(Type val) {
        Reserve(Size() + 1);
        ASSERT(!Full());
        __builtin_memcpy(&buffer[Mask(write++)], &val, sizeof(Type));
    }
    constexpr Type Pop() {
        ASSERT(!Empty());
        return buffer[Mask(read++)];
    }
    constexpr Optional<Type> TryPop() {
        if (Empty()) return nullopt;
        return Pop();
    }
    constexpr void Clear() {
        read = 0;
        write = 0;
    }

    constexpr u32 Mask(u32 val) const { return val & (buffer.size - 1); }

    Allocator& a;
    Span<Type> buffer {};
    u32 read {};
    u32 write {};
};
