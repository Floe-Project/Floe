// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "audio_data.hpp"
#include "sample_library/sample_library.hpp"

// We use the term 'resource' for things that are loaded from a sample library: instruments, irs, images, etc.

struct LoadedInstrument {
    sample_lib::Instrument const& instrument;
    Span<AudioData const*> audio_datas {}; // parallel to instrument.regions
    AudioData const* file_for_gui_waveform {};
};

struct LoadedIr {
    sample_lib::ImpulseResponse const& ir;
    AudioData const* audio_data;
};
