// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_modal_windows.hpp"

#include <IconsFontAwesome5.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/paths.hpp"

#include "framework/gui_frame.hpp"
#include "framework/gui_live_edit.hpp"
#include "gui.hpp"
#include "gui/framework/draw_list.hpp"
#include "gui/gui_button_widgets.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_label_widgets.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"
#include "layer_processor.hpp"
#include "plugin.hpp"
#include "plugin_instance.hpp"
#include "presets_folder.hpp"
#include "sample_library_server.hpp"
#include "settings/settings_file.hpp"
#include "settings/settings_filesystem.hpp"
#include "settings/settings_gui.hpp"

static imgui::Id IdForModal(ModalWindowType type) { return (imgui::Id)(ToInt(type) + 1000); }

static void DoMultilineText(Gui* g, String text, f32& y_pos) {
    auto& imgui = g->imgui;
    auto font = imgui.graphics->context->CurrentFont();
    auto const line_height = imgui.graphics->context->CurrentFontSize();

    // IMPORTANT: if the string is very long, it needs to be word-wrapped manually by including newlines in
    // the text. This is necessary because our text rendering system is bad at doing huge amounts of
    // word-wrapping. It still renders text that isn't visible unless there's no word-wrapping, in which case
    // it's does skip rendering off-screen text.
    f32 const wrap_width = text.size < 10000 ? imgui.Width() : 0.0f;

    auto const size = draw::GetTextSize(font, text, wrap_width);

    auto const text_r = imgui.GetRegisteredAndConvertedRect({0, y_pos, size.x, size.y});
    imgui.graphics->AddText(font,
                            font->font_size_no_scale,
                            text_r.pos,
                            LiveCol(imgui, UiColMap::PopupItemText),
                            text,
                            wrap_width);

    y_pos += size.y + line_height / 2;
}

static bool DoButton(Gui* g, String button_text, f32& y_pos, f32 x_offset) {
    auto& imgui = g->imgui;
    auto const line_height = imgui.graphics->context->CurrentFontSize();
    auto const rounding = LiveSize(g->imgui, UiSizeId::CornerRounding);
    auto const padding = line_height / 3;

    auto const size = draw::GetTextSize(imgui.graphics->context->CurrentFont(), button_text, imgui.Width()) +
                      f32x2 {line_height, line_height / 2};
    auto const button_r = imgui.GetRegisteredAndConvertedRect({x_offset, y_pos, size.x, size.y});
    auto const id = imgui.GetID(button_text);

    auto const result =
        imgui.ButtonBehavior(button_r, id, {.left_mouse = true, .triggers_on_mouse_up = true});

    imgui.graphics->AddRectFilled(button_r,
                                  !imgui.IsHot(id) ? LiveCol(imgui, UiColMap::ModalWindowButtonBack)
                                                   : LiveCol(imgui, UiColMap::ModalWindowButtonBackHover),
                                  rounding);
    imgui.graphics->AddRect(button_r, LiveCol(imgui, UiColMap::ModalWindowButtonOutline), rounding);
    imgui.graphics->AddTextJustified(button_r.ReducedHorizontally(padding),
                                     button_text,
                                     LiveCol(imgui, UiColMap::ModalWindowButtonText),
                                     TextJustification::Centred);

    y_pos += line_height;
    return result;
}

static void DoLabelLine(imgui::Context& imgui, f32& y_pos, String label, String value) {
    auto const line_height = imgui.graphics->context->CurrentFontSize();
    auto const text_r = imgui.GetRegisteredAndConvertedRect({0, y_pos, imgui.Width(), line_height});
    imgui.graphics->AddTextJustified(text_r,
                                     label,
                                     LiveCol(imgui, UiColMap::PopupItemText),
                                     TextJustification::CentredLeft);
    imgui.graphics->AddTextJustified(text_r,
                                     value,
                                     LiveCol(imgui, UiColMap::PopupItemText),
                                     TextJustification::CentredRight);
    y_pos += line_height;
}

static void
DoHeading(Gui* g, f32& y_pos, String str, TextJustification justification = TextJustification::CentredLeft) {
    auto& imgui = g->imgui;
    auto const window_title_h = LiveSize(imgui, UiSizeId::ModalWindowTitleH);
    auto const window_title_gap_y = LiveSize(imgui, UiSizeId::ModalWindowTitleGapY);

    imgui.graphics->context->PushFont(g->mada);
    DEFER { imgui.graphics->context->PopFont(); };
    auto const r = imgui.GetRegisteredAndConvertedRect({0, y_pos, imgui.Width(), window_title_h});
    g->imgui.graphics->AddTextJustified(r, str, LiveCol(imgui, UiColMap::PopupItemText), justification);

    y_pos += window_title_h + window_title_gap_y;
}

static bool DoWindowCloseButton(Gui* g) {
    if (DoCloseButtonForCurrentWindow(g,
                                      "Close this window",
                                      buttons::BrowserIconButton(g->imgui).WithLargeIcon())) {
        g->imgui.CloseTopPopupOnly();
        return true;
    }
    return false;
}

static Rect ModalRect(imgui::Context const& imgui, UiSizeId width_id, UiSizeId height_id) {
    auto const size = f32x2 {LiveSize(imgui, width_id), LiveSize(imgui, height_id)};
    Rect r;
    r.pos = imgui.frame_input.window_size.ToFloat2() / 2 - size / 2; // centre
    r.size = size;
    return r;
}

static void DoErrorsModal(Gui* g) {
    g->frame_input.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;

    auto const r = ModalRect(imgui, UiSizeId::ErrorWindowWidth, UiSizeId::ErrorWindowHeight);
    auto const settings = ModalWindowSettings(g->imgui);

    auto font = imgui.graphics->context->CurrentFont();

    if (imgui.BeginWindowPopup(settings, IdForModal(ModalWindowType::LoadError), r, "ErrorModal")) {
        f32 y_pos = 0;
        auto text_style = labels::ErrorWindowLabel(imgui);

        auto const error_window_gap_after_desc = LiveSize(imgui, UiSizeId::ErrorWindowGapAfterDesc);
        auto const error_window_divider_spacing_y = LiveSize(imgui, UiSizeId::ErrorWindowDividerSpacingY);

        // title
        DoHeading(g, y_pos, "Errors");

        // new error list
        int num_errors = 0;
        {

            for (auto errors :
                 Array {&g->plugin.error_notifications, &g->plugin.shared_data.error_notifications}) {
                for (auto it = errors->items.begin(); it != errors->items.end(); ++it) {
                    auto e_ptr = it->TryRetain();
                    if (!e_ptr) continue;
                    DEFER { it->Release(); };
                    auto& e = *e_ptr;

                    imgui.PushID((int)e.id);
                    DEFER { imgui.PopID(); };

                    // title
                    {
                        imgui.graphics->context->PushFont(g->mada);
                        auto const error_window_item_h = LiveSize(imgui, UiSizeId::ErrorWindowItemH);
                        labels::Label(g,
                                      {0, y_pos, imgui.Width(), (f32)error_window_item_h},
                                      e.title,
                                      text_style);
                        imgui.graphics->context->PopFont();

                        y_pos += (f32)error_window_item_h;
                    }

                    // desc
                    {
                        DynamicArray<char> error_text {g->scratch_arena};
                        if (e.message.size) {
                            dyn::AppendSpan(error_text, e.message);
                            dyn::Append(error_text, '\n');
                        }
                        if (e.error_code) fmt::Append(error_text, "\n{}", *e.error_code);

                        auto const max_width = imgui.Width() * 0.95f;
                        auto const size = draw::GetTextSize(font, error_text, max_width);
                        auto desc_r = Rect {0, y_pos, size.x, size.y};
                        imgui.RegisterAndConvertRect(&desc_r);
                        imgui.graphics->AddText(font,
                                                font->font_size_no_scale,
                                                desc_r.pos,
                                                text_style.main_cols.reg,
                                                error_text,
                                                max_width);
                        y_pos += size.y + (f32)error_window_gap_after_desc;
                    }

                    // buttons
                    if (DoButton(g, "Dismiss", y_pos, 0)) {
                        errors->RemoveError(e.id);
                        continue;
                    }

                    // divider line
                    if (it->next.Load(LoadMemoryOrder::Relaxed) != nullptr) {
                        y_pos += (f32)error_window_gap_after_desc;
                        auto line_r = Rect {0, y_pos, imgui.Width(), 1};
                        imgui.RegisterAndConvertRect(&line_r);
                        imgui.graphics->AddLine(line_r.Min(), line_r.Max(), text_style.main_cols.reg);
                        y_pos += (f32)error_window_divider_spacing_y;
                    }

                    ++num_errors;
                }
            }
        }

        // Add space to the bottom of the scroll window
        imgui.GetRegisteredAndConvertedRect({0, y_pos, 1, imgui.graphics->context->CurrentFontSize()});

        if (!num_errors) imgui.ClosePopupToLevel(0);

        imgui.EndWindow();
    }
}

static void DoMetricsModal(Gui* g) {
    auto a = &g->plugin;
    g->frame_input.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;

    auto const r = ModalRect(imgui, UiSizeId::MetricsWindowWidth, UiSizeId::MetricsWindowHeight);
    auto const settings = ModalWindowSettings(g->imgui);

    if (imgui.BeginWindowPopup(settings, IdForModal(ModalWindowType::Metrics), r, "MetricsModal")) {
        DEFER { imgui.EndWindow(); };
        DoWindowCloseButton(g);
        f32 y_pos = 0;
        DoHeading(g, y_pos, "Metrics");

        auto& sample_library_server = g->plugin.shared_data.sample_library_server;
        DoLabelLine(imgui,
                    y_pos,
                    "Number of active voices:",
                    fmt::Format(g->scratch_arena,
                                "{}",
                                a->processor.voice_pool.num_active_voices.Load(LoadMemoryOrder::Relaxed)));
        DoLabelLine(imgui,
                    y_pos,
                    "Memory:",
                    fmt::Format(g->scratch_arena, "{} MB", MegabytesUsedBySamples(*a)));
        DoLabelLine(
            imgui,
            y_pos,
            "Memory (all instances):",
            fmt::Format(g->scratch_arena,
                        "{} MB",
                        sample_library_server.total_bytes_used_by_samples.Load(LoadMemoryOrder::Relaxed) /
                            (1024 * 1024)));
        DoLabelLine(imgui,
                    y_pos,
                    "Num Loaded Instruments:",
                    fmt::Format(g->scratch_arena,
                                "{}",
                                sample_library_server.num_insts_loaded.Load(LoadMemoryOrder::Relaxed)));
        DoLabelLine(imgui,
                    y_pos,
                    "Num Loaded Samples:",
                    fmt::Format(g->scratch_arena,
                                "{}",
                                sample_library_server.num_samples_loaded.Load(LoadMemoryOrder::Relaxed)));
    }
}

static void DoAboutModal(Gui* g) {
    g->frame_input.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;

    auto const r = ModalRect(imgui, UiSizeId::AboutWindowWidth, UiSizeId::AboutWindowHeight);
    auto const settings = ModalWindowSettings(g->imgui);

    if (imgui.BeginWindowPopup(settings, IdForModal(ModalWindowType::About), r, "AboutModal")) {
        DEFER { imgui.EndWindow(); };
        DoWindowCloseButton(g);
        f32 y_pos = 0;
        DoHeading(g, y_pos, "About");

        DoLabelLine(imgui, y_pos, "Name:", PRODUCT_NAME);

#if PRODUCTION_BUILD
        char const* release_mode = "";
#else
        char const* release_mode = " Debug";
#endif
        DoLabelLine(imgui,
                    y_pos,
                    "Version:",
                    fmt::Format(g->scratch_arena, "{}{}", FLOE_VERSION_STRING, release_mode));
        DoLabelLine(imgui,
                    y_pos,
                    "Compiled Date:",
                    fmt::Format(g->scratch_arena, "{}, {}", __DATE__, __TIME__));
    }
}

static void DoLoadingOverlay(Gui* g) {
    g->frame_input.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;

    auto const r = ModalRect(imgui, UiSizeId::LoadingOverlayBoxWidth, UiSizeId::LoadingOverlayBoxHeight);
    auto const settings = ModalWindowSettings(g->imgui);

    if (g->plugin.pending_state_change ||
        FetchOrRescanPresetsFolder(g->plugin.shared_data.preset_listing,
                                   RescanMode::DontRescan,
                                   g->settings.settings.filesystem.extra_presets_scan_folders,
                                   nullptr)
            .is_loading) {
        imgui.BeginWindow(settings, r, "LoadingModal");
        DEFER { imgui.EndWindow(); };

        f32 y_pos = 0;
        DoHeading(g, y_pos, "Loading...", TextJustification::Centred);
    }
}

static void DoInstrumentInfoModal(Gui* g) {
    g->frame_input.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;

    auto const r = ModalRect(imgui, UiSizeId::InfoWindowWidth, UiSizeId::InfoWindowHeight);
    auto const settings = ModalWindowSettings(g->imgui);

    if (imgui.BeginWindowPopup(settings, IdForModal(ModalWindowType::InstInfo), r, "InstInfo")) {
        DEFER { imgui.EndWindow(); };
        DoWindowCloseButton(g);
        f32 y_pos = 0;

        DoHeading(g, y_pos, fmt::Format(g->scratch_arena, "{} - Info", g->inst_info_title));
        for (auto& i : g->inst_info)
            DoLabelLine(imgui, y_pos, i.title, i.info);
    }
}

static void DoInstallWizardModal(Gui* g) {
    g->frame_input.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto const settings = ModalWindowSettings(g->imgui);
    auto const r = ModalRect(imgui, UiSizeId::InstallWizardWindowWidth, UiSizeId::InstallWizardWindowHeight);

    if (imgui.BeginWindowPopup(settings, IdForModal(ModalWindowType::InstallWizard), r, "Install")) {
        DEFER { imgui.EndWindow(); };
        f32 y_pos = 0;
        DoHeading(g, y_pos, "Install Libraries");
        DoWindowCloseButton(g);

        DoMultilineText(
            g,
            "Floe can extract and install sample library packages.\nThese are ZIP files that end with \"floe.zip.\"",
            y_pos);
        if (DoButton(g, "Choose Files", y_pos, 0)) g->OpenDialog(DialogType::InstallPackage);
    }
}

static void DoSettingsModal(Gui* g) {
    g->frame_input.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto const r = ModalRect(imgui, UiSizeId::SettingsWindowWidth, UiSizeId::SettingsWindowHeight);
    auto const settings = ModalWindowSettings(g->imgui);

    if (imgui.BeginWindowPopup(settings, IdForModal(ModalWindowType::Settings), r, "Settings")) {
        DEFER { imgui.EndWindow(); };
        f32 y_pos = 0;
        DoHeading(g, y_pos, "Settings");
        DoWindowCloseButton(g);

        auto subwindow_settings =
            FloeWindowSettings(imgui, [](imgui::Context const& imgui, imgui::Window* w) {
                imgui.graphics->AddRectFilled(w->unpadded_bounds.Min(),
                                              w->unpadded_bounds.Max(),
                                              LiveCol(imgui, UiColMap::PopupWindowBack));
            });
        subwindow_settings.draw_routine_scrollbar = settings.draw_routine_scrollbar;
        imgui.BeginWindow(subwindow_settings, {0, y_pos, imgui.Width(), imgui.Height() - y_pos}, "inner");
        DEFER { imgui.EndWindow(); };
        y_pos = 0;

        auto const line_height = imgui.graphics->context->CurrentFontSize();
        auto const left_col_width = line_height * 10;
        auto const right_col_width = imgui.Width() - left_col_width;
        ASSERT(right_col_width > line_height);
        auto const path_gui_height = line_height * 1.5f;
        auto const path_gui_spacing = line_height / 3;
        auto const rounding = LiveSize(g->imgui, UiSizeId::CornerRounding);

        auto do_toggle_button = [&](String title, bool& state) {
            bool result = false;
            if (buttons::Toggle(g,
                                {left_col_width, y_pos, right_col_width, line_height},
                                state,
                                title,
                                buttons::SettingsWindowButton(imgui))) {
                g->settings.tracking.changed = true;
                result = true;
            }

            y_pos += line_height * 1.5f;
            return result;
        };

        auto do_divider = [&](String title) {
            auto const div_r =
                imgui.GetRegisteredAndConvertedRect({0, y_pos, imgui.Width(), line_height * 2});
            g->imgui.graphics->AddTextJustified(div_r,
                                                title,
                                                LiveCol(imgui, UiColMap::SettingsWindowMainText),
                                                TextJustification::Left,
                                                TextOverflowType::ShowDotsOnRight);
            auto const line_y = div_r.y + line_height * 1.1f;
            g->imgui.graphics->AddLine({div_r.x, line_y},
                                       {div_r.Right(), line_y},
                                       LiveCol(imgui, UiColMap::SettingsWindowMainText));
            y_pos += div_r.h + line_height * 0.1f;
        };

        auto do_lhs_title = [&](String text) {
            auto const title_r =
                imgui.GetRegisteredAndConvertedRect({0, y_pos, imgui.Width(), line_height * 2});
            g->imgui.graphics->AddTextJustified(title_r,
                                                text,
                                                LiveCol(imgui, UiColMap::SettingsWindowMainText),
                                                TextJustification::Left,
                                                TextOverflowType::ShowDotsOnRight);
        };

        auto do_icon_button = [&imgui, g](Rect r, String icon, String tooltip) {
            auto const id = imgui.GetID(icon);
            bool const clicked =
                imgui.ButtonBehavior(r, id, {.left_mouse = true, .triggers_on_mouse_up = true});
            g->frame_input.graphics_ctx->PushFont(g->icons);
            g->imgui.graphics->AddTextJustified(r,
                                                icon,
                                                !imgui.IsHot(id)
                                                    ? LiveCol(imgui, UiColMap::SettingsWindowIconButton)
                                                    : LiveCol(imgui, UiColMap::SettingsWindowIconButtonHover),
                                                TextJustification::CentredLeft,
                                                TextOverflowType::AllowOverflow,
                                                0.9f);
            g->frame_input.graphics_ctx->PopFont();

            Tooltip(g, id, r, tooltip, true);
            return clicked;
        };

        do_divider("Appearance");

        {
            do_lhs_title("GUI size");

            Optional<int> width_change {};
            auto box_r =
                imgui.GetRegisteredAndConvertedRect({left_col_width, y_pos, right_col_width, line_height});
            if (do_icon_button(rect_cut::CutLeft(box_r, line_height),
                               ICON_FA_MINUS_SQUARE,
                               "Decrease GUI size"))
                width_change = -110;
            rect_cut::CutLeft(box_r, line_height / 3.0f);
            if (do_icon_button(rect_cut::CutLeft(box_r, line_height),
                               ICON_FA_PLUS_SQUARE,
                               "Increase GUI size"))
                width_change = 110;

            if (width_change) {
                auto const new_width = (int)g->settings.settings.gui.window_width + *width_change;
                if (new_width > 0 && new_width < UINT16_MAX)
                    gui_settings::SetWindowSize(g->settings.settings.gui,
                                                g->settings.tracking,
                                                (u16)new_width);
            }

            y_pos += line_height * 1.5f;
        }

        do_toggle_button("Show tooltips", g->settings.settings.gui.show_tooltips);

        {
            bool& state = g->settings.settings.gui.show_keyboard;
            if (do_toggle_button("Show keyboard", state))
                gui_settings::SetShowKeyboard(g->settings.settings.gui, g->settings.tracking, state);
        }

        do_toggle_button("High contrast GUI", g->settings.settings.gui.high_contrast_gui);

        y_pos += line_height;
        do_divider("Folders");

        auto do_rhs_subheading = [&](String text) {
            auto const info_text_r =
                imgui.GetRegisteredAndConvertedRect({left_col_width, y_pos, right_col_width, line_height});
            g->imgui.graphics->AddTextJustified(info_text_r,
                                                text,
                                                LiveCol(imgui, UiColMap::SettingsWindowDullText),
                                                TextJustification::Left,
                                                TextOverflowType::ShowDotsOnRight);
            y_pos += line_height * 1.5f;
        };

        struct FolderEdits {
            Optional<String> remove {};
            bool add {};
        };

        auto do_scan_folder_gui = [&](String title,
                                      String subheading,
                                      Span<String> extra_paths,
                                      Span<String> always_scanned_paths) {
            imgui.PushID(title);
            DEFER { imgui.PopID(); };

            FolderEdits result {};
            do_lhs_title(title);
            do_rhs_subheading(subheading);
            auto const box_r = imgui.GetRegisteredAndConvertedRect(
                {left_col_width,
                 y_pos,
                 right_col_width,
                 path_gui_height * (f32)Max(1u, (u32)(extra_paths.size + always_scanned_paths.size))});

            g->imgui.graphics->AddRectFilled(box_r,
                                             LiveCol(imgui, UiColMap::SettingsWindowPathBackground),
                                             rounding);

            u32 pos = 0;
            for (auto paths : Array {always_scanned_paths, extra_paths}) {
                Optional<u32> remove_index {};
                for (auto [index, path] : Enumerate<u32>(paths)) {
                    imgui.PushID(pos);
                    DEFER { imgui.PopID(); };
                    auto const path_r = Rect {box_r.x,
                                              box_r.y + ((f32)pos * path_gui_height),
                                              right_col_width,
                                              path_gui_height};
                    auto reduced_path_r = path_r.ReducedHorizontally(path_gui_spacing);

                    if (paths.data == extra_paths.data) {
                        auto const del_button_r = rect_cut::CutRight(reduced_path_r, line_height);
                        rect_cut::CutRight(reduced_path_r, path_gui_spacing);
                        if (do_icon_button(del_button_r, ICON_FA_TIMES, "Remove")) remove_index = index;
                    }

                    auto const open_button_r = rect_cut::CutRight(reduced_path_r, line_height);
                    rect_cut::CutRight(reduced_path_r, path_gui_spacing);
                    if (do_icon_button(open_button_r, ICON_FA_EXTERNAL_LINK_ALT, "Open folder"))
                        OpenFolderInFileBrowser(path);

                    g->imgui.graphics->AddTextJustified(reduced_path_r,
                                                        path,
                                                        LiveCol(imgui, UiColMap::SettingsWindowMainText),
                                                        TextJustification::CentredLeft,
                                                        TextOverflowType::ShowDotsOnLeft);
                    ++pos;
                }

                if (remove_index) {
                    // IMPROVE: show an 'are you sure?' window
                    result.remove = paths[*remove_index];
                }
            }

            y_pos += box_r.h + line_height / 3.0f;

            DoButton(g, "Add", y_pos, left_col_width);
            y_pos += line_height * 0.5f;

            return result;
        };

        {
            auto edits = do_scan_folder_gui(
                "Library scan-folders",
                "Folders that contain libraries (scanned recursively)",
                g->settings.settings.filesystem.extra_libraries_scan_folders,
                g->plugin.shared_data.paths.always_scanned_folders[ToInt(ScanFolderType::Libraries)]);

            if (edits.remove)
                filesystem_settings::RemoveScanFolder(g->settings, ScanFolderType::Libraries, *edits.remove);
            if (edits.add) g->OpenDialog(DialogType::AddNewLibraryScanFolder);
        }
        y_pos += line_height * 1.5f;

        {
            auto edits = do_scan_folder_gui(
                "Preset scan-folders",
                "Folders that contain presets (scanned recursively)",
                g->settings.settings.filesystem.extra_presets_scan_folders,
                g->plugin.shared_data.paths.always_scanned_folders[ToInt(ScanFolderType::Presets)]);

            if (edits.remove)
                filesystem_settings::RemoveScanFolder(g->settings, ScanFolderType::Presets, *edits.remove);
            if (edits.add) g->OpenDialog(DialogType::AddNewPresetsScanFolder);
        }

        // Add whitespace at bottom of scrolling
        imgui.GetRegisteredAndConvertedRect({0, y_pos, imgui.Width(), line_height});
    }
}

static void DoLicencesModal(Gui* g) {
#include "third_party_licence_text.hpp"
    static bool open[ArraySize(k_third_party_licence_texts)];

    g->frame_input.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto const r = ModalRect(imgui, UiSizeId::LicencesWindowWidth, UiSizeId::LicencesWindowHeight);
    auto const settings = ModalWindowSettings(g->imgui);

    if (imgui.BeginWindowPopup(settings, IdForModal(ModalWindowType::Licences), r, "LicencesModal")) {
        DoWindowCloseButton(g);
        auto const h = LiveSize(imgui, UiSizeId::MenuItemHeight);
        f32 y_pos = 0;
        DoHeading(g, y_pos, "Licences");

        DoMultilineText(
            g,
            "Floe is free and open source under the GPLv3 licence. We use the following third-party libraries:",
            y_pos);

        for (auto const i : Range((int)ArraySize(k_third_party_licence_texts))) {
            auto& txt = k_third_party_licence_texts[i];
            bool state = open[i];
            bool const changed = buttons::Toggle(g,
                                                 imgui.GetID(&txt),
                                                 {0, y_pos, imgui.Width(), h},
                                                 state,
                                                 txt.name,
                                                 buttons::LicencesFoldButton(imgui));
            if (changed) {
                open[i] = !open[i];
                for (auto const j : Range((int)ArraySize(k_third_party_licence_texts))) {
                    if (j == i) continue;
                    open[j] = false;
                }
            }
            y_pos += h;

            if (open[i]) {
                DoMultilineText(g, txt.copyright, y_pos);
                DoMultilineText(g, txt.licence, y_pos);
            }
        }

        imgui.EndWindow();
    }
}

static bool AnyModalOpen(imgui::Context& imgui) {
    for (auto const i : Range(ToInt(ModalWindowType::Count)))
        if (imgui.IsPopupOpen(IdForModal((ModalWindowType)i))) return true;
    return false;
}

// ===============================================================================================================

void OpenModalIfNotAlready(imgui::Context& imgui, ModalWindowType type) {
    if (!imgui.IsPopupOpen(IdForModal(type))) {
        imgui.ClosePopupToLevel(0);
        imgui.OpenPopup(IdForModal(type));
    }
}

void DoModalWindows(Gui* g) {
    if (AnyModalOpen(g->imgui)) DoOverlayClickableBackground(g);
    DoErrorsModal(g);
    DoMetricsModal(g);
    DoAboutModal(g);
    DoLoadingOverlay(g);
    DoInstrumentInfoModal(g);
    DoInstallWizardModal(g);
    DoSettingsModal(g);
    DoLicencesModal(g);
}
