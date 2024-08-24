// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "threading.hpp"

#include "foundation/foundation.hpp"

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
