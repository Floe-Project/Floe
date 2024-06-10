// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"
#include "delay.h"

namespace vital {
  class ChorusModule : public SynthModule {
    public:
      static constexpr mono_float kMaxChorusModulation = 0.03f;
      static constexpr mono_float kMaxChorusDelay = 0.08f;
      static constexpr int kMaxDelayPairs = 4;

      ChorusModule(const Output* beats_per_second);
      virtual ~ChorusModule() { }

      void init() override;
      void enable(bool enable) override;

      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void correctToTime(double seconds) override;
      Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }

      int getNextNumVoicePairs();

    protected:
      const Output* beats_per_second_;
      Value* voices_;

      int last_num_voices_;
    
      cr::Output delay_status_outputs_[kMaxDelayPairs];

      Output* frequency_;
      Output* delay_time_1_;
      Output* delay_time_2_;
      Output* mod_depth_;
      Output* wet_output_;
      poly_float phase_;
      poly_float wet_;
      poly_float dry_;

      poly_float delay_input_buffer_[kMaxBufferSize];

      cr::Value delay_frequencies_[kMaxDelayPairs];
      MultiDelay* delays_[kMaxDelayPairs];

      JUCE_LEAK_DETECTOR(ChorusModule) 
  };
} // namespace vital

