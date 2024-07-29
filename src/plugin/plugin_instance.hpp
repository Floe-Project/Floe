// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/debug/debug.hpp"

#include "common/constants.hpp"
#include "cross_instance_systems.hpp"
#include "instrument.hpp"
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

    struct LastSnapshot {
        LastSnapshot() { metadata.name_or_path = "Default"; }

        void Set(StateSnapshotWithMetadata const& snapshot) {
            state = snapshot.state;
            metadata = snapshot.metadata.Clone(metadata_arena);
        }

        void SetMetadata(StateSnapshotMetadata const& m) {
            metadata_arena.ResetCursorAndConsolidateRegions();
            metadata = m.Clone(metadata_arena);
        }

        ArenaAllocatorWithInlineStorage<1000> metadata_arena {};
        StateSnapshot state {};
        StateSnapshotMetadata metadata {};
    };

    PluginInstance(clap_host const& host, CrossInstanceSystems& shared_data);
    ~PluginInstance();

    auto& Layer(u32 index) { return processor.layer_processors[index]; }

    clap_host const& host;
    CrossInstanceSystems& shared_data;
    ArenaAllocator error_arena {PageAllocator::Instance()};
    ThreadsafeErrorNotifications error_notifications {};
    AudioProcessor processor {host};

    u64 random_seed = SeedFromTime();

    ThreadsafeFunctionQueue main_thread_callbacks {.arena = {PageAllocator::Instance()}};

    Optional<PendingStateChange> pending_state_change {};
    LastSnapshot last_snapshot {};
    sample_lib_server::AsyncCommsChannel& sample_lib_server_async_channel {
        sample_lib_server::OpenAsyncCommsChannel(
            shared_data.sample_library_server,
            error_notifications,
            [&plugin = *this]() { plugin.host.request_callback(&plugin.host); })};

    // Presets
    // ========================================================================
    PresetBrowserFilters preset_browser_filters;
    Optional<PresetSelectionCriteria> pending_preset_selection_criteria {};
    u64 presets_folder_listener_id {};
};

PluginCallbacks<PluginInstance> PluginInstanceCallbacks();

void RunFunctionOnMainThread(PluginInstance& plugin, ThreadsafeFunctionQueue::Function function);

// one-off loading of a ir or instrument
Optional<u64> LoadConvolutionIr(PluginInstance& plugin, Optional<sample_lib::IrId> ir);
Optional<u64> LoadInstrument(PluginInstance& plugin, u32 layer_index, InstrumentId instrument_id);

void LoadRandomInstrument(PluginInstance& plugin,
                          u32 layer_index,
                          bool allow_none_to_be_selected,
                          bool disallow_previous_result = true,
                          sample_lib_server::LoadRequest* add_to_existing_batch = nullptr);

enum class CycleDirection { Forward, Backward };
void CycleInstrument(PluginInstance& plugin, u32 layer_index, CycleDirection direction);

usize MegabytesUsedBySamples(PluginInstance const& plugin);

void RandomiseAllLayerInsts(PluginInstance& plugin);

StateSnapshot CurrentStateSnapshot(PluginInstance const& plugin);
bool StateChangedSinceLastSnapshot(PluginInstance const& plugin);

void LoadPresetFromListing(PluginInstance& plugin,
                           PresetSelectionCriteria const& selection_criteria,
                           PresetsFolderScanResult const& listing);

void LoadPresetFromFile(PluginInstance& plugin, String path);

void SaveCurrentStateToFile(PluginInstance& plugin, String path);
