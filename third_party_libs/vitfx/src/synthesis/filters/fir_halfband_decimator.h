// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"
#include "synth_constants.h"

namespace vital {

  class FirHalfbandDecimator : public Processor {
    public:
      static constexpr int kNumTaps = 32;

      enum {
        kAudio,
        kNumInputs
      };

      FirHalfbandDecimator();
      virtual ~FirHalfbandDecimator() { }

      virtual Processor* clone() const override { return new FirHalfbandDecimator(*this); }

      void saveMemory(int num_samples);
      virtual void process(int num_samples) override;

    private:
      void reset(poly_mask reset_mask) override;

      poly_float memory_[kNumTaps / 2 - 1];
      poly_float taps_[kNumTaps / 2];

      JUCE_LEAK_DETECTOR(FirHalfbandDecimator)
  };
} // namespace vital

