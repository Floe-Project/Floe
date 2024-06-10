// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

struct DummyMutex {
    void Lock() {}
    void Unlock() {}
    void TryLock() {}
};
