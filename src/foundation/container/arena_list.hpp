// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/memory/allocators.hpp"
#include "foundation/utils/linked_list.hpp"

template <typename Type, bool k_owns_arena>
struct ArenaList {
    template <typename U = Type>
    requires(!k_owns_arena)
    ArenaList(ArenaAllocator& a) : arena(a) {}

    template <typename U = Type>
    requires(k_owns_arena)
    ArenaList(Allocator& a) : arena(a) {}

    template <typename U = Type>
    requires(k_owns_arena)
    ArenaList(ArenaList&& other)
        : arena(Move(other.arena))
        , first(Exchange(other.first, nullptr))
        , free_list(Exchange(other.free_list, nullptr)) {}

    ~ArenaList() {
        if constexpr (!TriviallyDestructible<Type>) ASSERT(first == nullptr);
    }

    struct Node {
        Type data;
        Node* next {};
    };

    using Iterator = SinglyLinkedListIterator<Node, Type>;

    Node* AllocateNodeUninitialised() {
        if (free_list) {
            auto result = free_list;
            free_list = free_list->next;
            return result;
        }

        return arena.template NewUninitialised<Node>();
    }

    void PrependNode(Node* node) {
        node->next = first;
        first = node;
    }

    template <typename... Args>
    void Prepend(Args&&... args) {
        auto ptr = PrependUninitialised();
        PLACEMENT_NEW(ptr) Type {Forward<Args>(args)...};
    }

    void Delete(Node* node) {
        node->data.~Type();
        node->next = free_list;
        free_list = node;
    }

    // call placement-new on the result
    Type* PrependUninitialised() {
        if (free_list) {
            auto result = free_list;
            free_list = free_list->next;
            PrependNode(result);
            return &result->data;
        }

        auto new_node = arena.template NewUninitialised<Node>();
        PrependNode(new_node);
        return &new_node->data;
    }

    template <typename FunctionType>
    void RemoveIf(FunctionType&& should_remove_value) {
        SinglyLinkedListRemoveIf(
            first,
            [&should_remove_value](Node const& node) { return should_remove_value(node.data); },
            [this](Node* node) { Delete(node); });
    }

    void Remove(Type const* value) {
        SinglyLinkedListRemoveIf(
            first,
            [value](Node const& node) { return &node.data == value; },
            [this](Node* node) { Delete(node); });
    }

    void Clear() {
        while (first) {
            auto next = first->next;
            Delete(first);
            first = next;
        }
    }

    bool Empty() const { return first == nullptr; }

    Iterator begin() const { return Iterator {first}; }
    Iterator end() const { return Iterator {nullptr}; }

    using Arena = Conditional<k_owns_arena, ArenaAllocator, ArenaAllocator&>;

    Arena arena;
    Node* first {};
    Node* free_list {};
};
