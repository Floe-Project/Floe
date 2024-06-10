// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"

#include "synth_constants.h"

namespace vital {
  class CombFilter;
  
  class CombModule : public SynthModule {
    public:
      static constexpr int kMaxFeedbackSamples = 25000;

      enum {
        kAudio,
        kReset,
        kMidiCutoff,
        kMidiBlendTranspose,
        kFilterCutoffBlend,
        kStyle,
        kResonance,
        kMidi,
        kNumInputs
      };

      CombModule();
      virtual ~CombModule() { }

      void init() override;
      void reset(poly_mask reset_mask) override;
      void hardReset() override;
      virtual Processor* clone() const override { return new CombModule(*this); }

    protected:
      CombFilter* comb_filter_;

    JUCE_LEAK_DETECTOR(CombModule)
  };
} // namespace vital

