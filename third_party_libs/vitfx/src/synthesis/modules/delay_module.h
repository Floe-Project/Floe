// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_constants.h"
#include "synth_module.h"

#include "delay.h"

namespace vital {

  class DelayModule : public SynthModule {
    public:
      static constexpr mono_float kMaxDelayTime = 4.0f;
    
      DelayModule(const Output* beats_per_second);
      virtual ~DelayModule();

      virtual void init() override;
      virtual void hardReset() override { delay_->hardReset(); }
      virtual void enable(bool enable) override {
        SynthModule::enable(enable);
        process(1);
        if (!enable)
          delay_->hardReset();
      }
      virtual void setSampleRate(int sample_rate) override;
      virtual void setOversampleAmount(int oversample) override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;
      virtual Processor* clone() const override { return new DelayModule(*this); }
    
    protected:
      const Output* beats_per_second_;
      StereoDelay* delay_;

      JUCE_LEAK_DETECTOR(DelayModule)
  };
} // namespace vital

