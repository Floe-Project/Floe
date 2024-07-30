// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/debug/debug.hpp"

#include "audio_processing_context.hpp"
#include "effect_infos.hpp"
#include "param.hpp"
#include "param_info.hpp"
#include "processing/stereo_audio_frame.hpp"
#include "smoothed_value_system.hpp"

struct ParamState {
    ParamIndex index;
    f32 value;
};

inline void UpdateSilentSeconds(f32& silent_seconds, Span<StereoAudioFrame const> frames, f32 sample_rate) {
    bool all_silent = true;
    for (auto const& f : frames)
        if (!f.IsSilent()) {
            all_silent = false;
            break;
        }
    if (all_silent)
        silent_seconds += (f32)frames.size / sample_rate;
    else
        silent_seconds = 0;
}

struct EffectWetDryHelper {
    EffectWetDryHelper(FloeSmoothedValueSystem& s)
        : m_wet_smoother_id(s.CreateSmoother())
        , m_dry_smoother_id(s.CreateSmoother()) {}

    static void SetValue(FloeSmoothedValueSystem& s, FloeSmoothedValueSystem::FloatId smoother, f32 amp) {
        s.Set(smoother, amp, 10);
    }

    void SetWet(FloeSmoothedValueSystem& s, f32 amp) { SetValue(s, m_wet_smoother_id, amp); }
    void SetDry(FloeSmoothedValueSystem& s, f32 amp) { SetValue(s, m_dry_smoother_id, amp); }

    f32 Mix(FloeSmoothedValueSystem& s, u32 frame_index, f32 w, f32 d) {
        return w * s.Value(m_wet_smoother_id, frame_index) + d * s.Value(m_dry_smoother_id, frame_index);
    }

    StereoAudioFrame
    MixStereo(FloeSmoothedValueSystem& s, u32 frame_index, StereoAudioFrame wet, StereoAudioFrame dry) {
        return wet * s.Value(m_wet_smoother_id, frame_index) + dry * s.Value(m_dry_smoother_id, frame_index);
    }

  private:
    FloeSmoothedValueSystem::FloatId const m_wet_smoother_id;
    FloeSmoothedValueSystem::FloatId const m_dry_smoother_id;
};

class EffectLegacyModeHelper;

struct ScratchBuffers {
    class Buffer {
      public:
        Buffer(f32* b, u32 size) : m_buffer(b), m_block_size(size) { ASSERT((usize)b % 16 == 0); }

        Span<StereoAudioFrame> Interleaved() { return ToStereoFramesSpan(m_buffer, m_block_size); }

        Array<f32*, 2> Channels() { return {m_buffer, m_buffer + m_block_size}; }

      private:
        f32* m_buffer;
        u32 m_block_size;
    };

    ScratchBuffers(u32 block_size, f32* b1, f32* b2) : buf1(b1, block_size), buf2(b2, block_size) {}
    Buffer buf1, buf2;
};

// Base class for effects.
// The subclass can either override ProcessFrame or ProcessBlock
class Effect {
  public:
    Effect(FloeSmoothedValueSystem& s, EffectType type)
        : type(type)
        , m_smoothed_value_system(s)
        , m_mix_smoother_id(m_smoothed_value_system.CreateSmoother()) {}

    virtual ~Effect() {}

    // audio-thread
    void OnParamChange(ChangedParams changed_params, AudioProcessingContext const& context) {
        if (auto p = changed_params.Param(k_effect_info[(u32)type].on_param_index))
            m_smoothed_value_system.Set(m_mix_smoother_id, p->ValueAsBool() ? 1.0f : 0.0f, 4);
        OnParamChangeInternal(changed_params, context);
    }

    // main-thread but never while any audio-thread function is being called
    virtual void PrepareToPlay(AudioProcessingContext const&) {}

    // audio-thread
    virtual void SetTempo(f64) {}

    // audio-thread
    virtual bool ProcessBlock(Span<StereoAudioFrame> frames,
                              [[maybe_unused]] ScratchBuffers scratch_buffers,
                              AudioProcessingContext const& context) {
        if (!ShouldProcessBlock()) return false;

        for (auto [i, frame] : Enumerate<u32>(frames)) {
            auto const mix = m_smoothed_value_system.Value(m_mix_smoother_id, i);
            frame = LinearInterpolate(mix, frame, ProcessFrame(context, frame, i));
        }
        return true;
    }

    // audio-thread
    void Reset() {
        if (!m_state_is_reset) ResetInternal();
        m_state_is_reset = true;
    }

    EffectType const type;

    friend class EffectLegacyModeHelper;

  protected:
    bool ShouldProcessBlock() {
        if (m_smoothed_value_system.Value(m_mix_smoother_id, 0) == 0 &&
            m_smoothed_value_system.TargetValue(m_mix_smoother_id) == 0)
            return false;
        m_state_is_reset = false;
        return true;
    }

    StereoAudioFrame MixOnOffSmoothing(StereoAudioFrame wet, StereoAudioFrame dry, u32 frame_index) {
        return LinearInterpolate(m_smoothed_value_system.Value(m_mix_smoother_id, frame_index), dry, wet);
    }

    FloeSmoothedValueSystem& m_smoothed_value_system;

  private:
    virtual StereoAudioFrame
    ProcessFrame(AudioProcessingContext const&, StereoAudioFrame in, u32 frame_index) {
        (void)frame_index;
        return in;
    }

    virtual void OnParamChangeInternal(ChangedParams changed_params,
                                       AudioProcessingContext const& context) = 0;

    virtual void ResetInternal() {}

    FloeSmoothedValueSystem::FloatId const m_mix_smoother_id;
    bool m_state_is_reset = true;
};
