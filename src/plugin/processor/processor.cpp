// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "processor.hpp"

#include "os/threading.hpp"

#include "clap/ext/params.h"
#include "descriptors/param_descriptors.hpp"
#include "param.hpp"
#include "plugin/plugin.hpp"
#include "voices.hpp"

bool EffectIsOn(Parameters const& params, Effect* effect) {
    return params[ToInt(k_effect_info[ToInt(effect->type)].on_param_index)].ValueAsBool();
}

bool IsMidiCCLearnActive(AudioProcessor const& processor) {
    ASSERT(IsMainThread(processor.host));
    return processor.midi_learn_param_index.Load(LoadMemoryOrder::Relaxed).HasValue();
}

void LearnMidiCC(AudioProcessor& processor, ParamIndex param) {
    ASSERT(IsMainThread(processor.host));
    processor.midi_learn_param_index.Store((s32)param, StoreMemoryOrder::Relaxed);
}

void CancelMidiCCLearn(AudioProcessor& processor) {
    ASSERT(IsMainThread(processor.host));
    processor.midi_learn_param_index.Store(k_nullopt, StoreMemoryOrder::Relaxed);
}

void UnlearnMidiCC(AudioProcessor& processor, ParamIndex param, u7 cc_num_to_remove) {
    ASSERT(IsMainThread(processor.host));
    processor.events_for_audio_thread.Push(RemoveMidiLearn {.param = param, .midi_cc = cc_num_to_remove});
    processor.host.request_process(&processor.host);
}

Bitset<128> GetLearnedCCsBitsetForParam(AudioProcessor const& processor, ParamIndex param) {
    ASSERT(IsMainThread(processor.host));
    return processor.param_learned_ccs[ToInt(param)].GetBlockwise();
}

bool CcControllerMovedParamRecently(AudioProcessor const& processor, ParamIndex param) {
    ASSERT(IsMainThread(processor.host));
    return (processor.time_when_cc_moved_param[ToInt(param)].Load(LoadMemoryOrder::Relaxed) + 0.4) >
           TimePoint::Now();
}

static void HandleMuteSolo(AudioProcessor& processor) {
    bool const any_solo = processor.solo.AnyValuesSet();

    for (auto const layer_index : Range(k_num_layers)) {
        bool state = any_solo;

        auto solo = processor.solo.Get(layer_index);
        if (solo) {
            state = false;
            SetSilent(processor.layer_processors[layer_index], state);
            continue;
        }

        auto mute = processor.mute.Get(layer_index);
        if (mute) {
            state = true;
            SetSilent(processor.layer_processors[layer_index], state);
            continue;
        }

        SetSilent(processor.layer_processors[layer_index], state);
    }
}

void SetAllParametersToDefaultValues(AudioProcessor& processor) {
    ASSERT(IsMainThread(processor.host));
    for (auto& param : processor.params)
        param.SetLinearValue(param.DefaultLinearValue());

    processor.events_for_audio_thread.Push(EventForAudioThreadType::ReloadAllAudioState);
    auto const host = &processor.host;
    auto const params = (clap_host_params const*)host->get_extension(host, CLAP_EXT_PARAMS);
    if (params) params->rescan(host, CLAP_PARAM_RESCAN_VALUES);
    host->request_process(host);
}

static void ProcessorRandomiseAllParamsInternal(AudioProcessor& processor, bool only_effects) {
    // TODO(1.0): this should create a new StateSnapshot and apply it, rather than change params/insts
    // individually
    (void)processor;
    (void)only_effects;

#if 0
    RandomIntGenerator<int> int_gen;
    RandomFloatGenerator<f32> float_gen;
    u64 seed = SeedFromCpu();
    RandomNormalDistribution normal_dist {0.5, 0.20};
    RandomNormalDistribution normal_dist_strong {0.5, 0.10};

    auto SetParam = [&](Parameter &p, f32 v) {
        if (p.info.flags & param_flags::Truncated) v = roundf(v);
        ASSERT(v >= p.info.linear_range.min && v <= p.info.linear_range.max);
        p.SetLinearValue(v);
    };
    auto SetAnyRandom = [&](Parameter &p) {
        SetParam(p, float_gen.GetRandomInRange(seed, p.info.linear_range.min, p.info.linear_range.max));
    };

    enum class BiasType {
        Normal,
        Strong,
    };

    auto RandomiseNearToLinearValue = [&](Parameter &p, BiasType bias, f32 linear_value) {
        f32 rand_v = 0;
        switch (bias) {
            case BiasType::Normal: {
                rand_v = (f32)normal_dist.Next(seed);
                break;
            }
            case BiasType::Strong: {
                rand_v = (f32)normal_dist_strong.Next(seed);
                break;
            }
            default: PanicIfReached();
        }

        const auto v = Clamp(rand_v, 0.0f, 1.0f);
        SetParam(p, MapFrom01(v, p.info.linear_range.min, p.info.linear_range.max));
    };

    auto RandomiseNearToDefault = [&](Parameter &p, BiasType bias = BiasType::Normal) {
        RandomiseNearToLinearValue(p, bias, p.DefaultLinearValue());
    };

    auto RandomiseButtonPrefferingDefault = [&](Parameter &p, BiasType bias = BiasType::Normal) {
        f32 new_param_val = p.DefaultLinearValue();
        const auto v = int_gen.GetRandomInRange(seed, 1, 100, false);
        if ((bias == BiasType::Normal && v <= 10) || (bias == BiasType::Strong && v <= 5))
            new_param_val = Abs(new_param_val - 1.0f);
        SetParam(p, new_param_val);
    };

    auto RandomiseDetune = [&](Parameter &p) {
        const bool should_detune = int_gen.GetRandomInRange(seed, 1, 10) <= 2;
        if (!should_detune) {
            SetParam(p, 0);
            return;
        }
        RandomiseNearToDefault(p);
    };

    auto RandomisePitch = [&](Parameter &p) {
        const auto r = int_gen.GetRandomInRange(seed, 1, 10);
        switch (r) {
            case 1:
            case 2:
            case 3:
            case 4:
            case 5: {
                SetParam(p, 0);
                break;
            }
            case 6:
            case 7:
            case 8:
            case 9: {
                const f32 potential_vals[] = {-24, -12, -5, 7, 12, 19, 24, 12, -12};
                SetParam(
                    p,
                    potential_vals[int_gen.GetRandomInRange(seed, 0, (int)ArraySize(potential_vals) - 1)]);
                break;
            }
            case 10: {
                RandomiseNearToDefault(p);
                break;
            }
            default: PanicIfReached();
        }
    };

    auto RandomisePan = [&](Parameter &p) {
        if (int_gen.GetRandomInRange(seed, 1, 10) < 4)
            SetParam(p, 0);
        else
            RandomiseNearToDefault(p, BiasType::Strong);
    };

    auto RandomiseLoopStartAndEnd = [&](Parameter &start, Parameter &end) {
        const auto mid = float_gen.GetRandomInRange(seed, 0, 1);
        const auto min_half_size = 0.1f;
        const auto max_half_size = Min(mid, 1 - mid);
        const auto half_size = float_gen.GetRandomInRange(seed, min_half_size, max_half_size);
        SetParam(start, Clamp(mid - half_size, 0.0f, 1.0f));
        SetParam(end, Clamp(mid + half_size, 0.0f, 1.0f));
    };

    //
    //
    //

    // Set all params to a random value
    for (auto &p : plugin.processor.params)
        if (!only_effects || (only_effects && p.info.IsEffectParam())) SetAnyRandom(p);

    // Specialise the randomness of specific params for better results
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::BitCrushWet)]);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::BitCrushDry)]);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::CompressorThreshold)], BiasType::Strong);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::CompressorRatio)]);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::CompressorGain)], BiasType::Strong);
    SetParam(plugin.processor.params[ToInt(ParamIndex::CompressorAutoGain)], 1.0f);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::FilterCutoff)]);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::FilterResonance)]);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::ChorusWet)]);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::ChorusDry)], BiasType::Strong);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::ReverbFreeverbWet)]);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::ReverbSvWet)]);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::ReverbDry)]);
    SetParam(plugin.processor.params[ToInt(ParamIndex::ReverbLegacyAlgorithm)], 0);
    SetParam(plugin.processor.params[ToInt(ParamIndex::DelayLegacyAlgorithm)], 0);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::PhaserWet)]);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::PhaserDry)]);
    RandomiseNearToLinearValue(plugin.processor.params[ToInt(ParamIndex::ConvolutionReverbWet)],
                               BiasType::Strong,
                               0.5f);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::ConvolutionReverbDry)], BiasType::Strong);
    RandomiseNearToDefault(plugin.processor.params[ToInt(ParamIndex::ConvolutionReverbHighpass)]);
    SetConvolutionIr(
        plugin,
        AssetLoadOptions::LoadIr {
            .library_name = String(k_core_library_name),
            .name =
                k_core_version_1_irs[(usize)int_gen.GetRandomInRange(seed, 0, k_core_version_1_irs.size - 1)],
        });

    {
        auto fx = plugin.processor.effects_ordered_by_type;
        Shuffle(fx, seed);
        plugin.processor.desired_effects_order.Store(EncodeEffectsArray(fx));
    }

    if (!only_effects) {
        SetParam(plugin.processor.params[ToInt(ParamIndex::MasterVolume)],
                 plugin.processor.params[ToInt(ParamIndex::MasterVolume)].DefaultLinearValue());
        for (auto &l : plugin.layers) {
            RandomiseNearToLinearValue(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::Volume))],
                BiasType::Strong,
                0.6f);
            RandomiseButtonPrefferingDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::Mute))]);
            RandomiseButtonPrefferingDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::Solo))]);
            RandomisePan(
                plugin.processor.params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::Pan))]);
            RandomiseDetune(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::TuneCents))]);
            RandomisePitch(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::TuneSemitone))]);
            SetParam(plugin.processor
                         .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::VolEnvOn))],
                     1.0f);

            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::VolumeAttack))]);
            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::VolumeDecay))]);
            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::VolumeSustain))]);
            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::VolumeRelease))]);

            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::FilterEnvAmount))]);
            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::FilterAttack))]);
            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::FilterDecay))]);
            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::FilterSustain))]);
            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::FilterRelease))]);

            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::FilterCutoff))]);
            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::FilterResonance))]);

            RandomiseLoopStartAndEnd(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::LoopStart))],
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::LoopEnd))]);

            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::EqGain1))]);
            RandomiseNearToDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::EqGain2))]);

            if (int_gen.GetRandomInRange(seed, 1, 10) < 4) {
                SetParam(
                    plugin.processor
                        .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::SampleOffset))],
                    0);
            } else {
                RandomiseNearToDefault(
                    plugin.processor
                        .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::SampleOffset))],
                    BiasType::Strong);
            }
            RandomiseButtonPrefferingDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::Reverse))]);

            RandomiseButtonPrefferingDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::Keytrack))],
                BiasType::Strong);
            RandomiseButtonPrefferingDefault(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::Monophonic))],
                BiasType::Strong);
            SetParam(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::MidiTranspose))],
                0.0f);
            SetParam(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::VelocityMapping))],
                0.0f);
            SetParam(
                plugin.processor
                    .params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::CC64Retrigger))],
                1.0f);
            SetParam(
                plugin.processor.params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::Mute))],
                0.0f);
            SetParam(
                plugin.processor.params[ToInt(ParamIndexFromLayerParamIndex(l.index, LayerParamIndex::Solo))],
                0.0f);

            if (l.processor.params[ToInt(LayerParamIndex::LfoOn)].ValueAsBool() &&
                l.processor.params[ToInt(LayerParamIndex::LfoDestination)]
                        .ValueAsInt<param_values::LfoDestination>() == param_values::LfoDestination::Filter &&
                !l.processor.params[ToInt(LayerParamIndex::FilterOn)].ValueAsBool()) {
                SetParam(l.processor.params[ToInt(LayerParamIndex::FilterOn)], 1.0f);
            }
        }

        // TODO: load random instruments
    }

    // IMPROVE: if we have only randomised the effects, then we don't need to trigger an entire state reload
    // including restarting voices.

    const auto host = &plugin.host;
    const auto host_params = (const clap_host_params *)host->get_extension(host, CLAP_EXT_PARAMS);
    if (host_params) host_params->rescan(host, CLAP_PARAM_RESCAN_VALUES);
    plugin.processor.events_for_audio_thread.Push(EventForAudioThreadType::ReloadAllAudioState);
#endif
}

void RandomiseAllEffectParameterValues(AudioProcessor& processor) {
    ProcessorRandomiseAllParamsInternal(processor, true);
}
void RandomiseAllParameterValues(AudioProcessor& processor) {
    ProcessorRandomiseAllParamsInternal(processor, false);
}

static void ProcessorOnParamChange(AudioProcessor& processor, ChangedParams changed_params) {
    ZoneScoped;
    ZoneValue(changed_params.m_changed.NumSet());

    if (auto param = changed_params.Param(ParamIndex::MasterVolume)) {
        processor.smoothed_value_system.SetVariableLength(processor.master_vol_smoother_id,
                                                          param->ProjectedValue(),
                                                          2,
                                                          25,
                                                          1);
    }

    if (auto param = changed_params.Param(ParamIndex::MasterDynamics)) {
        processor.dynamics_value_01 = param->ProjectedValue();
        for (auto& voice : processor.voice_pool.EnumerateActiveVoices())
            UpdateXfade(voice, processor.dynamics_value_01, true);
    }

    if (auto param = changed_params.Param(ParamIndex::MasterVelocity))
        processor.velocity_to_volume_01 = param->ProjectedValue();

    {
        bool mute_or_solo_changed = false;
        for (auto const layer_index : Range(k_num_layers)) {
            if (auto param = changed_params.Param(
                    ParamIndexFromLayerParamIndex((u32)layer_index, LayerParamIndex::Mute))) {
                processor.mute.SetToValue(layer_index, param->ValueAsBool());
                mute_or_solo_changed = true;
                break;
            }
            if (auto param = changed_params.Param(
                    ParamIndexFromLayerParamIndex((u32)layer_index, LayerParamIndex::Solo))) {
                processor.solo.SetToValue(layer_index, param->ValueAsBool());
                mute_or_solo_changed = true;
                break;
            }
        }
        if (mute_or_solo_changed) HandleMuteSolo(processor);
    }

    for (auto [index, l] : Enumerate(processor.layer_processors)) {
        OnParamChange(l,
                      processor.audio_processing_context,
                      processor.voice_pool,
                      changed_params.Subsection<k_num_layer_parameters>(0 + index * k_num_layer_parameters));
    }

    for (auto effect : processor.effects_ordered_by_type)
        effect->OnParamChange(changed_params, processor.audio_processing_context);
}

void ParameterJustStartedMoving(AudioProcessor& processor, ParamIndex index) {
    ASSERT(IsMainThread(processor.host));
    auto host_params =
        (clap_host_params const*)processor.host.get_extension(&processor.host, CLAP_EXT_PARAMS);
    if (!host_params) return;
    processor.param_events_for_audio_thread.Push(GuiStartedChangingParam {.param = index});
    host_params->request_flush(&processor.host);
}

void ParameterJustStoppedMoving(AudioProcessor& processor, ParamIndex index) {
    ASSERT(IsMainThread(processor.host));
    auto host_params =
        (clap_host_params const*)processor.host.get_extension(&processor.host, CLAP_EXT_PARAMS);
    if (!host_params) return;
    processor.param_events_for_audio_thread.Push(GuiEndedChangingParam {.param = index});
    host_params->request_flush(&processor.host);
}

bool SetParameterValue(AudioProcessor& processor, ParamIndex index, f32 value, ParamChangeFlags flags) {
    ASSERT(IsMainThread(processor.host));
    auto& param = processor.params[ToInt(index)];

    bool const changed =
        param.SetLinearValue(value); // TODO(1.0): remove this in favour of passing events around?

    processor.param_events_for_audio_thread.Push(
        GuiChangedParam {.value = value,
                         .param = index,
                         .host_should_not_record = flags.host_should_not_record != 0});
    processor.host.request_process(&processor.host);

    return changed;
}

void MoveEffectToNewSlot(EffectsArray& effects, Effect* effect_to_move, usize slot) {
    if (slot < 0 || slot >= k_num_effect_types) return;

    Optional<usize> original_slot = {};
    for (auto [index, fx] : Enumerate(effects)) {
        if (fx == effect_to_move) {
            original_slot = index;
            break;
        }
    }
    if (!original_slot) return;
    if (slot == *original_slot) return;

    // remove old location
    for (usize i = *original_slot; i < (k_num_effect_types - 1); ++i)
        effects[i] = effects[i + 1];

    // make room at new location
    for (usize i = k_num_effect_types - 1; i > slot; --i)
        effects[i] = effects[i - 1];
    // fill the slot
    effects[slot] = effect_to_move;
}

usize FindSlotInEffects(EffectsArray const& effects, Effect* fx) {
    if (auto index = Find(effects, fx)) return *index;
    PanicIfReached();
    return UINT64_MAX;
}

u64 EncodeEffectsArray(Array<EffectType, k_num_effect_types> const& arr) {
    static_assert(k_num_effect_types < 16, "The effect index is encoded into 4 bits");
    static_assert(k_num_effect_types * 4 <= sizeof(u64) * 8);
    u64 result {};
    for (auto [index, e] : Enumerate(arr)) {
        result |= (u64)e;
        if (index != k_num_effect_types - 1) result <<= 4;
    }
    return result;
}

u64 EncodeEffectsArray(EffectsArray const& arr) {
    Array<EffectType, k_num_effect_types> type_arr;
    for (auto [i, ptr] : Enumerate(arr))
        type_arr[i] = ptr->type;
    return EncodeEffectsArray(type_arr);
}

EffectsArray DecodeEffectsArray(u64 val, EffectsArray const& effects_ordered_by_type) {
    EffectsArray result {};
    for (int i = k_num_effect_types - 1; i >= 0; --i) {
        result[(usize)i] = effects_ordered_by_type[val & 0xf];
        val >>= 4;
    }
    return result;
}

static EffectsArray OrderEffectsToEnum(EffectsArray e) {
    if constexpr (!PRODUCTION_BUILD)
        for (auto effect : e)
            ASSERT(effect != nullptr);
    Sort(e, [](Effect const* a, Effect const* b) { return a->type < b->type; });
    return e;
}

static void HandleNoteOn(AudioProcessor& processor, MidiChannelNote note, f32 note_vel, u32 offset) {
    for (auto& layer : processor.layer_processors) {
        LayerHandleNoteOn(layer,
                          processor.audio_processing_context,
                          processor.voice_pool,
                          note,
                          note_vel,
                          offset,
                          processor.dynamics_value_01,
                          processor.velocity_to_volume_01);
    }
}

static void HandleNoteOff(AudioProcessor& processor, MidiChannelNote note, bool triggered_by_cc64) {
    for (auto& layer : processor.layer_processors) {
        LayerHandleNoteOff(layer,
                           processor.audio_processing_context,
                           processor.voice_pool,
                           note,
                           triggered_by_cc64,
                           processor.dynamics_value_01,
                           processor.velocity_to_volume_01);
    }
}

static void Deactivate(AudioProcessor& processor) {
    if (processor.activated) {
        for (auto const& event : processor.events_for_audio_thread.PopAll()) {
            if (auto remove_midi_learn = event.TryGet<RemoveMidiLearn>()) {
                processor.param_learned_ccs[ToInt(remove_midi_learn->param)].Clear(
                    remove_midi_learn->midi_cc);
            }
        }
        processor.voice_pool.EndAllVoicesInstantly();
        processor.activated = false;
    }
}

void SetInstrument(AudioProcessor& processor, u32 layer_index, Instrument const& instrument) {
    ASSERT(IsMainThread(processor.host));

    // If we currently have a sampler instrument, we keep it alive by storing it and releasing at a later
    // time.
    if (auto current = processor.layer_processors[layer_index]
                           .instrument.TryGet<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>())
        dyn::Append(processor.lifetime_extended_insts, *current);

    // Retain the new instrument
    if (auto sampled_inst = instrument.TryGet<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>())
        sampled_inst->Retain();

    processor.layer_processors[layer_index].instrument = instrument;

    switch (instrument.tag) {
        case InstrumentType::Sampler: {
            auto& sampler_inst =
                instrument.Get<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>();
            processor.layer_processors[layer_index].desired_inst.Set(&*sampler_inst);
            break;
        }
        case InstrumentType::WaveformSynth: {
            auto& w = instrument.Get<WaveformType>();
            processor.layer_processors[layer_index].desired_inst.Set(w);
            break;
        }
        case InstrumentType::None: {
            processor.layer_processors[layer_index].desired_inst.SetNone();
            break;
        }
    }

    processor.events_for_audio_thread.Push(LayerInstrumentChanged {.layer_index = layer_index});
    processor.host.request_process(&processor.host);
}

void SetConvolutionIrAudioData(AudioProcessor& processor, AudioData const* audio_data) {
    ASSERT(IsMainThread(processor.host));
    processor.convo.ConvolutionIrDataLoaded(audio_data);
    processor.events_for_audio_thread.Push(EventForAudioThreadType::ConvolutionIRChanged);
    processor.host.request_process(&processor.host);
}

void ApplyNewState(AudioProcessor& processor, StateSnapshot const& state, StateSource source) {
    if (source == StateSource::Daw)
        for (auto [i, cc] : Enumerate(processor.param_learned_ccs))
            cc.AssignBlockwise(state.param_learned_ccs[i]);

    for (auto const i : Range(k_num_parameters))
        processor.params[i].SetLinearValue(state.param_values[i]);

    processor.desired_effects_order.Store(EncodeEffectsArray(state.fx_order), StoreMemoryOrder::Relaxed);

    // reload everything
    {
        if (auto const host_params =
                (clap_host_params const*)processor.host.get_extension(&processor.host, CLAP_EXT_PARAMS))
            host_params->rescan(&processor.host, CLAP_PARAM_RESCAN_VALUES);
        processor.events_for_audio_thread.Push(EventForAudioThreadType::ReloadAllAudioState);
        processor.host.request_process(&processor.host);
    }
}

StateSnapshot MakeStateSnapshot(AudioProcessor const& processor) {
    StateSnapshot result {};
    auto const ordered_fx_pointers =
        DecodeEffectsArray(processor.desired_effects_order.Load(LoadMemoryOrder::Relaxed),
                           processor.effects_ordered_by_type);
    for (auto [i, fx_pointer] : Enumerate(ordered_fx_pointers))
        result.fx_order[i] = fx_pointer->type;

    for (auto const i : Range(k_num_layers))
        result.inst_ids[i] = processor.layer_processors[i].instrument_id;

    result.ir_id = processor.convo.ir_id;

    for (auto const i : Range(k_num_parameters))
        result.param_values[i] = processor.params[i].LinearValue();

    for (auto [i, cc] : Enumerate(processor.param_learned_ccs))
        result.param_learned_ccs[i] = cc.GetBlockwise();

    return result;
}

inline void
ResetProcessor(AudioProcessor& processor, Bitset<k_num_parameters> processing_change, u32 num_frames) {
    ZoneScoped;
    processor.whole_engine_volume_fade.ForceSetFullVolume();

    // Set pending parameter changes
    processing_change |= Exchange(processor.pending_param_changes, {});
    if (processing_change.AnyValuesSet())
        ProcessorOnParamChange(processor, {processor.params.data, processing_change});

    // Discard any smoothing
    processor.smoothed_value_system.ResetAll();
    if (num_frames) processor.smoothed_value_system.ProcessBlock(num_frames);

    // Set the convolution IR
    processor.convo.SwapConvolversIfNeeded();

    // Set the effects order
    processor.actual_fx_order =
        DecodeEffectsArray(processor.desired_effects_order.Load(LoadMemoryOrder::Relaxed),
                           processor.effects_ordered_by_type);

    // Reset the effects
    for (auto fx : processor.actual_fx_order)
        fx->Reset();
    processor.fx_need_another_frame_of_processing = false;

    // Reset layers
    for (auto& l : processor.layer_processors)
        ChangeInstrumentIfNeededAndReset(l, processor.voice_pool);
}

static bool Activate(AudioProcessor& processor, PluginActivateArgs args) {
    if (args.sample_rate <= 0 || args.max_block_size == 0) {
        PanicIfReached();
        return false;
    }

    processor.audio_processing_context.process_block_size_max = args.max_block_size;
    processor.audio_processing_context.sample_rate = (f32)args.sample_rate;

    for (auto& fx : processor.effects_ordered_by_type)
        fx->PrepareToPlay(processor.audio_processing_context);

    if (Exchange(processor.previous_block_size, processor.audio_processing_context.process_block_size_max) <
        processor.audio_processing_context.process_block_size_max) {

        // We reserve up-front a large allocation so that it's less likely we have to do multiple
        // calls to the OS. Roughly 1.2MB for a block size of 512.
        auto const alloc_size = (usize)processor.audio_processing_context.process_block_size_max * 2544;
        processor.audio_data_allocator = ArenaAllocator(PageAllocator::Instance(), alloc_size);

        processor.voice_pool.PrepareToPlay(processor.audio_data_allocator,
                                           processor.audio_processing_context);

        for (auto [index, l] : Enumerate(processor.layer_processors))
            PrepareToPlay(l, processor.audio_data_allocator, processor.audio_processing_context);

        processor.peak_meter.PrepareToPlay(processor.audio_processing_context.sample_rate,
                                           processor.audio_data_allocator);

        processor.smoothed_value_system.PrepareToPlay(
            processor.audio_processing_context.process_block_size_max,
            processor.audio_processing_context.sample_rate,
            processor.audio_data_allocator);
    }

    Bitset<k_num_parameters> changed_params;
    changed_params.SetAll();
    ResetProcessor(processor, changed_params, 0);

    processor.activated = true;
    return true;
}

static void ProcessClapNoteOrMidi(AudioProcessor& processor,
                                  clap_event_header const& event,
                                  clap_output_events const& out,
                                  bool& request_main_thread_callback) {
    // IMPROVE: support per-param modulation and automation - each param can opt in to it individually

    Bitset<k_num_parameters> changed_params {};

    switch (event.type) {
        case CLAP_EVENT_NOTE_ON: {
            auto note = (clap_event_note const&)event;
            if (note.channel != 0) break;
            if (note.key > MidiMessage::k_u7_max) break;
            if (note.channel > MidiMessage::k_u4_max) break;
            MidiChannelNote const chan_note {.note = (u7)note.key, .channel = (u4)note.channel};

            processor.audio_processing_context.midi_note_state.NoteOn(chan_note, (f32)note.velocity);
            HandleNoteOn(processor, chan_note, (f32)note.velocity, note.header.time);
            break;
        }
        case CLAP_EVENT_NOTE_OFF: {
            auto note = (clap_event_note const&)event;
            if (note.channel != 0) break;
            if (note.key > MidiMessage::k_u7_max) break;
            if (note.channel > MidiMessage::k_u4_max) break;
            MidiChannelNote const chan_note {.note = (u7)note.key, .channel = (u4)note.channel};

            processor.audio_processing_context.midi_note_state.NoteOff(chan_note);
            HandleNoteOff(processor, chan_note, false);
            break;
        }
        case CLAP_EVENT_NOTE_CHOKE: {
            auto note = (clap_event_note const&)event;

            if (note.key == -1) {
                if (note.channel == -1) {
                    for (auto const chan : Range(16u)) {
                        processor.audio_processing_context.midi_note_state.keys_held[chan].ClearAll();
                        processor.audio_processing_context.midi_note_state.sustain_keys[chan].ClearAll();
                    }
                    processor.voice_pool.EndAllVoicesInstantly();
                } else if (note.channel >= 0 && note.channel < 16) {
                    processor.audio_processing_context.midi_note_state.keys_held[(usize)note.channel]
                        .ClearAll();
                    processor.audio_processing_context.midi_note_state.sustain_keys[(usize)note.channel]
                        .ClearAll();
                    for (auto& v : processor.voice_pool.EnumerateActiveVoices())
                        if (v.midi_key_trigger.channel == note.channel) EndVoiceInstantly(v);
                }
            } else if (note.key < 128 && note.key >= 0) {
                if (note.channel == -1) {
                    for (auto const chan : Range(16u)) {
                        processor.audio_processing_context.midi_note_state.keys_held[chan].Clear(
                            (usize)note.key);
                        processor.audio_processing_context.midi_note_state.sustain_keys[chan].Clear(
                            (usize)note.key);
                    }
                    for (auto& v : processor.voice_pool.EnumerateActiveVoices())
                        if (v.midi_key_trigger.note == note.key) EndVoiceInstantly(v);
                } else if (note.channel >= 0 && note.channel < 16) {
                    processor.audio_processing_context.midi_note_state.keys_held[(usize)note.channel].Clear(
                        (usize)note.key);
                    processor.audio_processing_context.midi_note_state.sustain_keys[(usize)note.channel]
                        .Clear((usize)note.key);
                    for (auto& v : processor.voice_pool.EnumerateActiveVoices())
                        if (v.midi_key_trigger.note == note.key && v.midi_key_trigger.channel == note.channel)
                            EndVoiceInstantly(v);
                }
            }

            break;
        }
        case CLAP_EVENT_NOTE_EXPRESSION: {
            // IMPROVE: support expression
            break;
        }
        case CLAP_EVENT_MIDI: {
            auto midi = (clap_event_midi const&)event;
            MidiMessage message {};
            message.status = midi.data[0];
            message.data1 = midi.data[1];
            message.data2 = midi.data[2];

            auto type = message.Type();
            if (type == MidiMessageType::NoteOn || type == MidiMessageType::NoteOff ||
                type == MidiMessageType::ControlChange) {
                processor.for_main_thread.flags.FetchOr(AudioProcessor::MainThreadCallbackFlagsUpdateGui,
                                                        RmwMemoryOrder::Relaxed);
                request_main_thread_callback = true;
            }

            switch (message.Type()) {
                case MidiMessageType::NoteOn: {
                    processor.audio_processing_context.midi_note_state.NoteOn(message.ChannelNote(),
                                                                              message.Velocity() / 127.0f);
                    HandleNoteOn(processor, message.ChannelNote(), message.Velocity() / 127.0f, event.time);
                    break;
                }
                case MidiMessageType::NoteOff: {
                    processor.audio_processing_context.midi_note_state.NoteOff(message.ChannelNote());
                    HandleNoteOff(processor, message.ChannelNote(), false);
                    break;
                }
                case MidiMessageType::PitchWheel: {
                    break;
                    constexpr f32 k_pitch_bend_semitones = 48;
                    auto const channel = message.ChannelNum();
                    auto const pitch_pos = (message.PitchBend() / 16383.0f - 0.5f) * 2.0f;

                    for (auto& v : processor.voice_pool.EnumerateActiveVoices()) {
                        if (v.midi_key_trigger.channel == channel) {
                            SetVoicePitch(v,
                                          v.controller->tune + pitch_pos * k_pitch_bend_semitones,
                                          processor.audio_processing_context.sample_rate);
                        }
                    }
                    break;
                }
                case MidiMessageType::ControlChange: {
                    auto const cc_num = message.CCNum();
                    auto const cc_val = message.CCValue();
                    auto const channel = message.ChannelNum();

                    if (cc_num == 64) {
                        if (cc_val >= 64) {
                            auto const notes_to_end =
                                processor.audio_processing_context.midi_note_state.SustainPedalUp(channel);
                            notes_to_end.ForEachSetBit([&processor, channel](usize note) {
                                HandleNoteOff(processor, {CheckedCast<u7>(note), channel}, true);
                            });
                        } else {
                            processor.audio_processing_context.midi_note_state.SustainPedalDown(channel);
                        }
                    }

                    if (k_midi_learn_controller_bitset.Get(cc_num)) {
                        if (auto param_index =
                                processor.midi_learn_param_index.Exchange(k_nullopt, RmwMemoryOrder::Relaxed);
                            param_index.HasValue()) {
                            processor.param_learned_ccs[(usize)param_index.Value()].Set(cc_num);
                        }

                        for (auto const [param_index, param_ccs] :
                             Enumerate<u16>(processor.param_learned_ccs)) {
                            if (!param_ccs.Get(cc_num)) continue;

                            processor.time_when_cc_moved_param[param_index].Store(TimePoint::Now(),
                                                                                  StoreMemoryOrder::Relaxed);

                            auto& info = processor.params[param_index].info;
                            auto const percent = (f32)cc_val / 127.0f;
                            auto const val = info.linear_range.min + (info.linear_range.Delta() * percent);
                            processor.params[param_index].SetLinearValue(val);
                            changed_params.Set(param_index);

                            clap_event_param_value value_event {};
                            value_event.header.type = CLAP_EVENT_PARAM_VALUE;
                            value_event.header.size = sizeof(value_event);
                            value_event.header.flags = CLAP_EVENT_IS_LIVE | CLAP_EVENT_DONT_RECORD;
                            value_event.note_id = -1;
                            value_event.port_index = -1;
                            value_event.channel = -1;
                            value_event.key = -1;
                            value_event.value = (f64)val;
                            value_event.param_id = ParamIndexToId(ParamIndex {param_index});
                            out.try_push(&out, (clap_event_header const*)&value_event);
                        }
                    }
                    break;
                }
                case MidiMessageType::PolyAftertouch: {
                    break;
                    auto const note = message.NoteNum();
                    auto const channel = message.ChannelNum();
                    auto const value = message.PolyAftertouch();
                    for (auto& v : processor.voice_pool.EnumerateActiveVoices()) {
                        if (v.midi_key_trigger.channel == channel && v.midi_key_trigger.note == note) {
                            v.aftertouch_multiplier =
                                1 + trig_table_lookup::SinTurns(value / 127.0f / 4.0f) * 2;
                        }
                    }
                    break;
                }
                case MidiMessageType::ChannelAftertouch: {
                    break;
                    auto const channel = message.ChannelNum();
                    auto const value = message.ChannelPressure();
                    for (auto& v : processor.voice_pool.EnumerateActiveVoices()) {
                        if (v.midi_key_trigger.channel == channel) {
                            v.aftertouch_multiplier =
                                1 + trig_table_lookup::SinTurns(value / 127.0f / 4.0f) * 2;
                        }
                    }

                    break;
                }
                case MidiMessageType::SystemMessage: break;
                case MidiMessageType::ProgramChange: break;
                case MidiMessageType::None: PanicIfReached(); break;
            }

            break;
        }
    }

    if (changed_params.AnyValuesSet()) ProcessorOnParamChange(processor, {processor.params, changed_params});
}

static void ConsumeParamEventsFromHost(Parameters& params,
                                       clap_input_events const& events,
                                       Bitset<k_num_parameters>& params_changed) {
    ZoneScoped;
    // IMPROVE: support sample-accurate value changes
    for (auto const event_index : Range(events.size(&events))) {
        auto e = events.get(&events, event_index);
        if (e->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;

        // IMPROVE: support CLAP_EVENT_PARAM_MOD

        if (e->type == CLAP_EVENT_PARAM_VALUE) {
            auto value = CheckedPointerCast<clap_event_param_value const*>(e);

            // IMRPOVE: support polyphonic
            if (value->note_id != -1 || value->channel > 0 || value->key > 0) continue;

            if (auto index = ParamIdToIndex(value->param_id)) {
                params[ToInt(*index)].SetLinearValue((f32)value->value);
                params_changed.Set(ToInt(*index));
            }
        }
    }
}

static void ConsumeParamEventsFromGui(AudioProcessor& processor,
                                      clap_output_events const& out,
                                      Bitset<k_num_parameters>& params_changed) {
    ZoneScoped;
    for (auto const& e : processor.param_events_for_audio_thread.PopAll()) {
        switch (e.tag) {
            case EventForAudioThreadType::ParamChanged: {
                auto const& value = e.Get<GuiChangedParam>();

                clap_event_param_value event {};
                event.header.type = CLAP_EVENT_PARAM_VALUE;
                event.header.size = sizeof(event);
                event.header.flags = CLAP_EVENT_IS_LIVE;
                event.note_id = -1;
                event.port_index = -1;
                event.channel = -1;
                event.key = -1;
                event.value = (f64)value.value;
                event.param_id = ParamIndexToId(value.param);
                if (!value.host_should_not_record) event.header.flags |= CLAP_EVENT_DONT_RECORD;
                out.try_push(&out, (clap_event_header const*)&event);
                params_changed.Set(ToInt(value.param));
                break;
            }
            case EventForAudioThreadType::ParamGestureBegin: {
                auto const& gesture = e.Get<GuiStartedChangingParam>();

                clap_event_param_gesture event {};
                event.header.type = CLAP_EVENT_PARAM_GESTURE_BEGIN;
                event.header.size = sizeof(event);
                event.header.flags = CLAP_EVENT_IS_LIVE;
                event.param_id = ParamIndexToId(gesture.param);
                out.try_push(&out, (clap_event_header const*)&event);
                break;
            }
            case EventForAudioThreadType::ParamGestureEnd: {
                auto const& gesture = e.Get<GuiEndedChangingParam>();

                clap_event_param_gesture event {};
                event.header.type = CLAP_EVENT_PARAM_GESTURE_END;
                event.header.size = sizeof(event);
                event.header.flags = CLAP_EVENT_IS_LIVE;
                event.param_id = ParamIndexToId(gesture.param);
                out.try_push(&out, (clap_event_header const*)&event);
                break;
            }
            case EventForAudioThreadType::FxOrderChanged:
            case EventForAudioThreadType::ReloadAllAudioState:
            case EventForAudioThreadType::ConvolutionIRChanged:
            case EventForAudioThreadType::LayerInstrumentChanged:
            case EventForAudioThreadType::StartNote:
            case EventForAudioThreadType::EndNote:
            case EventForAudioThreadType::RemoveMidiLearn: PanicIfReached();
        }
    }
}

static void
FlushParameterEvents(AudioProcessor& processor, clap_input_events const& in, clap_output_events const& out) {
    Bitset<k_num_parameters> params_changed {};
    ConsumeParamEventsFromHost(processor.params, in, params_changed);
    ConsumeParamEventsFromGui(processor, out, params_changed);

    if (processor.activated) {
        if (params_changed.AnyValuesSet())
            ProcessorOnParamChange(processor, {processor.params, params_changed});
    } else {
        // If we are not activated, then we don't need to call processor param change because the
        // state of the processing plugin will be reset activate()
    }
}

clap_process_status Process(AudioProcessor& processor, clap_process const& process) {
    ZoneScoped;
    ASSERT(process.audio_outputs_count == 1);

    if (process.audio_outputs->channel_count != 2) return CLAP_PROCESS_ERROR;

    clap_process_status result = CLAP_PROCESS_CONTINUE;
    auto const num_sample_frames = process.frames_count;
    auto outputs = process.audio_outputs->data32;

    // Handle transport changes
    {
        // IMPROVE: support per-sample tempo changes by processing CLAP_EVENT_TRANSPORT events

        bool tempo_changed = false;
        if (process.transport && (process.transport->flags & CLAP_TRANSPORT_HAS_TEMPO) &&
            process.transport->tempo != processor.audio_processing_context.tempo &&
            process.transport->tempo > 0) {
            processor.audio_processing_context.tempo = process.transport->tempo;
            tempo_changed = true;
        }
        if (processor.audio_processing_context.tempo <= 0) {
            processor.audio_processing_context.tempo = 120;
            tempo_changed = true;
        }

        if (tempo_changed) {
            // IMPROVE: only recalculate changes if the effect is actually on and is currently using
            // tempo-synced processing
            for (auto fx : processor.effects_ordered_by_type)
                fx->SetTempo(processor.audio_processing_context.tempo);
            for (auto& layer : processor.layer_processors)
                SetTempo(layer, processor.voice_pool, processor.audio_processing_context);
        }
    }

    constexpr f32 k_fade_out_ms = 30;
    constexpr f32 k_fade_in_ms = 10;

    auto internal_events = processor.events_for_audio_thread.PopAll();
    Bitset<k_num_parameters> params_changed {};
    Array<bool, k_num_layers> layers_changed {};
    bool mark_convolution_for_fade_out = false;

    bool request_main_thread_callback = false;
    DEFER {
        if (processor.previous_process_status != result) {
            result = processor.previous_process_status;
            request_main_thread_callback = true;
        }
        if (request_main_thread_callback) processor.host.request_callback(&processor.host);
        processor.for_main_thread.notes_currently_held.AssignBlockwise(
            processor.audio_processing_context.midi_note_state.NotesCurrentlyHeldAllChannels());
    };

    ConsumeParamEventsFromGui(processor, *process.out_events, params_changed);
    ConsumeParamEventsFromHost(processor.params, *process.in_events, params_changed);

    Optional<AudioProcessor::FadeType> new_fade_type {};
    for (auto const& e : internal_events) {
        switch (e.tag) {
            case EventForAudioThreadType::LayerInstrumentChanged: {
                auto const& layer_changed = e.Get<LayerInstrumentChanged>();
                layers_changed[layer_changed.layer_index] = true;
                break;
            }
            case EventForAudioThreadType::FxOrderChanged: {
                if (!new_fade_type) new_fade_type = AudioProcessor::FadeType::OutAndIn;
                break;
            }
            case EventForAudioThreadType::ReloadAllAudioState: {
                params_changed.SetAll();
                new_fade_type = AudioProcessor::FadeType::OutAndRestartVoices;
                for (auto& l : layers_changed)
                    l = true;
                break;
            }
            case EventForAudioThreadType::ConvolutionIRChanged: {
                mark_convolution_for_fade_out = true;
                break;
            }
            case EventForAudioThreadType::RemoveMidiLearn: {
                auto const& remove_midi_learn = e.Get<RemoveMidiLearn>();
                processor.param_learned_ccs[ToInt(remove_midi_learn.param)].Clear(remove_midi_learn.midi_cc);
                break;
            }
            case EventForAudioThreadType::ParamChanged:
            case EventForAudioThreadType::ParamGestureBegin:
            case EventForAudioThreadType::ParamGestureEnd: PanicIfReached();
            case EventForAudioThreadType::StartNote: break;
            case EventForAudioThreadType::EndNote: break;
        }
    }

    if (new_fade_type) {
        processor.whole_engine_volume_fade_type = *new_fade_type;
        processor.whole_engine_volume_fade.SetAsFadeOutIfNotAlready(
            processor.audio_processing_context.sample_rate,
            k_fade_out_ms);
    }

    if (processor.peak_meter.Silent() && !processor.fx_need_another_frame_of_processing) {
        ResetProcessor(processor, params_changed, num_sample_frames);
        params_changed = {};
    }

    switch (processor.whole_engine_volume_fade.GetCurrentState()) {
        case VolumeFade::State::Silent: {
            ResetProcessor(processor, params_changed, num_sample_frames);

            // We have just done a hard reset on everything, any other state change is no longer
            // valid.
            params_changed = {};

            if (processor.whole_engine_volume_fade_type == AudioProcessor::FadeType::OutAndRestartVoices) {
                processor.voice_pool.EndAllVoicesInstantly();
                processor.restart_voices_for_layer_bitset = ~0; // restart all voices
            } else {
                processor.whole_engine_volume_fade.SetAsFadeIn(processor.audio_processing_context.sample_rate,
                                                               k_fade_in_ms);
            }

            ASSERT(processor.whole_engine_volume_fade.GetCurrentState() == VolumeFade::State::FullVolume);
            break;
        }
        case VolumeFade::State::FadeOut: {
            // If we are going to be fading out anyways, let's apply param changes at that time too to
            // avoid any pops
            processor.pending_param_changes |= params_changed;
            params_changed = {};
            break;
        }
        default: break;
    }

    if (params_changed.AnyValuesSet())
        ProcessorOnParamChange(processor, {processor.params.data, params_changed});

    processor.smoothed_value_system.ProcessBlock(num_sample_frames);

    // Create new voices for layer if requested. We want to do this after parameters have been updated
    // so that the voices start with the most recent parameter values.
    if (auto restart_layer_bitset = Exchange(processor.restart_voices_for_layer_bitset, 0)) {
        for (u32 chan = 0; chan <= 15; ++chan) {
            auto const keys_to_start =
                processor.audio_processing_context.midi_note_state.NotesHeldIncludingSustained((u4)chan);
            if (keys_to_start.AnyValuesSet()) {
                for (auto [layer_index, layer] : Enumerate(processor.layer_processors)) {
                    if (restart_layer_bitset & (1 << layer_index)) {
                        for (u8 note_num = 0; note_num <= 127; ++note_num) {
                            if (keys_to_start.Get(note_num)) {
                                LayerHandleNoteOn(layer,
                                                  processor.audio_processing_context,
                                                  processor.voice_pool,
                                                  {.note = (u7)note_num, .channel = (u4)chan},
                                                  processor.audio_processing_context.midi_note_state
                                                      .velocities[chan][note_num],
                                                  0,
                                                  processor.dynamics_value_01,
                                                  processor.velocity_to_volume_01);
                            }
                        }
                    }
                }
            }
        }
    }

    {
        for (auto const i : Range(process.in_events->size(process.in_events))) {
            auto e = process.in_events->get(process.in_events, i);
            ProcessClapNoteOrMidi(processor, *e, *process.out_events, request_main_thread_callback);
        }
        for (auto& e : internal_events) {
            switch (e.tag) {
                case EventForAudioThreadType::StartNote: {
                    auto const start = e.Get<GuiNoteClicked>();
                    clap_event_note note {};
                    note.header.type = CLAP_EVENT_NOTE_ON;
                    note.header.size = sizeof(note);
                    note.key = start.key;
                    note.velocity = (f64)start.velocity;
                    note.note_id = -1;
                    ProcessClapNoteOrMidi(processor,
                                          note.header,
                                          *process.out_events,
                                          request_main_thread_callback);
                    break;
                }
                case EventForAudioThreadType::EndNote: {
                    auto const end = e.Get<GuiNoteClickReleased>();
                    clap_event_note note {};
                    note.header.type = CLAP_EVENT_NOTE_OFF;
                    note.header.size = sizeof(note);
                    note.key = end.key;
                    note.note_id = -1;
                    ProcessClapNoteOrMidi(processor,
                                          note.header,
                                          *process.out_events,
                                          request_main_thread_callback);
                    break;
                }
                default: break;
            }
        }
    }

    // Voices and layers
    // ======================================================================================================
    // IMPROVE: support sending the host CLAP_EVENT_NOTE_END events when voices end
    auto const layer_buffers =
        ProcessVoices(processor.voice_pool, num_sample_frames, processor.audio_processing_context);

    Span<f32> interleaved_outputs {};
    bool audio_was_generated_by_voices = false;
    for (auto const i : Range(k_num_layers)) {
        auto const process_result = ProcessLayer(processor.layer_processors[i],
                                                 processor.audio_processing_context,
                                                 processor.voice_pool,
                                                 num_sample_frames,
                                                 layers_changed[i],
                                                 layer_buffers[i]);

        if (process_result.did_any_processing) {
            audio_was_generated_by_voices = true;
            if (interleaved_outputs.size == 0)
                interleaved_outputs = layer_buffers[i];
            else
                SimdAddAlignedBuffer(interleaved_outputs.data,
                                     layer_buffers[i].data,
                                     (usize)num_sample_frames * 2);
        }

        if (process_result.instrument_swapped) {
            request_main_thread_callback = true;

            // Start new voices. We don't want to do that here because we want all parameter changes
            // to be applied beforehand.
            processor.restart_voices_for_layer_bitset |= 1 << i;
        }
    }

    if (interleaved_outputs.size == 0) {
        interleaved_outputs = processor.voice_pool.buffer_pool[0];
        SimdZeroAlignedBuffer(interleaved_outputs.data, num_sample_frames * 2);
    } else if constexpr (RUNTIME_SAFETY_CHECKS_ON && !PRODUCTION_BUILD) {
        for (auto const frame : Range(num_sample_frames)) {
            auto const& l = interleaved_outputs[frame * 2 + 0];
            auto const& r = interleaved_outputs[frame * 2 + 1];
            ASSERT(l >= -k_erroneous_sample_value && l <= k_erroneous_sample_value);
            ASSERT(r >= -k_erroneous_sample_value && r <= k_erroneous_sample_value);
        }
    }

    auto interleaved_stereo_samples = ToStereoFramesSpan(interleaved_outputs.data, num_sample_frames);

    if (audio_was_generated_by_voices || processor.fx_need_another_frame_of_processing) {
        // Effects
        // ==================================================================================================

        // interleaved_outputs is one of the voice buffers, we want to find 2 more to pass to the
        // effects rack
        u32 unused_buffer_indexes[2] = {UINT32_MAX, UINT32_MAX};
        {
            u32 unused_buffer_indexes_index = 0;
            for (auto const i : Range(k_num_voices)) {
                if (interleaved_outputs.data != processor.voice_pool.buffer_pool[i].data) {
                    unused_buffer_indexes[unused_buffer_indexes_index++] = i;
                    if (unused_buffer_indexes_index == 2) break;
                }
            }
        }
        ASSERT_HOT(unused_buffer_indexes[0] != UINT32_MAX);
        ASSERT_HOT(unused_buffer_indexes[1] != UINT32_MAX);

        ScratchBuffers const scratch_buffers(
            num_sample_frames,
            processor.voice_pool.buffer_pool[(usize)unused_buffer_indexes[0]].data,
            processor.voice_pool.buffer_pool[(usize)unused_buffer_indexes[1]].data);

        bool fx_need_another_frame_of_processing = false;
        for (auto fx : processor.actual_fx_order) {
            if (fx->type == EffectType::ConvolutionReverb) {
                auto const r = ((ConvolutionReverb*)fx)
                                   ->ProcessBlockConvolution(processor.audio_processing_context,
                                                             interleaved_stereo_samples,
                                                             scratch_buffers,
                                                             mark_convolution_for_fade_out);
                if (r.effect_process_state == EffectProcessResult::ProcessingTail)
                    fx_need_another_frame_of_processing = true;
                if (r.changed_ir) request_main_thread_callback = true;
            } else {
                auto const r = fx->ProcessBlock(interleaved_stereo_samples,
                                                scratch_buffers,
                                                processor.audio_processing_context);
                if (r == EffectProcessResult::ProcessingTail) fx_need_another_frame_of_processing = true;
            }
        }
        processor.fx_need_another_frame_of_processing = fx_need_another_frame_of_processing;

        // Master
        // ==================================================================================================

        for (auto [frame_index, frame] : Enumerate<u32>(interleaved_stereo_samples)) {
            frame *= processor.smoothed_value_system.Value(processor.master_vol_smoother_id, frame_index);

            // frame = Clamp(frame, {-1, -1}, {1, 1}); // hard limit
            frame *= processor.whole_engine_volume_fade.GetFade();
        }
        processor.peak_meter.AddBuffer(interleaved_stereo_samples);
    } else {
        processor.peak_meter.Zero();
        for (auto& l : processor.layer_processors)
            l.peak_meter.Zero();
        result = CLAP_PROCESS_SLEEP;
    }

    //
    // ======================================================================================================
    if (outputs)
        CopyInterleavedToSeparateChannels(outputs[0], outputs[1], interleaved_outputs, num_sample_frames);

    // Mark gui dirty
    {
        bool mark_gui_dirty = false;
        if (!processor.peak_meter.Silent()) mark_gui_dirty = true;
        for (auto& layer : processor.layer_processors)
            if (!layer.peak_meter.Silent()) mark_gui_dirty = true;
        if (mark_gui_dirty) {
            processor.for_main_thread.flags.FetchOr(AudioProcessor::MainThreadCallbackFlagsUpdateGui,
                                                    RmwMemoryOrder::Relaxed);
            request_main_thread_callback = true;
        }
    }

    return result;
}

static void Reset(AudioProcessor&) {
    // TODO(1.0):
    // - Clears all buffers, performs a full reset of the processing state (filters, oscillators,
    //   envelopes, lfo, ...) and kills all voices.
    // - The parameter's value remain unchanged.
    // - clap_process.steady_time may jump backward.
}

static void OnMainThread(AudioProcessor& processor, bool& update_gui) {
    ZoneScoped;
    processor.convo.DeletedUnusedConvolvers();

    auto flags = processor.for_main_thread.flags.Exchange(0, RmwMemoryOrder::Relaxed);
    if (flags & AudioProcessor::MainThreadCallbackFlagsRescanParameters) {
        auto host_params =
            (clap_host_params const*)processor.host.get_extension(&processor.host, CLAP_EXT_PARAMS);
        if (host_params) host_params->rescan(&processor.host, CLAP_PARAM_RESCAN_VALUES);
    }
    if (flags & AudioProcessor::MainThreadCallbackFlagsUpdateGui) update_gui = true;

    // Clear any instruments that aren't used anymore. The audio thread will request this callback after it
    // swaps any instruments.
    if (processor.lifetime_extended_insts.size) {
        bool all_layers_have_completed_swap = true;
        for (auto& l : processor.layer_processors) {
            if (!l.desired_inst.IsConsumed()) {
                all_layers_have_completed_swap = false;
                break;
            }
        }
        if (all_layers_have_completed_swap) {
            for (auto& i : processor.lifetime_extended_insts)
                i.Release();
            dyn::Clear(processor.lifetime_extended_insts);
        }
    }
}

static void OnThreadPoolExec(AudioProcessor& processor, u32 index) {
    OnThreadPoolExec(processor.voice_pool, index);
}

AudioProcessor::AudioProcessor(clap_host const& host)
    : host(host)
    , audio_processing_context {.host = host}
    , distortion(smoothed_value_system)
    , bit_crush(smoothed_value_system)
    , compressor(smoothed_value_system)
    , filter_effect(smoothed_value_system)
    , stereo_widen(smoothed_value_system)
    , chorus(smoothed_value_system)
    , reverb(smoothed_value_system)
    , delay(smoothed_value_system)
    , phaser(smoothed_value_system)
    , convo(smoothed_value_system)
    , effects_ordered_by_type(OrderEffectsToEnum(EffectsArray {
          &distortion,
          &bit_crush,
          &compressor,
          &filter_effect,
          &stereo_widen,
          &chorus,
          &reverb,
          &delay,
          &phaser,
          &convo,
      })) {

    for (auto const i : Range(k_num_parameters)) {
        PLACEMENT_NEW(&params[i])
        Parameter {
            .info = k_param_descriptors[i],
            .value = k_param_descriptors[i].default_linear_value,
        };
    }

    Bitset<k_num_parameters> changed;
    changed.SetAll();
    ProcessorOnParamChange(*this, {params.data, changed});
    smoothed_value_system.ResetAll();

    processor_callbacks = {
        .activate = Activate,
        .deactivate = Deactivate,
        .reset = Reset,
        .process = Process,
        .flush_parameter_events = FlushParameterEvents,
        .on_main_thread = OnMainThread,
        .on_thread_pool_exec = OnThreadPoolExec,
    };
}

AudioProcessor::~AudioProcessor() {
    for (auto& i : lifetime_extended_insts)
        i.Release();
}
