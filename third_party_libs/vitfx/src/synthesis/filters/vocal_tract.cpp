// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vocal_tract.h"

namespace vital {
  VocalTract::VocalTract() : ProcessorRouter(kNumInputs, 1) { }

  VocalTract::~VocalTract() { }

  void VocalTract::reset(poly_mask reset_mask) {
  }

  void VocalTract::hardReset() {
    reset(constants::kFullMask);
  }

  void VocalTract::process(int num_samples) {
    const poly_float* audio_in = input(kAudio)->source->buffer;
    processWithInput(audio_in, num_samples);
  }

  void VocalTract::processWithInput(const poly_float* audio_in, int num_samples) {
  }
} // namespace vital
