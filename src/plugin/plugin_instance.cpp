// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugin_instance.hpp"

#include <clap/ext/params.h>

#include "foundation/foundation.hpp"
#include "utils/debug/debug.hpp"

#include "common/common_errors.hpp"
#include "common/constants.hpp"
#include "cross_instance_systems.hpp"
#include "instrument.hpp"
#include "layer_processor.hpp"
#include "param_info.hpp"
#include "plugin.hpp"
#include "sample_library_server.hpp"
#include "settings/settings_file.hpp"
#include "state/state_coding.hpp"
#include "state/state_snapshot.hpp"

static void RequestGuiRedraw(PluginInstance& plugin) {
    plugin.processor.for_main_thread.flags.FetchOr(AudioProcessor::MainThreadCallbackFlagsUpdateGui);
    plugin.processor.host.request_callback(&plugin.host);
}

static void SetLastSnapshot(PluginInstance& plugin, StateSnapshotWithMetadata const& state) {
    plugin.last_snapshot.Set(state);
    RequestGuiRedraw(plugin);
    // do this at the end because the pending state could be the arg of this function
    plugin.pending_state_change.Clear();
}

static void LoadNewState(PluginInstance& plugin, StateSnapshotWithMetadata const& state, StateSource source) {
    ZoneScoped;
    ASSERT(IsMainThread(plugin.host));

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
            plugin.processor.layer_processors[layer_index].instrument_id = i;
            switch (i.tag) {
                case InstrumentType::None:
                    SetInstrument(plugin.processor, layer_index, InstrumentType::None);
                    break;
                case InstrumentType::WaveformSynth:
                    SetInstrument(plugin.processor,
                                  layer_index,
                                  i.GetFromTag<InstrumentType::WaveformSynth>());
                    break;
                case InstrumentType::Sampler: PanicIfReached(); break;
            }
        }

        ASSERT(!state.state.ir_id.HasValue());
        plugin.processor.convo.ir_id = nullopt;
        SetConvolutionIrAudioData(plugin.processor, nullptr);

        ApplyNewState(plugin.processor, state.state, source);
        SetLastSnapshot(plugin, state);
    } else {
        plugin.pending_state_change.Emplace();
        auto& pending = *plugin.pending_state_change;
        pending.snapshot.state = state.state;
        pending.snapshot.metadata = state.metadata.Clone(pending.arena);
        pending.source = source;

        for (auto [layer_index, i] : Enumerate<u32>(state.state.inst_ids)) {
            plugin.processor.layer_processors[layer_index].instrument_id = i;

            if (i.tag != InstrumentType::Sampler) continue;

            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                                        plugin.sample_lib_server_async_channel,
                                                        sample_lib_server::LoadRequestInstrumentIdWithLayer {
                                                            .id = i.Get<sample_lib::InstrumentId>(),
                                                            .layer_index = layer_index,
                                                        });
            dyn::Append(pending.requests, async_id);
        }

        plugin.processor.convo.ir_id = state.state.ir_id;
        if (state.state.ir_id) {
            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                                        plugin.sample_lib_server_async_channel,
                                                        *state.state.ir_id);
            dyn::Append(pending.requests, async_id);
        }
    }
}

static Instrument InstrumentFromPendingState(PluginInstance::PendingStateChange const& pending_state_change,
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

static AudioData const*
IrAudioDataFromPendingState(PluginInstance::PendingStateChange const& pending_state_change) {
    auto const ir_id = pending_state_change.snapshot.state.ir_id;
    if (!ir_id) return nullptr;
    for (auto const& r : pending_state_change.retained_results) {
        auto const loaded_ir = r.TryExtract<sample_lib_server::RefCounted<sample_lib::LoadedIr>>();
        if (loaded_ir && *ir_id == **loaded_ir) return (*loaded_ir)->audio_data;
    }
    return nullptr;
}

static void ApplyNewStateFromPending(PluginInstance& plugin) {
    ZoneScoped;
    ASSERT(IsMainThread(plugin.host));

    auto const& pending_state_change = *plugin.pending_state_change;

    for (auto const layer_index : Range(k_num_layers))
        SetInstrument(plugin.processor,
                      layer_index,
                      InstrumentFromPendingState(pending_state_change, layer_index));
    SetConvolutionIrAudioData(plugin.processor, IrAudioDataFromPendingState(pending_state_change));
    ApplyNewState(plugin.processor, pending_state_change.snapshot.state, pending_state_change.source);

    // do it last because it clears pending_state_change
    SetLastSnapshot(plugin, pending_state_change.snapshot);
}

static void SampleLibraryResourceLoaded(PluginInstance& plugin, sample_lib_server::LoadResult result) {
    ZoneScoped;
    ASSERT(IsMainThread(plugin.host));

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

    switch (source) {
        case Source::OneOff: {
            if (result.result.tag != sample_lib_server::LoadResult::ResultType::Success) break;

            auto const resource = result.result.Get<sample_lib_server::Resource>();
            switch (resource.tag) {
                case sample_lib_server::LoadRequestType::Instrument: {
                    auto const loaded_inst =
                        resource.Get<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>();

                    for (auto [layer_index, l] : Enumerate<u32>(plugin.processor.layer_processors)) {
                        if (auto const i = l.instrument_id.TryGet<sample_lib::InstrumentId>()) {
                            if (*i == *loaded_inst) SetInstrument(plugin.processor, layer_index, loaded_inst);
                        }
                    }
                    break;
                }
                case sample_lib_server::LoadRequestType::Ir: {
                    auto const loaded_ir =
                        resource.Get<sample_lib_server::RefCounted<sample_lib::LoadedIr>>();

                    auto const current_ir_id = plugin.processor.convo.ir_id;
                    if (current_ir_id.HasValue()) {
                        if (*current_ir_id == *loaded_ir)
                            SetConvolutionIrAudioData(plugin.processor, loaded_ir->audio_data);
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

StateSnapshot CurrentStateSnapshot(PluginInstance const& plugin) {
    if (plugin.pending_state_change) return plugin.pending_state_change->snapshot.state;
    return MakeStateSnapshot(plugin.processor);
}

static auto PrintInstrumentId(InstrumentId id) {
    DynamicArrayInline<char, 100> result {};
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

static void AssignDiffDescription(dyn::DynArray auto& diff_desc,
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
                        k_param_infos[param_index].name,
                        old_state.param_values[param_index],
                        new_state.param_values[param_index]);
        }
    }

    if (old_state.fx_order != new_state.fx_order) fmt::Append(diff_desc, "FX order changed\n"_s);
}

bool StateChangedSinceLastSnapshot(PluginInstance& plugin) {
    auto current = CurrentStateSnapshot(plugin);
    // we don't check the params ccs for changes
    current.param_learned_ccs = plugin.last_snapshot.state.param_learned_ccs;
    bool const changed = plugin.last_snapshot.state != current;

    if constexpr (!PRODUCTION_BUILD) {
        if (changed)
            AssignDiffDescription(plugin.state_change_description, plugin.last_snapshot.state, current);
        else
            dyn::Clear(plugin.state_change_description);
    }

    return changed;
}

// one-off load
Optional<u64> LoadConvolutionIr(PluginInstance& plugin, Optional<sample_lib::IrId> ir_id) {
    ASSERT(IsMainThread(plugin.host));
    plugin.processor.convo.ir_id = ir_id;

    if (ir_id)
        return SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                    plugin.sample_lib_server_async_channel,
                                    *ir_id);
    else
        SetConvolutionIrAudioData(plugin.processor, nullptr);
    return nullopt;
}

// one-off load
Optional<u64> LoadInstrument(PluginInstance& plugin, u32 layer_index, InstrumentId inst_id) {
    ASSERT(IsMainThread(plugin.host));
    plugin.processor.layer_processors[layer_index].instrument_id = inst_id;

    switch (inst_id.tag) {
        case InstrumentType::Sampler:
            return SendAsyncLoadRequest(plugin.shared_data.sample_library_server,
                                        plugin.sample_lib_server_async_channel,
                                        sample_lib_server::LoadRequestInstrumentIdWithLayer {
                                            .id = inst_id.GetFromTag<InstrumentType::Sampler>(),
                                            .layer_index = layer_index,
                                        });
        case InstrumentType::None: {
            SetInstrument(plugin.processor, layer_index, InstrumentType::None);
            break;
        }
        case InstrumentType::WaveformSynth:
            SetInstrument(plugin.processor, layer_index, inst_id.Get<WaveformType>());
            break;
    }
    return nullopt;
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
        plugin.last_snapshot.SetMetadata(StateSnapshotMetadata {.name_or_path = path});
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

void LoadRandomInstrument(PluginInstance& plugin,
                          u32 layer_index,
                          bool allow_none_to_be_selected,
                          bool disallow_previous_result,
                          sample_lib_server::LoadRequest* add_to_existing_batch) {
    // TODO
    (void)plugin;
    (void)layer_index;
    (void)allow_none_to_be_selected;
    (void)disallow_previous_result;
    (void)add_to_existing_batch;
}

void CycleInstrument(PluginInstance& plugin, u32 layer_index, CycleDirection direction) {
    // TODO
    (void)plugin;
    (void)layer_index;
    (void)direction;
}

void RandomiseAllLayerInsts(PluginInstance& plugin) {
    // TODO
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

    ArenaAllocatorWithInlineStorage<4000> scratch_arena {};
    while (auto f = plugin.main_thread_callbacks.TryPop(scratch_arena))
        (*f)();

    while (auto f = plugin.sample_lib_server_async_channel.results.TryPop()) {
        SampleLibraryResourceLoaded(plugin, *f);
        f->Release();
    }
}

PluginInstance::PluginInstance(clap_host const& host, CrossInstanceSystems& shared_data)
    : host(host)
    , shared_data(shared_data) {

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
    shared_data.preset_listing.scanned_folder.listeners.Remove(presets_folder_listener_id);

    sample_lib_server::CloseAsyncCommsChannel(shared_data.sample_library_server,
                                              sample_lib_server_async_channel);
}

usize MegabytesUsedBySamples(PluginInstance const& plugin) {
    usize result = 0;
    for (auto& l : plugin.processor.layer_processors) {
        if (auto i = l.instrument.TryGet<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>())
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
    auto const outcome =
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
        auto const item = plugin.error_notifications.NewError();
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
