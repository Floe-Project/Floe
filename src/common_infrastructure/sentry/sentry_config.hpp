// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "foundation/foundation.hpp"

namespace sentry {

constexpr bool k_online_reporting =
#ifdef SENTRY_DSN
    true;
#else
    false;
#endif

constexpr String k_dsn =
#ifdef SENTRY_DSN
    SENTRY_DSN;
#else
    // When online submission is not active we still want our sentry functions to compile so we can test them
    // and not have to have #ifdefs everywhere, so we define a dummy DSN. We need to make sure we don't
    // actually use the dummy by doing if constexpr (k_online_reporting)
    "https://publickey@host.com/123";
#endif

} // namespace sentry
