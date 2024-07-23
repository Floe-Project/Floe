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
        result.insts[i] = plugin.layers[i].desired_instrument;

    result.ir_index = plugin.processor.convo.ir_index;
    for (auto const i : Range(k_num_parameters))
        result.param_values[i] = plugin.processor.params[i].LinearValue();

    for (auto [i, cc] : Enumerate(plugin.processor.param_learned_ccs))
        result.param_learned_ccs[i] = cc.GetBlockwise();

    return result;
}

static void SetDesiredInstrument(PluginInstance& plugin,
                                 u32 layer_index,
                                 PluginInstance::Layer::Instrument const& instrument,
                                 bool notify_audio_thread) {
    ZoneScoped;
    DebugAssertMainThread(plugin.host);

    using namespace sample_lib_server;

    // We keep the instrument alive by putting it in this storage and cleaning up at a later time.
    if (auto current = plugin.layers[layer_index].instrument.TryGet<RefCounted<LoadedInstrument>>())
        dyn::Append(plugin.lifetime_extended_insts, *current);

    plugin.layers[layer_index].instrument = instrument;

    switch (instrument.tag) {
        case InstrumentType::Sampler: {
            auto& sampler_inst = instrument.Get<RefCounted<LoadedInstrument>>();
            sampler_inst.Retain();
            DebugLn("Setting desired instrument for layer {} to: {}",
                    layer_index,
                    sampler_inst->instrument.name);
            plugin.processor.layer_processors[layer_index].desired_inst.Set(&*sampler_inst);
            break;
        }
        case InstrumentType::WaveformSynth: {
            auto& w = instrument.Get<WaveformType>();
            DebugLn("Setting desired instrument for layer {} to: {}",
                    layer_index,
                    k_waveform_type_names[ToInt(w)]);
            plugin.processor.layer_processors[layer_index].desired_inst.Set(w);
            break;
        }
        case InstrumentType::None: {
            plugin.processor.layer_processors[layer_index].desired_inst.SetNone();
            break;
        }
    }

    if (notify_audio_thread) {
        plugin.processor.events_for_audio_thread.Push(LayerInstrumentChanged {.layer_index = layer_index});
        plugin.host.request_process(&plugin.host);
    }
}

static void
SetDesiredConvolutionIr(PluginInstance& plugin, AudioData const* audio_data, bool notify_audio_thread) {
    DebugAssertMainThread(plugin.host);
    plugin.processor.convo.ConvolutionIrDataLoaded(audio_data);
    if (notify_audio_thread) {
        plugin.processor.events_for_audio_thread.Push(EventForAudioThreadType::ConvolutionIRChanged);
        plugin.host.request_process(&plugin.host);
    }
}

static void
ApplyNewStateExceptAsync(PluginInstance& plugin, StateSnapshotWithMetadata const& state, StateSource source) {
    ZoneScoped;
    DebugAssertMainThread(plugin.host);

    DEFER {
        // do this at the end because the pending state could be the arg of this function
        plugin.pending_state_change.Clear();
    };

    plugin.last_snapshot.state = state.state;
    plugin.last_snapshot_metadata_arena.ResetCursorAndConsolidateRegions();
    plugin.last_snapshot.metadata = state.metadata.Clone(plugin.last_snapshot_metadata_arena);

    if (source == StateSource::Daw)
        for (auto [i, cc] : Enumerate(plugin.processor.param_learned_ccs))
            cc.AssignBlockwise(plugin.last_snapshot.state.param_learned_ccs[i]);

    for (auto const i : Range(k_num_parameters))
        plugin.processor.params[i].SetLinearValue(state.state.param_values[i]);

    plugin.processor.desired_effects_order.Store(EncodeEffectsArray(state.state.fx_order));
    plugin.processor.engine_version.Store(state.state.engine_version);

    // reload everything
    {
        if (auto const host_params =
                (clap_host_params const*)plugin.host.get_extension(&plugin.host, CLAP_EXT_PARAMS))
            host_params->rescan(&plugin.host, CLAP_PARAM_RESCAN_VALUES);
        plugin.processor.events_for_audio_thread.Push(EventForAudioThreadType::ReloadAllAudioState);
        plugin.host.request_process(&plugin.host);
    }

    // redraw the gui
    {
        plugin.gui_needs_to_handle_preset_name_change = true;
        plugin.processor.for_main_thread.flags.FetchOr(AudioProcessor::MainThreadCallbackFlagsUpdateGui);
        plugin.host.request_callback(&plugin.host);
    }
}

void ApplyNewState(PluginInstance& plugin, StateSnapshotWithMetadata const& state, StateSource source) {
    ZoneScoped;
    DebugAssertMainThread(plugin.host);

    auto const async = ({
        bool a = false;
        for (auto const& i : state.state.insts) {
            if (i.tag == InstrumentType::Sampler) {
                a = true;
                break;
            }
        }
        if (state.state.ir_index) a = true;
        a;
    });
    DebugLn("{} async: {}", __func__, async);

    if (!async) {
        // async stuff (but not async here)
        for (auto [layer_index, i] : Enumerate<u32>(plugin.last_snapshot.state.insts)) {
            plugin.layers[layer_index].desired_instrument = i;
            switch (i.tag) {
                case InstrumentType::None:
                    SetDesiredInstrument(plugin, layer_index, i.GetFromTag<InstrumentType::None>(), false);
                    break;
                case InstrumentType::WaveformSynth:
                    SetDesiredInstrument(plugin,
                                         layer_index,
                                         i.GetFromTag<InstrumentType::WaveformSynth>(),
                                         false);
                    break;
                case InstrumentType::Sampler: PanicIfReached(); break;
            }
        }
        ASSERT(!state.state.ir_index.HasValue());
        plugin.processor.convo.ir_index = nullopt;
        SetDesiredConvolutionIr(plugin, nullptr, false);

        // the rest
        ApplyNewStateExceptAsync(plugin, state, source);
    } else {
        plugin.pending_state_change.Emplace();
        auto& pending = *plugin.pending_state_change;
        pending.snapshot.state = state.state;
        pending.snapshot.metadata = state.metadata.Clone(pending.arena);
        pending.source = source;

        for (auto [layer_index, i] : Enumerate<u32>(state.state.insts)) {
            if (i.tag != InstrumentType::Sampler) continue;

            plugin.layers[layer_index].desired_instrument = i;
            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                                        plugin.sample_lib_server_async_channel,
                                                        sample_lib_server::LoadRequestInstrumentIdWithLayer {
                                                            .id = i.Get<sample_lib::InstrumentId>(),
                                                            .layer_index = layer_index,
                                                        });
            dyn::Append(pending.requests, {async_id, layer_index});
        }

        if (state.state.ir_index) {
            plugin.processor.convo.ir_index = state.state.ir_index;
            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                                        plugin.sample_lib_server_async_channel,
                                                        *state.state.ir_index);
            dyn::Append(pending.requests, {async_id, nullopt});
        }
    }
}

static void ApplyNewStateFromPending(PluginInstance& plugin) {
    ZoneScoped;
    DebugAssertMainThread(plugin.host);

    DebugLoc();

    for (auto const& r : plugin.pending_state_change->retained_results) {
        switch (r.result.result.tag) {
            case sample_lib_server::LoadResult::ResultType::Success: {
                auto const& resource = r.result.result.Get<sample_lib_server::Resource>();
                switch (resource.tag) {
                    case sample_lib_server::LoadRequestType::Instrument: {
                        auto& loaded_inst = resource.Get<sample_lib_server::RefCounted<LoadedInstrument>>();
                        SetDesiredInstrument(plugin, *r.layer_index, loaded_inst, false);
                        break;
                    }
                    case sample_lib_server::LoadRequestType::Ir: {
                        auto audio_data = resource.Get<sample_lib_server::RefCounted<AudioData>>();
                        SetDesiredConvolutionIr(plugin, &*audio_data, false);
                        break;
                    }
                }

                break;
            }
            case sample_lib_server::LoadResult::ResultType::Error:
            case sample_lib_server::LoadResult::ResultType::Cancelled: break;
        }
    }

    ApplyNewStateExceptAsync(plugin,
                             plugin.pending_state_change->snapshot,
                             plugin.pending_state_change->source);
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
        for (auto const& i : state_outcome.Value().insts) {
            switch (i.tag) {
                case InstrumentType::Sampler: {
                    auto const& inst = i.GetFromTag<InstrumentType::Sampler>();
                    DBG_PRINT_EXPR(inst.inst_name);
                    break;
                }
                case InstrumentType::WaveformSynth: {
                    auto const& wf = i.GetFromTag<InstrumentType::WaveformSynth>();
                    DBG_PRINT_EXPR(wf);
                    break;
                }
                case InstrumentType::None: {
                    break;
                }
            }
        }

        ApplyNewState(plugin,
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

    Optional<u32> request_layer_index {};
    auto const source = ({
        Source s {Source::OneOff};
        if (plugin.pending_state_change) {
            if (auto opt_index = FindIf(plugin.pending_state_change->requests,
                                        [&](PluginInstance::PendingStateChange::Request const& r) {
                                            return r.id == result.id;
                                        })) {
                s = Source::PartOfPendingStateChange;
                request_layer_index = plugin.pending_state_change->requests[*opt_index].layer_index;
                dyn::Remove(plugin.pending_state_change->requests, *opt_index);
                if (plugin.pending_state_change->requests.size == 0) s = Source::LastInPendingStateChange;
            }
        }
        s;
    });

    DBG_PRINT_EXPR(source);

    switch (source) {
        case Source::OneOff: {
            if (result.result.tag == sample_lib_server::LoadResult::ResultType::Success) {
                auto resource = result.result.Get<sample_lib_server::Resource>();
                switch (resource.tag) {
                    case sample_lib_server::LoadRequestType::Instrument: {
                        auto loaded_inst = resource.Get<sample_lib_server::RefCounted<LoadedInstrument>>();
                        for (auto [layer_index, l] : Enumerate<u32>(plugin.layers)) {
                            if (auto i = l.desired_instrument.TryGet<sample_lib::InstrumentId>()) {
                                if (i->library_name == loaded_inst->instrument.library.name &&
                                    i->inst_name == loaded_inst->instrument.name) {
                                    SetDesiredInstrument(plugin, layer_index, loaded_inst, true);
                                }
                            }
                        }
                        break;
                    }
                    case sample_lib_server::LoadRequestType::Ir: {
                        auto audio_data = resource.Get<sample_lib_server::RefCounted<AudioData>>();
                        SetDesiredConvolutionIr(plugin, &*audio_data, true);
                        break;
                    }
                }
            }
            break;
        }
        case Source::PartOfPendingStateChange: {
            result.Retain();
            dyn::Append(plugin.pending_state_change->retained_results, {result, request_layer_index});
            break;
        }
        case Source::LastInPendingStateChange: {
            result.Retain();
            dyn::Append(plugin.pending_state_change->retained_results, {result, request_layer_index});
            ApplyNewStateFromPending(plugin);
            break;
        }
        case Source::Count: PanicIfReached(); break;
    }

    plugin.processor.for_main_thread.flags.FetchOr(AudioProcessor::MainThreadCallbackFlagsUpdateGui);
}

Optional<u64> SetConvolutionIr(PluginInstance& plugin, Optional<sample_lib::IrId> ir_id) {
    DebugAssertMainThread(plugin.host);
    plugin.processor.convo.ir_index = ir_id;

    if (ir_id)
        return SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                    plugin.sample_lib_server_async_channel,
                                    *ir_id);
    else
        SetDesiredConvolutionIr(plugin, nullptr, true);
    return nullopt;
}

Optional<u64> SetInstrument(PluginInstance& plugin, u32 layer_index, InstrumentId inst) {
    DebugLoc();
    DebugAssertMainThread(plugin.host);
    auto& layer = plugin.layers[layer_index];
    layer.desired_instrument = inst;

    if (auto sample_inst = inst.TryGet<sample_lib::InstrumentId>())
        return SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                    plugin.sample_lib_server_async_channel,
                                    sample_lib_server::LoadRequestInstrumentIdWithLayer {
                                        .id = *sample_inst,
                                        .layer_index = layer_index,
                                    });
    else {
        switch (inst.tag) {
            case InstrumentType::Sampler: PanicIfReached(); break;
            case InstrumentType::None: {
                SetDesiredInstrument(plugin, layer_index, InstrumentType::None, true);
                break;
            }
            case InstrumentType::WaveformSynth:
                SetDesiredInstrument(plugin, layer_index, inst.Get<WaveformType>(), true);
                break;
        }
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

    ApplyNewState(plugin, {.state = state, .metadata = {.name_or_path = "DAW State"}}, StateSource::Daw);
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
