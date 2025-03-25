// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

struct InstPickerState {
    enum class Tab : u32 {
        FloeLibaries,
        MirageLibraries,
        Waveforms,
        Count,
    };

    sample_lib::FileFormat FileFormatForCurrentTab() const {
        switch (tab) {
            case InstPickerState::Tab::FloeLibaries: return sample_lib::FileFormat::Lua;
            case InstPickerState::Tab::MirageLibraries: return sample_lib::FileFormat::Mdata;
            case InstPickerState::Tab::Waveforms:
            case InstPickerState::Tab::Count: PanicIfReached();
        }
        return sample_lib::FileFormat::Lua;
    }

    void ClearAllFilters() {
        dyn::Clear(selected_library_hashes);
        dyn::Clear(selected_mirage_library_hashes);
        dyn::Clear(selected_tags_hashes);
        dyn::Clear(search);
    }

    bool HasFilters() const {
        return selected_library_hashes.size || selected_mirage_library_hashes.size ||
               selected_tags_hashes.size || search.size;
    }

    Tab tab {Tab::FloeLibaries};
    DynamicArray<u64> selected_library_hashes {Malloc::Instance()};
    DynamicArray<u64> selected_mirage_library_hashes {Malloc::Instance()};
    DynamicArray<u64> selected_tags_hashes {Malloc::Instance()};
    DynamicArrayBounded<char, 100> search;
    bool scroll_to_show_selected = false;
};
