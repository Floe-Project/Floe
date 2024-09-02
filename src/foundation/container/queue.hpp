// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/memory/allocators.hpp"
#include "foundation/utils/dummy_mutex.hpp"
#include "foundation/utils/linked_list.hpp"

#include "optional.hpp"

template <typename Type, typename MutexType = DummyMutex>
struct Queue {
    struct Node {
        Node* prev {};
        Node* next {};
        Type data {};
    };

    template <typename... Args>
    void Push(Args&&... args) {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        auto node = arena.NewUninitialised<Node>();
        PLACEMENT_NEW(&node->data) Type(Forward<Args>(args)...);

        DoublyLinkedListAppend(*this, node);
    }

    Optional<Type> TryPop() {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        if (!first) return k_nullopt;
        auto result = first->data;

        DoublyLinkedListRemoveFirst(*this);
        if (!first) arena.ResetCursorAndConsolidateRegions();

        return result;
    }

    bool Empty() {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        return first == nullptr;
    }

    ArenaAllocator arena;
    Node* first {};
    Node* last {};
    [[no_unique_address]] MutexType mutex {};
};
