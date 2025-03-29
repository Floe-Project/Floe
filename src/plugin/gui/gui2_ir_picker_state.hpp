// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

struct IrPickerState {
    void ClearAllFilters() {
        dyn::Clear(selected_library_hashes);
        dyn::Clear(selected_tags_hashes);
        dyn::Clear(search);
    }

    DynamicArray<u64> selected_library_hashes {Malloc::Instance()};
    DynamicArray<u64> selected_library_author_hashes {Malloc::Instance()};
    DynamicArray<u64> selected_tags_hashes {Malloc::Instance()};
    DynamicArrayBounded<char, 100> search;
    bool scroll_to_show_selected = false;
};
