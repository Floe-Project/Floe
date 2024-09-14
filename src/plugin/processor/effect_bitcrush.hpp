// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "effect.hpp"
#include "infos/param_info.hpp"
#include "processing_utils/smoothed_value_system.hpp"

struct BitCrushProcessor {
    static s64 IntegerPowerBase2(s64 exponent) {
        s64 i = 1;
        for (int j = 1; j <= exponent; j++)
            i *= 2;
        return i;
    }

    f32x2 BitCrush(f32x2 input, f32 sample_rate, int bit_depth, int bit_rate) {
        auto const resolution = IntegerPowerBase2(bit_depth) - 1;
        auto const step = (int)(sample_rate / (f32)bit_rate);

        if (pos % step == 0) {
            if (bit_depth < 32)
                held_sample = Round((input + 1.0f) * (f32)resolution) / (f32)resolution - 1.0f;
            else
                held_sample = input;
        }

        pos++;
        if (pos >= bit_rate) pos -= bit_rate;
        pos = Clamp(pos, 0, bit_rate - 1);

        return held_sample;
    }

    int pos = 0;
    f32x2 held_sample = 0;
};

class BitCrush final : public Effect {
  public:
    BitCrush(FloeSmoothedValueSystem& s) : Effect(s, EffectType::BitCrush), m_wet_dry(s) {}

  private:
    StereoAudioFrame
    ProcessFrame(AudioProcessingContext const& context, StereoAudioFrame in, u32 frame_index) override {
        auto const v = m_bit_crusher.BitCrush({in.l, in.r}, context.sample_rate, m_bit_depth, m_bit_rate);
        StereoAudioFrame const wet {v[0], v[1]};
        return m_wet_dry.MixStereo(smoothed_value_system, frame_index, wet, in);
    }

    void OnParamChangeInternal(ChangedParams changed_params, AudioProcessingContext const&) override {
        if (auto p = changed_params.Param(ParamIndex::BitCrushBits)) m_bit_depth = p->ValueAsInt<int>();
        if (auto p = changed_params.Param(ParamIndex::BitCrushBitRate))
            m_bit_rate = (int)(p->ProjectedValue() + 0.5f);
        if (auto p = changed_params.Param(ParamIndex::BitCrushWet))
            m_wet_dry.SetWet(smoothed_value_system, p->ProjectedValue());
        if (auto p = changed_params.Param(ParamIndex::BitCrushDry))
            m_wet_dry.SetDry(smoothed_value_system, p->ProjectedValue());
    }

    int m_bit_depth, m_bit_rate;
    BitCrushProcessor m_bit_crusher;
    EffectWetDryHelper m_wet_dry;
};
