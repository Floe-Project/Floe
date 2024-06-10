// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "phaser_filter.h"

#include "futils.h"

namespace vital {
  PhaserFilter::PhaserFilter(bool clean) : Processor(PhaserFilter::kNumInputs, 1) {
    clean_ = clean;
    hardReset();
    invert_mult_ = 1.0f;
  }

  void PhaserFilter::reset(poly_mask reset_mask) {
    allpass_output_ = utils::maskLoad(allpass_output_, 0.0f, reset_mask);
    for (int i = 0; i < kMaxStages; ++i)
      stages_[i].reset(reset_mask);

    remove_lows_stage_.reset(reset_mask);
    remove_highs_stage_.reset(reset_mask);
  }

  void PhaserFilter::hardReset() {
    reset(constants::kFullMask);
    resonance_ = 0.0f;
    drive_ = 0.0f;
    peak1_amount_ = 0.0f;
    peak3_amount_ = 0.0f;
    peak5_amount_ = 0.0f;
    allpass_output_ = 0.0f;
  }

  void PhaserFilter::process(int num_samples) {
    VITAL_ASSERT(inputMatchesBufferSize(kAudio));
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }
  
  void PhaserFilter::processWithInput(const poly_float* audio_in, int num_samples) {
    if (clean_)
      process<futils::tanh, utils::pass>(audio_in, num_samples);
    else
      process<utils::pass, futils::hardTanh>(audio_in, num_samples);
  }

  void PhaserFilter::setupFilter(const FilterState& filter_state) {
    poly_float resonance_percent = utils::clamp(filter_state.resonance_percent, 0.0f, 1.0f);
    resonance_ = utils::interpolate(kMinResonance, kMaxResonance, resonance_percent);
    drive_ = (resonance_ * 0.5f + 1.0f) * filter_state.drive;

    poly_float blend = filter_state.pass_blend;
    peak1_amount_ = utils::clamp(-blend + 1.0f, 0.0f, 1.0f);
    peak5_amount_ = utils::clamp(blend - 1.0f, 0.0f, 1.0f);
    peak3_amount_ = -peak1_amount_ - peak5_amount_ + 1.0f;

    if (filter_state.style)
      invert_mult_ = -1.0f;
    else
      invert_mult_ = 1.0f;
  }
} // namespace vital
