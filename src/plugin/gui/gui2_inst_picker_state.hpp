// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

#include "state/instrument.hpp"

struct InstPickerState {
    enum class Tab : u8 {
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

    Tab tab {Tab::FloeLibaries};
    DynamicArray<u64> selected_library_hashes {Malloc::Instance()};
    DynamicArray<u64> selected_mirage_library_hashes {Malloc::Instance()};
    DynamicArray<u64> selected_tags_hashes {Malloc::Instance()};
    DynamicArrayBounded<char, 100> search;
};
