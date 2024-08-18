// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wextra-semi"
#include "tracy/Tracy.hpp" // IWYU pragma: export
#include "tracy/TracyC.h" // IWYU pragma: export
#pragma clang diagnostic pop

#ifdef TRACY_ENABLE
constexpr bool k_tracy_enable = true;
#else
constexpr bool k_tracy_enable = false;
#endif
