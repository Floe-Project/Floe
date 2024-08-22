#pragma once

#include <foundation/foundation.hpp>
#include <os/threading.hpp>

// Lock-free triple buffer with atomic swap. Single producer, single consumer. Producer thread writes and
// publishes when it's done. Consumer thread can safely read blocks of data that are published.
// Super fast and simple but uses 3x the memory.
// https://github.com/brilliantsugar/trio
// https://github.com/HadrienG2/triple-buffer
template <TriviallyCopyable Type>
struct AtomicSwapBuffer {
    // producer thread
    Type& Write() { return buffers[back_buffer_index].data; }

    // producer thread
    void Publish() {
        // place the back buffer index in the middle buffer state along with the dirty bit
        auto const old_middle_state =
            middle_buffer_state.Exchange(back_buffer_index | k_dirty_bit, RmwMemoryOrder::AcquireRelease);

        // the old middle state is what we will use for writing next
        back_buffer_index = old_middle_state & k_dirty_mask;
    }

    // consumer thread
    // we could return a bool to indicate if the buffer was dirty, but we don't need it for now
    Type const& Consume() {
        // if it's not dirty, we don't swap anything, just return the front
        if (!(middle_buffer_state.Load(LoadMemoryOrder::Relaxed) & k_dirty_bit))
            return buffers[front_buffer_index].data;

        // if it's dirty, we swap the middle with the front (with no dirty bit)
        auto const prev = middle_buffer_state.Exchange(front_buffer_index, RmwMemoryOrder::AcquireRelease);
        front_buffer_index = prev & k_dirty_mask;
        return buffers[front_buffer_index].data;
    }

    static constexpr u32 k_dirty_bit = 1u << 31;
    static constexpr u32 k_dirty_mask = ~k_dirty_bit;

    struct Buffer {
        alignas(k_destructive_interference_size) Type data;
    };
    Buffer buffers[3];

    // producer only
    alignas(k_destructive_interference_size) u32 back_buffer_index {0};
    // producer and consumer, contains both index and dirty bit
    alignas(k_destructive_interference_size) Atomic<u32> middle_buffer_state {1};
    // consumer
    alignas(k_destructive_interference_size) u32 front_buffer_index {2};
};
