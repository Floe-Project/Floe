#pragma once
#include "audio_data.hpp"
#include "sample_library/sample_library.hpp"

struct LoadedInstrument {
    sample_lib::Instrument const& instrument;
    Span<AudioData const*> audio_datas {}; // parallel to instrument.regions
    AudioData const* file_for_gui_waveform {};
};
