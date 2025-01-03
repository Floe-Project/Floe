#pragma once

#include "foundation/universal_defs.hpp"

struct ErrorTrace {
    static constexpr u32 k_max_errors = 8; // must be a power of 2
    SourceLocation error_trace[k_max_errors] {};
    u32 count = 0;

    void Begin() { count = 0; }

    void Trace(SourceLocation e) {
        if (count < k_max_errors) {
            error_trace[count] = e;
            ++count;
        }
    }
};

static ErrorTrace g_error_trace;
