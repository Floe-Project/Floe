// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_constants.h"
#include "synth_module.h"

namespace vital {

  class MultibandCompressor;

  class CompressorModule : public SynthModule {
    public:
      enum {
        kAudio,
        kLowInputMeanSquared,
        kBandInputMeanSquared,
        kHighInputMeanSquared,
        kLowOutputMeanSquared,
        kBandOutputMeanSquared,
        kHighOutputMeanSquared,
        kNumOutputs
      };

      CompressorModule();
      virtual ~CompressorModule();

      virtual void init() override;
      virtual void setSampleRate(int sample_rate) override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;
      virtual void enable(bool enable) override;
      virtual void hardReset() override;
      virtual Processor* clone() const override { return new CompressorModule(*this); }

    protected:
      MultibandCompressor* compressor_;

      JUCE_LEAK_DETECTOR(CompressorModule)
  };
} // namespace vital

