// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/error_notifications.hpp"

#include "sqlite/sqlite3.h"

struct FloeLibrary;

extern ErrorCodeCategory const sqlite_error_category;

ErrorCodeOr<sqlite3*> CreatePresetDatabase(ArenaAllocator& arena);
void DestroyPresetDatabase(sqlite3* db);

ErrorCodeOr<void> RescanPresetDatabase(sqlite3* db,
                                       ArenaAllocator& arena,
                                       Span<String> always_scanned_folders,
                                       Span<String> extra_scan_folders,
                                       ThreadsafeErrorNotifications& errors);
