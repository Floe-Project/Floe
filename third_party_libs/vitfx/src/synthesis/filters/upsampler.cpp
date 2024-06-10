// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "upsampler.h"

namespace vital {
  Upsampler::Upsampler() : ProcessorRouter(kNumInputs, 1) { }

  Upsampler::~Upsampler() { }

  void Upsampler::process(int num_samples) {
    const poly_float* audio_in = input(kAudio)->source->buffer;
    processWithInput(audio_in, num_samples);
  }

  void Upsampler::processWithInput(const poly_float* audio_in, int num_samples) {
    poly_float* destination = output()->buffer;

    int oversample_amount = getOversampleAmount();

    for (int i = 0; i < num_samples; ++i) {
      int offset = i * oversample_amount;
      for (int s = 0; s < oversample_amount; ++s)
        destination[offset + s] = audio_in[i];
    }
  }
} // namespace vital
