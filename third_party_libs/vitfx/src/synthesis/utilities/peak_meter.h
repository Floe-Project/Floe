// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"

namespace vital {

  class PeakMeter : public Processor {
    public:
      static constexpr int kMaxRememberedPeaks = 16;

      enum {
        kLevel,
        kMemoryPeak,
        kNumOutputs
      };

      PeakMeter();

      virtual Processor* clone() const override { return new PeakMeter(*this); }
      void process(int num_samples) override;

    protected:
      poly_float current_peak_;
      poly_float current_square_sum_;

      poly_float remembered_peak_;
      poly_int samples_since_remembered_;

      JUCE_LEAK_DETECTOR(PeakMeter)
  };
} // namespace vital

