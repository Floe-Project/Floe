// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_constants.h"
#include "synth_module.h"

namespace vital {

  class Phaser;

  class PhaserModule : public SynthModule {
    public:
      enum {
        kAudioOutput,
        kCutoffOutput,
        kNumOutputs
      };

      PhaserModule(const Output* beats_per_second);
      virtual ~PhaserModule();

      void init() override;
      void hardReset() override;
      void enable(bool enable) override;

      void correctToTime(double seconds) override;
      void setSampleRate(int sample_rate) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      Processor* clone() const override { return new PhaserModule(*this); }

    protected:
      const Output* beats_per_second_;
      Phaser* phaser_;

      JUCE_LEAK_DETECTOR(PhaserModule)
  };
} // namespace vital

