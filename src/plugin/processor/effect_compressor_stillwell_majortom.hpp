// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "dsp_stillwell_majortom.hpp"
#include "effect.hpp"
#include "infos/param_info.hpp"
#include "processing_utils/audio_utils.hpp"
#include "processing_utils/smoothed_value_system.hpp"

class Compressor final : public Effect {
  public:
    Compressor(FloeSmoothedValueSystem& s) : Effect(s, EffectType::Compressor) {}

  private:
    StereoAudioFrame
    ProcessFrame(AudioProcessingContext const& context, StereoAudioFrame in, u32 frame_index) override {
        (void)frame_index;
        StereoAudioFrame out;
        m_compressor.Process(context.sample_rate, in.l, in.r, out.l, out.r);
        return out;
    }

    void OnParamChangeInternal(ChangedParams changed_params, AudioProcessingContext const& context) override {
        if (auto p = changed_params.Param(ParamIndex::CompressorThreshold))
            m_compressor.slider_threshold = AmpToDb(p->ProjectedValue());
        if (auto p = changed_params.Param(ParamIndex::CompressorRatio))
            m_compressor.slider_ratio = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::CompressorGain))
            m_compressor.slider_gain = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::CompressorAutoGain))
            m_compressor.slider_auto_gain = p->ValueAsBool();

        m_compressor.Update(context.sample_rate);
    }

    void ResetInternal() override { m_compressor.Reset(); }

    void PrepareToPlay(AudioProcessingContext const& context) override {
        m_compressor.SetSampleRate(context.sample_rate);
    }

    StillwellMajorTom m_compressor;
};
