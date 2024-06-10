// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"
#include "synth_oscillator.h"

namespace vital {
  class Wavetable;

  class OscillatorModule : public SynthModule {
    public:
      enum {
        kReset,
        kRetrigger,
        kMidi,
        kActiveVoices,
        kNumInputs
      };

      enum {
        kRaw,
        kLevelled,
        kNumOutputs
      };

      OscillatorModule(std::string prefix = "");
      virtual ~OscillatorModule() { }

      void process(int num_samples) override;
      void init() override;
      virtual Processor* clone() const override { return new OscillatorModule(*this); }

      Wavetable* getWavetable() { return wavetable_.get(); }
      force_inline SynthOscillator* oscillator() { return oscillator_; }
      SynthOscillator::DistortionType getDistortionType() {
        int val = distortion_type_->value();
        return static_cast<SynthOscillator::DistortionType>(val);
      }

    protected:
      std::string prefix_;
      std::shared_ptr<Wavetable> wavetable_;
      std::shared_ptr<bool> was_on_;

      Value* on_;
      SynthOscillator* oscillator_;
      Value* distortion_type_;

      JUCE_LEAK_DETECTOR(OscillatorModule)
  };
} // namespace vital

