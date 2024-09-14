// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "effect.hpp"
#include "param_info.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/filters.hpp"
#include "smoothed_value_system.hpp"

class FilterEffect final : public Effect {
  public:
    FilterEffect(FloeSmoothedValueSystem& s)
        : Effect(s, EffectType::FilterEffect)
        , m_filter_coeff_smoother_id(s.CreateFilterSmoother()) {}

    static bool IsUsingGainParam(StaticSpan<Parameter, k_num_parameters> params) {
        auto filter_type = params[ToInt(ParamIndex::FilterType)].ValueAsInt<param_values::EffectFilterType>();
        return filter_type == param_values::EffectFilterType::HighShelf ||
               filter_type == param_values::EffectFilterType::LowShelf ||
               filter_type == param_values::EffectFilterType::Peak;
    }

  private:
    void PrepareToPlay(AudioProcessingContext const& context) override {
        m_filter_params.fs = context.sample_rate;
        smoothed_value_system.Set(m_filter_coeff_smoother_id, m_filter_params);
    }

    void OnParamChangeInternal(ChangedParams changed_params, AudioProcessingContext const&) override {
        bool set_params = false;
        if (auto p = changed_params.Param(ParamIndex::FilterCutoff)) {
            m_filter_params.fc = p->ProjectedValue();
            set_params = true;
        }
        if (auto p = changed_params.Param(ParamIndex::FilterResonance)) {
            m_filter_params.q = MapFrom01Skew(p->ProjectedValue(), 0.5f, 2, 5);
            set_params = true;
        }
        if (auto p = changed_params.Param(ParamIndex::FilterGain)) {
            m_filter_params.peak_gain = p->ProjectedValue();
            set_params = true;
        }
        if (auto p = changed_params.Param(ParamIndex::FilterType)) {
            rbj_filter::Type filter_type {};
            switch (p->ValueAsInt<param_values::EffectFilterType>()) {
                case param_values::EffectFilterType::LowPass: filter_type = rbj_filter::Type::LowPass; break;
                case param_values::EffectFilterType::HighPass:
                    filter_type = rbj_filter::Type::HighPass;
                    break;
                case param_values::EffectFilterType::BandPass:
                    filter_type = rbj_filter::Type::BandPassCzpg;
                    break;
                case param_values::EffectFilterType::Notch: filter_type = rbj_filter::Type::Notch; break;
                case param_values::EffectFilterType::Peak: filter_type = rbj_filter::Type::Peaking; break;
                case param_values::EffectFilterType::LowShelf:
                    filter_type = rbj_filter::Type::LowShelf;
                    break;
                case param_values::EffectFilterType::HighShelf:
                    filter_type = rbj_filter::Type::HighShelf;
                    break;
                case param_values::EffectFilterType::Count: PanicIfReached(); break;
            }
            m_filter_params.type = filter_type;
            set_params = true;
        }

        if (set_params) smoothed_value_system.Set(m_filter_coeff_smoother_id, m_filter_params);
    }

    StereoAudioFrame
    ProcessFrame(AudioProcessingContext const&, StereoAudioFrame in, u32 frame_index) override {
        auto [coeffs, filter_mix] = smoothed_value_system.Value(m_filter_coeff_smoother_id, frame_index);
        return Process(m_filter2, coeffs, Process(m_filter1, coeffs, in * filter_mix));
    }

    void ResetInternal() override {
        m_filter1 = {};
        m_filter2 = {};
    }

    FloeSmoothedValueSystem::FilterId const m_filter_coeff_smoother_id;
    rbj_filter::StereoData m_filter1 {};
    rbj_filter::StereoData m_filter2 {};
    rbj_filter::Params m_filter_params = {};
};
