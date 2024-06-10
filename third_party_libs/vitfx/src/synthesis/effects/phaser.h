// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor_router.h"
#include "phaser_filter.h"
#include "operators.h"

namespace vital {

  class PhaserFilter;

  class Phaser : public ProcessorRouter {
    public:
      enum {
        kAudio,
        kMix,
        kRate,
        kFeedbackGain,
        kCenter,
        kModDepth,
        kPhaseOffset,
        kBlend,
        kNumInputs
      };

      enum {
        kAudioOutput,
        kCutoffOutput,
        kNumOutputs
      };

      Phaser();
      virtual ~Phaser() { }

      virtual Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }
      void process(int num_samples) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void init() override;
      void hardReset() override;
      void correctToTime(double seconds);
      void setOversampleAmount(int oversample) override {
        ProcessorRouter::setOversampleAmount(oversample);
        cutoff_.ensureBufferSize(oversample * kMaxBufferSize);
      }

    private:
      Output cutoff_;
      PhaserFilter* phaser_filter_;
      poly_float mix_;
      poly_float mod_depth_;
      poly_float phase_offset_;
      poly_int phase_;

  };
} // namespace vital

