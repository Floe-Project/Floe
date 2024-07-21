// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <vitfx/wrapper.hpp>

#include "effects/effect.hpp"

class Reverb final : public Effect {
  public:
    Reverb(FloeSmoothedValueSystem& s) : Effect(s, EffectType::Reverb), reverb(vitfx::reverb::Create()) {}
    ~Reverb() override { vitfx::reverb::Destroy(reverb); }

    void ResetInternal() override { vitfx::reverb::HardReset(*reverb); }

    void PrepareToPlay(AudioProcessingContext const& context) override {
        vitfx::reverb::SetSampleRate(*reverb, (int)context.sample_rate);
    }

    bool ProcessBlock(Span<StereoAudioFrame> io_frames,
                      ScratchBuffers scratch_buffers,
                      AudioProcessingContext const&) override {
        if (!ShouldProcessBlock()) return false;

        auto wet = scratch_buffers.buf1.Interleaved();
        wet.size = io_frames.size;
        CopyMemory(wet.data, io_frames.data, io_frames.size * sizeof(StereoAudioFrame));

        auto num_frames = (u32)io_frames.size;
        u32 pos = 0;
        while (num_frames) {
            u32 const chunk_size = Min(num_frames, 64u);

            vitfx::reverb::ProcessReverbArgs args {
                .num_frames = (int)chunk_size,
                .in_interleaved = (f32*)(io_frames.data + pos),
                .out_interleaved = (f32*)(wet.data + pos),
            };
            CopyMemory(args.params, params, sizeof(params));

            vitfx::reverb::Process(*reverb, args);

            num_frames -= chunk_size;
            pos += chunk_size;
        }

        for (auto const frame_index : Range((u32)io_frames.size))
            io_frames[frame_index] = MixOnOffSmoothing(wet[frame_index], io_frames[frame_index], frame_index);

        return true;
    }

    void OnParamChangeInternal(ChangedParams changed_params, AudioProcessingContext const&) override {
        using namespace vitfx::reverb;
        if (auto p = changed_params.Param(ParamIndex::ReverbDecayTimeMs))
            params[ToInt(Params::DecayTimeSeconds)] = p->ProjectedValue() / 1000.0f;
        if (auto p = changed_params.Param(ParamIndex::ReverbPreLowPassCutoff))
            params[ToInt(Params::PreLowPassCutoffSemitones)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbPreHighPassCutoff))
            params[ToInt(Params::PreHighPassCutoffSemitones)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbLowShelfCutoff))
            params[ToInt(Params::LowShelfCutoffSemitones)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbLowShelfGain))
            params[ToInt(Params::LowShelfGainDb)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbHighShelfCutoff))
            params[ToInt(Params::HighShelfCutoffSemitones)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbHighShelfGain))
            params[ToInt(Params::HighShelfGainDb)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbChorusAmount))
            params[ToInt(Params::ChorusAmount)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbChorusFrequency))
            params[ToInt(Params::ChorusFrequency)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbSize))
            params[ToInt(Params::Size)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbDelay))
            params[ToInt(Params::DelaySeconds)] = p->ProjectedValue() / 1000.0f;
        if (auto p = changed_params.Param(ParamIndex::ReverbMix))
            params[ToInt(Params::Mix)] = p->ProjectedValue();
    }

    vitfx::reverb::Reverb* reverb {};
    f32 params[ToInt(vitfx::reverb::Params::Count)] {};
};
