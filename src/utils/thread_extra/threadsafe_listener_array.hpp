// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

template <typename Function>
class ThreadsafeListenerArray {
  public:
    u64 Add(Function&& f) {
        ScopedMutexLock const lock(m_mutex);
        auto const id = m_id++;
        dyn::Append(m_listeners,
                    Item {
                        .id = id,
                        .function = Move(f),
                    });
        return id;
    }

    void Remove(u64 id) {
        ScopedMutexLock const lock(m_mutex);
        dyn::RemoveValueIfSwapLast(m_listeners, [id](Item const& l) { return l.id == id; });
    }

    template <typename... Args>
    void Call(Args... args) {
        ScopedMutexLock const lock(m_mutex);
        for (auto& l : m_listeners)
            l.function(Forward<Args>(args)...);
    }

  private:
    struct Item {
        u64 id;
        Function function;
    };

    Mutex m_mutex {};
    u64 m_id = 1;
    DynamicArray<Item> m_listeners {Malloc::Instance()};
};
