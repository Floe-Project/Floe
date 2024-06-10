// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"
#include "synth_constants.h"

namespace vital {

  class IirHalfbandDecimator : public Processor {
    public:
      static constexpr int kNumTaps9 = 2;
      static constexpr int kNumTaps25 = 6;
      static poly_float kTaps9[kNumTaps9];
      static poly_float kTaps25[kNumTaps25];

      enum {
        kAudio,
        kNumInputs
      };

      IirHalfbandDecimator();
      virtual ~IirHalfbandDecimator() { }

      virtual Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }

      virtual void process(int num_samples) override;
      void reset(poly_mask reset_mask) override;
      force_inline void setSharpCutoff(bool sharp_cutoff) { sharp_cutoff_ = sharp_cutoff; }

    private:
      bool sharp_cutoff_;
      poly_float in_memory_[kNumTaps25];
      poly_float out_memory_[kNumTaps25];

      JUCE_LEAK_DETECTOR(IirHalfbandDecimator)
  };
} // namespace vital

