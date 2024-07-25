// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/debug/debug.hpp"

#include "common/constants.hpp"
#include "cross_instance_systems.hpp"
#include "instrument_type.hpp"
#include "layer_processor.hpp"
#include "plugin.hpp"
#include "processor.hpp"
#include "sample_library_server.hpp"
#include "state/state_snapshot.hpp"

// TODO(1.0): Core-Library Repo: tag all IRs in the floe.lua
//
// TODO(1.0): fetch the IRs dynamically once we have the new library infrastructure
constexpr auto k_core_version_1_irs = Array {
    "2s Airy 1"_s,
    "2s Rough Crackle",
    "3s Creaky Door 2",
    "3s Crunchy",
    "3s Rattle",
    "3s Shivering Cold",
    "3s Smooooth",
    "4s Space Didgeridoo",
    "4s Standard Bright",
    "4s Wind",
    "5s Shimmer",
    "Ambi 4 Fade",
    "Formant 1",
    "Realistic Cathedral A",
    "Realistic Cathedral B",
    "Realistic Large A",
    "Realistic Large B",
    "Realistic Subtle",
};

struct PluginInstance {
    struct Layer {
        Layer(u32 index, LayerProcessor& p) : index(index), processor(p) {}
        ~Layer() {
            if (auto sampled_inst = instrument.TryGet<sample_lib_server::RefCounted<LoadedInstrument>>())
                sampled_inst->Release();
        }

        AudioData const* GetSampleForGUIWaveform() const {
            if (auto sampled_inst = instrument.TryGet<sample_lib_server::RefCounted<LoadedInstrument>>()) {
                if (*sampled_inst) return (*sampled_inst)->file_for_gui_waveform;
            } else {
                // TODO: get waveform audio data
            }
            return nullptr;
        }

        String InstName() const {
            switch (instrument.tag) {
                case InstrumentType::WaveformSynth: {
                    return k_waveform_type_names[ToInt(instrument.Get<WaveformType>())];
                }
                case InstrumentType::Sampler: {
                    return instrument.Get<sample_lib_server::RefCounted<LoadedInstrument>>()->instrument.name;
                }
                case InstrumentType::None: return "None"_s;
            }
            return {};
        }

        Optional<String> LibName() const {
            if (instrument.tag != InstrumentType::Sampler) return nullopt;
            return instrument.Get<sample_lib_server::RefCounted<LoadedInstrument>>()->instrument.library.name;
        }

        u32 index = (u32)-1;
        LayerProcessor& processor;
        RandomIntGenerator<mdata::Index> inst_index_generator {};

        Instrument instrument {InstrumentType::None};
        InstrumentId instrument_id {InstrumentType::None};
    };

    PluginInstance(clap_host const& host, CrossInstanceSystems& shared_data);
    ~PluginInstance();

    CrossInstanceSystems& shared_data;
    ArenaAllocator error_arena {PageAllocator::Instance()};
    ThreadsafeErrorNotifications error_notifications {};
    clap_host const& host;
    AudioProcessor processor;

    u64 random_seed = SeedFromTime();

    Layer layers[k_num_layers] {
        {0, processor.layer_processors[0]},
        {1, processor.layer_processors[1]},
        {2, processor.layer_processors[2]},
    };

    bool in_destructor = false;

    ThreadsafeFunctionQueue main_thread_callbacks {.arena = {PageAllocator::Instance()}};

    // State
    // ========================================================================
    struct PendingStateChange {
        ~PendingStateChange() {
            for (auto& r : retained_results)
                r.Release();
        }

        ArenaAllocator arena {PageAllocator::Instance()};
        DynamicArrayInline<sample_lib_server::RequestId, k_num_layers + 1> requests;
        DynamicArrayInline<sample_lib_server::LoadResult, k_num_layers + 1> retained_results;
        StateSnapshotWithMetadata snapshot;
        StateSource source;
    };
    Optional<PendingStateChange> pending_state_change {};
    ArenaAllocatorWithInlineStorage<1000> last_snapshot_metadata_arena {};
    StateSnapshotWithMetadata last_snapshot {
        .metadata = {.name_or_path = "Default"},
    };

    // Presets
    // ========================================================================
    PresetBrowserFilters preset_browser_filters;
    Optional<PresetSelectionCriteria> pending_preset_selection_criteria {};
    u64 presets_folder_listener_id {};
    bool gui_needs_to_handle_preset_name_change {}; // TODO: find other solution

    // ========================================================================
    sample_lib_server::AsyncCommsChannel& sample_lib_server_async_channel;

    DynamicArray<sample_lib_server::RefCounted<LoadedInstrument>> lifetime_extended_insts {
        Malloc::Instance()};
};

PluginCallbacks<PluginInstance> PluginInstanceCallbacks();

void RunFunctionOnMainThread(PluginInstance& plugin, ThreadsafeFunctionQueue::Function function);

Optional<u64> LoadConvolutionIr(PluginInstance& plugin, Optional<sample_lib::IrId> ir);

Optional<u64> LoadInstrument(PluginInstance& plugin, u32 layer_index, InstrumentId instrument_info);

void LoadRandomInstrument(PluginInstance& plugin,
                          u32 layer_index,
                          bool allow_none_to_be_selected,
                          bool disallow_previous_result = true,
                          sample_lib_server::LoadRequest* add_to_existing_batch = nullptr);

enum class CycleDirection { Forward, Backward };
void CycleInstrument(PluginInstance& plugin, u32 layer_index, CycleDirection direction);

usize MegabytesUsedBySamples(PluginInstance const& plugin);

void RandomiseAllLayerInsts(PluginInstance& plugin);

StateSnapshot CurrentStateSnapshot(PluginInstance& plugin);
bool StateChangedSinceLastSnapshot(PluginInstance& plugin);

void ApplyNewState(PluginInstance& plugin, StateSnapshotMetadata const& metadata, StateSource source);

void LoadPresetFromListing(PluginInstance& plugin,
                           PresetSelectionCriteria const& selection_criteria,
                           PresetsFolderScanResult const& listing);

void LoadPresetFromFile(PluginInstance& plugin, String path);

void SaveCurrentStateToFile(PluginInstance& plugin, String path);

void SetAllParametersToDefaultValues(PluginInstance& plugin);

void RandomiseAllParameterValues(PluginInstance& plugin);
void RandomiseAllEffectParameterValues(PluginInstance& plugin);
