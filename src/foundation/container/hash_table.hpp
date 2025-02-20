// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// This code is based on htab (https://github.com/rofl0r/htab/), itself based on musl's hsearch.
// Copyright Szabolcs Nagy A.K.A. nsz
// Copyright rofl0r
// SPDX-License-Identifier: MIT

#pragma once
#include "foundation/memory/allocators.hpp"

// IMPORTANT: don't set k_hash_function to a function that is header-only. It can result in the type of the
// HashTable being different across compilation units.

struct DummyValueType {};

template <typename T>
concept TriviallyCopyableOrDummy = TriviallyCopyable<T> || Same<DummyValueType, T>;

template <TriviallyCopyable KeyType,
          TriviallyCopyableOrDummy ValueType,
          u64 (*k_hash_function)(KeyType) = nullptr>
struct HashTable {
    struct Element {
        [[no_unique_address]] ValueType data {};
        KeyType key {};
        u64 hash {};
        bool active {};
    };

    struct Iterator {
        friend bool operator==(Iterator const& a, Iterator const& b) {
            return &a.table == &b.table && a.index == b.index;
        }
        friend bool operator!=(Iterator const& a, Iterator const& b) {
            return &a.table != &b.table || a.index != b.index;
        }
        struct Item {
            KeyType key;
            [[no_unique_address]] Conditional<Same<DummyValueType, ValueType>, DummyValueType, ValueType*>
                value_ptr;
        };
        Item operator*() const { return item; }
        Iterator& operator++() {
            ++index;
            for (; index < table.mask + 1; ++index) {
                auto& element = table.elems[index];
                if (element.active) {
                    item.key = element.key;
                    if constexpr (!Same<DummyValueType, ValueType>) item.value_ptr = &element.data;
                    break;
                }
            }
            return *this;
        }

        HashTable const& table;
        Item item {};
        usize index {};
    };

    static constexpr usize k_min_size = 8;
    static constexpr usize k_max_size = ((usize)-1 / 2 + 1);
    static constexpr u64 k_tombstone = 0xdeadc0de;

    static u64 Hash(KeyType k) {
        // IMPORTANT: we don't set Hash as the k_hash_function in the template arguments because we don't know
        // if Hash is consistent accross different compilation units. It might be a header-only function in
        // which case the _type_ of the HashTable will vary depending on the compilation unit leading to
        // cryptic linker errors.
        if constexpr (k_hash_function == nullptr)
            return ::Hash(k);
        else
            return k_hash_function(k);
    }

    // Quadratic probing is used if there's a hash collision
    Element* Lookup(KeyType key, u64 hash, u64 dead_hash_value) const {
        ASSERT(elems);

        Element* element;
        usize index = hash;
        usize step = 1;

        while (true) {
            element = elems + (index & mask);

            if (!element->active) {
                if (!element->hash) break;
                if (element->hash == dead_hash_value) break;
            }

            if (element->hash == hash && element->key == key) break;

            index += step;
            step++;
        }

        return element;
    }

    Element* FindElement(KeyType key) const {
        if (!elems) return nullptr;
        Element* element = Lookup(key, Hash(key), 0);
        if (element->active) return element;
        return nullptr;
    }

    static usize PowerOf2Capacity(usize capacity) {
        if (capacity > k_max_size) capacity = k_max_size;
        usize new_capacity;
        for (new_capacity = k_min_size; new_capacity < capacity; new_capacity *= 2)
            ;
        return new_capacity;
    }

    static usize RecommendedCapacity(usize num_items) { return PowerOf2Capacity(num_items * 2); }

    [[nodiscard]] static HashTable Create(Allocator& a, usize size) {
        auto const cap = RecommendedCapacity(size);
        HashTable table {};
        table.elems = a.NewMultiple<Element>(cap).data;
        table.mask = cap - 1;
        return table;
    }

    usize Capacity() const { return mask + 1; }

    void Free(Allocator& a) {
        auto element = Elements();
        if (element.size) a.Free(element.ToByteSpan());
    }

    Span<Element const> Elements() const {
        return elems ? Span<Element const> {elems, mask + 1} : Span<Element const> {};
    }
    Span<Element> Elements() { return elems ? Span<Element> {elems, mask + 1} : Span<Element> {}; }

    ValueType* Find(KeyType key) const {
        static_assert(!Same<ValueType, DummyValueType>,
                      "HashTable::Find called on a set, use FindElement instead");
        Element* element = FindElement(key);
        if (!element) return nullptr;
        return &element->data;
    }

    bool Delete(KeyType key) {
        Element* element = FindElement(key);
        if (!element) return false;
        element->active = false;
        element->hash = k_tombstone;
        --size;
        ++num_dead;
        return true;
    }

    void DeleteIndex(usize index) {
        elems[index].active = false;
        elems[index].hash = k_tombstone;
        --size;
        ++num_dead;
    }

    void DeleteAll() {
        for (auto it = begin(); it != end(); ++it)
            DeleteIndex(it.index);
    }

    // allocator must be the same as created this table
    void IncreaseCapacity(Allocator& allocator, usize capacity) {
        auto old_elements = Elements();

        capacity = PowerOf2Capacity(capacity);
        elems = allocator.template NewMultiple<Element>(capacity).data;
        mask = capacity - 1;

        if (old_elements.size) {
            Element* new_element;
            for (auto& old_element : old_elements)
                if (old_element.active) {
                    for (u64 i = old_element.hash, j = 1;; i += j++) {
                        new_element = elems + (i & mask);
                        if (!new_element->active) break;
                    }
                    *new_element = old_element;
                }
            allocator.Free(old_elements.ToByteSpan());
        }
        return;
    }

    bool InsertWithoutGrowing(KeyType key, ValueType value) {
        if (!elems) {
            PanicIfReached();
            return false;
        }
        auto const hash = Hash(key);
        Element* element = Lookup(key, hash, k_tombstone);

        if (element->active) return false; // already exists
        if (size + num_dead > mask - mask / 4) {
            PanicIfReached();
            return false; // too full
        }

        if (element->hash == k_tombstone) --num_dead;
        ++size;
        element->key = key;
        element->active = true;
        element->data = value;
        element->hash = hash;
        return true;
    }

    // allocator must be the same as created this table
    bool InsertGrowIfNeeded(Allocator& allocator, KeyType key, ValueType value) {
        if (!elems) IncreaseCapacity(allocator, k_min_size);
        auto const hash = Hash(key);
        Element* element = Lookup(key, hash, k_tombstone);
        if (element->active) return false; // already exists

        auto const old_hash = element->hash; // save old hash in case it's tombstone marker
        element->active = true;
        element->key = key;
        element->data = value;
        element->hash = hash;
        if (++size + num_dead > mask - mask / 4) {
            IncreaseCapacity(allocator, 2 * size);
            num_dead = 0;
        } else if (old_hash == k_tombstone) {
            // re-used tomb
            --num_dead;
        }
        return true;
    }

    Iterator begin() const {
        if (!elems) return end();
        typename Iterator::Item item {};
        usize index = 0;
        for (; index < mask + 1; ++index) {
            auto& element = elems[index];
            if (element.active) {
                item.key = element.key;
                item.value_ptr = &element.data;
                break;
            }
        }
        return Iterator {*this, item, index};
    }
    Iterator end() const { return Iterator {*this, {}, mask + 1}; }

    Element* elems {};
    usize mask {};
    usize size {};
    usize num_dead {};
};

template <TriviallyCopyable KeyType,
          TriviallyCopyableOrDummy ValueType,
          u64 (*k_hash_function)(KeyType) = nullptr>
struct DynamicHashTable {
    using Table = HashTable<KeyType, ValueType, k_hash_function>;

    DynamicHashTable(Allocator& alloc, usize initial_capacity = 0) : allocator(alloc) {
        if (initial_capacity) IncreaseCapacity(initial_capacity);
    }

    ~DynamicHashTable() { Free(); }

    DynamicHashTable(DynamicHashTable&& other) : allocator(other.allocator), table(other.table) {
        other.table = {};
    }

    DynamicHashTable& operator=(DynamicHashTable&& other) {
        Free();

        table = other.table;
        if (&other.allocator == &allocator)
            table.elems = other.table.elems;
        else {
            table.elems = allocator.Clone(other.table.Elements(), CloneType::Deep).data;
            other.Free();
        }

        other.table = {};

        return *this;
    }

    NON_COPYABLE(DynamicHashTable);

    Table ToOwnedTable() {
        auto result = table;
        table = {};
        return result;
    }

    // table must have been created with allocator
    static constexpr DynamicHashTable FromOwnedTable(Table table, Allocator& allocator) {
        DynamicHashTable result {allocator};
        result.table = table;
        return result;
    }

    void Free() { table.Free(allocator); }

    void IncreaseCapacity(usize capacity) { table.IncreaseCapacity(allocator, capacity); }

    ValueType* Find(KeyType key) const { return table.Find(key); }
    Table::Element* FindElement(KeyType key) const { return table.FindElement(key); }

    bool Delete(KeyType key) { return table.Delete(key); }
    void DeleteIndex(usize i) { table.DeleteIndex(i); }
    void DeleteAll() { table.DeleteAll(); }

    bool Insert(KeyType key, ValueType value) { return table.InsertGrowIfNeeded(allocator, key, value); }

    auto begin() const { return table.begin(); }
    auto end() const { return table.end(); }

    Allocator& allocator;
    Table table {};
};

template <TriviallyCopyable KeyType, u64 (*k_hash_function)(KeyType) = nullptr>
struct Set : HashTable<KeyType, DummyValueType, k_hash_function> {
    using Table = HashTable<KeyType, DummyValueType, k_hash_function>;

    // delete methods that don't make sense for a set
    bool InsertWithoutGrowing(KeyType key, DummyValueType) = delete;
    bool InsertGrowIfNeeded(Allocator& allocator, KeyType key, DummyValueType) = delete;
    DummyValueType* Find(KeyType key) const = delete;

    // replace with methods that make sense for a set
    static Set Create(Allocator& a, usize size) { return Set {Table::Create(a, size)}; }
    bool InsertWithoutGrowing(KeyType key) { return Table::InsertWithoutGrowing(key, {}); }
    // allocator must be the same as created this table
    bool InsertGrowIfNeeded(Allocator& allocator, KeyType key) {
        return Table::InsertGrowIfNeeded(allocator, key, {});
    }
    bool Contains(KeyType key) const { return this->FindElement(key); }
};

template <TriviallyCopyable KeyType, u64 (*k_hash_function)(KeyType) = nullptr>
struct DynamicSet : DynamicHashTable<KeyType, DummyValueType, k_hash_function> {
    using Set = Set<KeyType, k_hash_function>;

    DynamicSet(Allocator& alloc, usize initial_capacity = 0)
        : DynamicHashTable<KeyType, DummyValueType, k_hash_function>(alloc, initial_capacity) {}

    bool Insert(KeyType key) {
        return DynamicHashTable<KeyType, DummyValueType, k_hash_function>::Insert(key, {});
    }

    Set ToOwnedSet() {
        auto result = this->table;
        this->table = {};
        return result;
    }

    DummyValueType* Find(KeyType key) const = delete;
    Set::Element* FindElement(KeyType key) const = delete;

    bool Contains(KeyType key) const { return this->table.FindElement(key); }
};
