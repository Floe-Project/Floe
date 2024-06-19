// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_preset_browser.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/directory_listing/directory_listing.hpp"

#include "config.h"
#include "gui.hpp"
#include "gui/gui_label_widgets.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_editor_ui_style.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"
#include "icons-fa/IconsFontAwesome5.h"
#include "presets_folder.hpp"

PresetBrowser::PresetBrowser(Gui* g, PresetBrowserPersistentData& persistent_data, bool force_listing_fetch)
    : persistent_data(persistent_data)
    , imgui(g->imgui)
    , g(g) {

    if (persistent_data.show_preset_panel || force_listing_fetch) {
        listing = FetchOrRescanPresetsFolder(g->plugin.shared_data.preset_listing,
                                             RescanMode::RescanAsyncIfNeeded,
                                             g->settings.settings.filesystem.extra_presets_scan_folders,
                                             &g->plugin.shared_data.thread_pool);
    }
    if (listing.listing) {
        current_preset = g->plugin.latest_snapshot.metadata.Path()
                             ? listing.listing->Find(*g->plugin.latest_snapshot.metadata.Path())
                             : nullptr;
        selected_folder = listing.listing->Find(g->plugin.preset_browser_filters.selected_folder_hash);
    }

    preset_folders_panel_width = editor::GetSize(imgui, UiSizeId::PresetFoldersPanelWidth);
    preset_panel_xgap = editor::GetSize(imgui, UiSizeId::PresetPanelGapX);

    preset_button_folder_initial_indent = editor::GetSize(imgui, UiSizeId::PresetButtonFolderInitialIndent);
    preset_button_folder_arrow_indent = editor::GetSize(imgui, UiSizeId::PresetButtonFolderArrorIndent);
    preset_button_folder_indent = editor::GetSize(imgui, UiSizeId::PresetButtonFolderIndent);
    preset_button_file_indent = editor::GetSize(imgui, UiSizeId::PresetButtonFileIndent);
    file_folder_gap_above = editor::GetSize(imgui, UiSizeId::PresetFilesFolderHeadingGapAbove);

    preset_button_height = editor::GetSize(imgui, UiSizeId::PresetButtonHeight);
    preset_button_ygap = editor::GetSize(imgui, UiSizeId::PresetButtonGapY);

    subheading_above = editor::GetSize(imgui, UiSizeId::PresetSubheadingAbove);
    subheading_height = editor::GetSize(imgui, UiSizeId::PresetSubheadingHeight);
    subheading_below = editor::GetSize(imgui, UiSizeId::PresetSubheadingBelow);
    left_margin = editor::GetSize(imgui, UiSizeId::SidePanelTextMarginLeft);
    file_arrow_size = editor::GetSize(imgui, UiSizeId::PresetFileArrowSize);

    wnd_settings = StandalonePopupSettings(imgui);
    wnd_settings.pad_top_left = {editor::GetSize(imgui, UiSizeId::PresetPadL),
                                 editor::GetSize(imgui, UiSizeId::PresetPadT)};
    wnd_settings.pad_bottom_right = {editor::GetSize(imgui, UiSizeId::PresetPadR),
                                     editor::GetSize(imgui, UiSizeId::PresetPadB)};
}

PresetBrowser::~PresetBrowser() {
    if (persistent_data.current_dragging_preset &&
        !imgui.IsActive(persistent_data.current_dragging_preset->imgui_id)) {
        persistent_data.current_dragging_preset = nullopt;
    }
}

Rect PresetBrowser::GetAndIncrementRect(bool const is_in_folder_list, f32& ypos, int const indent) const {
    auto const indent_px =
        is_in_folder_list ? ((f32)indent * preset_button_folder_indent + preset_button_folder_initial_indent)
                          : (f32)indent * preset_button_file_indent;
    auto const result = Rect {indent_px, ypos, imgui.Width() - indent_px, preset_button_height};
    ypos += preset_button_height + preset_button_ygap;
    return result;
}

Rect PresetBrowser::GetAndIncrementColumnRect(f32& ypos, int const column) const {
    f32 temp_ypos = ypos;
    auto r = GetAndIncrementRect(false, temp_ypos, 0);
    r.w /= k_preset_browser_num_columns;
    r.x += r.w * (f32)column;
    if (column == k_preset_browser_num_columns - 1) ypos = temp_ypos;
    return r;
}

DirectoryListing::Entry const*
PresetBrowser::DoPresetFolderRecurse(DirectoryListing::Entry const* f, f32& ypos, int& indent) {
    DirectoryListing::Entry const* clicked_preset_folder = nullptr;

    while (f) {
        auto const imgui_id = imgui.GetID(f->Hash());
        if (f->IsDirectory() && (f->HasChildren() || !PRODUCTION_BUILD)) {
            auto const r = GetAndIncrementRect(true, ypos, indent);
            if (selected_folder && (f == selected_folder || f->IsDecendentOf(selected_folder))) {
                auto text_r = imgui.GetRegisteredAndConvertedRect(r);
                imgui.graphics->AddText(g->icons,
                                        file_arrow_size,
                                        f32x2 {text_r.x - preset_button_folder_arrow_indent,
                                               text_r.y + (text_r.h / 2 - file_arrow_size / 2)},
                                        GMC(PresetBrowserFileDownArrow),
                                        ICON_FA_CHECK);
            }

            bool state = f->Hash() == g->plugin.preset_browser_filters.selected_folder_hash;

            DynamicArray<char> name {f->Filename(), g->scratch_arena};
            if (name == "."_s) PanicIfReached();
            if (!f->HasChildren()) dyn::AppendSpan(name, " <empty>"_s);
            if (buttons::Toggle(g, imgui_id, r, state, name, buttons::PresetsBrowserFolderButton(imgui)))
                clicked_preset_folder = f;
            auto const rel_pos = imgui.ScreenPosToWindowPos(imgui.platform->cursor_pos);
            if (persistent_data.current_dragging_preset) {
                if (imgui.WasJustDeactivated(persistent_data.current_dragging_preset->imgui_id) &&
                    r.Contains(rel_pos)) {
                    auto const from =
                        listing.listing->Find(persistent_data.current_dragging_preset->entry_hash);
                    if (from) {
                        auto const to = f;
                        if (path::IsAbsolute(from->Path()) && path::IsAbsolute(to->Path())) {
                            if (auto o = MoveFileOrDirIntoFolder(from->Path(),
                                                                 to->Path(),
                                                                 ExistingDestinationHandling::Fail);
                                o.HasError()) {
                                int const b = 0;
                                (void)b;
                            }
                        }
                    }
                }
            }
            indent++;
            if (auto clicked = DoPresetFolderRecurse(f->FirstChild(), ypos, indent))
                clicked_preset_folder = clicked;
            indent--;
        }
        f = f->Next();
    }
    return clicked_preset_folder;
}

static bool IsOnScreen(imgui::Context& imgui, Rect const& r) {
    return Rect::DoRectsIntersect(imgui.GetRegisteredAndConvertedRect(r),
                                  imgui.CurrentWindow()->clipping_rect);
}

static DynamicArray<char> TrimPath(Allocator& a, String path) {
    DynamicArray<char> result {path, a};

    if (auto num_sections = Count(path, '/') + 1; num_sections > 2) {
        auto const num_sections_to_remove = num_sections - 2;
        usize section = 0;

        Optional<usize> cursor = 0uz;
        auto whole = path;
        while (cursor && section < num_sections_to_remove) {
            auto item = SplitWithIterator(whole, cursor, '/');
            path.RemovePrefix(item.size + 1);
            ++section;
        }

        dyn::Assign(result, "../"_s);
        dyn::AppendSpan(result, path);
    }

    return result;
}

DirectoryListing::Entry const* PresetBrowser::DoPresetFilesRecurse(DirectoryListing::Entry const* f,
                                                                   String current_selected_folder,
                                                                   f32& ypos,
                                                                   int& count,
                                                                   u64 const current_preset_hash,
                                                                   bool const is_root) {
    DirectoryListing::Entry const* clicked_preset_file = nullptr;

    bool first_in_folder = true;

    while (f) {
        auto const imgui_id = imgui.GetID(f->Hash());

        if (f->HasChildren()) {
            if (auto clicked = DoPresetFilesRecurse(f->FirstChild(),
                                                    current_selected_folder,
                                                    ypos,
                                                    count,
                                                    current_preset_hash,
                                                    false)) {
                clicked_preset_file = clicked;
            }
        } else if (f->IsFile()) {
            if (EntryMatchesSearchFilter(*f,
                                         *listing.listing,
                                         g->plugin.preset_browser_filters.search_filter,
                                         selected_folder)) {
                if (first_in_folder) {
                    first_in_folder = false;

                    if (count != 0) {
                        if (((count - 1) % k_preset_browser_num_columns) != k_preset_browser_num_columns - 1)
                            ypos += preset_button_height;
                        ypos += file_folder_gap_above;
                    }
                    count = 0;

                    auto const starting_y = ypos;
                    Rect const r {0,
                                  ypos,
                                  imgui.Width(),
                                  editor::GetSize(imgui, UiSizeId::PresetFilesFolderHeadingHeight)};
                    ypos += r.h + editor::GetSize(imgui, UiSizeId::PresetFilesFolderHeadingPadBelow);

                    if (IsOnScreen(imgui, r)) {
                        if (starting_y >= preset_button_height) {
                            auto const divider =
                                imgui.GetRegisteredAndConvertedRect({0, r.y, imgui.Width(), 1});
                            imgui.graphics->AddRectFilled(divider.Min(),
                                                          divider.Max(),
                                                          GMC(BrowserFileDivider));
                        }

                        auto const max_heading_w = r.w / 2;
                        auto const heading_font = g->mada;
                        imgui.graphics->context->PushFont(heading_font);
                        labels::Label(g,
                                      r.WithW(max_heading_w),
                                      f->Parent()->Filename(),
                                      labels::PresetBrowserFolder(imgui));
                        imgui.graphics->context->PopFont();

                        if (f->Parent()->Parent()) {
                            auto const gap_between_title_and_path = r.h * 2;
                            auto const path_x =
                                Min(max_heading_w,
                                    draw::GetTextWidth(heading_font, f->Parent()->Filename())) +
                                gap_between_title_and_path;

                            auto const prefix = path::Directory(current_selected_folder).ValueOr({});
                            auto parent_parent_path = f->Parent()->Parent()->Path();
                            if (prefix.size < parent_parent_path.size) {
                                auto path = f->Parent()->Parent()->Path().SubSpan(prefix.size);
                                if (StartsWith(path, '/')) path.RemovePrefix(1);

                                labels::Label(g,
                                              r.CutLeft(path_x),
                                              TrimPath(g->scratch_arena, path),
                                              labels::PresetBrowserFolderPath(imgui));
                            }
                        }
                    }
                }

                int const column = count % k_preset_browser_num_columns;
                auto const r = GetAndIncrementColumnRect(ypos, column);
                bool state = current_preset_hash == f->Hash();
                if (state && persistent_data.scroll_to_show_current_preset &&
                    !imgui.WasWindowJustCreated(imgui.CurrentWindow()->id)) {
                    persistent_data.scroll_to_show_current_preset = false;
                    imgui.ScrollWindowToShowRectangle(r);
                }
                if (IsOnScreen(imgui, r)) {
                    auto const name = f->FilenameNoExt();
                    if (buttons::Toggle(g,
                                        imgui_id,
                                        r,
                                        state,
                                        name,
                                        buttons::PresetsBrowserFileButton(imgui)))
                        clicked_preset_file = f;
                    if (imgui.WasJustActivated(imgui_id)) {
                        persistent_data.current_dragging_preset =
                            PresetBrowserPersistentData::DraggingPreset {
                                .entry_hash = f->Hash(),
                                .imgui_id = imgui_id,
                            };
                    }
                    if (imgui.platform->double_left_click && imgui.IsHot(imgui_id))
                        persistent_data.show_preset_panel = false;

                    DynamicArray<char> tooltip {g->scratch_arena};
                    fmt::Append(tooltip, "Load preset: {}\n", name);
                    if (f->Metadata()) {
                        auto const meta = *(PresetMetadata*)f->Metadata();
                        if (meta.used_libraries.size) {
                            dyn::AppendSpan(tooltip, "Libraries: "_s);
                            auto const divider = ", "_s;
                            for (auto l : meta.used_libraries) {
                                dyn::AppendSpan(tooltip, l);
                                dyn::AppendSpan(tooltip, divider);
                            }
                            dyn::Resize(tooltip, tooltip.size - divider.size);
                            dyn::AppendSpan(tooltip, "\n"_s);
                        }
                    }
                    dyn::TrimWhitespace(tooltip);
                    Tooltip(g, imgui_id, r, tooltip);
                }
                count++;
            }
        }
        if (is_root) break;
        f = f->Next();
    }
    return clicked_preset_file;
}

void PresetBrowser::DoAllPresetFolders() {
    if (!listing.listing) return;
    auto root = listing.listing->MasterRoot();

    auto initial_folder_hash = g->plugin.preset_browser_filters.selected_folder_hash;

    if (persistent_data.scroll_to_show_current_preset) {
        if (current_preset && root) {
            if (!current_preset->IsDecendentOf(selected_folder)) {
                g->plugin.preset_browser_filters.selected_folder_hash = root->Hash();
                selected_folder = root;
            }
        }
    }

    int indent = 1;
    f32 ypos = preset_panel_xgap;
    if (auto folder_clicked_on = DoPresetFolderRecurse(root, ypos, indent); folder_clicked_on != nullptr)
        g->plugin.preset_browser_filters.selected_folder_hash = folder_clicked_on->Hash();

    if (initial_folder_hash != g->plugin.preset_browser_filters.selected_folder_hash) folder_changed = true;
}

void PresetBrowser::PopulateRowsAndCols(
    DirectoryListing::Entry const* f,
    DynamicArray<Array<FileBrowserGUIItem, k_preset_browser_num_columns>>& rows,
    bool is_root) {
    int count = 0;
    while (f) {
        if (f->HasChildren()) {
            PopulateRowsAndCols(f->FirstChild(), rows, false);
        } else if (f->IsFile()) {
            if (EntryMatchesSearchFilter(*f,
                                         *listing.listing,
                                         g->plugin.preset_browser_filters.search_filter,
                                         selected_folder)) {
                int const column = count % k_preset_browser_num_columns;

                if (column == 0)
                    dyn::Append(rows, Array<FileBrowserGUIItem, k_preset_browser_num_columns> {});
                Last(rows)[(usize)column] = {f};

                count++;
            }
        }
        if (is_root) break;
        f = f->Next();
    }
}

DirectoryListing::Entry const*
PresetBrowser::HandleKeyPresses(DirectoryListing::Entry const* current_selected_folder) {
    if (imgui.GetTextInput()) return {};

    bool const left = g->gui_platform.IsKeyPressed(KeyCodeLeftArrow);
    bool const right = g->gui_platform.IsKeyPressed(KeyCodeRightArrow);
    bool const up = g->gui_platform.IsKeyPressed(KeyCodeUpArrow);
    bool const down = g->gui_platform.IsKeyPressed(KeyCodeDownArrow);

    if (left || right || up || down) {
        DynamicArray<Array<FileBrowserGUIItem, k_preset_browser_num_columns>> rows {g->scratch_arena};
        PopulateRowsAndCols(current_selected_folder, rows, true);

        if (!current_preset && rows.size)
            return rows[0][0].f;
        else if (!rows.size)
            return nullptr;

        usize curr_row {};
        usize curr_col {};
        bool found = false;
        for (auto const row_ind : Range(rows.size)) {
            auto const& col = rows[row_ind];
            for (auto const col_ind : Range(col.size)) {
                auto const& item = col[col_ind];
                if (current_preset == item.f) {
                    curr_row = row_ind;
                    curr_col = col_ind;
                    found = true;
                }
            }
        }
        ASSERT(found);

        DirectoryListing::Entry const* preset_to_load = nullptr;
        if (left) {
            if (curr_col != 0) {
                preset_to_load = rows[curr_row][curr_col - 1].f;
            } else if (curr_row != 0) {
                auto row = curr_row - 1;
                auto col = k_preset_browser_num_columns - 1 + 1;
                do {
                    col--;
                    preset_to_load = rows[row][(usize)col].f;
                } while (preset_to_load == nullptr && col != 0);
            }
        }
        if (right) {
            if (curr_col != k_preset_browser_num_columns - 1) {
                preset_to_load = rows[curr_row][curr_col + 1].f;
                if (!preset_to_load && curr_row != (rows.size - 1)) preset_to_load = rows[curr_row + 1][0].f;
            } else if (curr_row != (rows.size - 1)) {
                preset_to_load = rows[curr_row + 1][0].f;
            }
        }
        if (up) {
            if (curr_row != 0) {
                auto row = curr_row;
                do {
                    row--;
                    preset_to_load = rows[row][curr_col].f;
                } while (preset_to_load == nullptr && row != 0);
            }
        }
        if (down) {
            if (curr_row != rows.size - 1) {
                auto row = curr_row;
                do {
                    row++;
                    preset_to_load = rows[row][curr_col].f;
                } while (preset_to_load == nullptr && row != (rows.size - 1));
            }
        }

        if (preset_to_load) persistent_data.scroll_to_show_current_preset = true;

        return preset_to_load;
    }
    return nullptr;
}

void PresetBrowser::DoAllPresetFiles() {
    f32 ypos = preset_panel_xgap;

    if (listing.is_loading) {
        // IMPROVE: it's not ideal so show loading here - it should always present rather than just at the top
        // of the list
        auto const heading_font = g->mada;
        imgui.graphics->context->PushFont(heading_font);
        labels::Label(g,
                      {GetAndIncrementRect(false, ypos, 0)},
                      "Loading...",
                      labels::PresetBrowserFolder(imgui));
        imgui.graphics->context->PopFont();
        imgui.RedrawAtIntervalSeconds(g->redraw_counter, 0.5);
    }

    if (!listing.listing) return;

    auto const selected_preset_folder =
        listing.listing->Find(g->plugin.preset_browser_filters.selected_folder_hash);

    int count = 0;

    DirectoryListing::Entry const* preset_to_load = nullptr;

    if (auto preset_navigated_to = HandleKeyPresses(selected_preset_folder))
        preset_to_load = preset_navigated_to;

    if (auto preset_clicked_on = DoPresetFilesRecurse(
            selected_preset_folder,
            selected_preset_folder->Path(),
            ypos,
            count,
            preset_to_load ? preset_to_load->Hash() : (current_preset ? current_preset->Hash() : 0),
            true)) {
        preset_to_load = preset_clicked_on;
    }
    // add a bit of whitespace at the bottom of the listing
    ypos += preset_button_height;
    imgui.GetRegisteredAndConvertedRect(GetAndIncrementRect(false, ypos, 0));

    if (preset_to_load) LoadPresetFromFile(g->plugin, preset_to_load->Path());
}

void PresetBrowser::DoPresetBrowserPanel(Rect const mid_panel_r) {
    if (persistent_data.show_preset_panel) {
        g->gui_platform.gui_update_requirements.wants_just_arrow_keys = true;

        if (DoOverlayClickableBackground(g)) g->preset_browser_data.show_preset_panel = false;

        //
        //
        //
        {
            auto const width = editor::GetSize(imgui, UiSizeId::PresetWidth);
            auto const height = editor::GetSize(imgui, UiSizeId::PresetHeight);
            auto const y_offs = editor::GetSize(imgui, UiSizeId::PresetYOffset);
            imgui.BeginWindow(wnd_settings,
                              {mid_panel_r.CentreX() - width / 2,
                               mid_panel_r.CentreY() - height / 2 - y_offs,
                               width,
                               height},
                              "PresetPanel");
        }

        auto const gap_above_heading = editor::GetSize(imgui, UiSizeId::PresetHeadingPadT);
        auto const heading_height = editor::GetSize(imgui, UiSizeId::PresetHeadingHeight);
        auto const gap_below_heading = editor::GetSize(imgui, UiSizeId::PresetHeadingPadB);
        auto const gap_left_heading = editor::GetSize(imgui, UiSizeId::PresetHeadingPadL);

        if (DoCloseButtonForCurrentWindow(g,
                                          "Close the preset browser panel",
                                          buttons::BrowserIconButton(imgui).WithLargeIcon())) {
            persistent_data.show_preset_panel = false;
        }

        f32 panel_ypos = gap_above_heading;
        {
            {
                imgui.graphics->context->PushFont(g->mada);
                labels::Label(
                    g,
                    {gap_left_heading, panel_ypos, imgui.Width() - gap_left_heading, heading_height},
                    "Floe Presets",
                    labels::BrowserHeading(imgui));
                imgui.graphics->context->PopFont();
            }

            LayID load_file_button;
            auto& lay = g->layout;
            DEFER { lay.Reset(); };
            {
                auto const pad_t = editor::GetSize(imgui, UiSizeId::PresetTopControlsPadT);
                auto const pad_r = editor::GetSize(imgui, UiSizeId::PresetTopControlsPadR);
                auto const h = editor::GetSize(imgui, UiSizeId::PresetTopControlsHeight);
                auto const preset_btn_w = editor::GetSize(imgui, UiSizeId::PresetTopControlsButtonWidth);

                auto root = lay.CreateRootItem((LayScalar)imgui.Width(),
                                               (LayScalar)imgui.Height(),
                                               LAY_ROW | LAY_END);
                lay.SetTopMargin(root, (LayScalar)(gap_above_heading + pad_t));

                load_file_button = lay.CreateChildItem(root, preset_btn_w, h, LAY_TOP);
                lay.SetRightMargin(load_file_button, pad_r);
                lay.PerformLayout();
            }

            {
                Rect const load_file_r {lay.GetRect(load_file_button)};
                auto const load_file_id = imgui.GetID("load");
                if (buttons::Button(g,
                                    load_file_id,
                                    load_file_r,
                                    "Load from File",
                                    buttons::PresetsBrowserPopupButton(imgui))) {
                    g->OpenDialog(DialogType::LoadPreset);
                }
                Tooltip(g, load_file_id, load_file_r, "Load an external preset from a file"_s);
            }
        }
        panel_ypos += heading_height + gap_below_heading;

        //
        //
        //
        auto const table_title_h = editor::GetSize(imgui, UiSizeId::PresetSectionHeadingHeight);
        auto const files_panel_width = imgui.Width() - preset_folders_panel_width;

        imgui.BeginWindow(
            FloeWindowSettings(
                imgui,
                [&](IMGUI_DRAW_WINDOW_BG_ARGS) {
                    auto const r = window->unpadded_bounds;
                    auto const rounding = editor::GetSize(imgui, UiSizeId::CornerRounding);

                    imgui.graphics->AddRectFilled(r.Min(),
                                                  r.Min() + f32x2 {r.w, table_title_h},
                                                  GMC(BrowserTopRowBack),
                                                  rounding,
                                                  1 | 2);

                    imgui.graphics->AddRectFilled(r.Min() + f32x2 {0, table_title_h},
                                                  imgui.Min() + f32x2 {preset_folders_panel_width, r.h},
                                                  GMC(PresetBrowserFoldersBack),
                                                  rounding,
                                                  8);

                    imgui.graphics->AddRectFilled(r.Min() + f32x2 {preset_folders_panel_width, table_title_h},
                                                  r.Max(),
                                                  GMC(PresetBrowserFilesBack),
                                                  rounding,
                                                  8);

                    imgui.graphics->AddRect(r.Min(), r.Max(), GMC(BrowserBorderRect), rounding);

                    auto const line_col = GMC(BrowserSectionHeadingLine);
                    imgui.graphics->AddLine(r.Min() + f32x2 {preset_folders_panel_width, 0},
                                            r.Min() + f32x2 {preset_folders_panel_width, r.h},
                                            line_col);

                    imgui.graphics->AddLine(r.Min() + f32x2 {0, table_title_h},
                                            r.Min() + f32x2 {r.w, table_title_h},
                                            line_col);
                }),
            {0, panel_ypos, imgui.Width(), imgui.Height() - panel_ypos},
            "Preset Folders");
        DEFER { imgui.EndWindow(); };

        labels::Label(g,
                      {0, 0, preset_folders_panel_width, table_title_h},
                      "Filter By Folder",
                      labels::PresetSectionHeading(imgui));

        {
            imgui.BeginWindow(FloeWindowSettings(imgui, [](IMGUI_DRAW_WINDOW_BG_ARGS) {}),
                              {preset_folders_panel_width, 0, files_panel_width, table_title_h},
                              "files heading");
            DEFER { imgui.EndWindow(); };

            {
                auto& lay = g->layout;

                auto const heading_h = table_title_h;
                auto root = lay.CreateRootItem((LayScalar)files_panel_width, (LayScalar)heading_h, LAY_ROW);

                auto title = lay.CreateChildItem(root, 1, 1, LAY_FILL);

                LayID search;
                {
                    auto const h = editor::GetSize(imgui, UiSizeId::PresetSearchHeight);
                    auto const w = editor::GetSize(imgui, UiSizeId::PresetSearchWidth);
                    auto const pad_r = editor::GetSize(imgui, UiSizeId::PresetSearchPadR);
                    search = lay.CreateChildItem(root, w, h, LAY_VCENTER);
                    lay.SetRightMargin(search, pad_r);
                }

                LayID random;
                {
                    auto const size = editor::GetSize(imgui, UiSizeId::PresetRandomButtonSize);
                    auto const pad_r = editor::GetSize(imgui, UiSizeId::PresetRandomButtonPadR);
                    random = lay.CreateChildItem(root, size, size, LAY_VCENTER);
                    lay.SetRightMargin(random, pad_r);
                }

                lay.PerformLayout();

                labels::Label(g, title, "Presets", labels::PresetSectionHeading(imgui));

                {
                    auto const search_r = lay.GetRect(search);

                    auto settings = imgui::DefTextInput();
                    settings.draw = [](IMGUI_DRAW_TEXT_INPUT_ARGS) {
                        auto const rounding = editor::GetSize(imgui, UiSizeId::CornerRounding);
                        imgui.graphics->AddRectFilled(r.Min(), r.Max(), GMC(BrowserSearchBack), rounding);

                        if (result->HasSelection()) {
                            auto selection_r = result->GetSelectionRect();
                            imgui.graphics->AddRectFilled(selection_r.Min(),
                                                          selection_r.Max(),
                                                          GMC(BrowserSearchSelection));
                        }

                        if (result->show_cursor) {
                            auto cursor_r = result->GetCursorRect();
                            imgui.graphics->AddRectFilled(cursor_r.Min(),
                                                          cursor_r.Max(),
                                                          GMC(BrowserSearchCursor));
                        }

                        auto col = GMC(BrowserSearchText);
                        if (!text.size) {
                            text = "Search folders/presets...";
                            col = GMC(BrowserSearchTextInactive);
                        }

                        imgui.graphics->AddText(result->GetTextPos(), col, text);
                    };
                    settings.select_all_on_first_open = false;
                    auto const search_text_input =
                        imgui.TextInput(settings,
                                        search_r,
                                        imgui.GetID("search"),
                                        g->plugin.preset_browser_filters.search_filter);

                    if (search_text_input.buffer_changed)
                        dyn::Assign(g->plugin.preset_browser_filters.search_filter, search_text_input.text);

                    auto const icon_r = search_r.CutLeft(search_r.w - search_r.h);
                    if (g->plugin.preset_browser_filters.search_filter.size) {
                        auto const icon_id = imgui.GetID("clear");
                        if (buttons::Button(g,
                                            icon_id,
                                            icon_r,
                                            ICON_FA_TIMES_CIRCLE,
                                            buttons::BrowserIconButton(imgui))) {
                            dyn::Clear(g->plugin.preset_browser_filters.search_filter);
                        }
                        Tooltip(g, icon_id, icon_r, "Clear the search text"_s);
                    } else {
                        buttons::FakeButton(g, icon_r, ICON_FA_SEARCH, buttons::BrowserIconButton(imgui));
                    }
                }

                {
                    auto const rand_id = imgui.GetID("rand");
                    auto const rand_r = lay.GetRect(random);
                    if (buttons::Button(g,
                                        rand_id,
                                        rand_r,
                                        ICON_FA_RANDOM,
                                        buttons::BrowserIconButton(imgui))) {
                        LoadPresetFromListing(g->plugin,
                                              PresetRandomiseCriteria {g->plugin.preset_browser_filters},
                                              listing);
                    }
                    Tooltip(g,
                            rand_id,
                            rand_r,
                            "Load a random preset based on the current folder filters and search results"_s);
                }

                lay.Reset();
            }
        }

        auto scrollable_window_settings = FloeWindowSettings(imgui, [](IMGUI_DRAW_WINDOW_BG_ARGS) {});
        scrollable_window_settings.draw_routine_scrollbar = PopupWindowSettings(imgui).draw_routine_scrollbar;
        scrollable_window_settings.pad_bottom_right = {preset_panel_xgap, 0};
        scrollable_window_settings.pad_top_left = {preset_panel_xgap, 0};
        scrollable_window_settings.scrollbar_padding_top = preset_panel_xgap / 2;

        {
            imgui.BeginWindow(scrollable_window_settings,
                              {0, table_title_h, preset_folders_panel_width, imgui.Height() - table_title_h},
                              "Folders");
            DoAllPresetFolders();
            imgui.EndWindow();
        }

        {
            imgui.BeginWindow(scrollable_window_settings,
                              {preset_folders_panel_width,
                               table_title_h,
                               files_panel_width,
                               imgui.Height() - table_title_h},
                              "Files");
            if (folder_changed && !persistent_data.scroll_to_show_current_preset)
                imgui.SetYScroll(imgui.CurrentWindow(), 0);
            DoAllPresetFiles();
            imgui.EndWindow();
        }

        imgui.EndWindow();
    }
}
