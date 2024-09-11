// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"
#include "tests/framework.hpp"
#include "utils/leak_detecting_allocator.hpp"

constexpr auto k_foundation_mod_cat = "foundation"_log_module;

TEST_CASE(TestTaggedUnion) {
    enum class E {
        A,
        B,
        C,
        D,
    };
    using TU = TaggedUnion<E, TypeAndTag<int, E::A>, TypeAndTag<float, E::B>, TypeAndTag<String, E::C>>;

    TU u {int {}};

    SUBCASE("visit") {
        u = 999;
        u.Visit([&](auto const& arg) {
            tester.log.Debug(k_foundation_mod_cat, "Tagged union value is: {}", arg);
        });

        u = 3.14f;
        u.Visit([&](auto const& arg) {
            tester.log.Debug(k_foundation_mod_cat, "Tagged union value is: {}", arg);
        });

        u = E::D;
        u.Visit([&](auto const&) {
            tester.log.Debug(k_foundation_mod_cat, "ERROR not expected a tag without a type to be called");
        });

        u = "hello"_s;
        u.Visit([&](auto const& arg) {
            tester.log.Debug(k_foundation_mod_cat, "Tagged union value is: {}", arg);
        });

        tester.log.Debug({}, "Formatting a tagged union: {}", u);
    }

    SUBCASE("format") {
        u = "hello"_s;
        tester.log.Debug({}, "Formatting a tagged union: {}", u);
    }

    SUBCASE("comparison") {
        u = "hello"_s;
        CHECK(u == TU {"hello"_s});
        CHECK(u != TU {3.14f});
        CHECK(u != TU {E::D});

        u = E::D;
        CHECK(u == TU {E::D});
        CHECK(u != TU {3.14f});
    }

    return k_success;
}

TEST_CASE(TestBitset) {
    {
        Bitset<65> b;
        REQUIRE(!b.AnyValuesSet());
        b.Set(0);
        REQUIRE(b.Get(0));

        b <<= 1;
        REQUIRE(b.Get(1));
        REQUIRE(!b.Get(0));

        b >>= 1;
        REQUIRE(b.Get(0));
        REQUIRE(b.AnyValuesSet());
        b.ClearAll();
        REQUIRE(!b.AnyValuesSet());

        b.SetToValue(5, true);
        auto smaller_bitset = b.Subsection<10>(0);
        REQUIRE(smaller_bitset.Get(5));

        b.ClearAll();

        Bitset<65> other;
        other.SetAll();
        b = other;
        REQUIRE(b.AnyValuesSet());
        b = ~b;
        REQUIRE(!b.AnyValuesSet());

        other.ClearAll();
        other.Set(64);
        b |= other;
        REQUIRE(b.Get(64));
        REQUIRE(other.Get(64));

        other.ClearAll();
        b &= other;
        REQUIRE(!b.AnyValuesSet());

        b.ClearAll();
        REQUIRE(b.NumSet() == 0);
        b.Set(0);
        b.Set(64);
        REQUIRE(b.NumSet() == 2);
    }

    {
        Bitset<8> const b(0b00101010);
        REQUIRE(b.Subsection<3>(2).elements[0] == 0b010);
    }

    {
        Bitset<8> const b(0b11110000);
        REQUIRE(!b.Get(0));
        REQUIRE(b.Get(7));
        REQUIRE(b.Subsection<4>(4).elements[0] == 0b1111);
    }

    {
        Bitset<8> const b(0b00100100);
        REQUIRE(b.Subsection<4>(2).elements[0] == 0b1001);
    }

    {
        Bitset<128> b {};
        for (usize i = 64; i < 128; ++i)
            b.Set(i);
        REQUIRE(b.NumSet() == 64);

        auto const sub = b.Subsection<10>(60);
        REQUIRE(sub.Get(0) == 0);
        REQUIRE(sub.Get(1) == 0);
        REQUIRE(sub.Get(2) == 0);
        REQUIRE(sub.Get(3) == 0);
        REQUIRE(sub.Get(4) != 0);

        auto const sub2 = b.Subsection<64>(64);
        REQUIRE(sub2.NumSet() == 64);
    }
    return k_success;
}

TEST_CASE(TestCircularBuffer) {
    LeakDetectingAllocator allocator;
    CircularBuffer<int> buf {allocator};

    SUBCASE("basics") {
        CHECK(buf.Empty());
        CHECK(buf.Full());
        CHECK(buf.Size() == 0);

        for (auto _ : Range(2)) {
            buf.Push(1);
            CHECK(!buf.Empty());
            CHECK(!buf.Full());
            CHECK(buf.Size() == 1);

            CHECK_EQ(buf.Pop(), 1);
            CHECK(buf.Empty());
            CHECK(!buf.Full());
            CHECK(buf.Size() == 0);
        }

        CHECK(IsPowerOfTwo(buf.buffer.size));
    }

    SUBCASE("push elements") {
        for (auto pre_pushes : Array {10, 11, 13, 50, 100, 9}) {
            CAPTURE(pre_pushes);
            for (auto const i : Range(pre_pushes))
                buf.Push(i);
            for (auto _ : Range(pre_pushes))
                buf.Pop();

            for (auto const i : Range(100))
                buf.Push(i);
            for (auto const i : Range(100))
                CHECK_EQ(buf.Pop(), i);
        }

        for (auto const i : Range(10000))
            buf.Push(i);
        for (auto const i : Range(10000))
            CHECK_EQ(buf.Pop(), i);
    }

    SUBCASE("clear") {
        for (auto const i : Range(32))
            buf.Push(i);
        buf.Clear();
        CHECK(buf.Empty());
        CHECK(!buf.TryPop().HasValue());
    }

    SUBCASE("move assign") {
        SUBCASE("both empty") {
            CircularBuffer<int> buf2 {allocator};
            buf = Move(buf2);
        }
        SUBCASE("new is full") {
            CircularBuffer<int> buf2 {allocator};
            for (auto const i : Range(32))
                buf2.Push(i);
            SUBCASE("old is full") {
                for (auto const i : Range(32))
                    buf.Push(i);
            }
            buf = Move(buf2);
            CHECK(buf.Size() == 32);
            for (auto const i : Range(32))
                CHECK_EQ(buf.Pop(), i);
        }
    }

    SUBCASE("move construct") {
        SUBCASE("empty") { CircularBuffer<int> const buf2 = Move(buf); }
        SUBCASE("full") {
            for (auto const i : Range(32))
                buf.Push(i);
            CircularBuffer<int> const buf2 = Move(buf);
        }
    }

    return k_success;
}

TEST_CASE(TestCircularBufferRefType) {
    LeakDetectingAllocator allocator;
    {
        struct Foo {
            int& i;
        };

        CircularBuffer<Foo> buf {allocator};

        int i = 66;
        Foo const foo {i};
        buf.Push(foo);
        auto result = buf.Pop();
        CHECK(&result.i == &i);
    }

    {
        Array<u16, 5000> bytes;
        for (auto [i, b] : Enumerate<u16>(bytes))
            b = i;

        struct Foo {
            u16& i;
        };
        CircularBuffer<Foo> buf {allocator};

        u16 warmup {};
        for (auto _ : Range(51))
            buf.Push({warmup});
        for (auto _ : Range(51))
            CHECK(&buf.Pop().i == &warmup);

        for (auto& b : bytes)
            buf.Push({b});

        for (auto& b : bytes)
            CHECK(&buf.Pop().i == &b);
    }

    {
        CircularBuffer<int> buf {PageAllocator::Instance()};

        int push_counter = 0;
        int pop_counter = 0;
        for (auto _ : Range(10000)) {
            auto update = RandomIntInRange<int>(tester.random_seed, -8, 8);
            if (update < 0) {
                while (update != 0) {
                    if (auto v = buf.TryPop()) REQUIRE_EQ(v, pop_counter++);
                    ++update;
                }
            } else {
                while (update != 0) {
                    buf.Push(push_counter++);
                    --update;
                }
            }
        }
    }

    return k_success;
}

TEST_CASE(TestDynamicArrayChar) {
    LeakDetectingAllocator a1;
    auto& a2 = Malloc::Instance();
    Allocator* allocators[] = {&a1, &a2};

    for (auto a_ptr : allocators) {
        auto& a = *a_ptr;
        SUBCASE("initialisation and assignment") {
            DynamicArray<char> s1(String("hello there"), a);
            DynamicArray<char> s2("hello there", a);
            DynamicArray<char> const s3(a);
            DynamicArray<char> const s4 {Malloc::Instance()};

            DynamicArray<char> const move_constructed(Move(s2));
            REQUIRE(move_constructed == "hello there"_s);

            DynamicArray<char> const move_assigned = Move(s1);
            REQUIRE(move_assigned == "hello there"_s);
        }

        SUBCASE("modify contents") {
            DynamicArray<char> s {a};
            dyn::AppendSpan(s, "aa"_s);
            REQUIRE(s.size == 2);
            REQUIRE(s == "aa"_s);
            dyn::Append(s, 'f');
            REQUIRE(s.size == 3);
            REQUIRE(s == "aaf"_s);
            dyn::PrependSpan(s, "bb"_s);
            REQUIRE(s.size == 5);
            REQUIRE(s == "bbaaf"_s);
            dyn::Prepend(s, 'c');
            REQUIRE(s == "cbbaaf"_s);

            dyn::Clear(s);
            REQUIRE(s.size == 0);

            dyn::Assign(s, "3000000"_s);
            dyn::Assign(s, "3"_s);
            REQUIRE(NullTerminatedSize(dyn::NullTerminated(s)) == s.size);
        }

        SUBCASE("iterators") {
            DynamicArray<char> const s {"hey", a};
            char const chars[] = {'h', 'e', 'y'};
            int index = 0;
            for (auto c : s)
                REQUIRE(c == chars[index++]);
        }
    }
    return k_success;
}

TEST_CASE(TestWriter) {
    SUBCASE("alloced") {
        LeakDetectingAllocator a;
        DynamicArray<char> buf {a};
        auto writer = dyn::WriterFor(buf);
        TRY(writer.WriteBytes(Array {(u8)'a'}));
        CHECK_EQ(buf.Items(), "a"_s);
    }

    SUBCASE("inline") {
        DynamicArrayBounded<char, 128> buf {};
        auto writer = dyn::WriterFor(buf);
        TRY(writer.WriteBytes(Array {(u8)'a'}));
        CHECK_EQ(buf.Items(), "a"_s);
    }
    return k_success;
}

TEST_CASE(TestDynamicArrayClone) {
    LeakDetectingAllocator a;

    SUBCASE("deep") {
        auto& arr_alloc = Malloc::Instance();

        DynamicArray<DynamicArray<String>> arr {arr_alloc};
        DynamicArray<String> const strs {arr_alloc};

        dyn::Append(arr, strs.Clone(a, CloneType::Deep));
        dyn::Append(arr, strs.Clone(a, CloneType::Deep));
        dyn::Prepend(arr, strs.Clone(a, CloneType::Deep));
        dyn::Insert(arr, 1, strs.Clone(a, CloneType::Deep));
        dyn::Remove(arr, 0);

        SUBCASE("move assigning does not change the allocator") {
            DynamicArray<DynamicArray<String>> other_arr {a};
            dyn::Append(other_arr, strs.Clone(a, CloneType::Deep));
            arr = Move(other_arr);
            REQUIRE(&arr.allocator == &arr_alloc);
            REQUIRE(&other_arr.allocator == &a);
        }
    }

    SUBCASE("shallow") {
        DynamicArray<Optional<String>> buf {a};
        dyn::Append(buf, "1"_s);
        dyn::Append(buf, "2"_s);
        dyn::Append(buf, k_nullopt);

        auto const duped = buf.Clone(a, CloneType::Shallow);
        REQUIRE(duped.size == 3);
        REQUIRE(duped[0].HasValue());
        REQUIRE(duped[0].Value() == "1"_s);
        REQUIRE(duped[1].HasValue());
        REQUIRE(duped[1].Value() == "2"_s);
        REQUIRE(!duped[2].HasValue());
    }

    return k_success;
}

TEST_CASE(TestDynamicArrayString) {
    DynamicArrayBounded<char, 64> buf;
    dyn::Assign(buf, "a   "_s);
    dyn::TrimWhitespace(buf);
    REQUIRE(buf == "a"_s);
    dyn::Assign(buf, "   a"_s);
    dyn::TrimWhitespace(buf);
    REQUIRE(buf == "a"_s);
    dyn::Assign(buf, "   a   "_s);
    dyn::TrimWhitespace(buf);
    REQUIRE(buf == "a"_s);
    return k_success;
}

TEST_CASE(TestDynamicArrayBoundedBasics) {
    SUBCASE("Basics") {
        DynamicArrayBounded<char, 10> arr {"aa"_s};
        REQUIRE(arr == "aa"_s);
        REQUIRE(arr.data);
        REQUIRE(arr.size);
        REQUIRE(*arr.data == 'a');
    }

    SUBCASE("Move") {
        DynamicArrayBounded<char, 10> a {"aa"_s};
        DynamicArrayBounded<char, 10> b {Move(a)};
        REQUIRE(b == "aa"_s);

        DynamicArrayBounded<char, 10> c {"bb"_s};
        b = Move(c);
        REQUIRE(b == "bb"_s);
    }

    SUBCASE("Overflow") {
        LeakDetectingAllocator alloc;
        DynamicArrayBounded<DynamicArray<char>, 4> arr;
        REQUIRE(dyn::Append(arr, DynamicArray<char>("foo", alloc)));
        REQUIRE(dyn::Append(arr, DynamicArray<char>("foo", alloc)));
        REQUIRE(dyn::Append(arr, DynamicArray<char>("foo", alloc)));
        REQUIRE(dyn::Append(arr, DynamicArray<char>("foo", alloc)));

        REQUIRE(!dyn::Append(arr, DynamicArray<char>("foo", alloc)));
        REQUIRE(!dyn::Insert(arr, 1, DynamicArray<char>("foo", alloc)));

        dyn::Clear(arr);

        REQUIRE(dyn::Append(arr, DynamicArray<char>("foo", alloc)));
    }
    return k_success;
}

struct AllocedString {
    AllocedString() : data() {}
    AllocedString(String d) : data(d.Clone(Malloc::Instance())) {}
    AllocedString(AllocedString const& other) : data(other.data.Clone(Malloc::Instance())) {}
    AllocedString(AllocedString&& other) : data(other.data) { other.data = {}; }
    AllocedString& operator=(AllocedString const& other) {
        if (data.size) Malloc::Instance().Free(data.ToByteSpan());
        data = other.data.Clone(Malloc::Instance());
        return *this;
    }
    AllocedString& operator=(AllocedString&& other) {
        if (data.size) Malloc::Instance().Free(data.ToByteSpan());
        data = other.data;
        other.data = {};
        return *this;
    }
    ~AllocedString() {
        if (data.size) Malloc::Instance().Free(data.ToByteSpan());
    }
    friend bool operator==(AllocedString const& a, AllocedString const& b) { return a.data == b.data; }

    String data {};
};

template <typename Type>
TEST_CASE(TestDynamicArrayBasics) {
    Malloc a1;
    FixedSizeAllocator<50> fixed_size_a;
    LeakDetectingAllocator a5;
    ArenaAllocator a2(fixed_size_a);
    ArenaAllocator a3(a5);
    FixedSizeAllocator<512> a4;
    Allocator* allocators[] = {&a1, &a2, &a3, &a4, &a5};

    for (auto a_ptr : allocators) {
        auto& a = *a_ptr;
        DynamicArray<Type> buf(a);
        auto const default_initialised = !Fundamental<Type>;

        auto check_grow_buffer_incrementally = [&]() {
            usize const max = 550;
            for (usize i = 1; i <= max; ++i) {
                dyn::Resize(buf, i);
                REQUIRE(buf.size == i);
                REQUIRE(buf.Items().size == i);
                if (default_initialised) REQUIRE(*buf.data == Type());
            }
            REQUIRE(buf.size == max);
            REQUIRE(buf.Items().size == max);
        };

        SUBCASE("Initial values") {
            REQUIRE(buf.size == 0);
            REQUIRE(buf.Items().size == 0);
        }

        SUBCASE("Reserve small") {
            buf.Reserve(10);
            REQUIRE(buf.size == 0);
            REQUIRE(buf.Items().size == 0);

            SUBCASE("Resize small") {
                dyn::Resize(buf, 1);
                REQUIRE(buf.size == 1);
                REQUIRE(buf.Items().size == 1);
                if (default_initialised) REQUIRE(*buf.data == Type());
            }

            SUBCASE("Resize incrementally") { check_grow_buffer_incrementally(); }
        }

        SUBCASE("Reserve large") {
            buf.Reserve(1000);
            REQUIRE(buf.size == 0);
            REQUIRE(buf.Items().size == 0);

            SUBCASE("Resize incrementally") { check_grow_buffer_incrementally(); }
        }

        SUBCASE("Grow incrementally") { check_grow_buffer_incrementally(); }

        SUBCASE("iterate") {
            dyn::Resize(buf, 4);
            for (auto& i : buf)
                (void)i;
            for (auto const& i : buf)
                (void)i;
        }

        if constexpr (Same<int, Type>) {
            SUBCASE("Add 10 values then resize to heap data") {
                dyn::Resize(buf, 10);
                REQUIRE(buf.size == 10);
                REQUIRE(buf.Items().size == 10);

                for (auto const i : Range(10))
                    buf.Items()[(usize)i] = i + 1;

                dyn::Resize(buf, 1000);

                for (auto const i : Range(10))
                    REQUIRE(buf.Items()[(usize)i] == i + 1);
            }

            SUBCASE("To owned span") {
                SUBCASE("with span lifetime shorter than array") {
                    dyn::Resize(buf, 10);
                    REQUIRE(buf.size == 10);

                    auto span = buf.ToOwnedSpan();
                    DEFER { a.Free(span.ToByteSpan()); };
                    REQUIRE(buf.size == 0);
                    REQUIRE(buf.Capacity() == 0);

                    REQUIRE(span.size == 10);
                }

                SUBCASE("with span lifetime longer than array") {
                    Span<int> span {};

                    {
                        DynamicArray<int> other {a};
                        dyn::Resize(other, 10);

                        span = other.ToOwnedSpan();
                        REQUIRE(other.size == 0);
                        REQUIRE(other.Capacity() == 0);
                        REQUIRE(span.size == 10);
                    }

                    a.Free(span.ToByteSpan());
                }
            }

            SUBCASE("Modify contents") {
                dyn::Append(buf, 10);
                REQUIRE(buf.size == 1);
                REQUIRE(buf[0] == 10);

                dyn::Clear(buf);
                REQUIRE(buf.size == 0);

                dyn::Append(buf, 20);
                dyn::Prepend(buf, 30);
                REQUIRE(buf.size == 2);
                REQUIRE(buf[0] == 30);
                REQUIRE(Last(buf) == 20);
                REQUIRE(buf[1] == 20);

                DynamicArray<Type> other {a};
                dyn::Append(other, 99);
                dyn::Append(other, 100);
                dyn::Append(other, 101);

                dyn::AppendSpan(buf, other.Items());
                REQUIRE(buf.size == 5);
                REQUIRE(buf[0] == 30);
                REQUIRE(buf[1] == 20);
                REQUIRE(buf[2] == 99);
                REQUIRE(buf[3] == 100);
                REQUIRE(buf[4] == 101);

                auto null_term_data = dyn::NullTerminated(buf);
                REQUIRE(buf.size == 5);
                REQUIRE(buf[0] == 30);
                REQUIRE(buf[1] == 20);
                REQUIRE(buf[2] == 99);
                REQUIRE(buf[3] == 100);
                REQUIRE(buf[4] == 101);
                REQUIRE(null_term_data[5] == 0);

                SUBCASE("RemoveValue") {
                    dyn::Assign(buf, ArrayT<int>({1, 3, 5, 1, 2, 1, 1}));
                    dyn::RemoveValue(buf, 1);
                    REQUIRE(buf.size == 3);
                    REQUIRE(buf[0] == 3);
                    REQUIRE(buf[1] == 5);
                    REQUIRE(buf[2] == 2);

                    dyn::Assign(buf, ArrayT<int>({1, 1, 1, 1}));
                    dyn::RemoveValue(buf, 1);
                    REQUIRE(buf.size == 0);
                }

                SUBCASE("RemoveSwapLast") {
                    dyn::Assign(buf, ArrayT<int>({3, 5, 6}));
                    dyn::RemoveSwapLast(buf, 0);
                    for (auto v : buf)
                        REQUIRE(v == 5 || v == 6);
                }

                SUBCASE("AppendIfNotAlreadyThere") {
                    dyn::Assign(buf, Array {3, 5, 6});
                    dyn::AppendIfNotAlreadyThere(buf, 3);
                    REQUIRE(buf.size == 3);
                    dyn::AppendIfNotAlreadyThere(buf, 4);
                    REQUIRE(buf.size == 4);
                    dyn::Clear(buf);
                    dyn::AppendIfNotAlreadyThere(buf, 1);
                    REQUIRE(buf.size);
                }
            }

            SUBCASE("Initialiser list") {
                dyn::Assign(buf, ArrayT<int>({20, 31, 50}));
                REQUIRE(buf.size == 3);
                REQUIRE(buf[0] == 20);
                REQUIRE(buf[1] == 31);
                REQUIRE(buf[2] == 50);

                DynamicArray<Type> other {a};
                dyn::Assign(other, ArrayT<Type>({999, 999}));
                REQUIRE(other.size == 2);
                REQUIRE(other[0] == 999);
                REQUIRE(other[1] == 999);

                dyn::Append(other, Type {40});
                REQUIRE(other.size == 3);
                dyn::AppendSpan(other, ArrayT<Type>({41, 42}));
                REQUIRE(other.size == 5);
            }

            SUBCASE("move") {
                SUBCASE("no reserve") { buf.Reserve(0); }
                SUBCASE("big reserve") { buf.Reserve(1000); }

                dyn::Append(buf, 10);
                dyn::Append(buf, 11);
                dyn::Append(buf, 12);
                SUBCASE("constructor") {
                    DynamicArray<Type> other(Move(buf));
                    REQUIRE(other[0] == 10);
                    REQUIRE(other[1] == 11);
                    REQUIRE(other[2] == 12);
                    REQUIRE(other.size == 3);
                }

                SUBCASE("assign operators") {
                    DynamicArray<Type> other {a};
                    SUBCASE("move") {
                        SUBCASE("existing static") {
                            dyn::Append(other, 99);
                            other = Move(buf);
                        }
                        SUBCASE("existing heap") {
                            other.Reserve(1000);
                            dyn::Append(other, 99);
                            other = Move(buf);
                        }
                    }

                    REQUIRE(other.size == 3);
                    REQUIRE(other[0] == 10);
                    REQUIRE(other[1] == 11);
                    REQUIRE(other[2] == 12);
                }

                SUBCASE("assign operator with different allocator") {
                    FixedSizeAllocator<512> other_a;
                    DynamicArray<Type> other(other_a);
                    dyn::Append(other, 99);
                    other = Move(buf);

                    REQUIRE(other.size == 3);
                    REQUIRE(other[0] == 10);
                    REQUIRE(other[1] == 11);
                    REQUIRE(other[2] == 12);
                }
            }
        }

        if constexpr (Same<AllocedString, Type>) {
            SUBCASE("Add 10 values then resize to heap data") {
                dyn::Resize(buf, 10);
                REQUIRE(buf.size == 10);
                REQUIRE(buf.Items().size == 10);

                auto make_long_string = [&tester](int i) {
                    return AllocedString(
                        fmt::Format(tester.scratch_arena, "this is a long string with a number: {}", i + 1));
                };

                for (auto const i : Range(10))
                    buf.Items()[(usize)i] = make_long_string(i);
            }
            SUBCASE("Modify contents with move") {
                AllocedString foo1 {"foo1"};
                AllocedString foo2 {"foo2"};
                AllocedString foo3 {"foo3"};

                dyn::Append(buf, Move(foo1));
                REQUIRE(buf.size == 1);
                REQUIRE(buf[0] == "foo1"_s);

                dyn::Clear(buf);
                REQUIRE(buf.size == 0);

                dyn::Append(buf, Move(foo2));
                dyn::Prepend(buf, Move(foo3));
                REQUIRE(buf.size == 2);
                REQUIRE(buf[0] == "foo3"_s);
                REQUIRE(Last(buf) == "foo2"_s);
            }

            SUBCASE("Modify contents") {
                dyn::Append(buf, "a");
                REQUIRE(buf.size == 1);
                REQUIRE(buf[0] == "a"_s);
                REQUIRE(buf[0] == "a"_s);

                dyn::Clear(buf);
                REQUIRE(buf.size == 0);

                dyn::Append(buf, "b"_s);
                dyn::Prepend(buf, "c"_s);
                REQUIRE(buf.size == 2);
                REQUIRE(buf[0] == "c"_s);
                REQUIRE(Last(buf) == "b"_s);
                REQUIRE(buf[1] == "b"_s);

                String long_string = "long string to ensure that short string optimisations are not involved";

                DynamicArray<Type> other {a};
                dyn::Append(other, "d"_s);
                dyn::Append(other, "e"_s);
                dyn::Append(other, long_string);

                dyn::AppendSpan(buf, other.Items());
                REQUIRE(buf.size == 5);
                REQUIRE(buf[0] == "c"_s);
                REQUIRE(buf[1] == "b"_s);
                REQUIRE(buf[2] == "d"_s);
                REQUIRE(buf[3] == "e"_s);
                REQUIRE(buf[4] == long_string);

                dyn::Insert(buf, 0, "yo"_s);
                REQUIRE(buf.size == 6);
                REQUIRE(buf[0] == "yo"_s);
                REQUIRE(buf[1] == "c"_s);

                dyn::Insert(buf, 3, "3"_s);
                REQUIRE(buf.size == 7);
                REQUIRE(buf[3] == "3"_s);
                REQUIRE(buf[4] == "d"_s);
                REQUIRE(buf[5] == "e"_s);
                REQUIRE(buf[6] == long_string);

                dyn::Insert(buf, 6, "6"_s);
                REQUIRE(buf.size == 8);
                REQUIRE(buf[6] == "6"_s);

                dyn::Remove(buf, 0);
                REQUIRE(buf.size == 7);
                REQUIRE(buf[0] == "c"_s);

                dyn::Assign(buf, ArrayT<Type>({"a"_s, "b"_s, "c"_s, "d"_s}));
                dyn::Remove(buf, 3);
                REQUIRE(buf.size == 3);
                REQUIRE(buf[0] == "a"_s);
                REQUIRE(buf[1] == "b"_s);
                REQUIRE(buf[2] == "c"_s);

                dyn::Remove(buf, 1);
                REQUIRE(buf.size == 2);
                REQUIRE(buf[0] == "a"_s);
                REQUIRE(buf[1] == "c"_s);

                dyn::Assign(buf, ArrayT<Type>({"a"_s, "b"_s, "c"_s, "d"_s}));
                dyn::Remove(buf, 1, 10);
                REQUIRE(buf.size == 1);
                REQUIRE(buf[0] == "a"_s);

                dyn::Assign(buf, ArrayT<Type>({"a"_s, "b"_s, "c"_s, "d"_s}));
                dyn::Remove(buf, 0, 2);
                REQUIRE(buf.size == 2);
                REQUIRE(buf[0] == "c"_s);
                REQUIRE(buf[1] == "d"_s);

                dyn::Assign(buf, ArrayT<Type>({"a"_s, "b"_s, "c"_s, "d"_s}));
                dyn::Remove(buf, 10, 2);
                REQUIRE(buf.size == 4);

                dyn::Clear(buf);
                dyn::Insert(buf, 0, "foo"_s);
                dyn::Clear(buf);
                dyn::Insert(buf, 10, "foo"_s);
                REQUIRE(buf.size == 0);

                dyn::Remove(buf, 0);
                dyn::Remove(buf, 10);

                AllocedString strs_data[] = {"1"_s, "2"_s, "3"_s};
                Span<AllocedString> const strs {strs_data, ArraySize(strs_data)};
                dyn::Clear(buf);
                dyn::InsertSpan(buf, 0, strs);
                REQUIRE(buf.size == 3);
                REQUIRE(buf[0] == "1"_s);
                REQUIRE(buf[1] == "2"_s);
                REQUIRE(buf[2] == "3"_s);

                dyn::InsertSpan(buf, 3, strs);
                REQUIRE(buf.size == 6);
                REQUIRE(buf[0] == "1"_s);
                REQUIRE(buf[1] == "2"_s);
                REQUIRE(buf[2] == "3"_s);
                REQUIRE(buf[3] == "1"_s);
                REQUIRE(buf[4] == "2"_s);
                REQUIRE(buf[5] == "3"_s);

                dyn::InsertSpan(buf, 2, strs);
                REQUIRE(buf.size == 9);
                REQUIRE(buf[0] == "1"_s);
                REQUIRE(buf[1] == "2"_s);
                REQUIRE(buf[2] == "1"_s);
                REQUIRE(buf[3] == "2"_s);
                REQUIRE(buf[4] == "3"_s);
                REQUIRE(buf[5] == "3"_s);
                REQUIRE(buf[6] == "1"_s);
                REQUIRE(buf[7] == "2"_s);
                REQUIRE(buf[8] == "3"_s);
            }

            SUBCASE("Remove") {
                DynamicArray<char> str {"012345"_s, a};
                dyn::Remove(str, 0, 2);
                REQUIRE(str == "2345"_s);
                dyn::Remove(str, 0, 100);
                REQUIRE(str == ""_s);
            }

            SUBCASE("Insert") {
                DynamicArray<char> str {"012345"_s, a};
                dyn::InsertSpan(str, 0, "aa"_s);
                REQUIRE(str == "aa012345"_s);
                dyn::InsertSpan(str, 4, "777"_s);
                REQUIRE(str == "aa017772345"_s);
            }

            SUBCASE("Replace") {
                DynamicArray<char> str {a};
                dyn::Assign(str, "aa bb cc aa d"_s);
                SUBCASE("with a longer string") {
                    dyn::Replace(str, "aa"_s, "fff"_s);
                    REQUIRE(str == "fff bb cc fff d"_s);
                }
                SUBCASE("with a shorter string") {
                    dyn::Replace(str, "aa"_s, "f"_s);
                    REQUIRE(str == "f bb cc f d"_s);
                }
                SUBCASE("a single character") {
                    dyn::Replace(str, "d"_s, "e"_s);
                    REQUIRE(str == "aa bb cc aa e"_s);
                }
                SUBCASE("empty existing value") {
                    dyn::Replace(str, ""_s, "fff"_s);
                    REQUIRE(str == "aa bb cc aa d"_s);
                }
                SUBCASE("empty replacement") {
                    dyn::Replace(str, "aa"_s, ""_s);
                    REQUIRE(str == " bb cc  d"_s);
                }
            }
        }
    }
    return k_success;
}

void SimpleFunction() {}

template <typename FunctionType>
static ErrorCodeOr<void> TestTrivialFunctionBasics(tests::Tester& tester, FunctionType& f) {
    f();
    int captured = 24;
    f = [captured, &tester]() { REQUIRE(captured == 24); };
    f();
    f = []() {};
    f();

    auto const lambda = [&tester]() { REQUIRE(true); };
    f = lambda;
    f();

    Array<char, 16> bloat;
    auto const lambda_large = [&tester, bloat]() {
        REQUIRE(true);
        (void)bloat;
    };
    f = lambda_large;
    f();

    f = Move(lambda);
    f();

    {
        f = [captured, &tester]() { REQUIRE(captured == 24); };
    }
    f();

    auto other_f = f;
    other_f();

    auto other_f2 = Move(f);
    other_f2();
    return k_success;
}

TEST_CASE(TestFunction) {
    SUBCASE("Fixed size") {
        SUBCASE("basics") {
            TrivialFixedSizeFunction<24, void()> f {SimpleFunction};
            static_assert(TriviallyCopyable<decltype(f)>);
            static_assert(TriviallyDestructible<decltype(f)>);
            TRY(TestTrivialFunctionBasics(tester, f));
        }

        SUBCASE("captures are copied 1") {
            int value = 0;
            TrivialFixedSizeFunction<8, void()> a = [&value]() { value = 1; };
            TrivialFixedSizeFunction<8, void()> b = [&value]() { value = 2; };

            value = 0;
            a();
            CHECK_EQ(value, 1);

            value = 0;
            b();
            CHECK_EQ(value, 2);

            value = 0;
            b = a;
            a = []() {};
            b();
            CHECK_EQ(value, 1);
        }

        SUBCASE("captures are copied 2") {
            bool a_value = false;
            bool b_value = false;
            TrivialFixedSizeFunction<8, void()> a = [&a_value]() { a_value = true; };
            TrivialFixedSizeFunction<8, void()> b = [&b_value]() { b_value = true; };

            b = a;
            a = []() {};
            b();
            CHECK(a_value);
            CHECK(!b_value);
        }
    }

    SUBCASE("Allocated") {
        LeakDetectingAllocator allocator;
        TrivialAllocatedFunction<void()> f {SimpleFunction, allocator};
        TRY(TestTrivialFunctionBasics(tester, f));

        SUBCASE("captures are copied 1") {
            int value = 0;
            TrivialAllocatedFunction<void()> a {[&value]() { value = 1; }, allocator};
            TrivialAllocatedFunction<void()> b {[&value]() { value = 2; }, allocator};

            value = 0;
            a();
            CHECK_EQ(value, 1);

            value = 0;
            b();
            CHECK_EQ(value, 2);

            value = 0;
            b = a;
            a = []() {};
            b();
            CHECK_EQ(value, 1);
        }

        SUBCASE("captures are copied 2") {
            bool a_value = false;
            bool b_value = false;
            TrivialAllocatedFunction<void()> a {[&a_value]() { a_value = true; }, allocator};
            TrivialAllocatedFunction<void()> b {[&b_value]() { b_value = true; }, allocator};

            b = a;
            a = []() {};
            b();
            CHECK(a_value);
            CHECK(!b_value);
        }
    }

    SUBCASE("Ref") {
        TrivialFunctionRef<void()> f {};
        static_assert(TriviallyCopyable<decltype(f)>);
        static_assert(TriviallyDestructible<decltype(f)>);

        f = &SimpleFunction;
        f();
        auto const lambda = [&tester]() { REQUIRE(true); };
        f = lambda;
        f();

        LeakDetectingAllocator allocator;
        {
            TrivialAllocatedFunction<void()> const allocated_f {f, allocator};
            allocated_f();
        }

        f = &SimpleFunction;
        {
            TrivialAllocatedFunction<void()> const allocated_f {f, allocator};
            allocated_f();
        }

        TrivialFunctionRef<void()> other;
        {
            int value = 100;
            f = [&tester, value]() { REQUIRE(value == 100); };
            other = f.CloneObject(tester.scratch_arena);
        }
        [[maybe_unused]] char push_stack[32];
        other();
    }

    return k_success;
}

TEST_CASE(TestFunctionQueue) {
    auto& a = tester.scratch_arena;

    FunctionQueue<> q {.arena = PageAllocator::Instance()};
    CHECK(q.Empty());

    int val = 0;

    {
        q.Push([&val]() { val = 1; });
        CHECK(!q.Empty());

        auto f = q.TryPop(a);
        REQUIRE(f.HasValue());
        (*f)();
        CHECK_EQ(val, 1);
        CHECK(q.Empty());
        CHECK(q.first == nullptr);
        CHECK(q.last == nullptr);
    }

    q.Push([&val]() { val = 2; });
    q.Push([&val]() { val = 3; });

    auto f2 = q.TryPop(a);
    auto f3 = q.TryPop(a);

    CHECK(f2);
    CHECK(f3);

    (*f2)();
    CHECK_EQ(val, 2);

    (*f3)();
    CHECK_EQ(val, 3);

    for (auto const i : Range(100))
        q.Push([i, &val] { val = i; });

    for (auto const i : Range(100)) {
        auto f = q.TryPop(a);
        CHECK(f);
        (*f)();
        CHECK_EQ(val, i);
    }

    return k_success;
}

TEST_CASE(TestHashTable) {
    auto& a = tester.scratch_arena;

    SUBCASE("table") {
        DynamicHashTable<String, usize> tab {a, 16u};

        CHECK(tab.table.size == 0);
        CHECK(tab.table.Elements().size >= 16);

        {
            usize count = 0;
            for (auto item : tab) {
                (void)item;
                ++count;
            }
            CHECK(count == 0);
        }

        CHECK(tab.Insert("foo", 42));
        CHECK(tab.Insert("bar", 31337));
        CHECK(tab.Insert("qux", 64));
        CHECK(tab.Insert("900", 900));
        CHECK(tab.Insert("112", 112));

        CHECK(tab.table.Elements().size > 5);
        CHECK(tab.table.size == 5);

        {
            auto v = tab.Find("bar");
            REQUIRE(v);
            tester.log.Debug({}, "{}", *v);
        }

        {
            usize count = 0;
            for (auto item : tab) {
                CHECK(item.value_ptr);
                CHECK(item.key.size);
                tester.log.Debug(k_foundation_mod_cat, "{} -> {}", item.key, *item.value_ptr);
                if (item.key == "112") (*item.value_ptr)++;
                ++count;
            }
            CHECK(count == 5);
            auto v = tab.Find("112");
            CHECK(v && *v == 113);
        }

        for (auto const i : Range(10000uz)) {
            char buffer[32];
            int const count = stbsp_snprintf(buffer, 32, "key%zu", i);
            CHECK(tab.Insert({buffer, (usize)count}, i));
        }
    }

    SUBCASE("no initial size") {
        DynamicHashTable<String, int> tab {a};
        CHECK(tab.Insert("foo", 100));
        for (auto item : tab)
            CHECK_EQ(*item.value_ptr, 100);
        auto v = tab.Find("foo");
        REQUIRE(v);
        CHECK_EQ(*v, 100);
        *v = 200;
        v = tab.Find("foo");
        REQUIRE(v);
        CHECK_EQ(*v, 200);

        CHECK(tab.table.size == 1);

        CHECK(tab.Delete("foo"));

        CHECK(tab.table.size == 0);
    }

    SUBCASE("move") {
        LeakDetectingAllocator a2;

        SUBCASE("construct") {
            DynamicHashTable<String, int> tab1 {a2};
            CHECK(tab1.Insert("foo", 100));
            DynamicHashTable<String, int> const tab2 {Move(tab1)};
            auto v = tab2.Find("foo");
            REQUIRE(v);
        }
        SUBCASE("assign same allocator") {
            DynamicHashTable<String, int> tab1 {a2};
            CHECK(tab1.Insert("foo", 100));
            DynamicHashTable<String, int> tab2 {a2};
            tab2 = Move(tab1);
            auto v = tab2.Find("foo");
            REQUIRE(v);
        }
        SUBCASE("assign different allocator") {
            DynamicHashTable<String, int> tab1 {a2};
            CHECK(tab1.Insert("foo", 100));
            DynamicHashTable<String, int> tab2 {Malloc::Instance()};
            tab2 = Move(tab1);
            auto v = tab2.Find("foo");
            REQUIRE(v);
        }
    }

    return k_success;
}

TEST_CASE(TestLinkedList) {
    LeakDetectingAllocator a;

    struct Node {
        int val;
        Node* next;
    };

    IntrusiveSinglyLinkedList<Node> list {};

    auto prepend = [&](int v) {
        auto new_node = a.New<Node>();
        new_node->val = v;
        SinglyLinkedListPrepend(list.first, new_node);
    };

    CHECK(list.Empty());

    prepend(1);
    prepend(2);

    CHECK(!list.Empty());

    usize count = 0;
    for (auto it : list) {
        if (count == 0) CHECK(it.val == 2);
        if (count == 1) CHECK(it.val == 1);
        ++count;
    }
    CHECK(count == 2);

    auto remove_if = [&](auto pred) {
        SinglyLinkedListRemoveIf(
            list.first,
            [&](Node const& node) { return pred(node.val); },
            [&](Node* node) { a.Delete(node); });
    };

    remove_if([](int) { return true; });
    CHECK(list.Empty());

    prepend(1);
    prepend(2);
    prepend(3);
    prepend(2);

    auto count_list = [&]() {
        usize count = 0;
        for ([[maybe_unused]] auto i : list)
            ++count;
        return count;
    };

    CHECK(count_list() == 4);

    remove_if([](int i) { return i == 1; });
    CHECK(count_list() == 3);
    for (auto i : list)
        CHECK(i.val != 1);

    remove_if([](int i) { return i == 2; });
    CHECK(count_list() == 1);
    CHECK(list.first->val == 3);

    remove_if([](int i) { return i == 3; });
    CHECK(count_list() == 0);
    CHECK(list.first == nullptr);

    prepend(3);
    prepend(2);
    prepend(2);
    prepend(1);
    CHECK(count_list() == 4);

    // remove first
    remove_if([](int i) { return i == 1; });
    CHECK(count_list() == 3);
    CHECK(list.first->val == 2);
    CHECK(list.first->next->val == 2);
    CHECK(list.first->next->next->val == 3);
    CHECK(list.first->next->next->next == nullptr);

    // remove last
    remove_if([](int i) { return i == 3; });
    CHECK(count_list() == 2);
    CHECK(list.first->val == 2);
    CHECK(list.first->next->val == 2);
    CHECK(list.first->next->next == nullptr);

    remove_if([](int i) { return i == 2; });
    CHECK(count_list() == 0);

    return k_success;
}

int TestValue(int) { return 10; }
AllocedString TestValue(AllocedString) { return "abc"_s; }

template <typename Type>
TEST_CASE(TestOptional) {

    SUBCASE("Empty") {
        Optional<Type> const o {};
        REQUIRE(!o.HasValue());
        REQUIRE(!o);
    }

    SUBCASE("Value") {
        Optional<Type> o {TestValue(Type())};
        REQUIRE(o.HasValue());
        REQUIRE(o);
        REQUIRE(o.Value() == TestValue(Type()));
        REQUIRE(*o == TestValue(Type()));

        SUBCASE("copy construct") {
            Optional<Type> other {o};
            REQUIRE(other.HasValue());
            REQUIRE(other.Value() == TestValue(Type()));
        }

        SUBCASE("copy assign") {
            Optional<Type> other {};
            other = o;
            REQUIRE(other.HasValue());
            REQUIRE(other.Value() == TestValue(Type()));
        }

        SUBCASE("move construct") {
            Optional<Type> other {Move(o)};
            REQUIRE(other.HasValue());
            REQUIRE(other.Value() == TestValue(Type()));
        }

        SUBCASE("move assign") {
            Optional<Type> other {};
            other = Move(o);
            REQUIRE(other.HasValue());
            REQUIRE(other.Value() == TestValue(Type()));
        }

        SUBCASE("arrow operator") {
            if constexpr (Same<Type, String>) REQUIRE(o->Size() != 0);
        }
    }
    return k_success;
}

TEST_CASE(TestSort) {
    SUBCASE("Sort") {
        SUBCASE("normal size") {
            int array[] = {7, 4, 6};
            Sort(array);
            REQUIRE(array[0] == 4);
            REQUIRE(array[1] == 6);
            REQUIRE(array[2] == 7);
        }
        SUBCASE("empty") {
            Span<int> span;
            Sort(span);
        }
        SUBCASE("one element") {
            int v = 10;
            Span<int> span {&v, 1};
            Sort(span);
        }
    }
    return k_success;
}

TEST_CASE(TestBinarySearch) {
    SUBCASE("BinarySearch") {
        REQUIRE(!FindBinarySearch(Span<int> {}, [](auto&) { return 0; }).HasValue());

        {
            int array[] = {1, 4, 6};
            Span<int> const span {array, ArraySize(array)};
            REQUIRE(FindBinarySearch(span, [](int i) {
                        if (i == 4) return 0;
                        if (i < 4) return -1;
                        return 1;
                    }).Value() == 1);
        }

        {
            int v = 1;
            Span<int> const span {&v, 1};
            REQUIRE(FindBinarySearch(span, [](int i) {
                        if (i == 1) return 0;
                        if (i < 1) return -1;
                        return 1;
                    }).Value() == 0);
        }
    }

    SUBCASE("BinarySearchForSlotToInsert") {
        Array<int, 5> arr = {0, 2, 4, 6, 8};
        auto span = arr.Items();

        auto const r0 = BinarySearchForSlotToInsert(span, [](int i) { return i - 0; });
        auto const r1 = BinarySearchForSlotToInsert(span, [](int i) { return i - 1; });
        auto const r2 = BinarySearchForSlotToInsert(span, [](int i) { return i - 3; });
        auto const r3 = BinarySearchForSlotToInsert(span, [](int i) { return i - 5; });
        auto const r4 = BinarySearchForSlotToInsert(span, [](int i) { return i - 7; });
        auto const r5 = BinarySearchForSlotToInsert(span, [](int i) { return i - 9000; });
        REQUIRE(r0 == 0);
        REQUIRE(r1 == 1);
        REQUIRE(r2 == 2);
        REQUIRE(r3 == 3);
        REQUIRE(r4 == 4);
        REQUIRE(r5 == 5);

        span = {};
        auto const empty = BinarySearchForSlotToInsert(span, [](int i) { return i - 0; });
        REQUIRE(empty == 0);
    }

    SUBCASE("BinarySearchForSlotToInsert 2") {
        Array<int, 4> arr = {0, 2, 4, 6};
        auto span = arr.Items();

        auto const r0 = BinarySearchForSlotToInsert(span, [](int i) { return i - 0; });
        auto const r1 = BinarySearchForSlotToInsert(span, [](int i) { return i - 1; });
        auto const r2 = BinarySearchForSlotToInsert(span, [](int i) { return i - 3; });
        auto const r3 = BinarySearchForSlotToInsert(span, [](int i) { return i - 5; });
        auto const r4 = BinarySearchForSlotToInsert(span, [](int i) { return i - 7; });
        REQUIRE(r0 == 0);
        REQUIRE(r1 == 1);
        REQUIRE(r2 == 2);
        REQUIRE(r3 == 3);
        REQUIRE(r4 == 4);
    }

    SUBCASE("BinarySearchForSlotToInsert 2") {
        Array<int, 11> arr = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20};
        auto span = arr.Items();

        auto const r0 = BinarySearchForSlotToInsert(span, [](int i) { return i - 0; });
        auto const r1 = BinarySearchForSlotToInsert(span, [](int i) { return i - 1; });
        auto const r2 = BinarySearchForSlotToInsert(span, [](int i) { return i - 3; });
        auto const r3 = BinarySearchForSlotToInsert(span, [](int i) { return i - 5; });
        auto const r4 = BinarySearchForSlotToInsert(span, [](int i) { return i - 7; });
        auto const r10 = BinarySearchForSlotToInsert(span, [](int i) { return i - 19; });
        REQUIRE(r0 == 0);
        REQUIRE(r1 == 1);
        REQUIRE(r2 == 2);
        REQUIRE(r3 == 3);
        REQUIRE(r4 == 4);
        REQUIRE(r10 == 10);
    }

    return k_success;
}

TEST_CASE(TestStringSearching) {
    CHECK(Contains("abc"_s, 'a'));
    CHECK(!Contains("abc"_s, 'd'));
    CHECK(!Contains(""_s, 'a'));

    CHECK(ContainsSpan("abc"_s, "a"_s));
    CHECK(ContainsSpan("abc"_s, "b"_s));
    CHECK(ContainsSpan("abc"_s, "abc"_s));
    CHECK(ContainsSpan("aaaabbb"_s, "aaaa"_s));
    CHECK(ContainsSpan("abcdefg"_s, "abc"_s));
    CHECK(ContainsSpan("abcdefg"_s, "bcd"_s));
    CHECK(ContainsSpan("abcdefg"_s, "cde"_s));
    CHECK(ContainsSpan("abcdefg"_s, "def"_s));
    CHECK(ContainsSpan("abcdefg"_s, "efg"_s));
    CHECK(!ContainsSpan("abcdefg"_s, "fgh"_s));
    CHECK(!ContainsSpan("aaabbb"_s, "aaaa"_s));
    CHECK(!ContainsSpan(""_s, ""_s));

    CHECK(FindSpan("abc"_s, "a"_s).ValueOr(999) == 0);
    CHECK(FindSpan("abc"_s, "b"_s).ValueOr(999) == 1);
    CHECK(FindSpan("abc"_s, "c"_s).ValueOr(999) == 2);
    CHECK(FindSpan("abc"_s, "abc"_s).ValueOr(999) == 0);
    CHECK(FindSpan("aaaabbb"_s, "aaaa"_s).ValueOr(999) == 0);
    CHECK(FindSpan("abcdefg"_s, "abc"_s).ValueOr(999) == 0);
    CHECK(FindSpan("abcdefg"_s, "bcd"_s).ValueOr(999) == 1);
    CHECK(FindSpan("abcdefg"_s, "cde"_s).ValueOr(999) == 2);
    CHECK(FindSpan("abcdefg"_s, "def"_s).ValueOr(999) == 3);
    CHECK(FindSpan("abcdefg"_s, "efg"_s).ValueOr(999) == 4);
    CHECK(!FindSpan("abcdefg"_s, "fgh"_s));
    CHECK(!FindSpan("aaabbb"_s, "aaaa"_s));
    CHECK(!FindSpan(""_s, ""_s));

    CHECK(StartsWith("aa"_s, 'a'));
    CHECK(!StartsWith("aa"_s, 'b'));
    CHECK(!StartsWith(""_s, 'b'));
    CHECK(StartsWithSpan("aaa"_s, "aa"_s));
    CHECK(!StartsWithSpan("baa"_s, "aa"_s));
    CHECK(!StartsWithSpan(""_s, "aa"_s));
    CHECK(!StartsWithSpan("aa"_s, ""_s));

    CHECK(NullTermStringStartsWith("aa", "a"));
    CHECK(!NullTermStringStartsWith("aa", "b"));
    CHECK(!NullTermStringStartsWith("", "b"));
    CHECK(NullTermStringStartsWith("", ""));
    CHECK(NullTermStringStartsWith("b", ""));

    CHECK(EndsWith("aa"_s, 'a'));
    CHECK(!EndsWith("aa"_s, 'b'));
    CHECK(EndsWithSpan("aaa"_s, "aa"_s));
    CHECK(!EndsWithSpan("aab"_s, "aa"_s));
    CHECK(!EndsWithSpan(""_s, "aa"_s));
    CHECK(!EndsWithSpan("aa"_s, ""_s));

    CHECK(ContainsOnly("aa"_s, 'a'));
    CHECK(!ContainsOnly("aab"_s, 'a'));
    CHECK(!ContainsOnly(""_s, 'a'));
    CHECK(!ContainsOnly("bb"_s, 'a'));

    CHECK(FindLast("aaa"_s, 'a').ValueOr(999) == 2);
    CHECK(FindLast("aab"_s, 'a').ValueOr(999) == 1);
    CHECK(FindLast("file/path"_s, '/').ValueOr(999) == 4);
    CHECK(FindLast("abb"_s, 'a').ValueOr(999) == 0);
    CHECK(!FindLast("aaa"_s, 'b'));
    CHECK(!FindLast(""_s, 'b'));

    CHECK(Find("aaa"_s, 'a').ValueOr(999) == 0);
    CHECK(Find("baa"_s, 'a').ValueOr(999) == 1);
    CHECK(Find("bba"_s, 'a').ValueOr(999) == 2);
    CHECK(!Find("aaa"_s, 'b'));
    CHECK(!Find(""_s, 'b'));

    CHECK(FindIf("abc"_s, [](char c) { return c == 'b'; }).ValueOr(999) == 1);
    CHECK(!FindIf("abc"_s, [](char c) { return c == 'd'; }));
    CHECK(!FindIf(""_s, [](char c) { return c == 'd'; }));

    Array<u8, 32> buffer;
    CHECK(ContainsPointer(buffer, buffer.data + 1));
    CHECK(ContainsPointer(buffer, buffer.data + 4));
    CHECK(!ContainsPointer(buffer, (u8 const*)((uintptr_t)buffer.data + 100)));
    CHECK(!ContainsPointer(buffer, (u8 const*)((uintptr_t)buffer.data - 1)));

    return k_success;
}

TEST_CASE(TestFormatStringReplace) {
    auto& a = tester.scratch_arena;
    CHECK_EQ(fmt::FormatStringReplace(a,
                                      "test __AAA__ bar __BBB__",
                                      ArrayT<fmt::StringReplacement>({
                                          {"__AAA__", "foo"},
                                          {"__BBB__", "bar"},
                                      })),
             "test foo bar bar"_s);
    CHECK_EQ(fmt::FormatStringReplace(a,
                                      "test __AAA____AAA__",
                                      ArrayT<fmt::StringReplacement>({
                                          {"__AAA__", "foo"},
                                      })),
             "test foofoo"_s);
    CHECK_EQ(fmt::FormatStringReplace(a, "abc", {}), "abc"_s);
    return k_success;
}

TEST_CASE(TestIntToString) {
    auto to_string = [](int value, fmt::IntToStringOptions options) {
        DynamicArrayBounded<char, 32> result;
        auto size = IntToString(value, result.data, options);
        result.ResizeWithoutCtorDtor(size);
        return result;
    };

    CHECK(to_string(10, {.base = fmt::IntToStringOptions::Base::Decimal}) == "10"_s);
    CHECK(to_string(-99, {.base = fmt::IntToStringOptions::Base::Decimal}) == "-99"_s);
    CHECK(to_string(10, {.base = fmt::IntToStringOptions::Base::Hexadecimal}) == "a");
    CHECK(to_string(255, {.base = fmt::IntToStringOptions::Base::Hexadecimal}) == "ff");
    CHECK(to_string(0xfedcba, {.base = fmt::IntToStringOptions::Base::Hexadecimal, .capitalize = true}) ==
          "FEDCBA");
    CHECK(to_string(-255, {.base = fmt::IntToStringOptions::Base::Hexadecimal}) == "-ff");
    return k_success;
}

TEST_CASE(TestFormat) {
    auto& a = tester.scratch_arena;

    SUBCASE("basics") {
        DynamicArrayBounded<char, 256> buf;
        fmt::Assign(buf, "text {}, end", 100);
        CHECK_EQ(buf, "text 100, end"_s);
    }

    SUBCASE("basics") {
        CHECK_EQ(fmt::Format(a, "foo {} bar", 1), "foo 1 bar"_s);
        CHECK_EQ(fmt::Format(a, "{} {} {} {}", 1, 2, 3, 99999), "1 2 3 99999"_s);
        CHECK_EQ(fmt::Format(a, "{} :: {}", "key"_s, 100), "key :: 100"_s);
        CHECK_EQ(fmt::Format(a, "{}", "yeehar"), "yeehar"_s);
        CHECK_EQ(fmt::Format(a, "empty format"), "empty format"_s);
        CHECK_NEQ(fmt::Format(a, "ptr: {}", (void const*)""), ""_s);
    }

    SUBCASE("formats") {
        CHECK_NEQ(fmt::Format(a, "auto f32: {g}", 2.0), ""_s);
        CHECK_EQ(fmt::Format(a, "{x}", 255), "ff"_s);
        CHECK_EQ(fmt::Format(a, "{.2}", 0.2), "0.20"_s);
        CHECK_EQ(fmt::Format(a, "{.1}", 0.8187f), "0.8"_s);
    }

    SUBCASE("width") {
        SUBCASE("pad with spaces") {
            CHECK_EQ(fmt::Format(a, "{0}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{1}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{2}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{3}", 10), " 10"_s);
            CHECK_EQ(fmt::Format(a, "{4}", 10), "  10"_s);
            CHECK_EQ(fmt::Format(a, "{4x}", 255), "  ff"_s);
        }

        SUBCASE("pad with zeros") {
            CHECK_EQ(fmt::Format(a, "{0}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{01}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{02}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{03}", 10), "010"_s);
            CHECK_EQ(fmt::Format(a, "{04}", 10), "0010"_s);
            CHECK_EQ(fmt::Format(a, "{04x}", 255), "00ff"_s);
            CHECK_EQ(fmt::Format(a, "{07.2}", 3.1111), "0003.11"_s);
        }
    }

    SUBCASE("errors") {
        CHECK_PANICS(fmt::Format(a, "{} {} {} {}", 1));
        CHECK_PANICS(fmt::Format(a, "{}", 1, 1, 1, 1));
        CHECK_PANICS(fmt::Format(a, "{sefsefsef}", 1));
        CHECK_PANICS(fmt::Format(a, "{{}", 1));
        CHECK_PANICS(fmt::Format(a, " {{} ", 1));
        CHECK_PANICS(fmt::Format(a, "{}}", 1));
        CHECK_PANICS(fmt::Format(a, " {}} ", 1));
    }

    SUBCASE("brace literals") {
        CHECK_EQ(fmt::Format(a, "{{}}"), "{}"_s);
        CHECK_EQ(fmt::Format(a, "{{}} {}", 10), "{} 10"_s);
        CHECK_EQ(fmt::Format(a, "{} {{}}", 10), "10 {}"_s);
        CHECK_EQ(fmt::Format(a, "{} {{fff}}", 10), "10 {fff}"_s);
    }

    SUBCASE("strings") {
        CHECK_EQ(fmt::Format(a, "{}", ""), ""_s);
        CHECK_EQ(fmt::Format(a, "{}", "string literal"), "string literal"_s);
        CHECK_EQ(fmt::Format(a, "{}", (char const*)"const char pointer"), "const char pointer"_s);
    }

    SUBCASE("Error") {
        ErrorCodeCategory const category {
            .category_id = "test",
            .message = [](Writer const& writer, ErrorCode error) -> ErrorCodeOr<void> {
                TRY(writer.WriteChars("error code: "));
                TRY(writer.WriteChars(
                    fmt::IntToString(error.code, {.base = fmt::IntToStringOptions::Base::Decimal})));
                return k_success;
            },
        };
        ErrorCode const err {category, 100};
        CHECK_NEQ(fmt::Format(a, "{}", err), ""_s);
        CHECK_NEQ(fmt::Format(a, "{u}", err), ""_s);
    }

    SUBCASE("Dump struct") {
        struct TestStruct {
            int a;
            int b;
            char const* c;
        };
        TestStruct const test {1, 2, "three"};
        tester.log.Debug(k_foundation_mod_cat, "struct1 is: {}", fmt::DumpStruct(test));

        auto const arr = Array {
            TestStruct {1, 2, "three"},
            TestStruct {4, 5, "six"},
        };
        tester.log.Debug(k_foundation_mod_cat, "struct2 is: {}", fmt::DumpStruct(arr));

        struct OtherStruct {
            int a;
            int b;
            char const* c;
            TestStruct d;
            TestStruct e;
        };
        OtherStruct const other {1, 2, "three", {4, 5, "six"}, {7, 8, "nine"}};
        tester.log.Debug(k_foundation_mod_cat, "struct3 is: {}", fmt::DumpStruct(other));

        tester.log.Debug(k_foundation_mod_cat, "struct4 is: {}", fmt::DumpStruct(tester));
    }

    return k_success;
}

TEST_CASE(TestRect) {
    SUBCASE("MakeRectThatEnclosesRects") {
        auto const r1 = Rect {0, 5, 50, 50};
        auto const r2 = Rect {5, 0, 100, 25};
        auto const enclosing = Rect::MakeRectThatEnclosesRects(r1, r2);
        REQUIRE(enclosing.x == 0);
        REQUIRE(enclosing.y == 0);
        REQUIRE(enclosing.w == 105);
        REQUIRE(enclosing.h == 55);
    }
    return k_success;
}

TEST_CASE(TestTrigLookupTable) {
    REQUIRE(trig_table_lookup::Sin(-maths::k_pi<>) == 0);
    REQUIRE(trig_table_lookup::Sin(-maths::k_pi<> / 2) == -1);
    REQUIRE(trig_table_lookup::Sin(0) == 0);
    REQUIRE(trig_table_lookup::Sin(maths::k_pi<> / 2) == 1);
    REQUIRE(trig_table_lookup::Sin(maths::k_pi<>) == 0);
    REQUIRE(trig_table_lookup::Sin(maths::k_pi<> * (3.0f / 2.0f)) == -1);
    REQUIRE(trig_table_lookup::Sin(maths::k_pi<> * 2) == 0);

    REQUIRE(trig_table_lookup::Cos(-maths::k_pi<>) == -1);
    REQUIRE(trig_table_lookup::Cos(-maths::k_pi<> / 2) == 0);
    REQUIRE(trig_table_lookup::Cos(0) == 1);
    REQUIRE(trig_table_lookup::Cos(maths::k_pi<> / 2) == 0);
    REQUIRE(trig_table_lookup::Cos(maths::k_pi<>) == -1);
    REQUIRE(trig_table_lookup::Cos(maths::k_pi<> * (3.0f / 2.0f)) == 0);
    REQUIRE(trig_table_lookup::Cos(maths::k_pi<> * 2) == 1);

    REQUIRE(trig_table_lookup::Tan(0) == 0);
    REQUIRE(trig_table_lookup::Tan(maths::k_pi<>) == 0);
    REQUIRE(trig_table_lookup::Tan(-maths::k_pi<>) == 0);

    f32 phase = -600;
    for (auto _ : Range(100)) {
        constexpr f32 k_arbitrary_value = 42.3432798f;
        REQUIRE(ApproxEqual(trig_table_lookup::Sin(phase), Sin(phase), 0.01f));
        REQUIRE(ApproxEqual(trig_table_lookup::Cos(phase), Cos(phase), 0.01f));
        REQUIRE(ApproxEqual(trig_table_lookup::Tan(phase), Tan(phase), 0.01f));
        phase += k_arbitrary_value;
    }
    return k_success;
}

TEST_CASE(TestMathsTrigTurns) {
    REQUIRE(trig_table_lookup::SinTurnsPositive(0) == 0);
    REQUIRE(trig_table_lookup::SinTurnsPositive(0.25f) == 1);
    REQUIRE(trig_table_lookup::SinTurnsPositive(0.5f) == 0);
    REQUIRE(trig_table_lookup::SinTurnsPositive(0.75f) == -1);
    REQUIRE(trig_table_lookup::SinTurnsPositive(1) == 0);
    REQUIRE(trig_table_lookup::SinTurnsPositive(2) == 0);
    REQUIRE(trig_table_lookup::SinTurnsPositive(1.25f) == 1);
    REQUIRE(trig_table_lookup::SinTurnsPositive(100.25f) == 1);

    REQUIRE(trig_table_lookup::SinTurns(0) == 0);
    REQUIRE(trig_table_lookup::SinTurns(0.25f) == 1);
    REQUIRE(trig_table_lookup::SinTurns(0.5f) == 0);
    REQUIRE(trig_table_lookup::SinTurns(0.75f) == -1);
    REQUIRE(trig_table_lookup::SinTurns(1) == 0);
    REQUIRE(trig_table_lookup::SinTurns(2) == 0);
    REQUIRE(trig_table_lookup::SinTurns(1.25f) == 1);
    REQUIRE(trig_table_lookup::SinTurns(100.25f) == 1);
    REQUIRE(trig_table_lookup::SinTurns(-0.25f) == -1);
    REQUIRE(trig_table_lookup::SinTurns(-0.5f) == 0);
    REQUIRE(trig_table_lookup::SinTurns(-0.75f) == 1);
    REQUIRE(trig_table_lookup::SinTurns(-1) == 0);
    REQUIRE(trig_table_lookup::SinTurns(-2) == 0);
    REQUIRE(trig_table_lookup::SinTurns(-200.25) == -1);

    REQUIRE(trig_table_lookup::CosTurns(-0.5f) == -1);
    REQUIRE(trig_table_lookup::CosTurns(-0.5f / 2) == 0);
    REQUIRE(trig_table_lookup::CosTurns(0) == 1);
    REQUIRE(trig_table_lookup::CosTurns(0.5f / 2) == 0);
    REQUIRE(trig_table_lookup::CosTurns(0.5f) == -1);
    REQUIRE(trig_table_lookup::CosTurns(0.5f * (3.0f / 2.0f)) == 0);
    REQUIRE(trig_table_lookup::CosTurns(0.5f * 2) == 1);

    REQUIRE(trig_table_lookup::TanTurns(0) == 0);
    REQUIRE(trig_table_lookup::TanTurns(0.5f) == 0);
    REQUIRE(trig_table_lookup::TanTurns(-0.5f) == 0);
    return k_success;
}

TEST_CASE(TestPath) {
    auto& scratch_arena = tester.scratch_arena;

    using namespace path;
    SUBCASE("Trim") {
        CHECK_EQ(TrimDirectorySeparatorsEnd("foo/"_s, Format::Posix), "foo"_s);
        CHECK_EQ(TrimDirectorySeparatorsEnd("/"_s, Format::Posix), "/"_s);
        CHECK_EQ(TrimDirectorySeparatorsEnd(""_s, Format::Posix), ""_s);
        CHECK_EQ(TrimDirectorySeparatorsEnd("foo////\\\\"_s, Format::Windows), "foo"_s);

        SUBCASE("windows") {
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/foo////"_s, Format::Windows), "C:/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\unc\\share\\foo\\bar\\", Format::Windows),
                     "\\\\unc\\share\\foo\\bar"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\unc\\share\\", Format::Windows), "\\\\unc\\share"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/foo/"_s, Format::Windows), "C:/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/foo"_s, Format::Windows), "C:/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/"_s, Format::Windows), "C:/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:////"_s, Format::Windows), "C:/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:////"_s, Format::Windows), "C:/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\?\\C:\\"_s, Format::Windows), "\\\\?\\C:\\"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd(""_s, Format::Windows), ""_s);
        }

        SUBCASE("posix") {
            CHECK_EQ(TrimDirectorySeparatorsEnd("/foo////"_s, Format::Posix), "/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("/foo/"_s, Format::Posix), "/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("/foo"_s, Format::Posix), "/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("/"_s, Format::Posix), "/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("////"_s, Format::Posix), "/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd(""_s, Format::Posix), ""_s);
        }
    }

    SUBCASE("Join") {
        DynamicArrayBounded<char, 128> s;
        s = "foo"_s;
        JoinAppend(s, "bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = "foo/"_s;
        JoinAppend(s, "bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = "foo"_s;
        JoinAppend(s, "/bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = "foo/"_s;
        JoinAppend(s, "/bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = ""_s;
        JoinAppend(s, "/bar"_s, Format::Posix);
        CHECK_EQ(s, "bar"_s);

        s = "foo"_s;
        JoinAppend(s, ""_s, Format::Posix);
        CHECK_EQ(s, "foo"_s);

        s = "foo"_s;
        JoinAppend(s, "/"_s, Format::Posix);
        CHECK_EQ(s, "foo"_s);

        s = ""_s;
        JoinAppend(s, ""_s, Format::Posix);
        CHECK_EQ(s, ""_s);

        s = "C:/"_s;
        JoinAppend(s, "foo"_s, Format::Windows);
        CHECK_EQ(s, "C:/foo"_s);

        s = "/"_s;
        JoinAppend(s, "foo"_s, Format::Posix);
        CHECK_EQ(s, "/foo"_s);

        {
            auto result = Join(scratch_arena, Array {"foo"_s, "bar"_s, "baz"_s}, Format::Posix);
            CHECK_EQ(result, "foo/bar/baz"_s);
        }
    }

    SUBCASE("Split") {
        CHECK_EQ(Filename("foo"), "foo"_s);
        CHECK_EQ(Extension("/file.txt"_s), ".txt"_s);
        CHECK(IsAbsolute("/file.txt"_s, Format::Posix));
        CHECK(IsAbsolute("C:/file.txt"_s, Format::Windows));
    }

    // This SUBCASE is based on Zig's code
    // https://github.com/ziglang/zig
    // Copyright (c) Zig contributors
    // SPDX-License-Identifier: MIT
    SUBCASE("Directory") {
        CHECK_EQ(Directory("/a/b/c", Format::Posix), "/a/b"_s);
        CHECK_EQ(Directory("/a/b/c///", Format::Posix), "/a/b"_s);
        CHECK_EQ(Directory("/a", Format::Posix), "/"_s);
        CHECK(!Directory("/", Format::Posix).HasValue());
        CHECK(!Directory("//", Format::Posix).HasValue());
        CHECK(!Directory("///", Format::Posix).HasValue());
        CHECK(!Directory("////", Format::Posix).HasValue());
        CHECK(!Directory("", Format::Posix).HasValue());
        CHECK(!Directory("a", Format::Posix).HasValue());
        CHECK(!Directory("a/", Format::Posix).HasValue());
        CHECK(!Directory("a//", Format::Posix).HasValue());

        CHECK(!Directory("c:\\", Format::Windows).HasValue());
        CHECK_EQ(Directory("c:\\foo", Format::Windows), "c:\\"_s);
        CHECK_EQ(Directory("c:\\foo\\", Format::Windows), "c:\\"_s);
        CHECK_EQ(Directory("c:\\foo\\bar", Format::Windows), "c:\\foo"_s);
        CHECK_EQ(Directory("c:\\foo\\bar\\", Format::Windows), "c:\\foo"_s);
        CHECK_EQ(Directory("c:\\foo\\bar\\baz", Format::Windows), "c:\\foo\\bar"_s);
        CHECK(!Directory("\\", Format::Windows).HasValue());
        CHECK_EQ(Directory("\\foo", Format::Windows), "\\"_s);
        CHECK_EQ(Directory("\\foo\\", Format::Windows), "\\"_s);
        CHECK_EQ(Directory("\\foo\\bar", Format::Windows), "\\foo"_s);
        CHECK_EQ(Directory("\\foo\\bar\\", Format::Windows), "\\foo"_s);
        CHECK_EQ(Directory("\\foo\\bar\\baz", Format::Windows), "\\foo\\bar"_s);
        CHECK(!Directory("c:", Format::Windows).HasValue());
        CHECK(!Directory("c:foo", Format::Windows).HasValue());
        CHECK(!Directory("c:foo\\", Format::Windows).HasValue());
        CHECK_EQ(Directory("c:foo\\bar", Format::Windows), "c:foo"_s);
        CHECK_EQ(Directory("c:foo\\bar\\", Format::Windows), "c:foo"_s);
        CHECK_EQ(Directory("c:foo\\bar\\baz", Format::Windows), "c:foo\\bar"_s);
        CHECK(!Directory("file:stream", Format::Windows).HasValue());
        CHECK_EQ(Directory("dir\\file:stream", Format::Windows), "dir"_s);
        CHECK(!Directory("\\\\unc\\share", Format::Windows).HasValue());
        CHECK_EQ(Directory("\\\\unc\\share\\foo", Format::Windows), "\\\\unc\\share\\"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\", Format::Windows), "\\\\unc\\share\\"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\bar", Format::Windows), "\\\\unc\\share\\foo"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\bar\\", Format::Windows), "\\\\unc\\share\\foo"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\bar\\baz", Format::Windows), "\\\\unc\\share\\foo\\bar"_s);
        CHECK_EQ(Directory("/a/b/", Format::Windows), "/a"_s);
        CHECK_EQ(Directory("/a/b", Format::Windows), "/a"_s);
        CHECK_EQ(Directory("/a", Format::Windows), "/"_s);
        CHECK(!Directory("", Format::Windows).HasValue());
        CHECK(!Directory("/", Format::Windows).HasValue());
        CHECK(!Directory("////", Format::Windows).HasValue());
        CHECK(!Directory("foo", Format::Windows).HasValue());
    }

    SUBCASE("IsWithinDirectory") {
        CHECK(IsWithinDirectory("/foo/bar/baz", "/foo"));
        CHECK(IsWithinDirectory("/foo/bar/baz", "/foo/bar"));
        CHECK(IsWithinDirectory("foo/bar/baz", "foo"));
        CHECK(!IsWithinDirectory("/foo", "/foo"));
        CHECK(!IsWithinDirectory("/foo/bar/baz", "/bar"));
        CHECK(!IsWithinDirectory("/foobar/baz", "/foo"));
        CHECK(!IsWithinDirectory("baz", "/foo"));
        CHECK(!IsWithinDirectory("baz", "/o"));
    }

    SUBCASE("Windows Parse") {
        {
            auto const p = ParseWindowsPath("C:/foo/bar");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "C:"_s);
        }
        {
            auto const p = ParseWindowsPath("//a/b");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "//a/b"_s);
        }
        {
            auto const p = ParseWindowsPath("c:../");
            CHECK(!p.is_abs);
            CHECK_EQ(p.drive, "c:"_s);
        }
        {
            auto const p = ParseWindowsPath({});
            CHECK(!p.is_abs);
            CHECK_EQ(p.drive, ""_s);
        }
        {
            auto const p = ParseWindowsPath("D:\\foo\\bar");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "D:"_s);
        }
        {
            auto const p = ParseWindowsPath("\\\\LOCALHOST\\c$\\temp\\test-file.txt");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "\\\\LOCALHOST\\c$"_s);
        }
    }

    return k_success;
}

constexpr int k_num_rand_test_repititions = 200;

TEST_CASE(TestRandomIntGeneratorUnsigned) {
    SUBCASE("unsigned") {
        RandomIntGenerator<unsigned int> generator;
        u64 seed = SeedFromTime();

        SUBCASE("Correct generation in range 0 to 3 with repeating last value allowed") {
            constexpr unsigned int k_max_val = 3;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, 0, k_max_val, false);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range 0 to 3000000000 with repeating last value allowed") {
            constexpr unsigned int k_max_val = 3000000000;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto random_num = generator.GetRandomInRange(seed, 0, k_max_val, false);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range 0 to 3 with repeating last value disallowed") {
            constexpr unsigned int k_max_val = 3;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, 0, k_max_val, true);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range 0 to 3000000000 with repeating last value disallowed") {
            constexpr unsigned int k_max_val = 3000000000;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto random_num = generator.GetRandomInRange(seed, 0, k_max_val, true);
                REQUIRE(random_num <= k_max_val);
            }
        }
    }
    SUBCASE("signed") {
        RandomIntGenerator<int> generator;
        u64 seed = SeedFromTime();

        SUBCASE("Correct generation in range -10 to 10 with repeating last value allowed") {
            constexpr int k_max_val = 10;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, -k_max_val, k_max_val, false);
                REQUIRE(random_num >= -k_max_val);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range -10 to 10 with repeating last value disallowed") {
            constexpr int k_max_val = 10;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, -k_max_val, k_max_val, true);
                REQUIRE(random_num >= -k_max_val);
                REQUIRE(random_num <= k_max_val);
            }
        }
    }
    SUBCASE("move object") {
        RandomIntGenerator<int> generator;
        u64 seed = SeedFromTime();

        constexpr int k_max_val = 10;
        {
            auto const random_num = generator.GetRandomInRange(seed, -k_max_val, k_max_val, false);
            REQUIRE(random_num >= -k_max_val);
            REQUIRE(random_num <= k_max_val);
        }

        auto generator2 = generator;
        {
            auto const random_num = generator2.GetRandomInRange(seed, -k_max_val, k_max_val, false);
            REQUIRE(random_num >= -k_max_val);
            REQUIRE(random_num <= k_max_val);
        }

        auto generator3 = Move(generator);
        {
            auto const random_num = generator3.GetRandomInRange(seed, -k_max_val, k_max_val, false);
            REQUIRE(random_num >= -k_max_val);
            REQUIRE(random_num <= k_max_val);
        }
    }
    return k_success;
}

template <typename T>
TEST_CASE(TestRandomFloatGenerator) {
    RandomFloatGenerator<T> generator;
    u64 seed = SeedFromTime();

    SUBCASE("random values are in a correct range") {
        auto test = [&](bool allow_repititions) {
            constexpr T k_max_val = 100;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num =
                    generator.GetRandomInRange(seed, -k_max_val, k_max_val, allow_repititions);
                REQUIRE(random_num >= -k_max_val);
                REQUIRE(random_num <= k_max_val);
            }
        };
        test(true);
        test(false);
    }
    return k_success;
}

TEST_CASE(TestVersion) {
    REQUIRE(Version {1, 0, 0}.ToString() == "1.0.0"_s);
    REQUIRE(Version {10, 99, 99}.ToString() == "10.99.99"_s);
    REQUIRE(Version {10, 99, 99, 2u}.ToString() == "10.99.99-Beta2"_s);

    REQUIRE(Version {1, 0, 0} == Version {1, 0, 0});
    REQUIRE(Version {1, 1, 0} > Version {1, 0, 0});
    REQUIRE(Version {1, 0, 0} < Version {1, 1, 0});
    REQUIRE(Version {0, 0, 0} < Version {1, 0, 0});
    REQUIRE(Version {1, 0, 100} < Version {2, 4, 10});
    REQUIRE(Version {0, 0, 100} < Version {0, 0, 101});

    REQUIRE(Version {1, 0, 0, 1u} < Version {1, 0, 0});
    REQUIRE(Version {1, 0, 0, 1u} == Version {1, 0, 0, 1u});
    REQUIRE(Version {1, 0, 0, 2u} > Version {1, 0, 0, 1u});

    auto const check_string_parsing = [&](String str, Version ver) {
        CAPTURE(str);
        auto const parsed_ver = ParseVersionString(str);
        REQUIRE(parsed_ver.HasValue());
        CAPTURE(parsed_ver->ToString(tester.scratch_arena));
        CAPTURE(ver.ToString(tester.scratch_arena));
        REQUIRE(ver == *parsed_ver);
    };

    REQUIRE(!ParseVersionString("1"));
    REQUIRE(!ParseVersionString("hello"));
    REQUIRE(!ParseVersionString(",,what"));
    REQUIRE(!ParseVersionString("1,1,2"));
    REQUIRE(!ParseVersionString("1a,1,2bv"));
    REQUIRE(!ParseVersionString("200a.200.400a"));
    REQUIRE(!ParseVersionString(".."));
    REQUIRE(!ParseVersionString("..."));
    REQUIRE(!ParseVersionString("1.2.3.4"));
    REQUIRE(!ParseVersionString(".1.2"));
    REQUIRE(!ParseVersionString("12.."));
    REQUIRE(!ParseVersionString(".1."));
    REQUIRE(!ParseVersionString("1.1.0-blah1"));
    REQUIRE(!ParseVersionString(""));

    check_string_parsing("1.1.1", {1, 1, 1});
    check_string_parsing(" 200   .  4.99 ", {200, 4, 99});
    check_string_parsing("0.0.0", {0, 0, 0});
    check_string_parsing("1.0.99", {1, 0, 99});
    check_string_parsing("1.0.0-Beta1", {1, 0, 0, 1u});
    check_string_parsing("1.0.0-Beta100", {1, 0, 0, 100u});

    {
        u32 prev_version = 0;
        u16 maj = 0;
        u8 min = 0;
        u8 pat = 0;
        for (auto _ : Range(256)) {
            ++pat;
            if (pat > 20) {
                pat = 0;
                ++min;
                if (min > 20) ++maj;
            }

            auto const version = PackVersionIntoU32(maj, min, pat);
            REQUIRE(version > prev_version);
            prev_version = version;
        }
    }

    REQUIRE(PackVersionIntoU32(1, 1, 2) < PackVersionIntoU32(1, 2, 0));
    return k_success;
}

TEST_CASE(TestMemoryUtils) {
    REQUIRE(BytesToAddForAlignment(10, 1) == 0);
    REQUIRE(BytesToAddForAlignment(9, 1) == 0);
    REQUIRE(BytesToAddForAlignment(3333333, 1) == 0);
    REQUIRE(BytesToAddForAlignment(0, 2) == 0);
    REQUIRE(BytesToAddForAlignment(1, 2) == 1);
    REQUIRE(BytesToAddForAlignment(2, 2) == 0);
    REQUIRE(BytesToAddForAlignment(1, 4) == 3);
    REQUIRE(BytesToAddForAlignment(2, 4) == 2);
    REQUIRE(BytesToAddForAlignment(3, 4) == 1);
    REQUIRE(BytesToAddForAlignment(4, 4) == 0);
    REQUIRE(BytesToAddForAlignment(31, 32) == 1);
    return k_success;
}

TEST_CASE(TestAsciiToUppercase) {
    REQUIRE(ToUppercaseAscii('a') == 'A');
    REQUIRE(ToUppercaseAscii('z') == 'Z');
    REQUIRE(ToUppercaseAscii('A') == 'A');
    REQUIRE(ToUppercaseAscii('M') == 'M');
    REQUIRE(ToUppercaseAscii('0') == '0');
    REQUIRE(ToUppercaseAscii(' ') == ' ');
    for (int i = SmallestRepresentableValue<char>(); i <= LargestRepresentableValue<char>(); ++i)
        ToUppercaseAscii((char)i);
    return k_success;
}

TEST_CASE(TestAsciiToLowercase) {
    REQUIRE(ToLowercaseAscii('A') == 'a');
    REQUIRE(ToLowercaseAscii('Z') == 'z');
    REQUIRE(ToLowercaseAscii('a') == 'a');
    REQUIRE(ToLowercaseAscii('m') == 'm');
    REQUIRE(ToLowercaseAscii('0') == '0');
    REQUIRE(ToLowercaseAscii(' ') == ' ');
    for (int i = SmallestRepresentableValue<char>(); i <= LargestRepresentableValue<char>(); ++i)
        ToLowercaseAscii((char)i);
    return k_success;
}

TEST_CASE(TestNullTermStringsEqual) {
    REQUIRE(NullTermStringsEqual("", ""));
    REQUIRE(!NullTermStringsEqual("a", ""));
    REQUIRE(!NullTermStringsEqual("", "a"));
    REQUIRE(!NullTermStringsEqual("aaa", "a"));
    REQUIRE(!NullTermStringsEqual("a", "aaa"));
    REQUIRE(NullTermStringsEqual("aaa", "aaa"));
    return k_success;
}

TEST_CASE(TestSplitWithIterator) {
    auto check = [&](String whole, char token, Span<String> expected_parts) {
        CAPTURE(whole);
        CAPTURE(expected_parts);

        Optional<usize> cursor {0uz};
        usize index = 0;
        while (cursor) {
            auto part = SplitWithIterator(whole, cursor, token);
            REQUIRE(part == expected_parts[index++]);
        }

        REQUIRE(index == expected_parts.size);
    };

    check("aa\nbb", '\n', ArrayT<String>({"aa", "bb"}));
    check("aa", '\n', Array {"aa"_s});
    check("aa\n\nbb", '\n', ArrayT<String>({"aa", "", "bb"}));
    check("\n\nbb", '\n', ArrayT<String>({"", "", "bb"}));
    return k_success;
}

TEST_CASE(TestSplit) {
    auto check = [&](String whole, char token, Span<String> expected_parts) {
        CAPTURE(whole);
        CAPTURE(expected_parts);

        auto split = Split(whole, token, tester.scratch_arena);
        REQUIRE(split.size == expected_parts.size);
        for (auto const i : Range(expected_parts.size))
            REQUIRE(split[i] == expected_parts[i]);
    };
    check("aa\nbb", '\n', Array<String, 2> {"aa", "bb"});
    check("aa", '\n', Array<String, 1> {"aa"});
    return k_success;
}

TEST_CASE(TestParseFloat) {
    CHECK(!ParseFloat(""));
    CHECK(!ParseFloat("string"));

    usize num_chars_read = 0;
    CHECK_APPROX_EQ(ParseFloat("0", &num_chars_read).Value(), 0.0, 0.0001);
    CHECK_EQ(num_chars_read, 1u);
    CHECK_APPROX_EQ(ParseFloat("10", &num_chars_read).Value(), 10.0, 0.0001);
    CHECK_EQ(num_chars_read, 2u);
    CHECK_APPROX_EQ(ParseFloat("-10", &num_chars_read).Value(), -10.0, 0.0001);
    CHECK_EQ(num_chars_read, 3u);
    CHECK_APPROX_EQ(ParseFloat("238942349.230", &num_chars_read).Value(), 238942349.230, 0.0001);
    CHECK_EQ(num_chars_read, 13u);
    return k_success;
}

TEST_CASE(TestParseInt) {
    CHECK(!ParseInt("", ParseIntBase::Decimal));
    CHECK(!ParseInt("string", ParseIntBase::Decimal));
    CHECK(!ParseInt("  ", ParseIntBase::Decimal));

    usize num_chars_read = 0;
    CHECK_EQ(ParseInt("0", ParseIntBase::Decimal, &num_chars_read).Value(), 0);
    CHECK_EQ(num_chars_read, 1u);
    CHECK_EQ(ParseInt("10", ParseIntBase::Decimal, &num_chars_read).Value(), 10);
    CHECK_EQ(num_chars_read, 2u);
    CHECK_EQ(ParseInt("-10", ParseIntBase::Decimal, &num_chars_read).Value(), -10);
    CHECK_EQ(num_chars_read, 3u);
    CHECK_EQ(ParseInt("238942349", ParseIntBase::Decimal, &num_chars_read).Value(), 238942349);
    CHECK_EQ(num_chars_read, 9u);

    CHECK_EQ(ParseInt("0", ParseIntBase::Hexadecimal, &num_chars_read).Value(), 0);
    CHECK_EQ(num_chars_read, 1u);
    CHECK_EQ(ParseInt("10", ParseIntBase::Hexadecimal, &num_chars_read).Value(), 0x10);
    CHECK_EQ(num_chars_read, 2u);
    CHECK_EQ(ParseInt("deadc0de", ParseIntBase::Hexadecimal, &num_chars_read).Value(), 0xdeadc0de);
    CHECK_EQ(num_chars_read, 8u);

    return k_success;
}

TEST_CASE(TestNarrowWiden) {
    auto& a = tester.scratch_arena;
    // IMPROVE: check against Windows MultiByteToWideChar
    auto const utf8_str = FromNullTerminated((char const*)u8"C:/testãingãã/†‡œÀÏàåùçÁéÄöüÜß.txt");
    auto const wstr = L"C:/testãingãã/†‡œÀÏàåùçÁéÄöüÜß.txt"_s;

    SUBCASE("standard functions") {
        auto const converted_wstr = Widen(a, utf8_str);
        CHECK(converted_wstr.HasValue());
        CHECK(converted_wstr.Value() == wstr);
        auto const original_str = Narrow(a, converted_wstr.Value());
        CHECK(original_str.HasValue());
        CHECK(original_str.Value() == utf8_str);
    }

    SUBCASE("widen append") {
        DynamicArray<wchar_t> str {a};
        CHECK(WidenAppend(str, utf8_str));
        CHECK(str.size == wstr.size);
        CHECK(str == wstr);
        CHECK(WidenAppend(str, utf8_str));
        CHECK(str.size == wstr.size * 2);
    }

    SUBCASE("narrow append") {
        DynamicArray<char> str {a};
        CHECK(NarrowAppend(str, wstr));
        CHECK(str.size == utf8_str.size);
        CHECK(str == utf8_str);
        CHECK(NarrowAppend(str, wstr));
        CHECK(str.size == utf8_str.size * 2);
    }
    return k_success;
}

TEST_CASE(TestCopyStringIntoBuffer) {
    SUBCASE("char[N] overload") {
        SUBCASE("Small buffer") {
            char buf[2];
            CopyStringIntoBufferWithNullTerm(buf, "abc");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == '\0');
        }

        SUBCASE("Size 1 buffer") {
            char buf[1];
            CopyStringIntoBufferWithNullTerm(buf, "abc");
            CHECK(buf[0] == '\0');
        }

        SUBCASE("Empty source") {
            char buf[8];
            CopyStringIntoBufferWithNullTerm(buf, "");
            CHECK(buf[0] == '\0');
        }

        SUBCASE("Whole source fits") {
            char buf[8];
            CopyStringIntoBufferWithNullTerm(buf, "aa");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == 'a');
            CHECK(buf[2] == '\0');
        }
    }

    SUBCASE("Span<char> overload") {
        SUBCASE("Dest empty") { CopyStringIntoBufferWithNullTerm(nullptr, 0, "abc"); }

        SUBCASE("Source empty") {
            char buffer[6];
            CopyStringIntoBufferWithNullTerm(buffer, 6, "");
            CHECK(buffer[0] == 0);
        }

        SUBCASE("Small buffer") {
            char buf[2];
            CopyStringIntoBufferWithNullTerm(buf, 2, "abc");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == '\0');
        }

        SUBCASE("Whole source fits") {
            char buf[8];
            CopyStringIntoBufferWithNullTerm(buf, "aa");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == 'a');
            CHECK(buf[2] == '\0');
        }
    }
    return k_success;
}

TEST_CASE(TestMatchWildcard) {
    CHECK(MatchWildcard("*foo*", "foobar"));
    CHECK(MatchWildcard(".*-file", ".text-file"));
    CHECK(MatchWildcard("floe_*.cpp", "floe_functions.cpp"));
    CHECK(MatchWildcard("mirtestãingããage_*.cpp", "mirtestãingããage_functions.cpp"));
    CHECK(MatchWildcard("*.floe*", "1.floe"));
    CHECK(MatchWildcard("*.floe*", "1.floe-wraith"));
    CHECK(MatchWildcard("*.floe*", "1.floe-none"));
    CHECK(!MatchWildcard("*.floe*", "foo.py"));
    return k_success;
}

TEST_CASE(TestStringAlgorithms) {
    SUBCASE("ContainsCaseInsensitiveAscii") {
        String const str = "abcde";
        CHECK(ContainsCaseInsensitiveAscii(str, "abcde"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "abcd"_s));
        CHECK(!ContainsCaseInsensitiveAscii(str, "abcdef"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "bc"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "BC"_s));
        CHECK(!ContainsCaseInsensitiveAscii(str, "cb"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "c"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "C"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, ""_s));
    }

    SUBCASE("Compare") {
        CHECK(CompareAscii("aaa"_s, "aaa"_s) == 0);
        CHECK_OP(CompareAscii("aaa"_s, "AAA"_s), >, 0);
        CHECK_OP(CompareAscii("za"_s, "AAA"_s), >, 0);
        CHECK_OP(CompareAscii(""_s, ""_s), ==, 0);
        CHECK_OP(CompareAscii("a"_s, ""_s), >, 0);
        CHECK_OP(CompareAscii(""_s, "a"_s), <, 0);

        CHECK(CompareCaseInsensitiveAscii("Aaa"_s, "aaa"_s) == 0);
        CHECK(CompareCaseInsensitiveAscii(""_s, ""_s) == 0);
    }

    SUBCASE("IsEqualToCaseInsensitveAscii") {
        CHECK(IsEqualToCaseInsensitiveAscii("aa"_s, "AA"_s));
        CHECK(IsEqualToCaseInsensitiveAscii(""_s, ""_s));
        CHECK(!IsEqualToCaseInsensitiveAscii("aa"_s, "AAA"_s));
        CHECK(!IsEqualToCaseInsensitiveAscii("aaa"_s, "AA"_s));
        CHECK(!IsEqualToCaseInsensitiveAscii("a"_s, ""_s));
        CHECK(!IsEqualToCaseInsensitiveAscii(""_s, "1"_s));
    }

    SUBCASE("whitespace") {
        CHECK(CountWhitespaceAtStart("  a"_s) == 2);
        CHECK(CountWhitespaceAtStart("\t\n\r a"_s) == 4);
        CHECK(CountWhitespaceAtStart(" "_s) == 1);
        CHECK(CountWhitespaceAtStart("a "_s) == 0);
        CHECK(CountWhitespaceAtStart(""_s) == 0);

        CHECK(CountWhitespaceAtEnd("a  "_s) == 2);
        CHECK(CountWhitespaceAtEnd("a \t\n\r"_s) == 4);
        CHECK(CountWhitespaceAtEnd(" "_s) == 1);
        CHECK(CountWhitespaceAtEnd(" a"_s) == 0);
        CHECK(CountWhitespaceAtEnd(""_s) == 0);

        CHECK(WhitespaceStripped(" aa  "_s) == "aa");
        CHECK(WhitespaceStrippedStart(" aa  "_s) == "aa  ");
    }

    return k_success;
}

struct ArenaAllocatorMalloc : ArenaAllocator {
    ArenaAllocatorMalloc() : ArenaAllocator(Malloc::Instance()) {}
};

struct ArenaAllocatorPage : ArenaAllocator {
    ArenaAllocatorPage() : ArenaAllocator(PageAllocator::Instance()) {}
};

struct ArenaAllocatorBigBuf : ArenaAllocator {
    ArenaAllocatorBigBuf() : ArenaAllocator(big_buf) {}
    FixedSizeAllocator<1000> big_buf;
};

template <typename AllocatorType>
TEST_CASE(TestAllocatorTypes) {
    AllocatorType a;

    SUBCASE("Pointers are unique when no existing data is passed in") {
        constexpr auto k_iterations = 1000;
        DynamicArrayBounded<Span<u8>, k_iterations> allocs;
        DynamicArrayBounded<void*, k_iterations> set;
        for (auto _ : Range(k_iterations)) {
            dyn::Append(allocs, a.Allocate({1, 1, true}));
            REQUIRE(Last(allocs).data != nullptr);
            dyn::AppendIfNotAlreadyThere(set, Last(allocs).data);
        }
        REQUIRE(set.size == k_iterations);
        for (auto alloc : allocs)
            a.Free(alloc);
    }

    SUBCASE("all sizes and alignments are handled") {
        usize const sizes[] = {1, 2, 3, 99, 7000};
        usize const alignments[] = {1, 2, 4, 8, 16, 32};
        auto const total_size = ArraySize(sizes) * ArraySize(alignments);
        DynamicArrayBounded<Span<u8>, total_size> allocs;
        DynamicArrayBounded<void*, total_size> set;
        for (auto s : sizes) {
            for (auto align : alignments) {
                dyn::Append(allocs, a.Allocate({s, align, true}));
                REQUIRE(Last(allocs).data != nullptr);
                dyn::AppendIfNotAlreadyThere(set, Last(allocs).data);
            }
        }
        REQUIRE(set.size == total_size);
        for (auto alloc : allocs)
            a.Free(alloc);
    }

    SUBCASE("reallocating an existing block still contains the same data") {
        auto data = a.template AllocateBytesForTypeOversizeAllowed<int>();
        DEFER { a.Free(data); };
        int const test_value = 1234567;
        *(CheckedPointerCast<int*>(data.data)) = test_value;

        data = a.template Reallocate<int>(100, data, 1, false);
        REQUIRE(*(CheckedPointerCast<int*>(data.data)) == test_value);
    }

    SUBCASE("shrink") {
        constexpr usize k_alignment = 8;
        constexpr usize k_original_size = 20;
        auto data = a.Allocate({k_original_size, k_alignment, true});
        DEFER { a.Free(data); };
        REQUIRE(data.size >= k_original_size);

        constexpr usize k_new_size = 10;
        auto shrunk_data = a.Resize({data, k_new_size});
        data = shrunk_data;
        REQUIRE(data.size == k_new_size);

        // do another allocation for good measure
        auto data2 = a.Allocate({k_original_size, k_alignment, true});
        DEFER { a.Free(data2); };
        REQUIRE(data2.size >= k_original_size);
        data2 = a.Resize({data2, k_new_size});
        REQUIRE(data2.size == k_new_size);
    }

    SUBCASE("clone") {
        constexpr usize k_alignment = 8;
        constexpr usize k_original_size = 20;
        auto data = a.Allocate({k_original_size, k_alignment, true});
        DEFER { a.Free(data); };
        FillMemory(data, 'a');

        auto cloned_data = a.Clone(data);
        DEFER { a.Free(cloned_data); };
        REQUIRE(cloned_data.data != data.data);
        REQUIRE(cloned_data.size == data.size);
        for (auto const i : Range(k_original_size))
            REQUIRE(cloned_data[i] == 'a');
    }

    SUBCASE("a complex mix of allocations, reallocations and frees work") {
        usize const sizes[] = {1,  1, 1, 1, 1,   1,   1,   1,  1,    3,   40034,
                               64, 2, 2, 2, 500, 500, 500, 99, 1000, 100, 20};
        usize const alignments[] = {1, 2, 4, 8, 16, 32};
        struct Allocation {
            usize size;
            usize align;
            Span<u8> data {};
        };
        Allocation allocs[ArraySize(sizes)];
        usize align_index = 0;
        for (auto const i : Range(ArraySize(sizes))) {
            auto& alloc = allocs[i];
            alloc.size = sizes[i];
            alloc.align = alignments[align_index];
            align_index++;
            if (align_index == ArraySize(alignments)) align_index = 0;
        }

        u64 seed = SeedFromTime();
        RandomIntGenerator<usize> rand_gen;
        usize index = 0;
        for (auto _ : Range(ArraySize(sizes) * 5)) {
            switch (rand_gen.GetRandomInRange(seed, 0, 5)) {
                case 0:
                case 1:
                case 2: {
                    auto const new_size = allocs[index].size;
                    auto const new_align = allocs[index].align;
                    auto const existing_data = allocs[index].data;
                    if (existing_data.size && new_size > existing_data.size) {
                        allocs[index].data = a.Resize({
                            .allocation = existing_data,
                            .new_size = new_size,
                            .allow_oversize_result = true,
                        });
                    } else if (new_size < existing_data.size) {
                        allocs[index].data = a.Resize({
                            .allocation = existing_data,
                            .new_size = new_size,
                        });
                    } else if (!existing_data.size) {
                        allocs[index].data = a.Allocate({
                            .size = new_size,
                            .alignment = new_align,
                            .allow_oversized_result = true,
                        });
                    }
                    break;
                }
                case 3:
                case 4: {
                    if (allocs[index].data.data) {
                        a.Free(allocs[index].data);
                        allocs[index].data = {};
                    }
                    break;
                }
                case 5: {
                    if (allocs[index].data.data) {
                        auto const new_size = allocs[index].data.size / 2;
                        if (new_size) {
                            allocs[index].data = a.Resize({
                                .allocation = allocs[index].data,
                                .new_size = new_size,
                            });
                        }
                    }
                }
            }
            index++;
            if (index == ArraySize(allocs)) index = 0;
        }

        for (auto& alloc : allocs)
            if (alloc.data.data) a.Free(alloc.data);
    }

    SUBCASE("speed benchmark") {
        constexpr usize k_alignment = 8;
        usize const sizes[] = {1,   16,  16,  16, 16,   32,  32, 32, 32, 32, 40034, 64, 128, 50, 239,
                               500, 500, 500, 99, 1000, 100, 20, 16, 16, 16, 64,    64, 64,  64, 64,
                               64,  64,  64,  64, 64,   64,  64, 64, 64, 64, 64,    64, 64};

        constexpr usize k_num_cycles = 10;
        Span<u8> allocations[ArraySize(sizes) * k_num_cycles];

        Stopwatch const stopwatch;

        for (auto const cycle : Range(k_num_cycles))
            for (auto const i : Range(ArraySize(sizes)))
                allocations[cycle * ArraySize(sizes) + i] = a.Allocate({sizes[i], k_alignment, true});

        if constexpr (!Same<ArenaAllocator, AllocatorType>)
            for (auto& alloc : allocations)
                a.Free(alloc);

        String type_name {};
        if constexpr (Same<AllocatorType, FixedSizeAllocator<1>>)
            type_name = "FixedSizeAllocator<1>";
        else if constexpr (Same<AllocatorType, FixedSizeAllocator<16>>)
            type_name = "FixedSizeAllocator<16>";
        else if constexpr (Same<AllocatorType, FixedSizeAllocator<1000>>)
            type_name = "FixedSizeAllocator<1000>";
        else if constexpr (Same<AllocatorType, Malloc>)
            type_name = "Malloc";
        else if constexpr (Same<AllocatorType, PageAllocator>)
            type_name = "PageAllocator";
        else if constexpr (Same<AllocatorType, ArenaAllocatorMalloc>)
            type_name = "ArenaAllocatorMalloc";
        else if constexpr (Same<AllocatorType, ArenaAllocatorPage>)
            type_name = "ArenaAllocatorPage";
        else if constexpr (Same<AllocatorType, ArenaAllocatorBigBuf>)
            type_name = "ArenaAllocatorBigBuf";
        else if constexpr (Same<AllocatorType, LeakDetectingAllocator>)
            type_name = "LeakDetectingAllocator";
        else if constexpr (Same<AllocatorType, LeakDetectingAllocator>)
            type_name = "LeakDetectingAllocator";
        else
            PanicIfReached();

        tester.log.Debug(k_foundation_mod_cat, "Speed benchmark: {} for {}", stopwatch, type_name);
    }
    return k_success;
}

TEST_CASE(TestArenaAllocatorCursor) {
    LeakDetectingAllocator leak_detecting_allocator;
    constexpr usize k_first_region_size = 64;
    ArenaAllocator arena {leak_detecting_allocator, k_first_region_size};
    CHECK(arena.first == arena.last);
    CHECK_OP(arena.first->BufferSize(), ==, k_first_region_size);

    auto const cursor1 = arena.TotalUsed();
    REQUIRE(cursor1 == 0);

    arena.NewMultiple<u8>(10);
    auto const cursor2 = arena.TotalUsed();
    CHECK_EQ(cursor2, (usize)10);
    CHECK(arena.first == arena.last);

    CHECK_EQ(arena.TryShrinkTotalUsed(cursor1), (usize)0);

    arena.NewMultiple<u8>(10);
    CHECK_EQ(arena.TotalUsed(), (usize)10);
    CHECK(arena.first == arena.last);

    arena.ResetCursorAndConsolidateRegions();
    CHECK_EQ(arena.TotalUsed(), (usize)0);
    CHECK(arena.first == arena.last);

    arena.AllocateExactSizeUninitialised<u8>(4000);
    CHECK(arena.first != arena.last);
    CHECK(arena.first->next == arena.last);
    CHECK(arena.last->prev == arena.first);
    CHECK_EQ(arena.TryShrinkTotalUsed(100), (usize)100);
    CHECK_EQ(arena.TotalUsed(), (usize)100);

    CHECK_EQ(arena.TryShrinkTotalUsed(4), k_first_region_size);
    CHECK_LTE(arena.TotalUsed(), k_first_region_size);

    arena.ResetCursorAndConsolidateRegions();
    CHECK_EQ(arena.TotalUsed(), (usize)0);
    return k_success;
}

TEST_REGISTRATION(RegisterFoundationTests) {
    REGISTER_TEST(TestAllocatorTypes<ArenaAllocatorBigBuf>);
    REGISTER_TEST(TestAllocatorTypes<ArenaAllocatorMalloc>);
    REGISTER_TEST(TestAllocatorTypes<ArenaAllocatorPage>);
    REGISTER_TEST(TestAllocatorTypes<FixedSizeAllocator<1000>>);
    REGISTER_TEST(TestAllocatorTypes<FixedSizeAllocator<16>>);
    REGISTER_TEST(TestAllocatorTypes<FixedSizeAllocator<1>>);
    REGISTER_TEST(TestAllocatorTypes<LeakDetectingAllocator>);
    REGISTER_TEST(TestAllocatorTypes<Malloc>);
    REGISTER_TEST(TestAllocatorTypes<PageAllocator>);
    REGISTER_TEST(TestArenaAllocatorCursor);
    REGISTER_TEST(TestAsciiToLowercase);
    REGISTER_TEST(TestAsciiToUppercase);
    REGISTER_TEST(TestBinarySearch);
    REGISTER_TEST(TestBitset);
    REGISTER_TEST(TestCircularBuffer);
    REGISTER_TEST(TestCircularBufferRefType);
    REGISTER_TEST(TestCopyStringIntoBuffer);
    REGISTER_TEST(TestDynamicArrayBasics<AllocedString>);
    REGISTER_TEST(TestDynamicArrayBasics<Optional<AllocedString>>);
    REGISTER_TEST(TestDynamicArrayBasics<int>);
    REGISTER_TEST(TestDynamicArrayChar);
    REGISTER_TEST(TestDynamicArrayClone);
    REGISTER_TEST(TestDynamicArrayBoundedBasics);
    REGISTER_TEST(TestDynamicArrayString);
    REGISTER_TEST(TestFormat);
    REGISTER_TEST(TestFormatStringReplace);
    REGISTER_TEST(TestFunction);
    REGISTER_TEST(TestFunctionQueue);
    REGISTER_TEST(TestHashTable);
    REGISTER_TEST(TestIntToString);
    REGISTER_TEST(TestLinkedList);
    REGISTER_TEST(TestMatchWildcard);
    REGISTER_TEST(TestMathsTrigTurns);
    REGISTER_TEST(TestMemoryUtils);
    REGISTER_TEST(TestNarrowWiden);
    REGISTER_TEST(TestNullTermStringsEqual);
    REGISTER_TEST(TestOptional<AllocedString>);
    REGISTER_TEST(TestOptional<int>);
    REGISTER_TEST(TestParseFloat);
    REGISTER_TEST(TestParseInt);
    REGISTER_TEST(TestPath);
    REGISTER_TEST(TestRandomFloatGenerator<f32>);
    REGISTER_TEST(TestRandomFloatGenerator<f64>);
    REGISTER_TEST(TestRandomIntGeneratorUnsigned);
    REGISTER_TEST(TestRect);
    REGISTER_TEST(TestSort);
    REGISTER_TEST(TestSplit);
    REGISTER_TEST(TestSplitWithIterator);
    REGISTER_TEST(TestStringAlgorithms);
    REGISTER_TEST(TestStringSearching);
    REGISTER_TEST(TestTaggedUnion);
    REGISTER_TEST(TestTrigLookupTable);
    REGISTER_TEST(TestVersion);
    REGISTER_TEST(TestWriter);
}
