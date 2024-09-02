// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/memory/allocators.hpp"
#include "foundation/utils/dummy_mutex.hpp"
#include "foundation/utils/linked_list.hpp"

#include "function.hpp"
#include "optional.hpp"

template <typename MutexType = DummyMutex>
struct FunctionQueue {
    using Function = TrivialFunctionRef<void()>;

    struct Node {
        Node* prev {};
        Node* next {};
        Function function {};
    };

    void Push(Function f) {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        auto node = arena.NewUninitialised<Node>();
        node->function = f.CloneObject(arena);

        DoublyLinkedListAppend(*this, node);
    }

    Optional<Function> TryPop(ArenaAllocator& result_arena) {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        if (!first) return k_nullopt;
        auto result = first->function.CloneObject(result_arena);

        DoublyLinkedListRemoveFirst(*this);
        if (!first) arena.ResetCursorAndConsolidateRegions();

        return result;
    }

    bool Empty() {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        return first == nullptr;
    }

    Node* first {};
    Node* last {};
    ArenaAllocator arena;
    [[no_unique_address]] MutexType mutex;
};
