// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shared_engine_systems.hpp"

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common_infrastructure/error_reporting.hpp"

#include "plugin/plugin.hpp"

void SharedEngineSystems::StartPollingThreadIfNeeded() {
    if (polling_running.Load(LoadMemoryOrder::Acquire)) return;
    polling_running.Store(1, StoreMemoryOrder::Release);
    polling_thread.Start(
        [this]() {
            try {
                {
                    ArenaAllocatorWithInlineStorage<2000> scratch_arena {PageAllocator::Instance()};
                    auto const o = CleanupOldLogFilesIfNeeded(scratch_arena);
                    if (o.HasError())
                        LogError(ModuleName::Global, "Failed to cleanup old log files: {}", o.Error());
                }

                while (polling_running.Load(LoadMemoryOrder::Relaxed)) {
                    WaitIfValueIsExpected(polling_running, 1, 1000u);
                    {
                        registered_floe_instances_mutex.Lock();
                        DEFER { registered_floe_instances_mutex.Unlock(); };
                        for (auto index : registered_floe_instances)
                            OnPollThread(index);
                    }
                }
            } catch (PanicException) {
            }
        },
        "polling");
}

SharedEngineSystems::SharedEngineSystems(Span<sentry::Tag const> tags)
    : arena(PageAllocator::Instance(), Kb(4))
    , paths(CreateFloePaths(arena))
    , prefs {.arena = PageAllocator::Instance()}
    , sample_library_server(thread_pool,
                            paths.always_scanned_folder[ToInt(ScanFolderType::Libraries)],
                            error_notifications)
    , preset_server {.error_notifications = error_notifications} {
    InitBackgroundErrorReporting(tags);

    prefs.on_change = [this](prefs::Key const& key, prefs::Value const* value) {
        ASSERT(CheckThreadName("main"));

        if (key == prefs::key::k_extra_libraries_folder) {
            DynamicArrayBounded<String, k_max_extra_scan_folders> extra_scan_folders;
            for (auto v = value; v; v = v->next) {
                if (extra_scan_folders.size == k_max_extra_scan_folders) break;
                dyn::AppendIfNotAlreadyThere(extra_scan_folders, v->Get<String>());
            }
            sample_lib_server::SetExtraScanFolders(sample_library_server, extra_scan_folders);
        } else if (key == prefs::key::k_extra_presets_folder) {
            DynamicArrayBounded<String, k_max_extra_scan_folders> extra_scan_folders;
            for (auto v = value; v; v = v->next) {
                if (extra_scan_folders.size == k_max_extra_scan_folders) break;
                dyn::AppendIfNotAlreadyThere(extra_scan_folders, v->Get<String>());
            }
            SetExtraScanFolders(preset_server, extra_scan_folders);
        }
        ErrorReportingOnPreferenceChanged(key, value);

        registered_floe_instances_mutex.Lock();
        DEFER { registered_floe_instances_mutex.Unlock(); };
        for (auto index : registered_floe_instances)
            OnPreferenceChanged(index, key, value);
    };

    thread_pool.Init("global", {});

    if (auto const path_used = prefs::Init(prefs, paths.possible_preferences_paths);
        !path_used || *path_used != 0) {
        // If we reach here then we can assume this is the first time Floe is run.

        if (path_used) {
            // We're assuming path[0] is Floe's prefs, and all other paths are Mirage.
            ASSERT_EQ(path::Extension(paths.possible_preferences_paths[0]), ".ini"_s);
        }

        // When Mirage opens, it scans its libraries/presets folder and adds all the paths to its
        // preferences file. It's possible that Mirage hasn't been opened after libraries/presets were
        // installed, so we need to recreate Mirage's behaviour here.
        struct MiragePath {
            ScanFolderType type;
            FloeKnownDirectoryType known_dir_type;
        };
        for (auto const p : ArrayT<MiragePath>({
                 {ScanFolderType::Libraries, FloeKnownDirectoryType::MirageDefaultLibraries},
                 {ScanFolderType::Presets, FloeKnownDirectoryType::MirageDefaultPresets},
             })) {
            PathArena path_arena {PageAllocator::Instance()};
            auto const dir = FloeKnownDirectory(path_arena, p.known_dir_type, k_nullopt, {.create = false});
            if (auto const o = GetFileType(dir); o.HasValue() && o.Value() == FileType::Directory)
                prefs::AddValue(prefs,
                                ExtraScanFolderDescriptor(paths, p.type),
                                (String)dir,
                                {.dont_send_on_change_event = true});
        }

        prefs.write_to_file_needed = true;
    }

    sample_lib_server::SetExtraScanFolders(sample_library_server,
                                           ExtraScanFolders(paths, prefs, ScanFolderType::Libraries));

    InitPresetServer(preset_server, paths.always_scanned_folder[ToInt(ScanFolderType::Presets)]);
    SetExtraScanFolders(preset_server, ExtraScanFolders(paths, prefs, ScanFolderType::Presets));
}

SharedEngineSystems::~SharedEngineSystems() {
    if (polling_running.Load(LoadMemoryOrder::Acquire)) {
        polling_running.Store(0, StoreMemoryOrder::Release);
        WakeWaitingThreads(polling_running, NumWaitingThreads::All);
        polling_thread.Join();
    }

    ShutdownPresetServer(preset_server);

    prefs::WriteIfNeeded(prefs);
    prefs::Deinit(prefs);

    ShutdownBackgroundErrorReporting();
}

void SharedEngineSystems::RegisterFloeInstance(FloeInstanceIndex index) {
    registered_floe_instances_mutex.Lock();
    DEFER { registered_floe_instances_mutex.Unlock(); };
    ASSERT(!Contains(registered_floe_instances, index));
    dyn::Append(registered_floe_instances, index);
}

void SharedEngineSystems::UnregisterFloeInstance(FloeInstanceIndex index) {
    registered_floe_instances_mutex.Lock();
    DEFER { registered_floe_instances_mutex.Unlock(); };
    auto const num_removed = dyn::RemoveValueSwapLast(registered_floe_instances, index);
    ASSERT_EQ(num_removed, 1u);
}
