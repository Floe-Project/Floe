// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "effect.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/distortion.hpp"

struct Distortion final : public Effect {
    Distortion() : Effect(EffectType::Distortion) {}

    void ProcessChangesInternal(ProcessBlockChanges const& changes, AudioProcessingContext const&) override {
        auto settings = dsp.settings;
        bool settings_changed = false;
        bool type_changed = false;

        if (auto p = changes.changed_params.IntValueLegacyAware<DistortionType>(ParamIndex::DistortionType)) {
            settings.type = *p;
            type_changed = true;
            settings_changed = true;
        }

        if (auto p = changes.changed_params.BoolValue(ParamIndex::DistortionAutoGain)) auto_gain_param = *p;

        // Toggling Auto Gain is faded so the level doesn't step. A type change is already a discontinuity
        // (the shapers are reset), so there the fade would only put a modern type through the Legacy
        // formula for a few ms.
        auto_gain = (IsLegacyDistortionType(settings.type) && !auto_gain_param) ? 0.0f : 1.0f;
        if (type_changed) auto_gain_smoother.prev_output = auto_gain;

        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::DistortionDrive)) amount = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::DistortionMix)) mix = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::DistortionPunish)) punish = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::DistortionTilt)) {
            settings.tilt = *p;
            settings_changed = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::DistortionGain))
            output_gain = DbToAmp(*p);

        if (settings_changed) dsp.SetSettings(settings);
    }

    // The oversampled wet path lags the input, so this doesn't use the base class's ProcessBlockByFrame
    // helper:
    // - Mix is blended here against the delayed dry signal (blending against the undelayed input would
    //   comb-filter), so the base class's mix_param is left at 1 and bypass_mix tracks only the on/off state.
    // - The on/off transition is crossfaded against the undelayed input. The delay entering or leaving the
    //   signal path is an unavoidable time-shift; spreading it over the 10ms crossfade is far less audible
    //   than the hard cut you'd get from switching instantly.
    // - Once fully bypassed the DSP is reset so the delay lines don't hold stale audio that would otherwise
    //   replay when the effect is next enabled.
    EffectProcessResult
    ProcessBlock(Span<f32x2> frames, AudioProcessingContext const& context, void*) override {
        auto const starting_from_reset = is_reset;
        if (!ShouldProcessBlock()) {
            Reset();
            return EffectProcessResult::Done;
        }
        if (starting_from_reset) bypass_smoother.prev_output = 0;

        for (auto& frame : frames) {
            auto const delayed_dry = dsp.DelayDry(frame);
            auto const smoothing = context.one_pole_smoothing_cutoff_10ms;
            auto const wet = dsp.Process(frame,
                                         {
                                             .drive01 = amount_smoother.LowPass(amount, smoothing),
                                             .punish01 = punish_smoother.LowPass(punish, smoothing),
                                             .auto_gain01 = auto_gain_smoother.LowPass(auto_gain, smoothing),
                                         }) *
                             output_gain_smoother.LowPass(output_gain, smoothing);
            auto const mixed = LinearInterpolate(mix_smoother.LowPass(mix, smoothing), delayed_dry, wet);
            frame = ApplyBypassCrossfade(context, mixed, frame);
        }
        return EffectProcessResult::Done;
    }

    void ResetInternal() override {
        dsp.Reset();
        amount_smoother.Reset();
        punish_smoother.Reset();
        auto_gain_smoother.Reset();
        output_gain_smoother.Reset();
        mix_smoother.Reset();
    }

    void PrepareToPlay(AudioProcessingContext const& context) override {
        dsp.SetSampleRate(context.sample_rate);
    }

    f32 amount {};
    f32 punish {};
    f32 output_gain = 1;
    f32 mix = 1;
    bool auto_gain_param = true;
    f32 auto_gain = 1;
    OnePoleLowPassFilter<f32> amount_smoother {};
    OnePoleLowPassFilter<f32> punish_smoother {};
    OnePoleLowPassFilter<f32> auto_gain_smoother {};
    OnePoleLowPassFilter<f32> output_gain_smoother {};
    OnePoleLowPassFilter<f32> mix_smoother {};
    DistortionDsp dsp {};
};
