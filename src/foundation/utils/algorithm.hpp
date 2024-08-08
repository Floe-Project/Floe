// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/contiguous.hpp"
#include "foundation/container/optional.hpp"
#include "foundation/container/span.hpp"
#include "foundation/universal_defs.hpp"

PUBLIC constexpr u32 U32FromChars(char const (&data)[5]) {
    return ((u32)(data[0]) << 0) | ((u32)(data[1]) << 8) | ((u32)(data[2]) << 16) | ((u32)(data[3]) << 24);
}

PUBLIC constexpr u64 U64FromChars(char const (&data)[9]) {
    return ((u64)(data[0]) << 0) | ((u64)(data[1]) << 8) | ((u64)(data[2]) << 16) | ((u64)(data[3]) << 24) |
           ((u64)(data[4]) << 32) | ((u64)(data[5]) << 40) | ((u64)(data[6]) << 48) | ((u64)(data[7]) << 56);
}

template <Fundamental T>
PUBLIC constexpr u64 HashFnv1a(Span<T const> data) {
    // FNV-1a https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1a_hash
    u64 hash = 0xcbf29ce484222325;
    for (auto& byte : data.ToByteSpan()) {
        hash ^= byte;
        hash *= 0x100000001b3;
    }
    return hash;
}

template <Fundamental T>
PUBLIC constexpr u64 HashMultipleFnv1a(Span<Span<T const> const> datas) {
    // FNV-1a https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1a_hash
    u64 hash = 0xcbf29ce484222325;
    for (auto data : datas) {
        for (auto& byte : data.ToByteSpan()) {
            hash ^= byte;
            hash *= 0x100000001b3;
        }
    }
    return hash;
}

template <Fundamental T>
PUBLIC constexpr u32 HashDbj(Span<T const> data) {
    // Dbj
    u32 hash = 5381;
    for (auto& byte : data.ToByteSpan())
        hash = ((hash << 5) + hash) + (u32)byte;
    return hash;
}

template <Fundamental T>
PUBLIC constexpr u32 HashMultipleDbj(Span<Span<T const> const> datas) {
    // Dbj
    u32 hash = 5381;
    for (auto data : datas)
        for (auto& byte : data.ToByteSpan())
            hash = ((hash << 5) + hash) + (u32)byte;
    return hash;
}

PUBLIC constexpr u64 Hash(auto data) { return HashFnv1a(data); }
PUBLIC constexpr u32 Hash32(auto data) { return HashDbj(data); }
PUBLIC constexpr u64 HashMultiple(auto data) { return HashMultipleFnv1a(data); }
PUBLIC constexpr u32 HashMultiple32(auto data) { return HashMultipleDbj(data); }

template <typename T>
PUBLIC constexpr auto Begin(T& c) -> decltype(c.begin()) {
    return c.begin();
}

template <typename T>
PUBLIC constexpr auto Begin(T const& c) -> decltype(c.begin()) {
    return c.begin();
}

template <typename T, usize N>
PUBLIC constexpr T* Begin(T (&array)[N]) {
    return &array[0];
}

template <typename T>
PUBLIC constexpr auto End(T& c) -> decltype(c.end()) {
    return c.end();
}

template <typename T>
PUBLIC constexpr auto End(T const& c) -> decltype(c.end()) {
    return c.end();
}

template <typename T, usize N>
PUBLIC constexpr T* End(T (&array)[N]) {
    return &array[N];
}

PUBLIC constexpr auto& Last(auto&& data) {
    ASSERT(data.size);
    return data.data[data.size - 1];
}

// Divides array into two partitions
PUBLIC constexpr int Partition(auto* arr, int lo, int hi, auto less_than_function) {
    auto const& pivot = arr[hi];

    auto i = lo - 1;

    for (auto j = lo; j < hi; ++j) {
        if (!less_than_function(pivot, arr[j])) {
            ++i;
            Swap(arr[i], arr[j]);
        }
    }

    ++i;
    Swap(arr[i], arr[hi]);
    return i;
}

// QSort implemented from Wikipedia pseudocode https://en.wikipedia.org/wiki/Quicksort
// Sorts a (portion of an) array, divides it into partitions, then sorts those
PUBLIC constexpr void QSort(auto* arr, int lo, int hi, auto less_than_function) {
    if (lo >= hi || lo < 0) return;

    auto const p = Partition(arr, lo, hi, less_than_function);
    QSort(arr, lo, p - 1, less_than_function); // Note: the pivot is now included
    QSort(arr, p + 1, hi, less_than_function);
}

PUBLIC constexpr void Sort(auto* data, usize size, auto less_than_function) {
    QSort(data, 0, (int)size - 1, less_than_function);
}

template <typename Type>
PUBLIC constexpr bool DefaultLessThanFunction(Type const& a, Type const& b) {
    return a < b;
}

// LessThanFunction has signature: bool cmp(const Type &a, const Type &b);
PUBLIC constexpr void Sort(ContiguousContainer auto const& data, auto less_than_function) {
    Sort(data.data, data.size, less_than_function);
}
PUBLIC constexpr auto& Sort(ContiguousContainer auto& data, auto less_than_function) {
    using ValueType = RemoveCV<typename RemoveReference<decltype(data)>::ValueType>;
    Sort((ValueType*)data.data, data.size, less_than_function);
    return data;
}

PUBLIC constexpr void Sort(ContiguousContainer auto const& data) {
    using ValueType = RemoveCV<typename RemoveReference<decltype(data)>::ValueType>;
    Sort(data, DefaultLessThanFunction<ValueType>);
}
PUBLIC constexpr auto& Sort(ContiguousContainer auto& data) {
    using ValueType = RemoveCV<typename RemoveReference<decltype(data)>::ValueType>;
    Sort(data, DefaultLessThanFunction<ValueType>);
    return data;
}

template <typename Type, usize N>
requires(N != 1)
PUBLIC constexpr void Sort(Type (&array)[N], auto less_than_function) {
    QSort(array, 0, (int)N - 1, less_than_function);
}
template <typename Type, usize N>
requires(N != 1)
PUBLIC constexpr void Sort(Type (&array)[N]) {
    QSort(array, 0, (int)N - 1, DefaultLessThanFunction<Type>);
}

// compare_to_target follows the same requirements as stdlib bsearch():
// [target](const Type &item) {
//     if (item == target) return 0;
//     if (item < target) return -1;
//     return 1;
// }
// The span must be sorted before calling this.
PUBLIC constexpr Optional<usize> FindBinarySearch(ContiguousContainer auto const& data,
                                                  auto&& compare_to_target) {
    ssize left = 0;
    ssize right = (ssize)data.size - 1;
    while (left <= right) {
        auto const mid = left + (right - left) / 2;
        auto const comp = compare_to_target(data[CheckedCast<usize>(mid)]);

        if (comp == 0)
            return (usize)mid;
        else if (comp < 0)
            left = mid + 1;
        else
            right = mid - 1;
    }

    return {};
}

// Same as FindBinarySearch, except it returns the index that you should use to insert a new element in
// order to maintain sorted order
PUBLIC constexpr usize BinarySearchForSlotToInsert(ContiguousContainer auto const& data,
                                                   auto&& compare_to_target) {
    ssize left = 0;
    ssize right = (ssize)data.size - 1;
    while (left <= right) {
        auto const mid = left + (right - left) / 2;
        auto const comp = compare_to_target(data[CheckedCast<usize>(mid)]);

        if (comp == 0)
            return (usize)mid;
        else if (comp < 0)
            left = mid + 1;
        else
            right = mid - 1;
    }

    return (usize)left;
}

PUBLIC constexpr usize CountIf(auto& data, auto&& predicate) {
    usize count = 0;
    for (auto const& v : data)
        if (predicate(v)) ++count;
    return count;
}

PUBLIC constexpr usize Count(auto& data, auto const& v) {
    usize count = 0;
    for (auto const& i : data)
        if (i == v) ++count;
    return count;
}

// LessThanFunction has signature: bool cmp(const Type &a, const Type &b);
PUBLIC constexpr auto LargestElement(ContiguousContainer auto const& data, auto&& less_than) {
    auto begin = Begin(data);
    auto end = End(data);
    if (begin == end) return begin;

    auto largest = begin;
    ++begin;
    for (; begin != end; ++begin)
        if (less_than(*largest, *begin)) largest = begin;
    return largest;
}

PUBLIC constexpr auto&
Replace(ContiguousContainer auto& data, auto const& existing_value, auto const& replacement) {
    for (auto& v : data)
        if (v == existing_value) v = replacement;
    return data;
}

PUBLIC constexpr bool StartsWithSpan(ContiguousContainer auto const& data,
                                     ContiguousContainer auto const& possible_prefix) {
    if (possible_prefix.size > data.size) return false;
    if (possible_prefix.size == 0 || data.size == 0) return false;
    for (auto const i : Range(possible_prefix.size))
        if (data[i] != possible_prefix[i]) return false;
    return true;
}

PUBLIC constexpr bool StartsWith(ContiguousContainer auto const& data, auto&& v) {
    return data.size ? data[0] == v : false;
}

PUBLIC constexpr bool StartsWithAnyOfCharacters(ContiguousContainer auto const& data,
                                                ContiguousContainer auto const& possible_first_items) {
    if (data.size == 0 || possible_first_items.size == 0) return false;
    for (auto const& i : possible_first_items)
        if (data[0] == i) return true;
    return false;
}

PUBLIC constexpr bool EndsWithSpan(ContiguousContainer auto const& data,
                                   ContiguousContainer auto const& possible_suffix) {
    if (possible_suffix.size > data.size) return false;
    if (possible_suffix.size == 0 || data.size == 0) return false;
    auto actual_suffix = data.data + (data.size - possible_suffix.size);
    ASSERT(actual_suffix + possible_suffix.size == (data.data + data.size));
    for (auto const i : Range(possible_suffix.size))
        if (actual_suffix[i] != possible_suffix[i]) return false;
    return true;
}

PUBLIC constexpr bool EndsWith(ContiguousContainer auto const& data, auto&& v) {
    return data.size ? data[data.size - 1] == v : false;
}

PUBLIC constexpr bool Contains(ContiguousContainer auto const& data, auto&& v) {
    for (auto const& i : data)
        if (i == v) return true;
    return false;
}

PUBLIC constexpr bool ContainsOnly(ContiguousContainer auto const& data, auto&& v) {
    if (data.size == 0) return false;
    for (auto i : data)
        if (i != v) return false;
    return true;
}

PUBLIC constexpr Optional<usize> FindLast(ContiguousContainer auto const& data, auto&& search_item) {
    for (usize i = data.size - 1; i != usize(-1); --i)
        if (data[i] == search_item) return i;
    return nullopt;
}

PUBLIC constexpr Optional<usize>
Find(ContiguousContainer auto const& data, auto const& search_item, usize start = 0) {
    for (usize i = start; i < data.size; ++i)
        if (data[i] == search_item) return i;
    return nullopt;
}

PUBLIC constexpr Optional<usize>
FindSpan(ContiguousContainer auto const& haystack, ContiguousContainer auto const& needle, usize start = 0) {
    if (start >= haystack.size) return nullopt;
    auto const haystack_size = haystack.size - start;
    if (needle.size > haystack_size) return nullopt;
    if (needle.size == 0) return nullopt;
    ASSERT(haystack_size != 0);
    auto const* haystack_data = haystack.data + start;
    for (auto const pos : Range((haystack_size - needle.size) + 1)) {
        auto const* test_ptr = haystack_data + pos;
        bool matches = true;
        for (auto const i : Range(needle.size)) {
            if (test_ptr[i] != needle[i]) {
                matches = false;
                break;
            }
        }
        if (matches) return pos + start;
    }
    return nullopt;
}

PUBLIC constexpr Optional<usize>
FindIf(ContiguousContainer auto const& data, auto&& item_is_desired, usize start = 0) {
    for (usize i = start; i < data.size; ++i)
        if (item_is_desired(data[i])) return i;
    return {};
}

PUBLIC constexpr Optional<usize> FindLastIf(ContiguousContainer auto const& data, auto&& item_is_desired) {
    for (usize i = data.size - 1; i != usize(-1); --i)
        if (item_is_desired(data[i])) return i;
    return nullopt;
}

PUBLIC constexpr bool ContainsSpan(ContiguousContainer auto const& haystack,
                                   ContiguousContainer auto const& needle) {
    return FindSpan(haystack, needle).HasValue();
}

PUBLIC constexpr bool ContainsPointer(ContiguousContainer auto const& data, auto const* ptr) {
    if (data.size == 0) return false;
    return ptr >= data.data && ptr < (data.data + data.size);
}

PUBLIC constexpr bool operator<(ContiguousContainer auto const& lhs, ContiguousContainer auto const& rhs) {
    auto first1 = Begin(lhs);
    auto last1 = End(lhs);
    auto first2 = Begin(rhs);
    auto last2 = End(rhs);

    for (; (first1 != last1) && (first2 != last2); ++first1, ++first2) {
        if (*first1 < *first2) return true;
        if (*first2 < *first1) return false;
    }
    return (first1 == last1) && (first2 != last2);
}

template <ContiguousContainer SpanType, usize N>
requires(CharacterType<typename SpanType::ValueType>)
PUBLIC constexpr bool operator==(SpanType const& span,
                                 typename SpanType::ValueType const (&string_literal)[N]) {
    if (span.size != (N - 1)) return false;
    for (auto const i : Range(span.size))
        if (span[i] != string_literal[i]) return false;
    return true;
}

template <ContiguousContainer TypeA, ContiguousContainerSimilarTo<TypeA> TypeB>
PUBLIC constexpr bool operator==(TypeA const& a, TypeB const& b) {
    if (a.size != b.size) return false;
    for (auto const i : Range(a.size))
        if (a[i] != b[i]) return false;
    return true;
}

template <ContiguousContainer TypeA, ContiguousContainerSimilarTo<TypeA> TypeB>
PUBLIC constexpr bool operator!=(TypeA const& a, TypeB const& b) {
    if (a.size != b.size) return true;
    for (auto const i : Range(a.size))
        if (a[i] != b[i]) return true;
    return false;
}
