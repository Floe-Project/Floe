// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"
#include "delay.h"

namespace vital {

  class FlangerModule : public SynthModule {
    public:
      static constexpr mono_float kMaxFlangerSemitoneOffset = 24.0f;
      static constexpr mono_float kFlangerDelayRange = 0.01f;
      static constexpr mono_float kFlangerCenter = kFlangerDelayRange * 0.5f + 0.0005f;
      static constexpr mono_float kModulationDelayBuffer = 0.0005f;

      enum {
        kAudioOutput,
        kFrequencyOutput,
        kNumOutputs
      };

      FlangerModule(const Output* beats_per_second);
      virtual ~FlangerModule();

      void init() override;
      void hardReset() override { delay_->hardReset(); }
      void enable(bool enable) override {
        SynthModule::enable(enable);
        process(1);
        if (!enable)
          delay_->hardReset();
      }

      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void correctToTime(double seconds) override;

      Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }

    protected:
      const Output* beats_per_second_;
      Output* frequency_;
      Output* phase_offset_;
      Output* center_;
      Output* mod_depth_;
      poly_float phase_;

      cr::Value delay_frequency_;
      StereoDelay* delay_;

      JUCE_LEAK_DETECTOR(FlangerModule)
  };
} // namespace vital

