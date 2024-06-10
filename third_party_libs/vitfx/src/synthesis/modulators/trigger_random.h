// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"
#include "utils.h"

namespace vital {

  class TriggerRandom : public Processor {
    public:
      enum {
        kReset,
        kNumInputs
      };

      TriggerRandom();
      virtual ~TriggerRandom() { }

      virtual Processor* clone() const override { return new TriggerRandom(*this); }
      virtual void process(int num_samples) override;

    private:
      poly_float value_;
      utils::RandomGenerator random_generator_;

      JUCE_LEAK_DETECTOR(TriggerRandom)
  };
} // namespace vital

