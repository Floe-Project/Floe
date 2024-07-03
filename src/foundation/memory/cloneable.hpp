// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/universal_defs.hpp"

struct Allocator;

enum class CloneType { Shallow, Deep };

template <typename Type>
concept Cloneable = requires(Type const t, Allocator& a) {
    { t.Clone(a, CloneType {}) } -> Same<Type>;
};
