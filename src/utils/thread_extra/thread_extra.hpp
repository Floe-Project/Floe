// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

template <typename Type>
using ThreadsafeQueue = Queue<Type, Mutex>;

using ThreadsafeFunctionQueue = FunctionQueue<Mutex>;
