// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugin_instance.hpp"

#include <clap/ext/params.h>

#include "foundation/foundation.hpp"
#include "utils/debug/debug.hpp"

#include "common/common_errors.hpp"
#include "common/constants.hpp"
#include "cross_instance_systems.hpp"
#include "effects/effect.hpp"
#include "instrument_type.hpp"
#include "layer_processor.hpp"
#include "param_info.hpp"
#include "plugin.hpp"
#include "sample_library_server.hpp"
#include "settings/settings_file.hpp"
#include "state/state_coding.hpp"
#include "state/state_snapshot.hpp"

bool StateChangedSinceLastSnapshot(PluginInstance& plugin) {
    return !(plugin.last_snapshot.state == CurrentStateSnapshot(plugin));
}

StateSnapshot CurrentStateSnapshot(PluginInstance& plugin) {
    StateSnapshot result {};
    auto const ordered_fx_pointers = DecodeEffectsArray(plugin.processor.desired_effects_order.Load(),
                                                        plugin.processor.effects_ordered_by_type);
    for (auto [i, fx_pointer] : Enumerate(ordered_fx_pointers))
        result.fx_order[i] = fx_pointer->type;

    for (auto const i : Range(k_num_layers))
        result.inst_ids[i] = plugin.pending_state_change
                                 ? plugin.pending_state_change->snapshot.state.inst_ids[i]
                                 : plugin.layers[i].instrument_id;

    result.ir_id = plugin.pending_state_change ? plugin.pending_state_change->snapshot.state.ir_id
                                               : plugin.processor.convo.ir_index;

    if (plugin.pending_state_change)
        for (auto const i : Range(k_num_parameters))
            result.param_values[i] = plugin.pending_state_change->snapshot.state.param_values[i];
    else
        for (auto const i : Range(k_num_parameters))
            result.param_values[i] = plugin.processor.params[i].LinearValue();

    for (auto [i, cc] : Enumerate(plugin.processor.param_learned_ccs))
        result.param_learned_ccs[i] = cc.GetBlockwise();

    return result;
}

static void SetInstrument(PluginInstance& plugin, u32 layer_index, Instrument const& instrument) {
    ZoneScoped;
    DebugAssertMainThread(plugin.host);

    using namespace sample_lib_server;

    // We keep the instrument alive by putting it in this storage and cleaning up at a later time.
    if (auto current = plugin.layers[layer_index].instrument.TryGet<RefCounted<LoadedInstrument>>())
        dyn::Append(plugin.lifetime_extended_insts, *current);

    if (auto sampled_inst = instrument.TryGet<RefCounted<LoadedInstrument>>()) sampled_inst->Retain();

    plugin.layers[layer_index].instrument = instrument;

    SetInstrument(plugin.processor, layer_index, instrument);
}

static void SetLastSnapshot(PluginInstance& plugin, StateSnapshotWithMetadata const& state) {
    ZoneScoped;
    DebugAssertMainThread(plugin.host);

    plugin.last_snapshot.state = state.state;
    plugin.last_snapshot_metadata_arena.ResetCursorAndConsolidateRegions();
    plugin.last_snapshot.metadata = state.metadata.Clone(plugin.last_snapshot_metadata_arena);
    plugin.gui_needs_to_handle_preset_name_change = true;
    plugin.processor.for_main_thread.flags.FetchOr(AudioProcessor::MainThreadCallbackFlagsUpdateGui);
    plugin.processor.host.request_callback(&plugin.host);

    // do this at the end because the pending state could be the arg of this function
    plugin.pending_state_change.Clear();
}

void LoadNewState(PluginInstance& plugin, StateSnapshotWithMetadata const& state, StateSource source) {
    ZoneScoped;
    DebugAssertMainThread(plugin.host);

    auto const async = ({
        bool a = false;
        for (auto const& i : state.state.inst_ids) {
            if (i.tag == InstrumentType::Sampler) {
                a = true;
                break;
            }
        }
        if (state.state.ir_id) a = true;
        a;
    });

    if (!async) {
        for (auto [layer_index, i] : Enumerate<u32>(plugin.last_snapshot.state.inst_ids)) {
            plugin.layers[layer_index].instrument_id = i;
            switch (i.tag) {
                case InstrumentType::None:
                    SetInstrument(plugin, layer_index, i.GetFromTag<InstrumentType::None>());
                    break;
                case InstrumentType::WaveformSynth:
                    SetInstrument(plugin, layer_index, i.GetFromTag<InstrumentType::WaveformSynth>());
                    break;
                case InstrumentType::Sampler: PanicIfReached(); break;
            }
        }
        ASSERT(!state.state.ir_id.HasValue());
        plugin.processor.convo.ir_index = nullopt;
        SetConvolutionIr(plugin.processor, nullptr);

        ApplyNewState(plugin.processor, state.state, source);
        SetLastSnapshot(plugin, state);
    } else {
        plugin.pending_state_change.Emplace();
        auto& pending = *plugin.pending_state_change;
        pending.snapshot.state = state.state;
        pending.snapshot.metadata = state.metadata.Clone(pending.arena);
        pending.source = source;

        for (auto [layer_index, i] : Enumerate<u32>(state.state.inst_ids)) {
            if (i.tag != InstrumentType::Sampler) continue;

            plugin.layers[layer_index].instrument_id = i;
            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                                        plugin.sample_lib_server_async_channel,
                                                        sample_lib_server::LoadRequestInstrumentIdWithLayer {
                                                            .id = i.Get<sample_lib::InstrumentId>(),
                                                            .layer_index = layer_index,
                                                        });
            dyn::Append(pending.requests, async_id);
        }

        if (state.state.ir_id) {
            plugin.processor.convo.ir_index = state.state.ir_id;
            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                                        plugin.sample_lib_server_async_channel,
                                                        *state.state.ir_id);
            dyn::Append(pending.requests, async_id);
        }
    }
}

static bool InstMatches(sample_lib::InstrumentId const& id,
                        sample_lib_server::RefCounted<LoadedInstrument> const& inst) {
    return id.library_name == inst->instrument.library.name && id.inst_name == inst->instrument.name;
}

static bool IrMatches(sample_lib::IrId const& id, sample_lib_server::RefCounted<LoadedIr> const& ir) {
    return id.library_name == ir->ir.library.name && id.ir_name == ir->ir.name;
}

static void ApplyNewStateFromPending(PluginInstance& plugin) {
    ZoneScoped;
    DebugAssertMainThread(plugin.host);

    DebugLoc();

    for (auto const layer_index : Range(k_num_layers)) {
        auto const inst_id = plugin.pending_state_change->snapshot.state.inst_ids[layer_index];

        Instrument instrument = InstrumentType::None;

        switch (inst_id.tag) {
            case InstrumentType::None: break;
            case InstrumentType::WaveformSynth: {
                instrument = inst_id.GetFromTag<InstrumentType::WaveformSynth>();
                break;
            }
            case InstrumentType::Sampler: {
                for (auto const& r : plugin.pending_state_change->retained_results) {
                    auto const loaded_inst = r.TryExtract<sample_lib_server::RefCounted<LoadedInstrument>>();

                    if (loaded_inst &&
                        InstMatches(inst_id.GetFromTag<InstrumentType::Sampler>(), *loaded_inst))
                        instrument = *loaded_inst;
                }
                break;
            }
        }

        SetInstrument(plugin, layer_index, instrument);
    }

    {
        auto const ir_id = plugin.pending_state_change->snapshot.state.ir_id;

        AudioData const* ir_audio_data = nullptr;
        if (ir_id) {
            for (auto const& r : plugin.pending_state_change->retained_results) {
                auto const loaded_ir = r.TryExtract<sample_lib_server::RefCounted<LoadedIr>>();
                if (loaded_ir && IrMatches(*ir_id, *loaded_ir)) ir_audio_data = (*loaded_ir)->audio_data;
            }
        }

        SetConvolutionIr(plugin.processor, ir_audio_data);
    }

    ApplyNewState(plugin.processor,
                  plugin.pending_state_change->snapshot.state,
                  plugin.pending_state_change->source);

    // do it last because it clears pending_state_change
    SetLastSnapshot(plugin, plugin.pending_state_change->snapshot);
}

void LoadPresetFromListing(PluginInstance& plugin,
                           PresetSelectionCriteria const& selection_criteria,
                           PresetsFolderScanResult const& listing) {
    if (listing.is_loading) {
        plugin.pending_preset_selection_criteria = selection_criteria;
    } else if (listing.listing) {
        if (auto entry = SelectPresetFromListing(*listing.listing,
                                                 selection_criteria,
                                                 plugin.last_snapshot.metadata.Path(),
                                                 plugin.random_seed)) {
            LoadPresetFromFile(plugin, entry->Path());
        }
    }
}

void LoadPresetFromFile(PluginInstance& plugin, String path) {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator, Kb(16)};
    auto state_outcome = LoadPresetFile(path, scratch_arena);

    if (state_outcome.HasValue()) {
        LoadNewState(plugin,
                     {
                         .state = state_outcome.Value(),
                         .metadata = {.name_or_path = path},
                     },
                     StateSource::PresetFile);
    } else {
        auto item = plugin.error_notifications.NewError();
        item->value = {
            .title = "Failed to load preset"_s,
            .message = path,
            .error_code = state_outcome.Error(),
            .id = U64FromChars("statload"),
        };
        plugin.error_notifications.AddOrUpdateError(item);
    }
}

void SaveCurrentStateToFile(PluginInstance& plugin, String path) {
    if (auto outcome = SavePresetFile(path, CurrentStateSnapshot(plugin)); outcome.Succeeded()) {
        plugin.last_snapshot_metadata_arena.ResetCursorAndConsolidateRegions();
        plugin.last_snapshot.metadata =
            StateSnapshotMetadata {.name_or_path = path}.Clone(plugin.last_snapshot_metadata_arena);
    } else {
        auto item = plugin.error_notifications.NewError();
        item->value = {
            .title = "Failed to save preset"_s,
            .message = path,
            .error_code = outcome.Error(),
            .id = U64FromChars("statsave"),
        };
        plugin.error_notifications.AddOrUpdateError(item);
    }
}

static void SampleLibraryResourceLoaded(PluginInstance& plugin, sample_lib_server::LoadResult result) {
    ZoneScoped;
    DebugAssertMainThread(plugin.host);

    enum class Source : u32 { OneOff, PartOfPendingStateChange, LastInPendingStateChange, Count };

    auto const source = ({
        Source s {Source::OneOff};
        if (plugin.pending_state_change) {
            auto& requests = plugin.pending_state_change->requests;
            if (auto const opt_index = FindIf(requests, [&](sample_lib_server::RequestId const& id) {
                    return id == result.id;
                })) {
                s = Source::PartOfPendingStateChange;
                dyn::Remove(requests, *opt_index);
                if (requests.size == 0) s = Source::LastInPendingStateChange;
            }
        }
        s;
    });

    DBG_PRINT_EXPR(source);

    switch (source) {
        case Source::OneOff: {
            if (result.result.tag != sample_lib_server::LoadResult::ResultType::Success) break;

            auto const resource = result.result.Get<sample_lib_server::Resource>();
            switch (resource.tag) {
                case sample_lib_server::LoadRequestType::Instrument: {
                    auto const loaded_inst = resource.Get<sample_lib_server::RefCounted<LoadedInstrument>>();

                    for (auto [layer_index, l] : Enumerate<u32>(plugin.layers)) {
                        if (auto const i = l.instrument_id.TryGet<sample_lib::InstrumentId>()) {
                            if (InstMatches(*i, loaded_inst)) SetInstrument(plugin, layer_index, loaded_inst);
                        }
                    }
                    break;
                }
                case sample_lib_server::LoadRequestType::Ir: {
                    auto const loaded_ir = resource.Get<sample_lib_server::RefCounted<LoadedIr>>();

                    auto const current_ir_id = plugin.processor.convo.ir_index;
                    if (current_ir_id.HasValue()) {
                        if (IrMatches(*current_ir_id, loaded_ir))
                            SetConvolutionIr(plugin.processor, loaded_ir->audio_data);
                    }
                    break;
                }
            }
            break;
        }
        case Source::PartOfPendingStateChange: {
            result.Retain();
            dyn::Append(plugin.pending_state_change->retained_results, result);
            break;
        }
        case Source::LastInPendingStateChange: {
            result.Retain();
            dyn::Append(plugin.pending_state_change->retained_results, result);
            ApplyNewStateFromPending(plugin);
            break;
        }
        case Source::Count: PanicIfReached(); break;
    }

    plugin.processor.for_main_thread.flags.FetchOr(AudioProcessor::MainThreadCallbackFlagsUpdateGui);
}

// one-off load
Optional<u64> LoadConvolutionIr(PluginInstance& plugin, Optional<sample_lib::IrId> ir_id) {
    DebugAssertMainThread(plugin.host);
    plugin.processor.convo.ir_index = ir_id;

    if (ir_id)
        return SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                    plugin.sample_lib_server_async_channel,
                                    *ir_id);
    else
        SetConvolutionIr(plugin.processor, nullptr);
    return nullopt;
}

// one-off load
Optional<u64> LoadInstrument(PluginInstance& plugin, u32 layer_index, InstrumentId inst) {
    DebugLoc();
    DebugAssertMainThread(plugin.host);
    auto& layer = plugin.layers[layer_index];
    layer.instrument_id = inst;

    switch (inst.tag) {
        case InstrumentType::Sampler:
            return SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                        plugin.sample_lib_server_async_channel,
                                        sample_lib_server::LoadRequestInstrumentIdWithLayer {
                                            .id = inst.GetFromTag<InstrumentType::Sampler>(),
                                            .layer_index = layer_index,
                                        });
        case InstrumentType::None: {
            SetInstrument(plugin, layer_index, InstrumentType::None);
            break;
        }
        case InstrumentType::WaveformSynth:
            SetInstrument(plugin, layer_index, inst.Get<WaveformType>());
            break;
    }
    return nullopt;
}

void LoadRandomInstrument(PluginInstance& plugin,
                          u32 layer_index,
                          bool allow_none_to_be_selected,
                          bool disallow_previous_result,
                          sample_lib_server::LoadRequest* add_to_existing_batch) {
    // TODO(1.0)
    (void)plugin;
    (void)layer_index;
    (void)allow_none_to_be_selected;
    (void)disallow_previous_result;
    (void)add_to_existing_batch;
}

void CycleInstrument(PluginInstance& plugin, u32 layer_index, CycleDirection direction) {
    // TODO(1.0)
    (void)plugin;
    (void)layer_index;
    (void)direction;
}

void RandomiseAllLayerInsts(PluginInstance& plugin) {
    // TODO(1.0)
    (void)plugin;
}

void RunFunctionOnMainThread(PluginInstance& plugin, ThreadsafeFunctionQueue::Function function) {
    if (auto thread_check =
            (clap_host_thread_check const*)plugin.host.get_extension(&plugin.host, CLAP_EXT_THREAD_CHECK)) {
        if (thread_check->is_main_thread(&plugin.host)) {
            function();
            return;
        }
    }
    plugin.main_thread_callbacks.Push(function);
    plugin.host.request_callback(&plugin.host);
}

static void OnMainThread(PluginInstance& plugin, bool& update_gui) {
    (void)update_gui;
    // Clear any instruments that aren't used anymore. The audio thread will request this callback after it
    // swaps any instruments.
    if (plugin.lifetime_extended_insts.size) {
        bool all_layers_have_completed_swap = true;
        for (auto& l : plugin.processor.layer_processors) {
            if (!l.desired_inst.IsConsumed()) {
                all_layers_have_completed_swap = false;
                break;
            }
        }
        if (all_layers_have_completed_swap) {
            for (auto& i : plugin.lifetime_extended_insts)
                i.Release();
            dyn::Clear(plugin.lifetime_extended_insts);
        }
    }

    ArenaAllocatorWithInlineStorage<4000> scratch_arena {};
    while (auto f = plugin.main_thread_callbacks.TryPop(scratch_arena))
        (*f)();

    while (auto f = plugin.sample_lib_server_async_channel.results.TryPop()) {
        SampleLibraryResourceLoaded(plugin, *f);
        f->Release();
    }
}

void SetAllParametersToDefaultValues(PluginInstance& plugin) {
    DebugAssertMainThread(plugin.host);
    for (auto& p : plugin.processor.params)
        p.SetLinearValue(p.DefaultLinearValue());

    plugin.processor.events_for_audio_thread.Push(EventForAudioThreadType::ReloadAllAudioState);
    auto const host = &plugin.host;
    auto const params = (clap_host_params const*)host->get_extension(host, CLAP_EXT_PARAMS);
    if (params) params->rescan(host, CLAP_PARAM_RESCAN_VALUES);
    host->request_process(host);
}

static void ProcessorRandomiseAllParamsInternal(PluginInstance& plugin, bool only_effects) {
    // TODO(1.0): this should create a new StateSnapshot and apply it, rather than change params/insts
    // individually
    (void)plugin;
    (void)only_effects;

#if 0
    RandomIntGenerator<int> int_gen;
    RandomFloatGenerator<f32> float_gen;
    u64 seed = SeedFromTime();
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

void RandomiseAllEffectParameterValues(PluginInstance& plugin) {
    ProcessorRandomiseAllParamsInternal(plugin, true);
}
void RandomiseAllParameterValues(PluginInstance& plugin) {
    ProcessorRandomiseAllParamsInternal(plugin, false);
}

PluginInstance::PluginInstance(clap_host const& host, CrossInstanceSystems& shared_data)
    : shared_data(shared_data)
    , host(host)
    , processor(host)
    , sample_lib_server_async_channel(sample_lib_server::OpenAsyncCommsChannel(
          shared_data.sample_library_server,
          error_notifications,
          [&plugin = *this]() { plugin.host.request_callback(&plugin.host); })) {

    last_snapshot.state = CurrentStateSnapshot(*this);

    for (auto ccs = shared_data.settings.settings.midi.cc_to_param_mapping; ccs != nullptr; ccs = ccs->next)
        for (auto param = ccs->param; param != nullptr; param = param->next)
            processor.param_learned_ccs[ToInt(*ParamIdToIndex(param->id))].Set(ccs->cc_num);

    presets_folder_listener_id = shared_data.preset_listing.scanned_folder.listeners.Add([&plugin = *this]() {
        RunFunctionOnMainThread(plugin, [&plugin]() {
            auto listing = FetchOrRescanPresetsFolder(
                plugin.shared_data.preset_listing,
                RescanMode::DontRescan,
                plugin.shared_data.settings.settings.filesystem.extra_presets_scan_folders,
                nullptr);

            if (plugin.pending_preset_selection_criteria) {
                LoadPresetFromListing(plugin, *plugin.pending_preset_selection_criteria, listing);
                plugin.pending_preset_selection_criteria = {};
            }

            PresetListingChanged(plugin.preset_browser_filters,
                                 listing.listing ? &*listing.listing : nullptr);
        });
    });
}

PluginInstance::~PluginInstance() {
    in_destructor = true;
    shared_data.preset_listing.scanned_folder.listeners.Remove(presets_folder_listener_id);

    for (auto& i : lifetime_extended_insts)
        i.Release();

    sample_lib_server::CloseAsyncCommsChannel(shared_data.sample_library_server,
                                              sample_lib_server_async_channel);
}

usize MegabytesUsedBySamples(PluginInstance const& plugin) {
    usize result = 0;
    for (auto& l : plugin.layers) {
        if (auto i = l.instrument.TryGet<sample_lib_server::RefCounted<LoadedInstrument>>())
            for (auto& d : (*i)->audio_datas)
                result += d->RamUsageBytes();
    }

    return (result) / (1024 * 1024);
}

static bool PluginSaveState(PluginInstance& plugin, clap_ostream const& stream) {
    auto state = CurrentStateSnapshot(plugin);
    auto outcome = CodeState(state,
                             CodeStateOptions {
                                 .mode = CodeStateOptions::Mode::Encode,
                                 .read_or_write_data = [&](void* data, usize bytes) -> ErrorCodeOr<void> {
                                     u64 bytes_written = 0;
                                     while (bytes_written != bytes) {
                                         ASSERT(bytes_written < bytes);
                                         auto const n = stream.write(&stream,
                                                                     (u8 const*)data + bytes_written,
                                                                     bytes - bytes_written);
                                         if (n < 0) return ErrorCode(CommonError::PluginHostError);
                                         bytes_written += (u64)n;
                                     }
                                     return k_success;
                                 },
                                 .source = StateSource::Daw,
                                 .abbreviated_read = false,
                             });

    if (outcome.HasError()) {
        auto item = plugin.error_notifications.NewError();
        item->value = {
            .title = "Failed to save state for DAW"_s,
            .message = {},
            .error_code = outcome.Error(),
            .id = U64FromChars("daw save"),
        };
        plugin.error_notifications.AddOrUpdateError(item);
        return false;
    }
    return true;
}

static bool PluginLoadState(PluginInstance& plugin, clap_istream const& stream) {
    StateSnapshot state {};
    auto outcome =
        CodeState(state,
                  CodeStateOptions {
                      .mode = CodeStateOptions::Mode::Decode,
                      .read_or_write_data = [&](void* data, usize bytes) -> ErrorCodeOr<void> {
                          u64 bytes_read = 0;
                          while (bytes_read != bytes) {
                              ASSERT(bytes_read < bytes);
                              auto const n = stream.read(&stream, (u8*)data + bytes_read, bytes - bytes_read);
                              if (n == 0)
                                  return ErrorCode(CommonError::FileFormatIsInvalid); // unexpected EOF
                              if (n < 0) return ErrorCode(CommonError::PluginHostError);
                              bytes_read += (u64)n;
                          }
                          return k_success;
                      },
                      .source = StateSource::Daw,
                      .abbreviated_read = false,
                  });

    if (outcome.HasError()) {
        auto item = plugin.error_notifications.NewError();
        item->value = {
            .title = "Failed to load DAW state"_s,
            .message = {},
            .error_code = outcome.Error(),
            .id = U64FromChars("daw load"),
        };
        plugin.error_notifications.AddOrUpdateError(item);
        return false;
    }

    LoadNewState(plugin, {.state = state, .metadata = {.name_or_path = "DAW State"}}, StateSource::Daw);
    return true;
}

PluginCallbacks<PluginInstance> PluginInstanceCallbacks() {
    PluginCallbacks<PluginInstance> result {
        .on_main_thread = OnMainThread,
        .save_state = PluginSaveState,
        .load_state = PluginLoadState,
    };
    return result;
}
