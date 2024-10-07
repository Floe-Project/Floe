// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/universal_defs.hpp"

template <typename Type, usize k_size>
struct BoundedList {
    using UnderlyingIndexType = Conditional < k_size < 256, u8, Conditional<k_size<65535, u16, u32>>;

    enum class Index : UnderlyingIndexType {};

    static constexpr Index k_invalid_index = Index {UnderlyingIndexType(-1)};

    struct Node {
        Type data; // must be first
        Index next;
        Index prev;
    };

    struct Iterator {
        friend bool operator==(Iterator const& a, Iterator const& b) { return a.index == b.index; }
        friend bool operator!=(Iterator const& a, Iterator const& b) { return a.index != b.index; }
        Type& operator*() const { return list->NodeAt(index)->data; }
        Type* operator->() { return &list->NodeAt(index)->data; }
        Iterator& operator++() {
            index = list->NodeAt(index)->next;
            return *this;
        }
        BoundedList const* list;
        Index index;
    };

    BoundedList() {
        // Fill the free_list. free_list is singly linked.
        auto nodes = (Node*)node_data;
        for (usize i = 0; i < k_size - 1; ++i)
            nodes[i].next = Index {UnderlyingIndexType(i + 1)};
        nodes[k_size - 1].next = k_invalid_index;
        free_list = Index {0};
    }

    ~BoundedList() {
        if constexpr (!TriviallyDestructible<Type>) ASSERT(Empty());
    }

    auto begin() const { return Iterator {this, first}; }
    auto end() const { return Iterator {this, k_invalid_index}; }

    Iterator Remove(Iterator it) {
        ASSERT(!Empty());
        ASSERT(it.index != k_invalid_index);

        auto node = NodeAt(it.index);
        auto next = node->next;
        node->data.~Type();

        // remove node from used list
        if (node->prev != k_invalid_index)
            NodeAt(node->prev)->next = node->next;
        else
            first = node->next;
        if (node->next != k_invalid_index)
            NodeAt(node->next)->prev = node->prev;
        else
            last = node->prev;

        // add node to free_list
        node->next = free_list;
        free_list = it.index;

        return Iterator {this, next};
    }

    void RemoveAll() {
        // IMPROVE: this could be done more efficiently
        while (!Empty())
            RemoveFirst();
    }

    Index Remove(Index index) {
        if (ToInt(index) >= k_size) return k_invalid_index;
        return Remove(Iterator {this, index}).index;
    }

    void RemoveFirst() {
        ASSERT(first != k_invalid_index);
        Remove(first);
    }

    void Remove(Type* value) {
        if (value == nullptr) return;
        ASSERT((uintptr)value >= (uintptr)node_data);
        ASSERT((uintptr)value < (uintptr)node_data + sizeof(node_data));
        ASSERT((uintptr)value % alignof(Node) == 0, "data must be first member of Node");
        auto node = (Node*)value;
        Remove(Index {(UnderlyingIndexType)(node - Nodes())});
    }

    // you must placement-new the result
    Type* AppendUninitialised() {
        if (Full()) return nullptr;

        // pop a node from the free_list
        auto result_index = free_list;
        auto result = NodeAt(result_index);
        free_list = result->next;

        // append node to used list
        result->prev = last;
        result->next = k_invalid_index;
        if (last != k_invalid_index) {
            NodeAt(last)->next = result_index;
            last = result_index;
        } else {
            ASSERT(first == k_invalid_index);
            first = result_index;
            last = result_index;
        }

        return &result->data;
    }

    // you must placement-new the result, never returns null
    Type* AppendUninitalisedOverwrite() {
        if (Full()) RemoveFirst();
        ASSERT(!Full());
        return AppendUninitialised();
    }

    bool Empty() const { return first == k_invalid_index; }
    bool Full() const { return free_list == k_invalid_index; }
    bool ContainsMoreThanOne() const {
        return first != k_invalid_index && NodeAt(first)->next != k_invalid_index;
    }

    Node* Nodes() const { return (Node*)node_data; }
    Node* NodeAt(Index index) const {
        ASSERT(ToInt(index) < k_size);
        return &Nodes()[ToInt(index)];
    }
    Type& First() const { return NodeAt(first)->data; }
    Type& Last() const { return NodeAt(last)->data; }

    // A node is either in the used list (first, last) or in the free_list, not both.
    Index first = k_invalid_index;
    Index last = k_invalid_index;
    Index free_list = k_invalid_index;
    alignas(Node) u8 node_data[k_size * sizeof(Node)];
};
