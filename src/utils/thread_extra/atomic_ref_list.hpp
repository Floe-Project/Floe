// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

// Lock-free list. Multiple readers, single writer.
//
// Reading speed is the priority. Designed for the case where a background-thread is creating
// expensive-to-construct objects (like file reading + decoding) and a reading thread (such as a GUI thread)
// needs to use the objects with little overhead. The writing thread needs to frequently add or remove items
// from the list. Nodes from this struct can be stored in other data structures such as hash tables if needed.
// So long as node values are accessed with TryRetain and Release.
//
// It's like a bit like a mutex-protected std::list<std::weak_ptr<MyObject>>.
//
// Usage of this class requires some careful considerations.
//
// Important things to note for the _reader_ thread:
// - Once you have a Node * it is guaranteed to always be valid memory. However, it might contain different
//   object for each subsequent access with TryRetain() and Release(). It's like you have a 'slot' rather than
//   an object. You can only be sure what's in the slot when you 'lock' it.
// - Inside a TryRetain() and Release() block, the object is guaranteed to never be deleted.
// - IMPORTANT: iterating though the list is not necessarily consistent. It is possible that live nodes are
//   skipped, or that you get the exact same node more than once. This is very unlikely though, or even
//   impossible depending on when the writer calls DeleteRemovedOrUnreferenced. This limitation is often
//   acceptable though because the reader needs act knowing that items are added or removed often: skipping or
//   repeating are similar in effect to adding or removing.

// IMPROVE: we're doing lots of atomic operations with SequentiallyConsistent ordering. We can use more
// relaxed modes for lots of these.

template <typename ValueType>
struct AtomicRefList {
    // Nodes are never destroyed or freed until this class is destroyed so use-after-free is not an issue. To
    // get around the issues of using-after-destructor, we use weak reference counting involving a bit flag.
    struct Node {
        // reader
        [[nodiscard]] ValueType* TryRetain() {
            auto const r = reader_uses.FetchAdd(1, MemoryOrder::Relaxed);
            if (r & k_dead_bit) [[unlikely]] {
                reader_uses.FetchSub(1, MemoryOrder::AcquireRelease);
                return nullptr;
            }
            return &value;
        }

        // reader, if TryRetain() returned non-null
        void Release() {
            auto const r = reader_uses.FetchSub(1, MemoryOrder::AcquireRelease);
            ASSERT(r != 0);
        }

        struct ScopedAccessPtr {
            NON_COPYABLE(ScopedAccessPtr);
            ScopedAccessPtr(Node* retained) : retained_node(retained) {}
            ScopedAccessPtr(ScopedAccessPtr&& other)
                : retained_node(Exchange(other.retained_node, nullptr)) {}
            ~ScopedAccessPtr() {
                if (retained_node) retained_node->Release();
            }
            explicit operator bool() const { return retained_node != nullptr; }
            ValueType* operator->() const { return &retained_node->value; }
            ValueType& operator*() const { return retained_node->value; }
            Node* retained_node;
        };

        [[nodiscard]] ScopedAccessPtr TryScoped() {
            if (TryRetain()) return {this};
            return {nullptr};
        }

        // Presence of this bit signifies that this node should not be read. However, increment and decrement
        // operations will still work fine regardless of whether it is set - there will be 31-bits of data
        // that track changes. Doing it this way moves the more expensive operations onto the writer thread
        // rather than the reader thread. The writer thread does atomic bitwise-AND (which is sometimes a CAS
        // loop in implementation), but the reader thread can do an atomic increment and then check the bit on
        // the result, non-atomically. The alternative might be to get the reader thread to do an atomic CAS
        // to determine if reader_uses is zero, and only increment it if it's not, but this is likely more
        // expensive.
        static constexpr u32 k_dead_bit = 1u << 31;

        Atomic<u32> reader_uses;
        ValueType value;
        Atomic<Node*> next;
        Node* writer_next;
    };

    struct Iterator {
        friend bool operator==(Iterator const& a, Iterator const& b) { return a.node == b.node; }
        friend bool operator!=(Iterator const& a, Iterator const& b) { return a.node != b.node; }
        Node& operator*() const { return *node; }
        Node* operator->() const { return node; }
        Iterator& operator++() {
            prev = node;
            node = node->next.Load(MemoryOrder::Relaxed);
            return *this;
        }
        Node* node {};
        Node* prev {};
    };

    ~AtomicRefList() {
        // You should RemoveAll and DeleteRemovedAndUnreferenced before the object is destroyed. We don't want
        // to do that here because we want this object to be able to live on a reader thread instead of living
        // on a writer thread.
        ASSERT(live_list.Load() == nullptr);
        ASSERT(dead_list == nullptr);
    }

    // reader or writer
    // If you are the reader the values should be consider weak references: you MUST call TryRetain (and
    // afterwards Release) on the object before using it.
    Iterator begin() const { return Iterator(live_list.Load(MemoryOrder::Acquire), nullptr); }
    Iterator end() const { return Iterator(nullptr, nullptr); }

    // writer, call placement-new on node->value
    Node* AllocateUninitialised() {
        if (free_list) {
            auto node = free_list;
            free_list = free_list->writer_next;
            return node;
        }

        auto node = arena.NewUninitialised<Node>();
        node->reader_uses.raw = 0;
        node->writer_next = nullptr;
        return node;
    }

    // writer, only pass a node just acquired from AllocateUnitialised and placement-new'ed
    void DiscardAllocatedInitialised(Node* node) {
        node->value.~ValueType();
        node->writer_next = free_list;
        free_list = node;
    }

    // writer, node from AllocateUninitalised
    void Insert(Node* node) {
        // insert so the memory is sequential for better cache locality
        Node* insert_after {};
        {
            Node* prev {};
            for (auto n = live_list.Load(MemoryOrder::Relaxed); n != nullptr;
                 n = n->next.Load(MemoryOrder::Relaxed)) {
                if (n > node) {
                    insert_after = prev;
                    break;
                }
                prev = n;
            }
        }

        // put it into the live list
        if (insert_after) {
            node->next.Store(insert_after->next.Load());
            insert_after->next.Store(node);
            ASSERT(node > insert_after);
        } else {
            node->next.Store(live_list.Load());
            live_list.Store(node);
        }

        // signal that the reader can now use this node
        node->reader_uses.FetchAnd(~Node::k_dead_bit);
    }

    // writer, returns next iterator (i.e. instead of ++it in a loop)
    Iterator Remove(Iterator iterator) {
        if constexpr (RUNTIME_SAFETY_CHECKS_ON) {
            bool found = false;
            for (auto n = live_list.Load(MemoryOrder::Relaxed); n != nullptr;
                 n = n->next.Load(MemoryOrder::Relaxed)) {
                if (n == iterator.node) {
                    found = true;
                    break;
                }
            }
            ASSERT(found);
        }

        // remove it from the live_list
        if (iterator.prev)
            iterator.prev->next.Store(iterator.node->next.Load());
        else
            live_list.Store(iterator.node->next.Load());

        // add it to the dead list. we use a separate 'next' variable for this because the reader still might
        // be using the node and it needs to know how to correctly iterate through the list rather than
        // suddendly being redirecting into iterating the dead list
        iterator.node->writer_next = dead_list;
        dead_list = iterator.node;

        // signal that the reader should no longer user this node
        // NOTE: we use the ADD operation here instead of bitwise OR because it's probably faster on x86: the
        // XADD instruction vs the CMPXCHG instruction. This is fine because we know that the dead bit isn't
        // already set and so doing and ADD is the same as doing an OR.
        auto const u = iterator.node->reader_uses.FetchAdd(Node::k_dead_bit);
        ASSERT((u & Node::k_dead_bit) == 0, "already dead");

        return Iterator {.node = iterator.node->next.Load(), .prev = iterator.prev};
    }

    // writer
    void Remove(Node* node) {
        Node* previous {};
        for (auto it = begin(); it != end(); ++it) {
            if (it.node == node) break;
            previous = it.node;
        }
        Remove(Iterator {node, previous});
    }

    // writer
    void RemoveAll() {
        for (auto it = begin(); it != end();)
            it = Remove(it);
    }

    // writer, call this regularly
    void DeleteRemovedAndUnreferenced() {
        Node* previous = nullptr;
        for (auto i = dead_list; i != nullptr;) {
            ASSERT(i->writer_next != i);
            ASSERT(previous != i);
            if (previous) ASSERT(previous != i->writer_next);

            if (i->reader_uses.Load() == Node::k_dead_bit) {
                if (!previous)
                    dead_list = i->writer_next;
                else
                    previous->writer_next = i->writer_next;
                auto next = i->writer_next;
                i->value.~ValueType();
                i->writer_next = free_list;
                free_list = i;
                i = next;
            } else {
                previous = i;
                i = i->writer_next;
            }
        }
    }

    Atomic<Node*> live_list {}; // reader or writer
    Node* dead_list {}; // writer
    Node* free_list {}; // writer
    ArenaAllocator arena {Malloc::Instance()}; // writer
};
