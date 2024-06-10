// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"

#include "memory.h"
#include "one_pole_filter.h"

namespace vital {

  template<class MemoryType>
  class Delay : public Processor {
    public:
      static constexpr mono_float kSpreadOctaveRange = 8.0f;
      static constexpr mono_float kDefaultPeriod = 100.0f;
      static constexpr mono_float kDelayHalfLife = 0.02f;
      static constexpr mono_float kMinDampNote = 60.0f;
      static constexpr mono_float kMaxDampNote = 136.0f;

      static poly_float getFilterRadius(poly_float spread) {
        return utils::max(spread * kSpreadOctaveRange * kNotesPerOctave, 0.0f);
      }

      enum {
        kAudio,
        kWet,
        kFrequency,
        kFrequencyAux,
        kFeedback,
        kDamping,
        kStyle,
        kFilterCutoff,
        kFilterSpread,
        kNumInputs
      };

      enum Style {
        kMono,
        kStereo,
        kPingPong,
        kMidPingPong,
        kNumStyles,
        kClampedDampened,
        kClampedUnfiltered,
        kUnclampedUnfiltered,
      };

      Delay(int size) : Processor(Delay::kNumInputs, 1) {
        memory_ = std::make_unique<MemoryType>(size);
        last_frequency_ = 2.0f;
        feedback_ = 0.0f;
        wet_ = 0.0f;
        dry_ = 0.0f;

        filter_gain_ = 0.0f;
        low_coefficient_ = 0.0f;
        high_coefficient_ = 0.0f;
        period_ = utils::min(kDefaultPeriod, size - 1);
        hardReset();
      }

      virtual ~Delay() { }

      virtual Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }

      void hardReset() override;
      void setMaxSamples(int max_samples);

      virtual void process(int num_samples) override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;

      void processCleanUnfiltered(const poly_float* audio_in, int num_samples,
                                  poly_float current_period, poly_float current_feedback,
                                  poly_float current_wet, poly_float current_dry);

      void processUnfiltered(const poly_float* audio_in, int num_samples,
                             poly_float current_period, poly_float current_feedback,
                             poly_float current_wet, poly_float current_dry);

      void process(const poly_float* audio_in, int num_samples,
                   poly_float current_period, poly_float current_feedback, poly_float current_filter_gain,
                   poly_float current_low_coefficient, poly_float current_high_coefficient,
                   poly_float current_wet, poly_float current_dry);

      void processDamped(const poly_float* audio_in, int num_samples,
                         poly_float current_period, poly_float current_feedback,
                         poly_float current_low_coefficient,
                         poly_float current_wet, poly_float current_dry);

      void processPingPong(const poly_float* audio_in, int num_samples,
                           poly_float current_period, poly_float current_feedback, poly_float current_filter_gain,
                           poly_float current_low_coefficient, poly_float current_high_coefficient,
                           poly_float current_wet, poly_float current_dry);

      void processMonoPingPong(const poly_float* audio_in, int num_samples,
                               poly_float current_period, poly_float current_feedback, poly_float current_filter_gain,
                               poly_float current_low_coefficient, poly_float current_high_coefficient,
                               poly_float current_wet, poly_float current_dry);

      poly_float tickCleanUnfiltered(poly_float audio_in, poly_float period, poly_float feedback,
                                     poly_float wet, poly_float dry);

      poly_float tickUnfiltered(poly_float audio_in, poly_float period, poly_float feedback,
                                poly_float wet, poly_float dry);

      poly_float tick(poly_float audio_in, poly_float period, poly_float feedback,
                      poly_float filter_gain, poly_float low_coefficient, poly_float high_coefficient,
                      poly_float wet, poly_float dry);

      poly_float tickDamped(poly_float audio_in, poly_float period,
                            poly_float feedback, poly_float low_coefficient,
                            poly_float wet, poly_float dry);

      poly_float tickPingPong(poly_float audio_in, poly_float period, poly_float feedback,
                              poly_float filter_gain, poly_float low_coefficient, poly_float high_coefficient,
                              poly_float wet, poly_float dry);

      poly_float tickMonoPingPong(poly_float audio_in, poly_float period, poly_float feedback,
                                  poly_float filter_gain, poly_float low_coefficient, poly_float high_coefficient,
                                  poly_float wet, poly_float dry);

    protected:
      Delay() : Processor(0, 0) { }

      std::unique_ptr<MemoryType> memory_;
      poly_float last_frequency_;
      poly_float feedback_;
      poly_float wet_;
      poly_float dry_;
      poly_float period_;

      poly_float low_coefficient_;
      poly_float high_coefficient_;
      poly_float filter_gain_;

      OnePoleFilter<> low_pass_;
      OnePoleFilter<> high_pass_;

  };

  typedef Delay<StereoMemory> StereoDelay;
  typedef Delay<Memory> MultiDelay;
} // namespace vital

