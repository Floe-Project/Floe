// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/directory_listing/directory_listing.hpp"

#include "framework/gui_imgui.hpp"
#include "presets_folder.hpp"

struct Gui;

struct PresetBrowserPersistentData {
    void ShowPresetBrowser() { show_preset_panel = true; }

    bool show_preset_panel {};
    bool scroll_to_show_current_preset {};
    struct DraggingPreset {
        u64 entry_hash;
        imgui::Id imgui_id;
    };
    Optional<DraggingPreset> current_dragging_preset {};
};

static constexpr int k_preset_browser_num_columns = 2;

struct PresetBrowser {
    PresetBrowser(Gui* g, PresetBrowserPersistentData& persistent_data, bool force_listing_fetch);
    ~PresetBrowser();
    void DoPresetBrowserPanel(Rect const mid_panel_r);

    // private

    struct FileBrowserGUIItem {
        DirectoryListing::Entry const* f {};
    };
    void PopulateRowsAndCols(DirectoryListing::Entry const* f,
                             DynamicArray<Array<FileBrowserGUIItem, k_preset_browser_num_columns>>& rows,
                             bool is_root);

    Rect GetAndIncrementColumnRect(f32& ypos, int column) const;
    Rect GetAndIncrementRect(bool is_in_folder_list, f32& ypos, int indent) const;
    DirectoryListing::Entry const*
    DoPresetFolderRecurse(DirectoryListing::Entry const* f, f32& ypos, int& indent);
    DirectoryListing::Entry const* DoPresetFilesRecurse(DirectoryListing::Entry const* f,
                                                        String selected_folder,
                                                        f32& ypos,
                                                        int& count,
                                                        u64 current_preset_hash,
                                                        bool is_root);
    void DoAllPresetFolders();
    void DoAllPresetFiles();
    void SectionHeading(f32& ypos, String const text);

    DirectoryListing::Entry const* HandleKeyPresses(DirectoryListing::Entry const* selected_folder);

    PresetBrowserPersistentData& persistent_data;

    DirectoryListing::Entry const* selected_folder;
    DirectoryListing::Entry const* current_preset;

    f32 subheading_above;
    f32 subheading_height;
    f32 subheading_below;
    f32 left_margin;
    f32 file_arrow_size;

    f32 preset_folders_panel_width;
    f32 preset_panel_xgap;

    f32 preset_button_folder_initial_indent;
    f32 preset_button_folder_indent;
    f32 preset_button_folder_arrow_indent;
    f32 preset_button_file_indent;
    f32 file_folder_gap_above;

    f32 preset_button_height;
    f32 preset_button_ygap;

    bool folder_changed {false};

    PresetsFolderScanResult listing {};

    imgui::WindowSettings wnd_settings;
    imgui::Context& imgui;
    Gui* g;
};
