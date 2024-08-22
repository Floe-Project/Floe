// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"
#include "utils/thread_extra/atomic_swap_buffer.hpp"

#include "audio_processing_context.hpp"
#include "common/constants.hpp"
#include "instrument.hpp"
#include "processing/adsr.hpp"
#include "processing/filters.hpp"
#include "processing/lfo.hpp"
#include "processing/midi.hpp"
#include "processing/smoothed_value_system.hpp"
#include "processing/volume_fade.hpp"
#include "sample_processing.hpp"

struct VoiceProcessingController;

struct HostThreadPool;

using VoiceSmoothedValueSystem = SmoothedValueSystem<7, 4, 0>;

struct WaveformInfo {
    u32 num_frames;
    u8 root_note;
    f32 sample_rate;
};

struct VoiceSample {
    NON_COPYABLE_AND_MOVEABLE(VoiceSample);

    VoiceSample(VoiceSmoothedValueSystem& s)
        : pitch_ratio_smoother_id(s.CreateDoubleSmoother())
        , sampler(s) {}

    bool is_active {false};

    VoiceSmoothedValueSystem::DoubleId const pitch_ratio_smoother_id;
    f64 pitch_ratio_mod = 0;
    f64 pos = 0;
    f32 amp = 1;

    // IMPROVE: for now, we have to keep sampler always valid because it uses a constructor for
    // xfade_vol_smoother_id. When we redo that system we should make this a TaggedUnion

    // IMPROVE: rename these to Instrument?
    // if generator == SoundGenerator::Sampler
    struct Sampler {
        constexpr Sampler(Sampler const&) = default;
        Sampler(VoiceSmoothedValueSystem& s) : xfade_vol_smoother_id(s.CreateSmoother()) {}
        sample_lib::Region const* region = nullptr;
        AudioData const* data = nullptr;
        VoiceSmoothedValueSystem::FloatId const xfade_vol_smoother_id;
        u32 loop_and_reverse_flags {};
        Optional<NormalisedLoop> loop {};
    } sampler;

    // if generator == SoundGenerator::WaveformSynth
    WaveformType waveform {WaveformType::Sine};

    InstrumentType generator {InstrumentType::WaveformSynth};
};

struct VoicePool;

struct Voice {
    NON_COPYABLE_AND_MOVEABLE(Voice);

    Voice(VoicePool& p) : pool(p) {}

    static constexpr int k_fade_out_samples_max = 64;
    static constexpr int k_filter_fade_in_samples_max = 64;

    VoiceSmoothedValueSystem smoothing_system;

    VoiceProcessingController* controller = {};
    u64 age = ~(u64)0;
    u16 id {};
    u32 frames_before_starting {};
    f32 current_gain {};

    bool is_active {false};
    bool written_to_buffer_this_block = false;

    u8 num_active_voice_samples = 0;
    Array<VoiceSample, k_max_num_voice_samples> voice_samples {
        MakeInitialisedArray<VoiceSample, k_max_num_voice_samples>(smoothing_system)};

    VoicePool& pool;

    u8 index = 0;

    bool filter_changed = false;
    sv_filter::CachedHelpers filter_coeffs = {};
    sv_filter::Data<f32x2> filters = {};
    VoiceSmoothedValueSystem::FloatId const filter_mix_smoother_id = {smoothing_system.CreateSmoother()};
    VoiceSmoothedValueSystem::FloatId const sv_filter_linear_cutoff_smoother_id = {
        smoothing_system.CreateSmoother()};
    VoiceSmoothedValueSystem::FloatId const sv_filter_resonance_smoother_id {
        smoothing_system.CreateSmoother()};

    //
    u7 note_num = 0;
    MidiChannelNote midi_key_trigger = {};
    int note_off_count = 0;

    LFO lfo = {};
    OnePoleLowPassFilter lfo_smoother {}; // TODO(1.0): does the lfo need to be smoothed?

    VolumeFade volume_fade;
    adsr::Processor vol_env = {};
    adsr::Processor fil_env = {};
    f32 amp_l = 1, amp_r = 1;
    f32 aftertouch_multiplier = 1;
};

struct VoiceEnvelopeMarkerForGui {
    u8 on : 1 {};
    u8 layer_index : 7 {};
    u8 state {}; // ADSRState
    u16 pos {};
    u16 sustain_level {};
    u16 id {};
};

struct VoiceWaveformMarkerForGui {
    u32 layer_index {};
    u16 position {};
    u16 intensity {};
};

template <typename Type>
concept ShouldSkipVoiceFunction = requires(Type function, Voice const& v) {
    { function(v) } -> Same<bool>;
};

struct VoicePool {
    template <bool k_early_out_if_none_active, ShouldSkipVoiceFunction Function>
    auto EnumerateVoices(Function&& should_skip_voice) {
        struct IterableWrapper {
            struct Iterator {
                constexpr bool operator!=(Iterator const& other) const { return index != other.index; }
                constexpr void operator++() {
                    ++index;
                    while (index < k_num_voices && wrapper.should_skip_voice(wrapper.pool.voices[index]))
                        ++index;
                }
                constexpr Voice& operator*() const { return wrapper.pool.voices[index]; }

                IterableWrapper const& wrapper;
                usize index;
            };

            constexpr Iterator begin() {
                if (k_early_out_if_none_active && pool.num_active_voices.Load(LoadMemoryOrder::Relaxed) == 0)
                    return end();

                usize i = 0;
                for (; i < k_num_voices; ++i)
                    if (!should_skip_voice(pool.voices[i])) break;
                return {*this, i};
            }

            constexpr Iterator end() { return {*this, k_num_voices}; }

            VoicePool& pool;
            Function should_skip_voice;
        };

        return IterableWrapper {*this, Forward<Function>(should_skip_voice)};
    }

    auto EnumerateActiveVoices() {
        return EnumerateVoices<true>([](Voice const& v) { return !v.is_active; });
    }

    auto EnumerateActiveLayerVoices(VoiceProcessingController const& controller) {
        return EnumerateVoices<true>(
            [&controller](Voice const& v) { return !v.is_active || v.controller != &controller; });
    }

    template <typename Function>
    void ForActiveSamplesInActiveVoices(Function&& f) {
        for (auto& v : voices) {
            if (v.is_active) {
                for (auto& s : v.voice_samples)
                    if (s.is_active) f(v, s);
            }
        }
    }

    void PrepareToPlay(ArenaAllocator& arena, AudioProcessingContext const& context);
    void EndAllVoicesInstantly();

    u64 voice_age_counter = 0;
    u16 voice_id_counter = 0;
    Atomic<u32> num_active_voices = 0;
    Array<Voice, k_num_voices> voices {MakeInitialisedArray<Voice, k_num_voices>(*this)};
    Array<Span<f32>, k_num_voices> buffer_pool {};

    // TODO(1.0): hide waveform markers for Waveform instruments, only show them for sampled instrument
    AtomicSwapBuffer<Array<VoiceWaveformMarkerForGui, k_num_voices>, true> voice_waveform_markers_for_gui {};
    AtomicSwapBuffer<Array<VoiceEnvelopeMarkerForGui, k_num_voices>, true> voice_vol_env_markers_for_gui {};
    AtomicSwapBuffer<Array<VoiceEnvelopeMarkerForGui, k_num_voices>, true> voice_fil_env_markers_for_gui {};
    Array<Atomic<s16>, 128> voices_per_midi_note_for_gui {};

    unsigned int random_seed = FastRandSeedFromTime();

    struct {
        u32 num_frames = 0;
    } multithread_processing;
};

inline void EndVoiceInstantly(Voice& voice) {
    ASSERT(voice.is_active);
    voice.pool.num_active_voices.FetchSub(1, RmwMemoryOrder::Relaxed);
    voice.pool.voices_per_midi_note_for_gui[voice.midi_key_trigger.note].FetchSub(1, RmwMemoryOrder::Relaxed);
    voice.is_active = false;
}
void EndVoice(Voice& voice);

void UpdateLFOWaveform(Voice& v);
void SetVoicePitch(Voice& v, f32 pitch, f32 sample_rate);
void UpdateLFOTime(Voice& v, f32 sample_rate);
void SetFilterRes(Voice& v, f32 res);
void SetFilterCutoff(Voice& v, f32 cutoff01);
void SetFilterOn(Voice& v, bool on);
void SetPan(Voice& v, f32 pan_pos);
void UpdateLoopInfo(Voice& v);
void UpdateXfade(Voice& v, f32 knob_pos_01, bool hard_set);

struct VoiceStartParams {
    struct SamplerParams {
        struct Region {
            sample_lib::Region const& region;
            AudioData const& audio_data;
            f32 amp {};
        };

        f32 initial_sample_offset01 {};
        f32 initial_dynamics_01 {};
        DynamicArrayInline<Region, k_max_num_voice_samples> voice_sample_params {};
    };

    struct WaveformParams {
        WaveformType type;
        f32 amp;
    };

    using Params = TaggedUnion<InstrumentType,
                               TypeAndTag<SamplerParams, InstrumentType::Sampler>,
                               TypeAndTag<WaveformParams, InstrumentType::WaveformSynth>>;

    f32 initial_pitch;
    MidiChannelNote midi_key_trigger;
    u7 note_num;
    f32 note_vel;
    unsigned int lfo_start_phase;
    u32 num_frames_before_starting;
    Params params;
};

void StartVoice(VoicePool& pool,
                VoiceProcessingController& voice_controller,
                VoiceStartParams const& params,
                AudioProcessingContext const& audio_processing_context);

void NoteOff(VoicePool& pool, VoiceProcessingController& controller, MidiChannelNote note);

Array<Span<f32>, k_num_layers> ProcessVoices(VoicePool& pool,
                                             u32 num_frames,
                                             AudioProcessingContext const& context,
                                             HostThreadPool* thread_pool);
