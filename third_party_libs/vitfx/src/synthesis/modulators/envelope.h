// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"
#include "utils.h"

namespace vital {

  class Envelope : public Processor {
    public:
      enum {
        kDelay,
        kAttack,
        kAttackPower,
        kHold,
        kDecay,
        kDecayPower,
        kSustain,
        kRelease,
        kReleasePower,
        kTrigger,
        kNumInputs
      };

      enum ProcessorOutput {
        kValue,
        kPhase,
        kNumOutputs
      };

      Envelope();
      virtual ~Envelope() { }

      virtual Processor* clone() const override { return new Envelope(*this); }
      virtual void process(int num_samples) override;

    private:
      void processControlRate(int num_samples);
      void processAudioRate(int num_samples);

      poly_float processSection(poly_float* audio_out, int from, int to,
                                poly_float power, poly_float delta_power,
                                poly_float position, poly_float delta_position,
                                poly_float start, poly_float end, poly_float delta_end);

      poly_float current_value_;

      poly_float position_;
      poly_float value_;
      poly_float poly_state_;
      poly_float start_value_;

      poly_float attack_power_;
      poly_float decay_power_;
      poly_float release_power_;
      poly_float sustain_;

      JUCE_LEAK_DETECTOR(Envelope)
  };
} // namespace vital

