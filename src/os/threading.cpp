// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "threading.hpp"

#include "foundation/foundation.hpp"
#include "utils/debug/tracy_wrapped.hpp"

#include "config.h"

static u64 g_main_thread_id {};

void DebugAssertMainThread() {
    if constexpr (PRODUCTION_BUILD) return;

    ASSERT(g_main_thread_id != 0, "Main thread has not been set");
    ASSERT(g_main_thread_id == CurrentThreadId());
}

void DebugSetThreadAsMainThread() {
    if constexpr (PRODUCTION_BUILD) return;

    g_main_thread_id = CurrentThreadId();
}

bool IsMainThread() {
    ASSERT(g_main_thread_id != 0, "Main thread has not been set");
    return g_main_thread_id == CurrentThreadId();
}

thread_local DynamicArrayInline<char, k_max_thread_name_size> g_thread_name {};

namespace detail {

void SetThreadLocalThreadName(String name) {
    ASSERT(name.size < k_max_thread_name_size, "Thread name is too long");
    for (auto c : name)
        ASSERT(c != ' ' && c != '_' && !IsUppercaseAscii(c),
               "Thread names must be lowercase and not contain spaces");

    dyn::Assign(g_thread_name, name);
    tracy::SetThreadName(dyn::NullTerminated(g_thread_name));
}

Optional<String> GetThreadLocalThreadName() {
    if (!g_thread_name.size) return nullopt;
    return g_thread_name.Items();
}

} // namespace detail
