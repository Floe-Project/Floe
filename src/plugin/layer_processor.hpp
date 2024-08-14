// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "audio_processing_context.hpp"
#include "clap/host.h"
#include "instrument.hpp"
#include "param.hpp"
#include "param_info.hpp"
#include "plugin.hpp"
#include "processing/adsr.hpp"
#include "processing/filters.hpp"
#include "processing/midi.hpp"
#include "processing/peak_meter.hpp"
#include "processing/volume_fade.hpp"
#include "sample_library_server.hpp"
#include "smoothed_value_system.hpp"

struct EqBand {
    EqBand(FloeSmoothedValueSystem& s) : eq_coeffs_smoother_id(s.CreateFilterSmoother()) {}

    StereoAudioFrame Process(FloeSmoothedValueSystem& s, StereoAudioFrame in, u32 frame_index) {
        auto [coeffs, mix] = s.Value(eq_coeffs_smoother_id, frame_index);
        return rbj_filter::Process(eq_data, coeffs, in * mix);
    }

    void OnParamChange(ChangedLayerParams changed_params,
                       f32 sample_rate,
                       FloeSmoothedValueSystem& s,
                       u32 band_num) {
        auto freq_param = LayerParamIndex::EqFreq1;
        auto reso_param = LayerParamIndex::EqResonance1;
        auto gain_param = LayerParamIndex::EqGain1;
        auto type_param = LayerParamIndex::EqType1;
        if (band_num == 1) {
            freq_param = LayerParamIndex::EqFreq2;
            reso_param = LayerParamIndex::EqResonance2;
            gain_param = LayerParamIndex::EqGain2;
            type_param = LayerParamIndex::EqType2;
        } else if (band_num != 0) {
            PanicIfReached();
        }

        bool changed = false;
        if (auto p = changed_params.Param(freq_param)) {
            eq_params.fs = sample_rate;
            eq_params.fc = p->ProjectedValue();
            changed = true;
        }
        if (auto p = changed_params.Param(reso_param)) {
            eq_params.fs = sample_rate;
            eq_params.q = MapFrom01Skew(p->ProjectedValue(), 0.5f, 8, 5);
            changed = true;
        }
        if (auto p = changed_params.Param(gain_param)) {
            eq_params.fs = sample_rate;
            eq_params.peak_gain = p->ProjectedValue();
            changed = true;
        }
        if (auto p = changed_params.Param(type_param)) {
            eq_params.fs = sample_rate;
            rbj_filter::Type type {rbj_filter::Type::Peaking};
            switch (ParamToInt<param_values::EqType>(p->LinearValue())) {
                case param_values::EqType::HighShelf: type = rbj_filter::Type::HighShelf; break;
                case param_values::EqType::LowShelf: type = rbj_filter::Type::LowShelf; break;
                case param_values::EqType::Peak: type = rbj_filter::Type::Peaking; break;
                case param_values::EqType::Count: PanicIfReached(); break;
            }
            eq_params.type = type;
            changed = true;
        }

        if (changed) s.Set(eq_coeffs_smoother_id, eq_params);
    }

    FloeSmoothedValueSystem::FilterId const eq_coeffs_smoother_id;
    rbj_filter::StereoData eq_data {};
    rbj_filter::Params eq_params {};
};

struct EqBands {
    EqBands(FloeSmoothedValueSystem& s) : eq_bands(s), eq_mix_smoother_id(s.CreateSmoother()) {}

    void OnParamChange(u32 band_num,
                       ChangedLayerParams changed_params,
                       FloeSmoothedValueSystem& s,
                       f32 sample_rate) {
        eq_bands[band_num].OnParamChange(changed_params, sample_rate, s, band_num);
    }

    void SetOn(FloeSmoothedValueSystem& s, bool on) { s.Set(eq_mix_smoother_id, on ? 1.0f : 0.0f, 4); }

    StereoAudioFrame Process(FloeSmoothedValueSystem& s, StereoAudioFrame in, u32 frame_index) {
        StereoAudioFrame result = in;
        if (auto mix = s.Value(eq_mix_smoother_id, frame_index); mix != 0) {
            for (auto& eq_band : eq_bands)
                result = eq_band.Process(s, result, frame_index);
            if (mix != 1) result = LinearInterpolate(mix, in, result);
        }
        return result;
    }

    InitialisedArray<EqBand, k_num_layer_eq_bands> eq_bands;
    FloeSmoothedValueSystem::FloatId const eq_mix_smoother_id;
};

// audio-thread data that voices use to control their sound
struct VoiceProcessingController {
    FloeSmoothedValueSystem& smoothing_system;

    f32 velocity_volume_modifier = 0.5f;
    u8 const layer_index;

    struct {
        bool on;
        param_values::LfoShape shape;
        param_values::LfoDestination dest;
        f32 amount;
        f32 time_hz;
    } lfo {};

    struct {
        f32 start;
        f32 end;
        f32 crossfade_size;
    } loop {};

    f32 tune = 1;
    FloeSmoothedValueSystem::FloatId const pan_pos_smoother_id; // -1 to 1

    f32 sv_filter_cutoff_linear = 0;
    f32 sv_filter_resonance = 0;
    sv_filter::Type filter_type = {};
    bool filter_on = false;

    bool vol_env_on = true;
    adsr::Params vol_env = {};

    adsr::Params fil_env = {};
    f32 fil_env_amount = 0;

    param_values::LoopMode loop_mode {};
    bool reverse {};

    bool no_key_tracking = false;
};

struct VoicePool;

struct LayerProcessor {
    LayerProcessor(FloeSmoothedValueSystem& system,
                   u8 index,
                   StaticSpan<Parameter, k_num_layer_parameters> params,
                   clap_host const& host)
        : params(params)
        , smoothed_value_system(system)
        , host(host)
        , index(index)
        , voice_controller({.smoothing_system = system,
                            .layer_index = index,
                            .pan_pos_smoother_id = system.CreateSmoother()})
        , vol_smoother_id(smoothed_value_system.CreateSmoother())
        , mute_solo_mix_smoother_id(smoothed_value_system.CreateSmoother())
        , eq_bands(smoothed_value_system) {}

    ~LayerProcessor() {
        if (auto sampled_inst =
                instrument.TryGet<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>())
            sampled_inst->Release();
    }

    String InstName() const {
        ASSERT(IsMainThread(host));
        switch (instrument.tag) {
            case InstrumentType::WaveformSynth: {
                return k_waveform_type_names[ToInt(instrument.Get<WaveformType>())];
            }
            case InstrumentType::Sampler: {
                return instrument.Get<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>()
                    ->instrument.name;
            }
            case InstrumentType::None: return "None"_s;
        }
        return {};
    }

    Optional<sample_lib::LibraryIdRef> LibId() const {
        ASSERT(IsMainThread(host));
        if (auto sampled_inst = instrument.TryGetFromTag<InstrumentType::Sampler>())
            return (*sampled_inst)->instrument.library.Id();
        return nullopt;
    }

    param_values::VelocityMappingMode GetVelocityMode() const {
        return ParamToInt<param_values::VelocityMappingMode>(
            params[ToInt(LayerParamIndex::VelocityMapping)].LinearValue());
    }

    StaticSpan<Parameter, k_num_layer_parameters> params {nullptr};

    FloeSmoothedValueSystem& smoothed_value_system;
    clap_host const& host;

    u8 const index;
    VoiceProcessingController voice_controller;

    Atomic<u32> note_on_rr_pos = 0;
    Atomic<u32> note_off_rr_pos = 0;

    Instrument instrument {InstrumentType::None};
    InstrumentId instrument_id {InstrumentType::None};

    InstrumentUnwrapped inst = InstrumentType::None;

    // Encodes possible instruments into a single atomic u64. We use the fact that the pointer's value must be
    // aligned to the type they point to, and therefore we can use unaligned numbers to represent other
    // things.
    struct DesiredInst {
        static constexpr u64 k_consumed = 1;
        void Set(WaveformType w) { value.Store(ValForWaveform(w), MemoryOrder::Release); }
        void Set(sample_lib::LoadedInstrument const* i) { value.Store((uintptr)i, MemoryOrder::Release); }
        void SetNone() { value.Store(0, MemoryOrder::Release); }
        Optional<InstrumentUnwrapped> Consume() {
            auto v = value.Exchange(k_consumed);
            if (v == k_consumed) return nullopt;
            if (v == 0) return InstrumentType::None;
            for (auto const w : Range((u64)WaveformType::Count))
                if (v == ValForWaveform((WaveformType)w)) return (WaveformType)w;
            return (sample_lib::LoadedInstrument const*)v;
        }
        static constexpr u64 ValForWaveform(WaveformType w) {
            auto const v = 1 + alignof(sample_lib::LoadedInstrument) * ((u64)w + 1);
            ASSERT(v % alignof(sample_lib::LoadedInstrument) != 0, "needs to be an invalid ptr");
            return v;
        }
        bool IsConsumed() const { return value.Load(MemoryOrder::Acquire) == k_consumed; }

        Atomic<u64> value {0};
    };

    DesiredInst desired_inst {};

    FloeSmoothedValueSystem::FloatId const vol_smoother_id;
    int midi_transpose = 0;
    int multisample_transpose = 0;
    f32 tune_semitone = 0;
    f32 tune_cents = 0;
    f32 sample_offset_01 = 0;

    bool sustain_pedal_retrigger {};
    bool monophonic {};

    param_values::LfoRestartMode lfo_restart_mode {};
    param_values::LfoSyncedRate lfo_synced_time {};
    f32 lfo_unsynced_hz {};
    bool lfo_is_synced {};

    FloeSmoothedValueSystem::FloatId const mute_solo_mix_smoother_id;
    Atomic<bool> is_silent = false;

    EqBands eq_bands;

    int num_velocity_regions = 1;
    Bitset<4> active_velocity_regions {};

    StereoPeakMeter peak_meter = {};

    VolumeFade inst_change_fade {};
};

void SetSilent(LayerProcessor& layer, bool state);
void SetTempo(LayerProcessor& layer, VoicePool& voice_pool, AudioProcessingContext const& context);
void OnParamChange(LayerProcessor& layer,
                   AudioProcessingContext const& context,
                   VoicePool& voice_pool,
                   ChangedLayerParams changed_params);
bool ChangeInstrumentIfNeededAndReset(LayerProcessor& layer, VoicePool& voice_pool);
void PrepareToPlay(LayerProcessor& layer, ArenaAllocator& allocator, AudioProcessingContext const& context);

void LayerHandleNoteOn(LayerProcessor& layer,
                       AudioProcessingContext const& context,
                       VoicePool& voice_pool,
                       MidiChannelNote note,
                       f32 vel,
                       u32 offset,
                       f32 dynamics_param_value_01,
                       f32 velocity_to_volume_01);
void LayerHandleNoteOff(LayerProcessor& layer,
                        AudioProcessingContext const& context,
                        VoicePool& voice_pool,
                        MidiChannelNote note,
                        bool triggered_by_cc64,
                        f32 dynamic_param_value_01,
                        f32 velocity_to_volume_01);

struct LayerProcessResult {
    bool instrument_swapped;
    bool did_any_processing;
};

LayerProcessResult ProcessLayer(LayerProcessor& layer,
                                AudioProcessingContext const& context,
                                VoicePool& voice_pool,
                                u32 num_frames,
                                bool start_fade_out,
                                Span<f32> buffer);
void ResetLayerAudioProcessing(LayerProcessor& layer);
