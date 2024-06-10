// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_constants.h"
#include "synth_module.h"

namespace vital {

  class Reverb;

  class ReverbModule : public SynthModule {
    public:
      ReverbModule();
      virtual ~ReverbModule();

      void init() override;
      void hardReset() override;
      void enable(bool enable) override;

      void setSampleRate(int sample_rate) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      Processor* clone() const override { return new ReverbModule(*this); }

    protected:
      Reverb* reverb_;

      JUCE_LEAK_DETECTOR(ReverbModule)
  };
} // namespace vital

