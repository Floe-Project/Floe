// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor_router.h"
#include "synth_constants.h"

namespace vital {

  class Upsampler : public ProcessorRouter {
    public:
      enum {
        kAudio,
        kNumInputs
      };

      Upsampler();
      virtual ~Upsampler();

      virtual Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }

      virtual void process(int num_samples) override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;

    private:
      JUCE_LEAK_DETECTOR(Upsampler)
  };
} // namespace vital

