#pragma once
#include "foundation/memory/allocators.hpp"

// Little util that allows a simple way to push items to a list and not worry about memory. Also allows
// easy access the last item.
template <typename Type>
struct ArenaStack {
    struct Node {
        Node* next {};
        Type data;
    };

    using Iterator = SinglyLinkedListIterator<Node, Type>;

    ArenaStack() = default;
    ArenaStack(Type t, ArenaAllocator& arena) { Append(t, arena); }

    void Append(Type data, ArenaAllocator& arena) {
        ++size;
        auto node = arena.NewUninitialised<Node>();
        node->data = data;
        node->next = nullptr;
        if (last) {
            last->next = node;
            last = node;
        } else {
            first = node;
            last = node;
        }
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
