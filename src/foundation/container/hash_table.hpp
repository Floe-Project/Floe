// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// This code is based on htab (https://github.com/rofl0r/htab/), itself based on musl's hsearch.
// Copyright Szabolcs Nagy A.K.A. nsz
// Copyright rofl0r
// SPDX-License-Identifier: MIT

#pragma once
#include "foundation/memory/allocators.hpp"

template <TriviallyCopyable KeyType,
          TriviallyCopyable ValueType,
          u64 (*k_hash_function)(KeyType) = Hash<KeyType>>
struct HashTable {
    struct Element {
        ValueType data {};
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
            ValueType* value_ptr;
        };
        Item operator*() const { return item; }
        Iterator& operator++() {
            ++index;
            for (; index < table.mask + 1; ++index) {
                auto& element = table.elems[index];
                if (element.active) {
                    item.key = element.key;
                    item.value_ptr = &element.data;
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

    static u64 Hash(KeyType k) { return k_hash_function(k); }

    // Quadratic probing is used in case of hash collision
    Element* Lookup(KeyType key, u64 hash, usize _dead) const {
        ASSERT(elems);
        Element* element;
        for (u64 i = hash, j = 1;; i += j++) {
            element = elems + (i & mask);
            if ((!element->active && (!element->hash || element->hash == _dead)) ||
                (element->hash == hash && element->key == key))
                break;
        }
        return element;
    }

    Element* FindElement(KeyType key) const {
        if (!elems) return nullptr;
        Element* element = Lookup(key, k_hash_function(key), 0);
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

    void Free(Allocator& a) {
        auto element = Elements();
        if (element.size) a.Free(element.ToByteSpan());
    }

    Span<Element const> Elements() const {
        return elems ? Span<Element const> {elems, mask + 1} : Span<Element const> {};
    }
    Span<Element> Elements() { return elems ? Span<Element> {elems, mask + 1} : Span<Element> {}; }

    ValueType* Find(KeyType key) const {
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
        ++dead;
        return true;
    }

    void DeleteIndex(usize index) {
        elems[index].active = false;
        elems[index].hash = k_tombstone;
        --size;
        ++dead;
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
        if (size + dead > mask - mask / 4) {
            PanicIfReached();
            return false; // too full
        }

        if (element->hash == k_tombstone) --dead;
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
        if (++size + dead > mask - mask / 4) {
            IncreaseCapacity(allocator, 2 * size);
            dead = 0;
        } else if (old_hash == k_tombstone) {
            // re-used tomb
            --dead;
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
    usize dead {};
};

template <TriviallyCopyable KeyType,
          TriviallyCopyable ValueType,
          u64 (*k_hash_function)(KeyType) = Hash<KeyType>>
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
    static constexpr DynamicHashTable FromOwnedSpan(Table table, Allocator& allocator) {
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
