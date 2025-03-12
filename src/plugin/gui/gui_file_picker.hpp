// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/paths.hpp"

#include "engine/engine.hpp"
#include "engine/package_installation.hpp"
#include "gui_framework/gui_frame.hpp"
#include "state/state_coding.hpp"

struct AddScanFolderFilePickerState {
    ScanFolderType type {};
    bool set_as_install_folder {};
};

enum class FilePickerStateType {
    None,
    AddScanFolder,
    InstallPackage,
    SavePreset,
    LoadPreset,
};

using FilePickerUnion =
    TaggedUnion<FilePickerStateType,
                TypeAndTag<AddScanFolderFilePickerState, FilePickerStateType::AddScanFolder>>;

struct FilePickerState {
    ArenaAllocator arena {Malloc::Instance(), 256};
    FilePickerUnion data;
};

PUBLIC void OpenFilePickerAddExtraScanFolders(FilePickerState& state,
                                              GuiFrameResult& frame_result,
                                              prefs::Preferences const& prefs,
                                              FloePaths const& paths,
                                              AddScanFolderFilePickerState data) {
    state.arena.ResetCursorAndConsolidateRegions();
    Optional<String> default_folder {};
    if (auto const extra_paths = ExtraScanFolders(paths, prefs, data.type); extra_paths.size)
        default_folder = state.arena.Clone(extra_paths[0]);

    frame_result.file_picker_dialog = FilePickerDialogOptions {
        .type = FilePickerDialogOptions::Type::SelectFolder,
        .title = ({
            String s {};
            switch (data.type) {
                case ScanFolderType::Libraries: s = "Select Libraries Folder"; break;
                case ScanFolderType::Presets: s = "Select Presets Folder"; break;
                case ScanFolderType::Count: PanicIfReached();
            }
            s;
        }),
        .default_path = default_folder,
        .filters = {},
        .allow_multiple_selection = true,
    };

    state.data = data;
}

PUBLIC void OpenFilePickerInstallPackage(FilePickerState& state, GuiFrameResult& frame_result) {
    state.arena.ResetCursorAndConsolidateRegions();

    static constexpr auto k_filters = ArrayT<FilePickerDialogOptions::FileFilter>({
        {
            .description = "Floe Package"_s,
            .wildcard_filter = "*.floe.zip"_s,
        },
    });

    frame_result.file_picker_dialog = FilePickerDialogOptions {
        .type = FilePickerDialogOptions::Type::OpenFile,
        .title = "Select 1 or more Floe Package",
        .default_path = KnownDirectory(state.arena, KnownDirectoryType::Downloads, {.create = false}),
        .filters = k_filters,
        .allow_multiple_selection = true,
    };

    state.data = FilePickerStateType::InstallPackage;
}

static String PresetFileDefaultPath(ArenaAllocator& arena, FloePaths const& paths) {
    auto const result = path::Join(arena,
                                   Array {paths.always_scanned_folder[ToInt(ScanFolderType::Presets)],
                                          "untitled" FLOE_PRESET_FILE_EXTENSION});
    return result;
}

PUBLIC void
OpenFilePickerSavePreset(FilePickerState& state, GuiFrameResult& frame_result, FloePaths const& paths) {
    state.arena.ResetCursorAndConsolidateRegions();

    static constexpr auto k_filters = ArrayT<FilePickerDialogOptions::FileFilter>({
        {
            .description = "Floe Preset"_s,
            .wildcard_filter = "*" FLOE_PRESET_FILE_EXTENSION,
        },
    });

    frame_result.file_picker_dialog = FilePickerDialogOptions {
        .type = FilePickerDialogOptions::Type::SaveFile,
        .title = "Save Floe Preset",
        .default_path = PresetFileDefaultPath(state.arena, paths),
        .filters = k_filters,
        .allow_multiple_selection = false,
    };

    state.data = FilePickerStateType::SavePreset;
}

PUBLIC void
OpenFilePickerLoadPreset(FilePickerState& state, GuiFrameResult& frame_result, FloePaths const& paths) {
    state.arena.ResetCursorAndConsolidateRegions();
    static constexpr auto k_filters = ArrayT<FilePickerDialogOptions::FileFilter>({
        {
            .description = "Floe Preset"_s,
            .wildcard_filter = "*.floe-*"_s,
        },
        {
            .description = "Mirage Preset"_s,
            .wildcard_filter = "*.mirage-*"_s,
        },
    });

    frame_result.file_picker_dialog = FilePickerDialogOptions {
        .type = FilePickerDialogOptions::Type::OpenFile,
        .title = "Load Floe Preset",
        .default_path = PresetFileDefaultPath(state.arena, paths),
        .filters = k_filters,
        .allow_multiple_selection = false,
    };

    state.data = FilePickerStateType::LoadPreset;
}

// Ephemeral
struct FilePickerContext {
    prefs::Preferences& prefs;
    FloePaths const& paths;
    package::InstallJobs& package_install_jobs;
    ThreadPool& thread_pool;
    ArenaAllocator& scratch_arena;
    sample_lib_server::Server& sample_lib_server;
    Engine& engine;
};

PUBLIC void CheckForFilePickerResults(GuiFrameInput const& frame_input,
                                      FilePickerState& state,
                                      FilePickerContext const& context) {
    if (frame_input.file_picker_results.size == 0) return;
    switch (state.data.tag) {
        case FilePickerStateType::None: break;
        case FilePickerStateType::AddScanFolder: {
            auto const data = state.data.Get<AddScanFolderFilePickerState>();
            for (auto const p : frame_input.file_picker_results) {
                ASSERT(IsValidUtf8(p));
                prefs::AddValue(context.prefs,
                                ExtraScanFolderDescriptor(context.paths, data.type),
                                (String)p);
            }
            if (data.set_as_install_folder)
                prefs::SetValue(context.prefs,
                                InstallLocationDescriptor(context.paths, context.prefs, data.type),
                                (String)frame_input.file_picker_results.first->data);
            break;
        }
        case FilePickerStateType::InstallPackage: {
            for (auto const path : frame_input.file_picker_results) {
                package::AddJob(context.package_install_jobs,
                                path,
                                context.prefs,
                                context.paths,
                                context.thread_pool,
                                context.scratch_arena,
                                context.sample_lib_server);
            }
            break;
        }
        case FilePickerStateType::SavePreset: {
            SaveCurrentStateToFile(context.engine, frame_input.file_picker_results.first->data);
            break;
        }
        case FilePickerStateType::LoadPreset: {
            LoadPresetFromFile(context.engine, frame_input.file_picker_results.first->data);
            break;
        }
    }
    state.data = FilePickerStateType::None;
}
