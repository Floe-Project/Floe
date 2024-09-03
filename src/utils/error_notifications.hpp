// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/threading.hpp"
#include "utils/thread_extra/atomic_ref_list.hpp"

// This is for errors that we want to let the user know about.
struct ThreadsafeErrorNotifications {
    struct Item {
        DynamicArrayBounded<char, 64> title;
        DynamicArrayBounded<char, 512> message;
        Optional<ErrorCode> error_code;
        u64 id;
    };

    using ItemList = AtomicRefList<Item>;

    static constexpr u64 Id(char const (&data)[5], String string_to_hash) {
        return ((u64)U32FromChars(data) << 32) | Hash32(string_to_hash);
    }

    ThreadsafeErrorNotifications() = default;
    NON_COPYABLE_AND_MOVEABLE(ThreadsafeErrorNotifications);

    ~ThreadsafeErrorNotifications() {
        writer_mutex.Lock();
        DEFER { writer_mutex.Unlock(); };
        items.RemoveAll();
        items.DeleteRemovedAndUnreferenced();
    }

    // returns an uninitialised node so that you can fill in the actual details rather than copying other
    // allocated formatted strings
    ItemList::Node* NewError() {
        writer_mutex.Lock();
        DEFER { writer_mutex.Unlock(); };

        return items.AllocateUninitialised();
    }

    void AddOrUpdateError(ItemList::Node* node) {
        writer_mutex.Lock();
        DEFER { writer_mutex.Unlock(); };

        for (auto& i : items) {
            if (i.value.id == node->value.id) {
                i.value = node->value;
                items.DiscardAllocatedInitialised(node);
                return;
            }
        }

        items.Insert(node);
    }

    void RemoveError(u64 id) {
        writer_mutex.Lock();
        DEFER { writer_mutex.Unlock(); };

        for (auto it = items.begin(); it != items.end();)
            if (it->value.id == id)
                it = items.Remove(it);
            else
                ++it;

        items.DeleteRemovedAndUnreferenced();
    }

    Mutex writer_mutex {};
    AtomicRefList<Item> items {.arena = {PageAllocator::Instance()}};
};
