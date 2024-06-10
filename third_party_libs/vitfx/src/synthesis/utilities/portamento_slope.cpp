// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "portamento_slope.h"

#include "utils.h"
#include "futils.h"

namespace vital {
  PortamentoSlope::PortamentoSlope() : Processor(kNumInputs, 1, true), position_(0.0f) { }

  void PortamentoSlope::processBypass(int start) {
    position_ = 1.0f;
    output()->buffer[0] = input(kTarget)->source->buffer[0];
  }

  void PortamentoSlope::process(int num_samples) {
    bool force = input(kPortamentoForce)->at(0)[0];
    poly_float run_seconds = input(kRunSeconds)->at(0);

    poly_mask active_mask = poly_float::greaterThan(run_seconds, kMinPortamentoTime);

    if (active_mask.anyMask() == 0) {
      processBypass(0);
      return;
    }

    poly_mask reset_mask = getResetMask(kReset);
    position_ = utils::maskLoad(position_, 0.0f, reset_mask);

    if (!force) {
      poly_float num_voices = input(kNumNotesPressed)->at(0);
      reset_mask = reset_mask & poly_float::equal(num_voices, 1.0f);
      position_ = utils::maskLoad(position_, 1.0f, reset_mask);
    }

    poly_float target = input(kTarget)->at(0);
    poly_float source = input(kSource)->at(0);

    if (input(kPortamentoScale)->at(0)[0]) {
      poly_float midi_delta = poly_float::abs(target - source);
      run_seconds *= midi_delta * (1.0f / kNotesPerOctave);
    }
    
    poly_float position_delta = poly_float(1.0f * num_samples) / (run_seconds * getSampleRate());
    position_ = utils::clamp(position_ + position_delta, 0.0f, 1.0f);

    poly_float power = -input(kSlopePower)->at(0);
    poly_float adjusted_position = futils::powerScale(position_, power);
    output()->buffer[0] = utils::interpolate(source, target, adjusted_position);
  }
} // namespace vital
