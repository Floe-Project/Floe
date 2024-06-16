// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "threading.hpp"

#include "foundation/foundation.hpp"

#include "config.h"

static thread_local char g_thread_name[k_max_thread_name_size];

void DebuggerSetThreadName(String name);
void SetThreadName(String name) {
    CopyStringIntoBufferWithNullTerm(g_thread_name, name);
    if (!PRODUCTION_BUILD) DebuggerSetThreadName(name);
}

String ThreadName() {
    if (g_thread_name[0] != '\0') {
        return FromNullTerminated(g_thread_name);
    } else {
        static unsigned counter = 0;
        static thread_local unsigned const id = counter++;
        static char buf[6];
        stbsp_snprintf(buf, (int)ArraySize(buf), "%04x", id);
        return FromNullTerminated(buf);
    }
    return "";
}

static u64 g_main_thread_id {};

void DebugAssertMainThread() {
    if constexpr (PRODUCTION_BUILD) return;

    ASSERT(g_main_thread_id != 0, "Main thread has not been set");
    ASSERT(g_main_thread_id == CurrentThreadID());
}

void DebugSetThreadAsMainThread() {
    if constexpr (PRODUCTION_BUILD) return;

    g_main_thread_id = CurrentThreadID();
}
