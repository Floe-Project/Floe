// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"

namespace vital {

  class LegatoFilter : public Processor {
    public:
      enum {
        kLegato,
        kTrigger,
        kNumInputs
      };

      enum {
        kRetrigger,
        kNumOutputs
      };

      LegatoFilter();

      virtual Processor* clone() const override {
        return new LegatoFilter(*this);
      }

      void process(int num_samples) override;

    private:
      poly_float last_value_;

      JUCE_LEAK_DETECTOR(LegatoFilter)
  };
} // namespace vital

