// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"

class LineGenerator;

namespace vital {
  class SynthLfo;

  class LfoModule : public SynthModule {
    public:
      enum {
        kNoteTrigger,
        kNoteCount,
        kMidi,
        kNumInputs
      };

      enum {
        kValue,
        kOscPhase,
        kOscFrequency,
        kNumOutputs
      };

      LfoModule(const std::string& prefix, LineGenerator* line_generator, const Output* beats_per_second);
      virtual ~LfoModule() { }

      void init() override;
      virtual Processor* clone() const override { return new LfoModule(*this); }
      void correctToTime(double seconds) override;
      void setControlRate(bool control_rate) override;

    protected:
      std::string prefix_;
      SynthLfo* lfo_;
      const Output* beats_per_second_;

      JUCE_LEAK_DETECTOR(LfoModule)
  };
} // namespace vital

