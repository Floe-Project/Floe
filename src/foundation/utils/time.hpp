// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/span.hpp"

constexpr String k_day_names_short[] = {
    "Sun"_s,
    "Mon"_s,
    "Tue"_s,
    "Wed"_s,
    "Thu"_s,
    "Fri"_s,
    "Sat"_s,
};

constexpr String k_month_names_short[] = {
    "Jan"_s,
    "Feb"_s,
    "Mar"_s,
    "Apr"_s,
    "May"_s,
    "Jun"_s,
    "Jul"_s,
    "Aug"_s,
    "Sep"_s,
    "Oct"_s,
    "Nov"_s,
    "Dec"_s,
};

struct DateAndTime {
    String MonthName() const { return k_month_names_short[months_since_jan]; }
    String DayName() const { return k_day_names_short[days_since_sunday]; }
    s16 year;
    s8 months_since_jan; // 0-11
    s8 day_of_month; // 1-31
    s8 days_since_sunday; // 0-6
    s8 hour;
    s8 minute;
    s8 second;
    s16 millisecond;
    s16 microsecond;
    s16 nanosecond;
};
