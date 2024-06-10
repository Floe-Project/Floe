// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "random_lfo_module.h"

#include "random_lfo.h"

namespace vital {

  RandomLfoModule::RandomLfoModule(const std::string& prefix, const Output* beats_per_second) :
      SynthModule(kNumInputs, 1), prefix_(prefix), beats_per_second_(beats_per_second) {
    lfo_ = new RandomLfo();
    addProcessor(lfo_);
  }

  void RandomLfoModule::init() {
    Output* free_frequency = createPolyModControl(prefix_ + "_frequency");
    Value* style = createBaseControl(prefix_ + "_style");
    Value* stereo = createBaseControl(prefix_ + "_stereo");
    Value* sync_type = createBaseControl(prefix_ + "_sync_type");

    Output* frequency = createTempoSyncSwitch(prefix_, free_frequency->owner, beats_per_second_, true, input(kMidi));
    lfo_->useInput(input(kNoteTrigger), RandomLfo::kReset);
    lfo_->useOutput(output());
    lfo_->plug(frequency, RandomLfo::kFrequency);
    lfo_->plug(style, RandomLfo::kStyle);
    lfo_->plug(stereo, RandomLfo::kStereo);
    lfo_->plug(sync_type, RandomLfo::kSync);
  }

  void RandomLfoModule::correctToTime(double seconds) {
    lfo_->correctToTime(seconds);
  }
} // namespace vital
