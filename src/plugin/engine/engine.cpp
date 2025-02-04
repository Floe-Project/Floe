// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.hpp"

#include <clap/ext/params.h>
#include <csignal>

#include "foundation/foundation.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/sample_library/attribution_requirements.hpp"

#include "clap/ext/timer-support.h"
#include "descriptors/param_descriptors.hpp"
#include "plugin/plugin.hpp"
#include "processor/layer_processor.hpp"
#include "sample_lib_server/sample_library_server.hpp"
#include "settings/settings.hpp"
#include "shared_engine_systems.hpp"
#include "state/instrument.hpp"
#include "state/state_coding.hpp"
#include "state/state_snapshot.hpp"

Optional<sample_lib::LibraryIdRef> LibraryForOverallBackground(Engine const& engine) {
    ASSERT(IsMainThread(engine.host));

    Array<Optional<sample_lib::LibraryIdRef>, k_num_layers> lib_ids {};
    for (auto [layer_index, l] : Enumerate<u32>(engine.processor.layer_processors))
        lib_ids[layer_index] = engine.processor.layer_processors[layer_index].LibId();

    Optional<sample_lib::LibraryIdRef> first_lib_id {};
    for (auto const& lib_id : lib_ids) {
        if (!lib_id) continue;
        if (!first_lib_id) {
            first_lib_id = *lib_id;
            break;
        }
    }

    if (!first_lib_id) return k_default_background_lib_id;

    for (auto const& lib_id : lib_ids) {
        if (!lib_id) continue;
        if (*lib_id != *first_lib_id) return k_default_background_lib_id;
    }

    return *first_lib_id;
}

static void UpdateAttributionText(Engine& engine, ArenaAllocator& scratch_arena) {
    ASSERT(IsMainThread(engine.host));

    DynamicArrayBounded<sample_lib::Instrument const*, k_num_layers> insts {};
    for (auto& l : engine.processor.layer_processors)
        if (auto opt_i = l.instrument.TryGet<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>())
            dyn::Append(insts, &(*opt_i)->instrument);

    sample_lib::ImpulseResponse const* ir = nullptr;
    sample_lib_server::RefCounted<sample_lib::Library> ir_lib {};
    DEFER { ir_lib.Release(); }; // IMPORTANT: release before we return
    if (engine.processor.params[(usize)ParamIndex::ConvolutionReverbOn].ValueAsBool()) {
        if (auto const ir_id = engine.processor.convo.ir_id) {
            ir_lib =
                sample_lib_server::FindLibraryRetained(engine.shared_engine_systems.sample_library_server,
                                                       ir_id->library);
            if (ir_lib) {
                if (auto const found_ir = ir_lib->irs_by_name.Find(ir_id->ir_name)) ir = *found_ir;
            }
        }
    }

    UpdateAttributionText(engine.attribution_requirements, scratch_arena, insts, ir);

    // TODO: if the attributions have changed, we should update the GUI
}

static void SetLastSnapshot(Engine& engine, StateSnapshotWithMetadata const& state) {
    engine.last_snapshot.Set(state);
    engine.update_gui.Store(true, StoreMemoryOrder::Relaxed);
    engine.host.request_callback(&engine.host);
    // do this at the end because the pending state could be the arg of this function
    engine.pending_state_change.Clear();
}

static void LoadNewState(Engine& engine, StateSnapshotWithMetadata const& state, StateSource source) {
    ZoneScoped;
    ASSERT(IsMainThread(engine.host));

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
        for (auto [layer_index, i] : Enumerate<u32>(state.state.inst_ids)) {
            engine.processor.layer_processors[layer_index].instrument_id = i;
            switch (i.tag) {
                case InstrumentType::None:
                    SetInstrument(engine.processor, layer_index, InstrumentType::None);
                    break;
                case InstrumentType::WaveformSynth:
                    SetInstrument(engine.processor,
                                  layer_index,
                                  i.GetFromTag<InstrumentType::WaveformSynth>());
                    break;
                case InstrumentType::Sampler: PanicIfReached(); break;
            }
        }

        ASSERT(!state.state.ir_id.HasValue());
        engine.processor.convo.ir_id = k_nullopt;
        SetConvolutionIrAudioData(engine.processor, nullptr);

        ApplyNewState(engine.processor, state.state, source);
        SetLastSnapshot(engine, state);

        MarkNeedsAttributionTextUpdate(engine.attribution_requirements);
        engine.host.request_callback(&engine.host);
    } else {
        engine.pending_state_change.Emplace();
        auto& pending = *engine.pending_state_change;
        pending.snapshot.state = state.state;
        pending.snapshot.metadata = state.metadata.Clone(pending.arena);
        pending.source = source;

        for (auto [layer_index, i] : Enumerate<u32>(state.state.inst_ids)) {
            engine.processor.layer_processors[layer_index].instrument_id = i;

            if (i.tag != InstrumentType::Sampler) continue;

            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(engine.shared_engine_systems.sample_library_server,
                                                        engine.sample_lib_server_async_channel,
                                                        sample_lib_server::LoadRequestInstrumentIdWithLayer {
                                                            .id = i.Get<sample_lib::InstrumentId>(),
                                                            .layer_index = layer_index,
                                                        });
            dyn::Append(pending.requests, async_id);
        }

        engine.processor.convo.ir_id = state.state.ir_id;
        if (state.state.ir_id) {
            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(engine.shared_engine_systems.sample_library_server,
                                                        engine.sample_lib_server_async_channel,
                                                        *state.state.ir_id);
            dyn::Append(pending.requests, async_id);
        }
    }
}

static Instrument InstrumentFromPendingState(Engine::PendingStateChange const& pending_state_change,
                                             u32 layer_index) {
    auto const inst_id = pending_state_change.snapshot.state.inst_ids[layer_index];

    Instrument instrument = InstrumentType::None;
    switch (inst_id.tag) {
        case InstrumentType::None: break;
        case InstrumentType::WaveformSynth: {
            instrument = inst_id.GetFromTag<InstrumentType::WaveformSynth>();
            break;
        }
        case InstrumentType::Sampler: {
            for (auto const& r : pending_state_change.retained_results) {
                auto const loaded_inst =
                    r.TryExtract<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>();

                if (loaded_inst && inst_id.GetFromTag<InstrumentType::Sampler>() == **loaded_inst)
                    instrument = *loaded_inst;
            }
            break;
        }
    }
    return instrument;
}

static AudioData const* IrAudioDataFromPendingState(Engine::PendingStateChange const& pending_state_change) {
    auto const ir_id = pending_state_change.snapshot.state.ir_id;
    if (!ir_id) return nullptr;
    for (auto const& r : pending_state_change.retained_results) {
        auto const loaded_ir = r.TryExtract<sample_lib_server::RefCounted<sample_lib::LoadedIr>>();
        if (loaded_ir && *ir_id == **loaded_ir) return (*loaded_ir)->audio_data;
    }
    return nullptr;
}

static void ApplyNewStateFromPending(Engine& engine) {
    ZoneScoped;
    ASSERT(IsMainThread(engine.host));

    auto const& pending_state_change = *engine.pending_state_change;

    for (auto const layer_index : Range(k_num_layers))
        SetInstrument(engine.processor,
                      layer_index,
                      InstrumentFromPendingState(pending_state_change, layer_index));
    SetConvolutionIrAudioData(engine.processor, IrAudioDataFromPendingState(pending_state_change));
    ApplyNewState(engine.processor, pending_state_change.snapshot.state, pending_state_change.source);

    // do it last because it clears pending_state_change
    SetLastSnapshot(engine, pending_state_change.snapshot);
}

static void SampleLibraryChanged(Engine& engine, sample_lib::LibraryIdRef library_id) {
    ZoneScoped;
    ASSERT(IsMainThread(engine.host));

    auto const current_ir_id = engine.processor.convo.ir_id;
    if (current_ir_id.HasValue()) {
        if (current_ir_id->library == library_id) LoadConvolutionIr(engine, *current_ir_id);
    }

    for (auto [layer_index, l] : Enumerate<u32>(engine.processor.layer_processors)) {
        if (auto const i = l.instrument_id.TryGet<sample_lib::InstrumentId>()) {
            if (i->library == library_id) LoadInstrument(engine, layer_index, *i);
        }
    }
}

static void SampleLibraryResourceLoaded(Engine& engine, sample_lib_server::LoadResult result) {
    ZoneScoped;
    ASSERT(IsMainThread(engine.host));

    enum class Source : u32 { OneOff, PartOfPendingStateChange, LastInPendingStateChange, Count };

    auto const source = ({
        Source s {Source::OneOff};
        if (engine.pending_state_change) {
            auto& requests = engine.pending_state_change->requests;
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

    switch (source) {
        case Source::OneOff: {
            if (result.result.tag != sample_lib_server::LoadResult::ResultType::Success) break;

            auto const resource = result.result.Get<sample_lib_server::Resource>();
            switch (resource.tag) {
                case sample_lib_server::LoadRequestType::Instrument: {
                    auto const loaded_inst =
                        resource.Get<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>();

                    for (auto [layer_index, l] : Enumerate<u32>(engine.processor.layer_processors)) {
                        if (auto const i = l.instrument_id.TryGet<sample_lib::InstrumentId>()) {
                            if (*i == *loaded_inst) SetInstrument(engine.processor, layer_index, loaded_inst);
                        }
                    }
                    break;
                }
                case sample_lib_server::LoadRequestType::Ir: {
                    auto const loaded_ir =
                        resource.Get<sample_lib_server::RefCounted<sample_lib::LoadedIr>>();

                    auto const current_ir_id = engine.processor.convo.ir_id;
                    if (current_ir_id.HasValue()) {
                        if (*current_ir_id == *loaded_ir)
                            SetConvolutionIrAudioData(engine.processor, loaded_ir->audio_data);
                    }
                    break;
                }
            }
            break;
        }
        case Source::PartOfPendingStateChange: {
            result.Retain();
            dyn::Append(engine.pending_state_change->retained_results, result);
            break;
        }
        case Source::LastInPendingStateChange: {
            result.Retain();
            dyn::Append(engine.pending_state_change->retained_results, result);
            ApplyNewStateFromPending(engine);
            break;
        }
        case Source::Count: PanicIfReached(); break;
    }

    engine.update_gui.Store(true, StoreMemoryOrder::Relaxed);
    engine.host.request_callback(&engine.host);
}

StateSnapshot CurrentStateSnapshot(Engine const& engine) {
    if (engine.pending_state_change) return engine.pending_state_change->snapshot.state;
    return MakeStateSnapshot(engine.processor);
}

[[maybe_unused]] auto PrintInstrumentId(InstrumentId id) {
    DynamicArrayBounded<char, 100> result {};
    switch (id.tag) {
        case InstrumentType::None: fmt::Append(result, "None"_s); break;
        case InstrumentType::WaveformSynth:
            fmt::Append(result, "WaveformSynth: {}"_s, id.Get<WaveformType>());
            break;
        case InstrumentType::Sampler:
            fmt::Append(result,
                        "Sampler: {}/{}/{}"_s,
                        id.Get<sample_lib::InstrumentId>().library.author,
                        id.Get<sample_lib::InstrumentId>().library.name,
                        id.Get<sample_lib::InstrumentId>().inst_name);
            break;
    }
    return result;
}

[[maybe_unused]] void AssignDiffDescription(dyn::DynArray auto& diff_desc,
                                            StateSnapshot const& old_state,
                                            StateSnapshot const& new_state) {
    dyn::Clear(diff_desc);

    if (old_state.ir_id != new_state.ir_id) {
        fmt::Append(diff_desc,
                    "IR changed, old: {}:{} vs new: {}:{}\n"_s,
                    old_state.ir_id.HasValue() ? old_state.ir_id.Value().library.name.Items() : "null"_s,
                    old_state.ir_id.HasValue() ? old_state.ir_id.Value().ir_name.Items() : "null"_s,
                    new_state.ir_id.HasValue() ? new_state.ir_id.Value().library.name.Items() : "null"_s,
                    new_state.ir_id.HasValue() ? new_state.ir_id.Value().ir_name.Items() : "null"_s);
    }

    for (auto layer_index : Range(k_num_layers)) {
        if (old_state.inst_ids[layer_index] != new_state.inst_ids[layer_index]) {
            fmt::Append(diff_desc,
                        "Layer {}: {} vs {}\n"_s,
                        layer_index,
                        PrintInstrumentId(old_state.inst_ids[layer_index]),
                        PrintInstrumentId(new_state.inst_ids[layer_index]));
        }
    }

    for (auto param_index : Range(k_num_parameters)) {
        if (old_state.param_values[param_index] != new_state.param_values[param_index]) {
            fmt::Append(diff_desc,
                        "Param {}: {} vs {}\n"_s,
                        k_param_descriptors[param_index].name,
                        old_state.param_values[param_index],
                        new_state.param_values[param_index]);
        }
    }

    if (old_state.fx_order != new_state.fx_order) fmt::Append(diff_desc, "FX order changed\n"_s);
}

bool StateChangedSinceLastSnapshot(Engine& engine) {
    auto current = CurrentStateSnapshot(engine);
    // we don't check the params ccs for changes
    current.param_learned_ccs = engine.last_snapshot.state.param_learned_ccs;
    bool const changed = engine.last_snapshot.state != current;

    if constexpr (!PRODUCTION_BUILD) {
        if (changed)
            AssignDiffDescription(engine.state_change_description, engine.last_snapshot.state, current);
        else
            dyn::Clear(engine.state_change_description);
    }

    return changed;
}

// one-off load
void LoadConvolutionIr(Engine& engine, Optional<sample_lib::IrId> ir_id) {
    ASSERT(IsMainThread(engine.host));
    engine.processor.convo.ir_id = ir_id;

    if (ir_id)
        SendAsyncLoadRequest(engine.shared_engine_systems.sample_library_server,
                             engine.sample_lib_server_async_channel,
                             *ir_id);
    else {
        MarkNeedsAttributionTextUpdate(engine.attribution_requirements);
        engine.host.request_callback(&engine.host);
        SetConvolutionIrAudioData(engine.processor, nullptr);
    }
}

// one-off load
void LoadInstrument(Engine& engine, u32 layer_index, InstrumentId inst_id) {
    ASSERT(IsMainThread(engine.host));
    engine.processor.layer_processors[layer_index].instrument_id = inst_id;

    switch (inst_id.tag) {
        case InstrumentType::Sampler:
            SendAsyncLoadRequest(engine.shared_engine_systems.sample_library_server,
                                 engine.sample_lib_server_async_channel,
                                 sample_lib_server::LoadRequestInstrumentIdWithLayer {
                                     .id = inst_id.GetFromTag<InstrumentType::Sampler>(),
                                     .layer_index = layer_index,
                                 });
            break;
        case InstrumentType::None: {
            MarkNeedsAttributionTextUpdate(engine.attribution_requirements);
            SetInstrument(engine.processor, layer_index, InstrumentType::None);
            break;
        }
        case InstrumentType::WaveformSynth:
            MarkNeedsAttributionTextUpdate(engine.attribution_requirements);
            SetInstrument(engine.processor, layer_index, inst_id.Get<WaveformType>());
            break;
    }
}

void LoadPresetFromListing(Engine& engine,
                           PresetSelectionCriteria const& selection_criteria,
                           PresetsFolderScanResult const& listing) {
    if (listing.is_loading) {
        engine.pending_preset_selection_criteria = selection_criteria;
    } else if (listing.listing) {
        if (auto entry = SelectPresetFromListing(*listing.listing,
                                                 selection_criteria,
                                                 engine.last_snapshot.metadata.Path(),
                                                 engine.random_seed)) {
            LoadPresetFromFile(engine, entry->Path());
        }
    }
}

void LoadPresetFromFile(Engine& engine, String path) {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator, Kb(16)};
    auto state_outcome = LoadPresetFile(path, scratch_arena);

    if (state_outcome.HasValue()) {
        LoadNewState(engine,
                     {
                         .state = state_outcome.Value(),
                         .metadata = {.name_or_path = path},
                     },
                     StateSource::PresetFile);
    } else {
        auto item = engine.error_notifications.NewError();
        item->value = {
            .title = "Failed to load preset"_s,
            .message = path,
            .error_code = state_outcome.Error(),
            .id = U64FromChars("statload"),
        };
        engine.error_notifications.AddOrUpdateError(item);
    }
}

void SaveCurrentStateToFile(Engine& engine, String path) {
    if (auto outcome = SavePresetFile(path, CurrentStateSnapshot(engine)); outcome.Succeeded()) {
        engine.last_snapshot.SetMetadata(StateSnapshotMetadata {.name_or_path = path});
    } else {
        auto item = engine.error_notifications.NewError();
        item->value = {
            .title = "Failed to save preset"_s,
            .message = path,
            .error_code = outcome.Error(),
            .id = U64FromChars("statsave"),
        };
        engine.error_notifications.AddOrUpdateError(item);
    }
}

void LoadRandomInstrument(Engine& engine,
                          u32 layer_index,
                          bool allow_none_to_be_selected,
                          bool disallow_previous_result,
                          sample_lib_server::LoadRequest* add_to_existing_batch) {
    // TODO
    (void)engine;
    (void)layer_index;
    (void)allow_none_to_be_selected;
    (void)disallow_previous_result;
    (void)add_to_existing_batch;
}

void CycleInstrument(Engine& engine, u32 layer_index, CycleDirection direction) {
    // TODO
    (void)engine;
    (void)layer_index;
    (void)direction;
}

void RandomiseAllLayerInsts(Engine& engine) {
    // TODO
    (void)engine;
}

void RunFunctionOnMainThread(Engine& engine, ThreadsafeFunctionQueue::Function function) {
    if (auto thread_check =
            (clap_host_thread_check const*)engine.host.get_extension(&engine.host, CLAP_EXT_THREAD_CHECK)) {
        if (thread_check->is_main_thread(&engine.host)) {
            function();
            return;
        }
    }
    engine.main_thread_callbacks.Push(function);
    engine.host.request_callback(&engine.host);
}

static void OnMainThread(Engine& engine) {
    ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};
    while (auto f = engine.main_thread_callbacks.TryPop(scratch_arena))
        (*f)();

    while (auto r = engine.sample_lib_server_async_channel.results.TryPop()) {
        SampleLibraryResourceLoaded(engine, *r);
        r->Release();
        MarkNeedsAttributionTextUpdate(engine.attribution_requirements);
    }
    if (AttributionTextNeedsUpdate(engine.attribution_requirements))
        UpdateAttributionText(engine, scratch_arena);

    if (engine.update_gui.Exchange(false, RmwMemoryOrder::Relaxed))
        engine.plugin_instance_messages.UpdateGui();
}

void Engine::OnProcessorChange(ChangeFlags flags) {
    if (flags & ProcessorListener::IrChanged) MarkNeedsAttributionTextUpdate(attribution_requirements);
    update_gui.Store(true, StoreMemoryOrder::Relaxed);
    host.request_callback(&host);
}

Engine::Engine(clap_host const& host,
               SharedEngineSystems& shared_engine_systems,
               PluginInstanceMessages& plugin_instance_messages)
    : host(host)
    , shared_engine_systems(shared_engine_systems)
    , plugin_instance_messages(plugin_instance_messages)
    , sample_lib_server_async_channel {sample_lib_server::OpenAsyncCommsChannel(
          shared_engine_systems.sample_library_server,
          {
              .error_notifications = error_notifications,
              .result_added_callback = [&engine = *this]() { engine.host.request_callback(&engine.host); },
              .library_changed_callback =
                  [&engine = *this](sample_lib::LibraryIdRef lib_id_ref) {
                      sample_lib::LibraryId lib_id = lib_id_ref;
                      engine.main_thread_callbacks.Push(
                          [lib_id, &engine]() { SampleLibraryChanged(engine, lib_id); });
                  },
          })} {

    last_snapshot.state = CurrentStateSnapshot(*this);

    for (auto ccs = shared_engine_systems.settings.settings.midi.cc_to_param_mapping; ccs != nullptr;
         ccs = ccs->next)
        for (auto param = ccs->param; param != nullptr; param = param->next)
            processor.param_learned_ccs[ToInt(*ParamIdToIndex(param->id))].Set(ccs->cc_num);

    presets_folder_listener_id =
        shared_engine_systems.preset_listing.scanned_folder.listeners.Add([&engine = *this]() {
            RunFunctionOnMainThread(engine, [&engine]() {
                auto listing =
                    FetchOrRescanPresetsFolder(engine.shared_engine_systems.preset_listing,
                                               RescanMode::DontRescan,
                                               engine.shared_engine_systems.settings.settings.filesystem
                                                   .extra_scan_folders[ToInt(ScanFolderType::Presets)],
                                               nullptr);

                if (engine.pending_preset_selection_criteria) {
                    LoadPresetFromListing(engine, *engine.pending_preset_selection_criteria, listing);
                    engine.pending_preset_selection_criteria = {};
                }

                PresetListingChanged(engine.preset_browser_filters,
                                     listing.listing ? &*listing.listing : nullptr);
            });
        });

    {
        if (auto const timer_support =
                (clap_host_timer_support const*)host.get_extension(&host, CLAP_EXT_TIMER_SUPPORT);
            timer_support && timer_support->register_timer) {
            clap_id timer_id;
            if (timer_support->register_timer(&host, 1000, &timer_id)) attributions_poll_timer_id = timer_id;
        }
        if (!attributions_poll_timer_id) shared_engine_systems.StartPollingThreadIfNeeded();
    }
}

Engine::~Engine() {
    ArenaAllocatorWithInlineStorage<1000> scratch_arena {PageAllocator::Instance()};
    DeinitAttributionRequirements(attribution_requirements, scratch_arena);
    package::ShutdownJobs(package_install_jobs);
    shared_engine_systems.preset_listing.scanned_folder.listeners.Remove(presets_folder_listener_id);

    sample_lib_server::CloseAsyncCommsChannel(shared_engine_systems.sample_library_server,
                                              sample_lib_server_async_channel);

    if (attributions_poll_timer_id) {
        auto const timer_support =
            (clap_host_timer_support const*)host.get_extension(&host, CLAP_EXT_TIMER_SUPPORT);
        if (timer_support && timer_support->unregister_timer)
            timer_support->unregister_timer(&host, *attributions_poll_timer_id);
    }
}

static void PluginOnTimer(Engine& engine, clap_id timer_id) {
    ASSERT(IsMainThread(engine.host));
    if (timer_id == *engine.attributions_poll_timer_id) {
        if (AttributionTextNeedsUpdate(engine.attribution_requirements))
            UpdateAttributionText(engine, engine.error_arena);
    }
}

static void PluginOnPollThread(Engine& engine) {
    // we want to poll for attribution text updates
    engine.host.request_callback(&engine.host);
}

usize MegabytesUsedBySamples(Engine const& engine) {
    usize result = 0;
    for (auto& l : engine.processor.layer_processors) {
        if (auto i = l.instrument.TryGet<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>())
            for (auto& d : (*i)->audio_datas)
                result += d->RamUsageBytes();
    }

    return (result) / (1024 * 1024);
}

static bool PluginSaveState(Engine& engine, clap_ostream const& stream) {
    auto state = CurrentStateSnapshot(engine);
    auto outcome = CodeState(state,
                             CodeStateArguments {
                                 .mode = CodeStateArguments::Mode::Encode,
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
        auto item = engine.error_notifications.NewError();
        item->value = {
            .title = "Failed to save state for DAW"_s,
            .message = {},
            .error_code = outcome.Error(),
            .id = U64FromChars("daw save"),
        };
        engine.error_notifications.AddOrUpdateError(item);
        return false;
    }
    return true;
}

static bool PluginLoadState(Engine& engine, clap_istream const& stream) {
    StateSnapshot state {};
    auto const outcome =
        CodeState(state,
                  CodeStateArguments {
                      .mode = CodeStateArguments::Mode::Decode,
                      .read_or_write_data = [&](void* data, usize bytes) -> ErrorCodeOr<void> {
                          u64 bytes_read = 0;
                          while (bytes_read != bytes) {
                              ASSERT(bytes_read < bytes);
                              auto const n = stream.read(&stream, (u8*)data + bytes_read, bytes - bytes_read);
                              if (n == 0) return ErrorCode(CommonError::InvalidFileFormat); // unexpected EOF
                              if (n < 0) return ErrorCode(CommonError::PluginHostError);
                              bytes_read += (u64)n;
                          }
                          return k_success;
                      },
                      .source = StateSource::Daw,
                      .abbreviated_read = false,
                  });

    if (outcome.HasError()) {
        auto const item = engine.error_notifications.NewError();
        item->value = {
            .title = "Failed to load DAW state"_s,
            .message = {},
            .error_code = outcome.Error(),
            .id = U64FromChars("daw load"),
        };
        engine.error_notifications.AddOrUpdateError(item);
        return false;
    }

    LoadNewState(engine, {.state = state, .metadata = {.name_or_path = "DAW State"}}, StateSource::Daw);
    return true;
}

PluginCallbacks<Engine> EngineCallbacks() {
    PluginCallbacks<Engine> result {
        .on_main_thread = OnMainThread,
        .on_timer = PluginOnTimer,
        .on_poll_thread = PluginOnPollThread,
        .save_state = PluginSaveState,
        .load_state = PluginLoadState,
    };
    return result;
}
