// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/span.hpp"
#include "foundation/error/error_code.hpp"
#include "foundation/memory/allocators.hpp"

struct ErrorWithPath {
    ErrorWithPath Clone(Allocator& alloc) const {
        return {
            .path = path.Clone(alloc),
            .error = error,
        };
    }

    String path;
    ErrorCode error;
};
