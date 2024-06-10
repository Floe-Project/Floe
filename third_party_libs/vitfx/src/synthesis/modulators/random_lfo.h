// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"
#include "utils.h"

namespace vital {

  class RandomLfo : public Processor {
    public:
      struct RandomState {
        RandomState() {
          offset = 0.0f;
          last_random_value = 0.0f;
          next_random_value = 0.0f;

          state1 = 0.1f;
          state2 = 0.0f;
          state3 = 0.0f;
        }

        poly_float offset;
        poly_float last_random_value;
        poly_float next_random_value;

        poly_float state1, state2, state3;
      };

      enum {
        kFrequency,
        kAmplitude,
        kReset,
        kSync,
        kStyle,
        kRandomType,
        kStereo,
        kNumInputs
      };

      enum RandomType {
        kPerlin,
        kSampleAndHold,
        kSinInterpolate,
        kLorenzAttractor,
        kNumStyles
      };

      RandomLfo();

      virtual Processor* clone() const override { return new RandomLfo(*this); }
      void process(int num_samples) override;
      void process(RandomState* state, int num_samples);
      void processSampleAndHold(RandomState* state, int num_samples);
      void processLorenzAttractor(RandomState* state, int num_samples);
      void correctToTime(double seconds);

    protected:
      void doReset(RandomState* state, bool mono, poly_float frequency);
      poly_int updatePhase(RandomState* state, int num_samples);

      RandomState state_;
      std::shared_ptr<RandomState> shared_state_;

      utils::RandomGenerator random_generator_;
      poly_float last_value_;

      std::shared_ptr<double> sync_seconds_;
      std::shared_ptr<double> last_sync_;

      JUCE_LEAK_DETECTOR(RandomLfo)
  };
} // namespace vital

