// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "sample_library/sample_library.hpp"

enum class InstrumentType : u32 {
    None,
    WaveformSynth,
    Sampler,
};

enum class WaveformType : u32 {
    Sine,
    WhiteNoiseMono,
    WhiteNoiseStereo,
    Count,
};

constexpr auto k_waveform_type_names = Array {
    "Sine"_s,
    "White Noise Mono",
    "White Noise Stereo",
};
static_assert(k_waveform_type_names.size == ToInt(WaveformType::Count));

using InstrumentId = TaggedUnion<InstrumentType,
                                 TypeAndTag<WaveformType, InstrumentType::WaveformSynth>,
                                 TypeAndTag<sample_lib::InstrumentId, InstrumentType::Sampler>>;
