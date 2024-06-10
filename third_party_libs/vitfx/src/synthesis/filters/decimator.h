// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor_router.h"
#include "synth_constants.h"

namespace vital {

  class IirHalfbandDecimator;

  class Decimator : public ProcessorRouter {
    public:
      enum {
        kAudio,
        kNumInputs
      };

      Decimator(int max_stages = 1);
      virtual ~Decimator();

      void init() override;
      void reset(poly_mask reset_mask) override;

      virtual Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }

      virtual void process(int num_samples) override;
      virtual void setOversampleAmount(int) override { }

    private:
      int num_stages_;
      int max_stages_;

      std::vector<IirHalfbandDecimator*> stages_;

      JUCE_LEAK_DETECTOR(Decimator)
  };
} // namespace vital

