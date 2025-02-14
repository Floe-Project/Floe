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

constexpr int k_days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

PUBLIC bool IsLeapYear(int year) {
    if (year % 4 != 0) return false;
    if (year % 100 != 0) return true;
    if (year % 400 != 0) return false;
    return true;
}

// Month is 0-11
PUBLIC int DaysOfMonth(int month, int year) {
    if (month == 1 && IsLeapYear(year)) return 29;
    return k_days_in_month[month];
}

struct DateAndTime {
    String MonthName() const { return k_month_names_short[months_since_jan]; }
    String DayName() const { return k_day_names_short[days_since_sunday]; }
    bool IsValid(bool require_after_epoch) const {
        if (require_after_epoch)
            if (year < 1970) return false;
        if (months_since_jan < 0 || months_since_jan > 11) return false;
        if (day_of_month < 1 || day_of_month > k_days_in_month[months_since_jan] +
                                                   (months_since_jan == 1 && IsLeapYear(year) ? 1 : 0))
            return false;
        if (hour < 0 || hour > 23) return false;
        if (minute < 0 || minute > 59) return false;
        if (second < 0 || second > 59) return false;
        if (millisecond < 0 || millisecond > 999) return false;
        if (microsecond < 0 || microsecond > 999) return false;
        if (nanosecond < 0 || nanosecond > 999) return false;
        return true;
    }
    s16 year;
    s8 months_since_jan; // 0-11
    s8 day_of_month; // 1-31
    s8 days_since_sunday; // 0-6 (not strictly necessary, but useful)
    s8 hour;
    s8 minute;
    s8 second;
    s16 millisecond;
    s16 microsecond;
    s16 nanosecond;
};
