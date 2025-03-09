// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/universal_defs.hpp"

template <usize k_size>
struct OpaqueHandle {
    template <typename T>
    T& As() {
        static_assert(sizeof(T) == k_size);
        static_assert(alignof(T) <= __alignof__(data));
        return *reinterpret_cast<T*>(data);
    }
    template <typename T>
    T const& As() const {
        return const_cast<OpaqueHandle*>(this)->As<T>();
    }
    alignas(8) u8 data[k_size];
};
