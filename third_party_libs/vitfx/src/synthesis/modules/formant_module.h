// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"
#include "formant_filter.h"

namespace vital {
  class FormantModule : public SynthModule {
    public:
      enum {
        kAudio,
        kReset,
        kResonance,
        kBlend,
        kStyle,
        kNumInputs
      };

      FormantModule(std::string prefix = "");
      virtual ~FormantModule() { }

      Output* createModControl(std::string name, bool audio_rate = false, bool smooth_value = false);

      void init() override;
      void process(int num_samples) override;
      void reset(poly_mask reset_mask) override;
      void hardReset() override;
      void setMono(bool mono) { mono_ = mono; }
      virtual Processor* clone() const override { return new FormantModule(*this); }

    protected:
      void setStyle(int new_style);

      std::string prefix_;

      Processor* formant_filters_[FormantFilter::kTotalFormantFilters];
      int last_style_;
      bool mono_;

      JUCE_LEAK_DETECTOR(FormantModule)
  };
} // namespace vital

