// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_constants.h"
#include "synth_module.h"

namespace vital {

  class Distortion;
  class DigitalSvf;

  class DistortionModule : public SynthModule {
    public:
      DistortionModule();
      virtual ~DistortionModule();

      virtual void init() override;
      virtual void setSampleRate(int sample_rate) override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;
      virtual Processor* clone() const override { return new DistortionModule(*this); }

    protected:
      Distortion* distortion_;
      Value* filter_order_;
      DigitalSvf* filter_;
      Output* distortion_mix_;
      poly_float mix_;

      JUCE_LEAK_DETECTOR(DistortionModule)
  };
} // namespace vital

