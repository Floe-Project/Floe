// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"

namespace vital {
  class Envelope;

  class EnvelopeModule : public SynthModule {
    public:
      enum {
        kTrigger,
        kNumInputs
      };
    
      enum {
        kValue,
        kPhase,
        kNumOutputs
      };

      EnvelopeModule(const std::string& prefix, bool force_audio_rate = false);
      virtual ~EnvelopeModule() { }

      void init() override;
      virtual Processor* clone() const override { return new EnvelopeModule(*this); }

      void setControlRate(bool control_rate) override { 
        if (!force_audio_rate_)
          envelope_->setControlRate(control_rate);
      }

    protected:
      std::string prefix_;
      Envelope* envelope_;
      bool force_audio_rate_;
    
      JUCE_LEAK_DETECTOR(EnvelopeModule)
  };
} // namespace vital

