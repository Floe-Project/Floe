// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "comb_module.h"

#include "comb_filter.h"

namespace vital {

  CombModule::CombModule() : SynthModule(kNumInputs, 1), comb_filter_(nullptr) { }

  void CombModule::init() {
    comb_filter_ = new CombFilter(kMaxFeedbackSamples);
    addProcessor(comb_filter_);

    comb_filter_->useInput(input(kAudio), CombFilter::kAudio);
    comb_filter_->useInput(input(kMidiCutoff), CombFilter::kMidiCutoff);
    comb_filter_->useInput(input(kStyle), CombFilter::kStyle);
    comb_filter_->useInput(input(kMidiBlendTranspose), CombFilter::kTranspose);
    comb_filter_->useInput(input(kFilterCutoffBlend), CombFilter::kPassBlend);
    comb_filter_->useInput(input(kResonance), CombFilter::kResonance);
    comb_filter_->useInput(input(kReset), CombFilter::kReset);
    comb_filter_->useOutput(output());

    SynthModule::init();
  }

  void CombModule::reset(poly_mask reset_mask) {
    getLocalProcessor(comb_filter_)->reset(reset_mask);
  }

  void CombModule::hardReset() {
    getLocalProcessor(comb_filter_)->hardReset();
  }
} // namespace vital
