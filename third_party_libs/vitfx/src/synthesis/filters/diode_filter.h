// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"
#include "synth_filter.h"

#include "futils.h"
#include "one_pole_filter.h"

namespace vital {

  class DiodeFilter : public Processor, public SynthFilter {
    public:
      static constexpr mono_float kMinResonance = 0.7f;
      static constexpr mono_float kMaxResonance = 17.0f;
      static constexpr mono_float kMinCutoff = 1.0f;
      static constexpr mono_float kHighPassFrequency = 20.0f;

      DiodeFilter();
      virtual ~DiodeFilter() { }

      virtual Processor* clone() const override { return new DiodeFilter(*this); }
      virtual void process(int num_samples) override;

      void setupFilter(const FilterState& filter_state) override;

      force_inline void tick(poly_float audio_in, poly_float coefficient,
                             poly_float high_pass_ratio, poly_float high_pass_amount,
                             poly_float high_pass_feedback_coefficient,
                             poly_float resonance, poly_float drive);

      void reset(poly_mask reset_mask) override;
      void hardReset() override;

      poly_float getResonance() { return resonance_; }
      poly_float getDrive() { return drive_; }
      poly_float getHighPassRatio() { return high_pass_ratio_; }
      poly_float getHighPassAmount() { return high_pass_amount_; }

    private:
      poly_float resonance_;
      poly_float drive_;
      poly_float post_multiply_;
      poly_float high_pass_ratio_;
      poly_float high_pass_amount_;
      poly_float feedback_high_pass_coefficient_;

      static force_inline poly_float saturate(poly_float value) {
        return futils::tanh(value);
      }

      static force_inline poly_float saturate2(poly_float value) {
        return utils::clamp(value, -1.0f, 1.0f);
      }

      OnePoleFilter<> high_pass_1_;
      OnePoleFilter<> high_pass_2_;

      OnePoleFilter<> high_pass_feedback_;
      OnePoleFilter<saturate> stage1_;
      OnePoleFilter<> stage2_;
      OnePoleFilter<> stage3_;
      OnePoleFilter<saturate2> stage4_;

  };
} // namespace vital

