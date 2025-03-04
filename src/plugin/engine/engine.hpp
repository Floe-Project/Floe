// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/sample_library/attribution_requirements.hpp"

#include "autosave.hpp"
#include "engine/package_installation.hpp"
#include "processor/processor.hpp"
#include "sample_lib_server/sample_library_server.hpp"
#include "shared_engine_systems.hpp"
#include "state/instrument.hpp"
#include "state/state_snapshot.hpp"

struct Engine : ProcessorListener {
    struct PendingStateChange {
        ~PendingStateChange() {
            for (auto& r : retained_results)
                r.Release();
        }

        ArenaAllocator arena {PageAllocator::Instance()};
        DynamicArrayBounded<sample_lib_server::RequestId, k_num_layers + 1> requests;
        DynamicArrayBounded<sample_lib_server::LoadResult, k_num_layers + 1> retained_results;
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

        ArenaAllocatorWithInlineStorage<1000> metadata_arena {Malloc::Instance()};
        StateSnapshot state {};
        StateSnapshotMetadata metadata {};
    };

    Engine(clap_host const& host,
           SharedEngineSystems& shared_engine_systems,
           PluginInstanceMessages& plugin_instance_messages);
    ~Engine();

    auto& Layer(u32 index) { return processor.layer_processors[index]; }

    void OnProcessorChange(ChangeFlags) override;

    clap_host const& host;
    SharedEngineSystems& shared_engine_systems;
    ArenaAllocator error_arena {PageAllocator::Instance()};
    ThreadsafeErrorNotifications error_notifications {};
    AudioProcessor processor {host, *this, shared_engine_systems.prefs};
    PluginInstanceMessages& plugin_instance_messages;

    u64 random_seed = (u64)NanosecondsSinceEpoch();

    Atomic<bool> update_gui = false;
    AutosaveState autosave_state {};

    package::InstallJobs package_install_jobs {};

    AttributionRequirementsState attribution_requirements {
        .shared_attributions_store = shared_engine_systems.shared_attributions_store,
    };
    Optional<clap_id> timer_id {};
    TimePoint last_poll_thread_time {};

    // IMPORTANT: debug-only, remove this
    DynamicArrayBounded<char, 200> state_change_description {};

    ThreadsafeFunctionQueue main_thread_callbacks {.arena = {PageAllocator::Instance()}};

    Optional<PendingStateChange> pending_state_change {};
    LastSnapshot last_snapshot {};

    sample_lib_server::AsyncCommsChannel& sample_lib_server_async_channel;
    // Presets
    // ========================================================================
    PresetBrowserFilters preset_browser_filters;
    Optional<PresetSelectionCriteria> pending_preset_selection_criteria {};
    u64 presets_folder_listener_id {};
};

PluginCallbacks<Engine> EngineCallbacks();

void RunFunctionOnMainThread(Engine& engine, ThreadsafeFunctionQueue::Function function);

constexpr sample_lib::LibraryIdRef k_default_background_lib_id = {
    .author = "floe",
    .name = "default-bg",
};

Optional<sample_lib::LibraryIdRef> LibraryForOverallBackground(Engine const& engine);

// one-off loading of a ir or instrument
void LoadConvolutionIr(Engine& engine, Optional<sample_lib::IrId> ir);
void LoadInstrument(Engine& engine, u32 layer_index, InstrumentId instrument_id);

void LoadRandomInstrument(Engine& engine,
                          u32 layer_index,
                          bool allow_none_to_be_selected,
                          bool disallow_previous_result = true,
                          sample_lib_server::LoadRequest* add_to_existing_batch = nullptr);

enum class CycleDirection { Forward, Backward };
void CycleInstrument(Engine& engine, u32 layer_index, CycleDirection direction);

usize MegabytesUsedBySamples(Engine const& engine);

void RandomiseAllLayerInsts(Engine& engine);

StateSnapshot CurrentStateSnapshot(Engine const& engine);
bool StateChangedSinceLastSnapshot(Engine& engine);

void LoadPresetFromListing(Engine& engine,
                           PresetSelectionCriteria const& selection_criteria,
                           PresetsFolderScanResult const& listing);

void LoadPresetFromFile(Engine& engine, String path);

void SaveCurrentStateToFile(Engine& engine, String path);
