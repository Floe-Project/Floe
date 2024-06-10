// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dc_filter.h"

namespace vital {

  DcFilter::DcFilter() : Processor(DcFilter::kNumInputs, 1) {
    coefficient_ = 0.0f;
    reset(constants::kFullMask);
  }

  void DcFilter::setSampleRate(int sample_rate) {
    Processor::setSampleRate(sample_rate);
    coefficient_ = 1.0f - kCoefficientToSrConstant / getSampleRate();
  }

  void DcFilter::process(int num_samples) {
    VITAL_ASSERT(inputMatchesBufferSize(kAudio));
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }

  void DcFilter::processWithInput(const poly_float* audio_in, int num_samples) {
    poly_mask reset_mask = getResetMask(kReset);
    if (reset_mask.anyMask())
      reset(reset_mask);

    poly_float* audio_out = output()->buffer;
    for (int i = 0; i < num_samples; ++i)
      tick(audio_in[i], audio_out[i]);
  }

  force_inline void DcFilter::tick(const poly_float& audio_in, poly_float& audio_out) {
    audio_out = utils::mulAdd(audio_in - past_in_, past_out_, coefficient_);
    past_out_ = audio_out;
    past_in_ = audio_in;
  }

  void DcFilter::reset(poly_mask reset_mask) {
    past_in_ = utils::maskLoad(past_in_, 0.0f, reset_mask);
    past_out_ = utils::maskLoad(past_in_, 0.0f, reset_mask);
  }
} // namespace vital
