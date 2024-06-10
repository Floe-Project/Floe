// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/dynamic_array.hpp"
#include "foundation/memory/allocators.hpp"
#include "foundation/universal_defs.hpp"
#include "os/threading.hpp"
#include "utils/debug/debug.hpp"

struct Allocation {
    Span<u8> data;
    Optional<StacktraceStack> stack_trace;
};

class LeakDetectingAllocator : public Allocator {
  public:
    ~LeakDetectingAllocator();

    Span<u8> DoCommand(AllocatorCommandUnion const& options) override;

  private:
    Mutex m_lock {};
    DynamicArray<Allocation> m_allocations {Malloc::Instance()};
};
