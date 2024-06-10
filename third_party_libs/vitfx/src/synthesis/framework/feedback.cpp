// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "feedback.h"

#include "processor_router.h"

namespace vital {

  void Feedback::process(int num_samples) {
    VITAL_ASSERT(inputMatchesBufferSize());

    const poly_float* audio_in = input(0)->source->buffer;
    for (int i = 0; i < num_samples; ++i) {
      buffer_[buffer_index_] = audio_in[i];
      buffer_index_ = (buffer_index_ + 1) % kMaxBufferSize;
    }
  }

  void Feedback::refreshOutput(int num_samples) {
    poly_float* audio_out = output(0)->buffer;
    int index = (kMaxBufferSize + buffer_index_ - num_samples) % kMaxBufferSize;
    for (int i = 0; i < num_samples; ++i) {
      audio_out[i] = buffer_[index];
      index = (index + 1) % kMaxBufferSize;
    }
  }
} // namespace vital
