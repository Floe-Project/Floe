// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"
#include "sample_source.h"

namespace vital {
  class SampleModule : public SynthModule {
    public:
      enum {
        kReset,
        kMidi,
        kNoteCount,
        kNumInputs
      };

      enum {
        kRaw,
        kLevelled,
        kNumOutputs
      };

      SampleModule();
      virtual ~SampleModule() { }

      void process(int num_samples) override;
      void init() override;
      virtual Processor* clone() const override { return new SampleModule(*this); }

      Sample* getSample() { return sampler_->getSample(); }
      force_inline Output* getPhaseOutput() const { return sampler_->getPhaseOutput(); }

    protected:
      std::shared_ptr<bool> was_on_;
      SampleSource* sampler_;
      Value* on_;

      JUCE_LEAK_DETECTOR(SampleModule)
  };
} // namespace vital

