// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"
#include "formant_module.h"

namespace vital {
  class CombModule;
  class DigitalSvf;
  class DiodeFilter;
  class DirtyFilter;
  class Interpolate;
  class LadderFilter;
  class PhaserFilter;
  class SallenKeyFilter;

  class FilterModule : public SynthModule {
    public:
      enum {
        kAudio,
        kReset,
        kKeytrack,
        kMidi,
        kNumInputs
      };

      FilterModule(std::string prefix = "");
      virtual ~FilterModule() { }

      void process(int num_samples) override;
      void setCreateOnValue(bool create_on_value) { create_on_value_ = create_on_value; }
      void setMono(bool mono);
      Output* createModControl(std::string name, bool audio_rate = false, bool smooth_value = false,
                               Output* internal_modulation = nullptr);
      void init() override;
      void hardReset() override;
      virtual Processor* clone() const override {
        FilterModule* newModule = new FilterModule(*this);
        newModule->last_model_ = -1;
        return newModule;
      }

      const Value* getOnValue() { return on_; }

    protected:
      void setModel(int new_model);

      int last_model_;
      bool was_on_;
      std::string prefix_;
      bool create_on_value_;
      bool mono_;

      Value* on_;
      Value* filter_model_;
      poly_float mix_;

      Output* filter_mix_;

      CombModule* comb_filter_;
      DigitalSvf* digital_svf_;
      DiodeFilter* diode_filter_;
      DirtyFilter* dirty_filter_;
      FormantModule* formant_filter_;
      LadderFilter* ladder_filter_;
      PhaserFilter* phaser_filter_;
      SallenKeyFilter* sallen_key_filter_;

      JUCE_LEAK_DETECTOR(FilterModule)
  };
} // namespace vital

