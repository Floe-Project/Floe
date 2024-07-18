// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/memory/allocators.hpp"
#include "foundation/utils/linked_list.hpp"

// Little util that allows a simple way to push items to a list and not worry about memory. Also allows
// easy access the last item.
template <typename Type>
struct ArenaStack {
    struct Node {
        Type data;
        Node* next {};
    };

    using Iterator = SinglyLinkedListIterator<Node, Type>;

    ArenaStack() = default;
    ArenaStack(Type t, ArenaAllocator& arena) { Append(t, arena); }

    void Append(Type data, ArenaAllocator& arena) {
        ++size;
        auto node = arena.NewUninitialised<Node>();
        node->data = data;
        DoublyLinkedListAppend(*this, node);
    }

    Type Last() const { return last->data; }

    void Clear() {
        first = nullptr;
        last = nullptr;
        size = 0;
    }

    auto begin() const { return Iterator {first}; }
    auto end() const { return Iterator {nullptr}; }

    Node* first {};
    Node* last {};
    u32 size {};
};
