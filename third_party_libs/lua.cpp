// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: Unlicense

// We compile as C++ so Lua uses C++ exceptions instead of setjmp/longjmp
#define MAKE_LIB
#include <onelua.c>
