// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"
#include "oscillator_module.h"
#include "sample_module.h"

namespace vital {
  class ProducersModule : public SynthModule {
    public:
      enum {
        kReset,
        kRetrigger,
        kMidi,
        kActiveVoices,
        kNoteCount,
        kNumInputs
      };

      enum {
        kToFilter1,
        kToFilter2,
        kRawOut,
        kDirectOut,
        kNumOutputs
      };

      static force_inline int getFirstModulationIndex(int index) {
        return index == 0 ? 1 : 0;
      }

      static force_inline int getSecondModulationIndex(int index) {
        return index == 1 ? 2 : (getFirstModulationIndex(index) + 1);
      }

      ProducersModule();
      virtual ~ProducersModule() { }

      void process(int num_samples) override;
      void init() override;
      virtual Processor* clone() const override { return new ProducersModule(*this); }

      Wavetable* getWavetable(int index) {
        return oscillators_[index]->getWavetable();
      }

      Sample* getSample() { return sampler_->getSample(); }
      Output* samplePhaseOutput() { return sampler_->getPhaseOutput(); }
      void setFilter1On(const Value* on) { filter1_on_ = on; }
      void setFilter2On(const Value* on) { filter2_on_ = on; }

    protected:
      bool isFilter1On() { return filter1_on_ == nullptr || filter1_on_->value() != 0.0f; }
      bool isFilter2On() { return filter2_on_ == nullptr || filter2_on_->value() != 0.0f; }
      OscillatorModule* oscillators_[kNumOscillators];
      Value* oscillator_destinations_[kNumOscillators];
      Value* sample_destination_;
      SampleModule* sampler_;

      const Value* filter1_on_;
      const Value* filter2_on_;

      JUCE_LEAK_DETECTOR(ProducersModule)
  };
} // namespace vital

