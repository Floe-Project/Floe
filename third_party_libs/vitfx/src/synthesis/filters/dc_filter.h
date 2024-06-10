// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"
#include "synth_constants.h"

namespace vital {

  class DcFilter : public Processor {
    public:
      static constexpr mono_float kCoefficientToSrConstant = 1.0f;

      enum {
        kAudio,
        kReset,
        kNumInputs
      };

      DcFilter();
      virtual ~DcFilter() { }

      virtual Processor* clone() const override { return new DcFilter(*this); }
      virtual void process(int num_samples) override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;

      void setSampleRate(int sample_rate) override;
      void tick(const poly_float& audio_in, poly_float& audio_out);

    private:
      void reset(poly_mask reset_mask) override;

      mono_float coefficient_;

      // Past input and output values.
      poly_float past_in_;
      poly_float past_out_;

      JUCE_LEAK_DETECTOR(DcFilter)
  };
} // namespace vital

