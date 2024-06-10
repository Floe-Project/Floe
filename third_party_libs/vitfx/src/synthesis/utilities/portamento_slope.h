// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"

namespace vital {

  class PortamentoSlope : public Processor {
    public:
      static constexpr float kMinPortamentoTime = 0.001f;

      enum {
        kTarget,
        kSource,
        kPortamentoForce,
        kPortamentoScale,
        kRunSeconds,
        kSlopePower,
        kReset,
        kNumNotesPressed,
        kNumInputs
      };

      PortamentoSlope();
      virtual ~PortamentoSlope() { }

      virtual Processor* clone() const override {
        return new PortamentoSlope(*this);
      }

      void processBypass(int start);
      virtual void process(int num_samples) override;

    private:
      poly_float position_;

      JUCE_LEAK_DETECTOR(PortamentoSlope)
  };
} // namespace vital

