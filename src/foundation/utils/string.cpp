// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "string.hpp"

#include <math.h> // HUGE_VAL
#include <stdlib.h> // strtod

Optional<double> ParseFloat(String str, usize* num_chars_read) {
    char buffer[32];
    CopyStringIntoBufferWithNullTerm(buffer, str);
    char* str_end {};
    auto result = strtod(buffer, &str_end);
    if (result == HUGE_VAL) return nullopt;
    usize const chars_read =
        (str_end >= buffer && str_end <= (buffer + sizeof(buffer))) ? (usize)(str_end - buffer) : 0;
    if (chars_read == 0) return nullopt;
    if (num_chars_read) *num_chars_read = chars_read;
    return result;
}
