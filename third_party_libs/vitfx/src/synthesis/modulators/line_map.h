// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"

class LineGenerator;

namespace vital {

  class LineMap : public Processor {
    public:
      static constexpr mono_float kMaxPower = 20.0f;

      enum MapOutput {
        kValue,
        kPhase,
        kNumOutputs
      };

      LineMap(LineGenerator* source);

      virtual Processor* clone() const override { return new LineMap(*this); }
      void process(int num_samples) override;
      void process(poly_float phase);

    protected:
      poly_float offset_;
      LineGenerator* source_;

      JUCE_LEAK_DETECTOR(LineMap)
  };
} // namespace vital

