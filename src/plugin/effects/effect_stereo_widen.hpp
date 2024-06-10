// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "effect.hpp"
#include "processing/stereo_audio_frame.hpp"
#include "smoothed_value_system.hpp"

// http://www.musicdsp.org/show_archive_comment.php?ArchiveID=256
// public domain
// 'width' is the stretch factor of the stereo field:
// width < 1: decrease in stereo width
// width = 1: no change
// width > 1: increase in stereo width
// width = 0: mono
inline void DoStereoWiden(f32 width, f32 in_left, f32 in_right, f32& out_left, f32& out_right) {
    auto const coef_s = width * 0.5f;
    auto const m = (in_left + in_right) * 0.5f;
    auto const s = (in_right - in_left) * coef_s;
    out_left = m - s;
    out_right = m + s;
}

inline StereoAudioFrame DoStereoWiden(f32 width, StereoAudioFrame in) {
    StereoAudioFrame result;
    DoStereoWiden(width, in.l, in.r, result.l, result.r);
    return result;
}

class StereoWiden final : public Effect {
  public:
    StereoWiden(FloeSmoothedValueSystem& s)
        : Effect(s, EffectType::StereoWiden)
        , m_width_smoother_id(s.CreateSmoother()) {}

    inline StereoAudioFrame
    ProcessFrame(AudioProcessingContext const&, StereoAudioFrame in, u32 frame_index) override {
        return DoStereoWiden(m_smoothed_value_system.Value(m_width_smoother_id, frame_index), in);
    }
    void OnParamChangeInternal(ChangedParams changed_params, AudioProcessingContext const&) override {
        if (auto p = changed_params.Param(ParamIndex::StereoWidenWidth)) {
            auto const val = p->ProjectedValue();
            f32 v = 0;
            if (val < 0)
                v = 1 - (-(val));
            else
                v = MapFrom01(val, 1, 4);

            m_smoothed_value_system.Set(m_width_smoother_id, v, 4);
        }
    }

  private:
    FloeSmoothedValueSystem::FloatId const m_width_smoother_id;
};
