// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"

namespace vital {
  class RandomLfo;

  class RandomLfoModule : public SynthModule {
    public:
      enum {
        kNoteTrigger,
        kMidi,
        kNumInputs
      };

      RandomLfoModule(const std::string& prefix, const Output* beats_per_second);
      virtual ~RandomLfoModule() { }

      void init() override;
      virtual Processor* clone() const override { return new RandomLfoModule(*this); }
      void correctToTime(double seconds) override;

    protected:
      std::string prefix_;
      RandomLfo* lfo_;
      const Output* beats_per_second_;

      JUCE_LEAK_DETECTOR(RandomLfoModule)
  };
} // namespace vital

