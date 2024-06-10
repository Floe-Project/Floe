// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor_router.h"
#include "circular_queue.h"
#include "synth_constants.h"

namespace vital {

  class VocalTract : public ProcessorRouter {
    public:
      enum {
        kAudio,
        kReset,
        kBlend,
        kTonguePosition,
        kTongueHeight,
        kNumInputs
      };

      VocalTract();
      virtual ~VocalTract();

      virtual Processor* clone() const override { return new VocalTract(*this); }

      void reset(poly_mask reset_mask) override;
      void hardReset() override;

      virtual void process(int num_samples) override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;

    private:
      JUCE_LEAK_DETECTOR(VocalTract)
  };
} // namespace vital

