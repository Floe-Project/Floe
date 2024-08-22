// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

template <usize val>
constexpr bool k_power_of_two = IsPowerOfTwo(val);

// An atomic lock-free fixed-size ring buffer.
//
// The size must be a power of 2.
//
// There are different functions for Pop/Push based on the whether you need multiple producers/consumers or
// not. The single producer/consumer functions should be marginally faster. You decide what combination you
// want using the template parameters. A consumer is a thread that calls Pop and a producer is a thread that
// calls Push.
//
// Some tricks used here:
// - Instead of doing a modulo to clamp indexes to the size, we use the bitwise AND operator and a mask of
//   size - 1. This is a cheaper operation and is a nice property of having a power-of-2 size.
// - The head/tail indexes are not clamped to the size of the buffer, instead they just keep increasing in
//   size. This allows us to distinguish between full and empty without wasting a slot. This works because
//   of the power-of-2 requirement and properties of unsigned integer overflow. See the snellman.net link.
//
// https://doc.dpdk.org/guides-19.05/prog_guide/ring_lib.html
// https://svnweb.freebsd.org/base/release/12.2.0/sys/sys/buf_ring.h?revision=367086&view=markup
// https://github.com/eldipa/loki
// https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/

enum class NumProducers { One, Many };
enum class NumConsumers { One, Many };

template <TriviallyCopyable Type, usize k_size, NumProducers k_num_producers, NumConsumers k_num_consumers>
requires(k_power_of_two<k_size>)
struct AtomicQueue {
    static constexpr u32 k_mask = k_size - 1;

    bool Push(Type const& item) { return Push({&item, 1}); }

    bool Pop(Type& item) {
        if (Pop({&item, 1})) return true;
        return false;
    }

    DynamicArrayInline<Type, k_size> PopAll() {
        DynamicArrayInline<Type, k_size> result;
        result.ResizeWithoutCtorDtor(k_size);
        auto num = Pop(result.Items());
        result.ResizeWithoutCtorDtor(num);
        return result;
    }

    template <typename U = Type>
    requires(k_num_producers == NumProducers::One)
    bool Push(Span<Type const> data) {
        // Step 1: copy into local variables and check for size
        auto const initial_producer_head = producer.head;
        auto const consumer_tail = consumer.tail.Load(LoadMemoryOrder::Acquire);

        // Step 2: check for entries and return if we can't do the push
        auto const entries_to_add = (u32)data.size;
        auto const free_entries = k_size - (initial_producer_head - consumer_tail);
        ASSERT(free_entries <= k_size);
        if (free_entries < entries_to_add) [[unlikely]]
            return false;

        // Step 3: calculate the new producer head
        auto new_producer_head = initial_producer_head + entries_to_add;
        producer.head = new_producer_head;

        // Step 4: perform the copy
        for (auto const i : Range(entries_to_add)) {
            auto const ring_index = (initial_producer_head + i) & k_mask;
            m_data[ring_index] = data[i];
        }

        // Step 5: we've done the copy, we can now move the tail so that any consumer can access the objects
        // we've added
        producer.tail.Store(new_producer_head, StoreMemoryOrder::Release);
        return true;
    }

    // Returns the number of elements that were actually popped
    template <typename Unused = Type>
    requires(k_num_consumers == NumConsumers::One)
    u32 Pop(Span<Type> out_buffer) {
        // Step 1: copy into local variables
        auto const initial_consumer_head = consumer.head;
        auto const producer_tail = producer.tail.Load(LoadMemoryOrder::Acquire);

        // Step 2: check for entries and ensure we only pop as many as are ready
        auto const ready_entries = producer_tail - initial_consumer_head;
        if (!ready_entries) return 0;
        auto entries_to_remove = (u32)out_buffer.size;
        if (ready_entries < entries_to_remove) entries_to_remove = ready_entries;

        // Step 3: calculate the new consumer head
        auto new_consumer_head = initial_consumer_head + entries_to_remove;
        consumer.head = new_consumer_head;

        // Step 4: perform the copy
        for (auto const i : Range(entries_to_remove)) {
            auto const ring_index = (initial_consumer_head + i) & k_mask;
            out_buffer[i] = m_data[ring_index];
        }

        // Step 5: we've done the copy, we can now move the tail so that any producer can use the slots again
        consumer.tail.Store(new_consumer_head, StoreMemoryOrder::Release);
        return entries_to_remove;
    }

    template <typename U = Type>
    requires(k_num_producers == NumProducers::Many)
    bool Push(Span<Type const> data) {
        auto const desired_number_to_push = (u32)data.size;

        auto producer_head = producer.head.Load(LoadMemoryOrder::Relaxed);

        u32 free_entries;
        u32 new_producer_head;
        while (true) {
            AtomicThreadFence(RmwMemoryOrder::Acquire);

            auto consumer_tail = consumer.tail.Load(LoadMemoryOrder::Acquire);

            free_entries = k_size - (producer_head - consumer_tail);
            if (free_entries < desired_number_to_push) [[unlikely]]
                return false;
            new_producer_head = producer_head + desired_number_to_push;

            // We redo this loop if producer.head has changed since we loaded producer_head. This would be the
            // case if another thread was also doing a Push simultaneously. When we redo, we recalculate the
            // region that we want to write. It's worth remembering that compare_exchange_weak updates the
            // producer_head value in the failure case.
            if (producer.head.CompareExchangeWeak(producer_head,
                                                  new_producer_head,
                                                  RmwMemoryOrder::Relaxed,
                                                  LoadMemoryOrder::Relaxed))
                break;
        }

        ASSERT(desired_number_to_push > 0 && desired_number_to_push < k_size);
        ASSERT(free_entries >= desired_number_to_push);

        for (auto const i : Range(desired_number_to_push)) {
            auto const ring_index = (producer_head + i) & k_mask;
            m_data[ring_index] = data[i];
        }

        // There might be another thread in this same path, it did the CAS loop but it hasn't done the data
        // copy yet. If we increase the producer.tail, we will be broadcasting that there are new entries
        // available to pop, but they would not be the entries the we just wrote above, they would be the
        // other thread's incomplete entries.
        while (producer.tail.Load(LoadMemoryOrder::Relaxed) != producer_head)
            SpinLoopPause();

        producer.tail.Store(new_producer_head, StoreMemoryOrder::Release);
        return true;
    }

    template <typename Unused = Type>
    requires(k_num_consumers == NumConsumers::Many)
    u32 Pop(Span<Type> out_buffer) {
        auto const desired_number_to_pop = (u32)out_buffer.size;

        auto old_cons_head = consumer.head.Load(LoadMemoryOrder::Relaxed);
        u32 new_consumer_head;
        u32 ready_entries;
        u32 entries_to_pop;
        while (true) {
            AtomicThreadFence(RmwMemoryOrder::Acquire);

            auto prod_tail = producer.tail.Load(LoadMemoryOrder::Acquire);

            ready_entries = prod_tail - old_cons_head;

            entries_to_pop = Min(desired_number_to_pop, ready_entries);
            if (!entries_to_pop) return 0;

            new_consumer_head = old_cons_head + entries_to_pop;

            if (consumer.head.CompareExchangeWeak(old_cons_head,
                                                  new_consumer_head,
                                                  RmwMemoryOrder::Relaxed,
                                                  LoadMemoryOrder::Relaxed))
                break;
        }

        ASSERT(entries_to_pop <= producer.tail.Load(LoadMemoryOrder::Relaxed) - old_cons_head);
        ASSERT(entries_to_pop > 0 && entries_to_pop <= desired_number_to_pop);
        ASSERT(ready_entries >= entries_to_pop);

        for (auto const i : Range(entries_to_pop)) {
            auto const ring_index = (old_cons_head + i) & k_mask;
            out_buffer[i] = m_data[ring_index];
        }

        while (consumer.tail.Load(LoadMemoryOrder::Relaxed) != old_cons_head)
            SpinLoopPause();

        consumer.tail.Store(new_consumer_head, StoreMemoryOrder::Release);
        return entries_to_pop;
    }

    struct alignas(k_destructive_interference_size) {
        Conditional<k_num_producers == NumProducers::One, u32 volatile, Atomic<u32>> head {0};
        Atomic<u32> tail {0};
    } producer;

    struct alignas(k_destructive_interference_size) {
        Conditional<k_num_consumers == NumConsumers::One, u32 volatile, Atomic<u32>> head {0};
        Atomic<u32> tail {0};
    } consumer;

    UninitialisedArray<Type, k_size> m_data {};
};
