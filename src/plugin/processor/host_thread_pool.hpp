// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "clap/ext/thread-pool.h"

struct HostThreadPool {
    using FunctionType = TrivialFixedSizeFunction<24, void(u32 index)>;

    static Optional<HostThreadPool> Create(clap_host const& host) {
        auto const thread_pool =
            (clap_host_thread_pool const*)host.get_extension(&host, CLAP_EXT_THREAD_POOL);
        return thread_pool != nullptr
                   ? Optional<HostThreadPool> {HostThreadPool {.host = host,
                                                               .host_thread_pool_interface = *thread_pool,
                                                               .function = {}}}
                   : k_nullopt;
    }

    bool RequestMultithreadedExecution(FunctionType&& f, u32 num_times_to_be_called) {
        function = Move(f);
        return host_thread_pool_interface.request_exec(&host, num_times_to_be_called);
    }

    void OnThreadPoolExec(u32 task_index) const { function(task_index); }

    clap_host const& host;
    clap_host_thread_pool const& host_thread_pool_interface;
    FunctionType function;
};
