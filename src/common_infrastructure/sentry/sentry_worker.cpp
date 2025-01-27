// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sentry_worker.hpp"

namespace sentry {

static Atomic<Worker*> g_worker = nullptr;
alignas(Worker) static u8 g_worker_storage[sizeof(Worker)];

Worker* InitGlobalWorker(Span<Tag const> tags) {
    if constexpr (!k_active) return nullptr;

    auto existing = g_worker.Load(LoadMemoryOrder::Acquire);
    if (existing) return existing;

    auto worker = PLACEMENT_NEW(g_worker_storage) Worker {};
    StartThread(*worker, tags);
    g_worker.Store(worker, StoreMemoryOrder::Release);
    return worker;
}

Worker* GlobalWorker() {
    if constexpr (!k_active) return nullptr;

    return g_worker.Load(LoadMemoryOrder::Acquire);
}

} // namespace sentry
