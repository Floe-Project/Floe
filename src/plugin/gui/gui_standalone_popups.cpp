// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_standalone_popups.hpp"

#include <icons-fa/IconsFontAwesome5.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common/constants.hpp"
#include "common/paths.hpp"
#include "framework/gui_live_edit.hpp"
#include "framework/gui_platform.hpp"
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
#include "sample_library_loader.hpp"
#include "settings/settings_file.hpp"
#include "settings/settings_filesystem.hpp"
#include "settings/settings_gui.hpp"

imgui::Id GetStandaloneID(StandaloneWindows type) { return (imgui::Id)(type + 666); }

bool IsAnyStandloneOpen(imgui::Context& imgui) {
    for (auto const i : Range(ToInt(StandaloneWindowsCount)))
        if (imgui.IsPopupOpen(GetStandaloneID((StandaloneWindows)i))) return true;
    return false;
}

void OpenStandalone(imgui::Context& imgui, StandaloneWindows type) {
    if (!imgui.IsPopupOpen(GetStandaloneID(type))) {
        imgui.ClosePopupToLevel(0);
        imgui.OpenPopup(GetStandaloneID(type));
    }
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

static void StandalonePopupHeading(Gui* g,
                                   f32& y_pos,
                                   String str,
                                   TextJustification justification = TextJustification::CentredLeft) {
    auto& imgui = g->imgui;
    auto error_window_title_h = LiveSize(imgui, UiSizeId::ErrorWindowTitleH);
    auto error_window_title_gap_y = LiveSize(imgui, UiSizeId::ErrorWindowTitleGapY);

    imgui.graphics->context->PushFont(g->mada);
    auto const r = imgui.GetRegisteredAndConvertedRect({0, y_pos, imgui.Width(), error_window_title_h});
    g->imgui.graphics->AddTextJustified(r, str, LiveCol(imgui, UiColMap::PopupItemText), justification);
    imgui.graphics->context->PopFont();
    y_pos += error_window_title_h + error_window_title_gap_y;
}

bool DoStandaloneCloseButton(Gui* g) {
    if (DoCloseButtonForCurrentWindow(g,
                                      "Close this window",
                                      buttons::BrowserIconButton(g->imgui).WithLargeIcon())) {
        g->imgui.CloseTopPopupOnly();
        return true;
    }
    return false;
}

void DoStandaloneErrorGUI(Gui* g) {
    auto& plugin = g->plugin;

    auto const host = plugin.host;
    auto const floe_ext = (FloeClapExtensionHost const*)host.get_extension(&host, k_floe_clap_extension_id);
    if (!floe_ext) return;

    g->gui_platform.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->gui_platform.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto platform = &g->gui_platform;
    static bool error_window_open = true;

    bool const there_is_an_error =
        floe_ext->standalone_midi_device_error || floe_ext->standalone_audio_device_error;
    if (error_window_open && there_is_an_error) {
        auto settings = imgui::DefWindow();
        settings.flags |= imgui::WindowFlags_AutoHeight | imgui::WindowFlags_AutoWidth;
        imgui.BeginWindow(settings, {0, 0, 200, 0}, "StandaloneErrors");
        f32 y_pos = 0;
        if (floe_ext->standalone_midi_device_error) {
            imgui.Text(imgui::DefText(), {0, y_pos, 100, 20}, "No MIDI input");
            y_pos += 20;
        }
        if (floe_ext->standalone_audio_device_error) {
            imgui.Text(imgui::DefText(), {0, y_pos, 100, 20}, "No audio devices");
            y_pos += 20;
        }
        if (imgui.Button(imgui::DefButton(), {0, y_pos, 100, 20}, imgui.GetID("closeErr"), "Close"))
            error_window_open = false;
        imgui.EndWindow();
    }
    if (floe_ext->standalone_midi_device_error) {
        platform->gui_update_requirements.wants_keyboard_input = true;
        if (platform->key_shift) {
            auto gen_midi_message = [&](bool on, u7 key) {
                if (on)
                    plugin.processor.events_for_audio_thread.Push(
                        GuiNoteClicked({.key = key, .velocity = 0.7f}));
                else
                    plugin.processor.events_for_audio_thread.Push(GuiNoteClickReleased({.key = key}));
            };

            struct Key {
                KeyCodes key;
                u7 midi_key;
            };
            static Key const keys[] = {
                {KeyCodeLeftArrow, 60},
                {KeyCodeRightArrow, 63},
                {KeyCodeUpArrow, 80},
                {KeyCodeDownArrow, 45},
            };

            for (auto& i : keys) {
                if (platform->KeyJustWentDown(i.key)) gen_midi_message(true, i.midi_key);
                if (platform->KeyJustWentUp(i.key)) gen_midi_message(false, i.midi_key);
            }
        }
    }
}

void DoErrorsStandalone(Gui* g) {
    g->gui_platform.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->gui_platform.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto popup_w = LiveSize(imgui, UiSizeId::ErrorWindowWidth);
    auto popup_h = LiveSize(imgui, UiSizeId::ErrorWindowHeight);

    auto settings = StandalonePopupSettings(g->imgui);

    Rect r;
    r.x = (f32)(int)((f32)g->gui_platform.window_size.width / 2 - popup_w / 2);
    r.y = (f32)(int)((f32)g->gui_platform.window_size.height / 2 - popup_h / 2);
    r.w = popup_w;
    r.h = popup_h;

    auto font = imgui.graphics->context->CurrentFont();

#define GUI_SIZE(cat, n, v, u) [[maybe_unused]] const auto cat##n = LiveSize(imgui, UiSizeId::cat##n);
#include SIZES_DEF_FILENAME
#undef GUI_SIZE

    if (imgui.BeginWindowPopup(settings, GetStandaloneID(StandaloneWindowsLoadError), r, "ErrorModal")) {
        f32 y_pos = 0;
        auto text_style = labels::ErrorWindowLabel(imgui);

        // title
        StandalonePopupHeading(g, y_pos, "Errors");

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
                        labels::Label(g,
                                      {0, y_pos, imgui.Width(), (f32)ErrorWindowItemH},
                                      e.title,
                                      text_style);
                        imgui.graphics->context->PopFont();

                        y_pos += (f32)ErrorWindowItemH;
                    }

                    // desc
                    {
                        DynamicArray<char> error_text {g->scratch_arena};
                        if (e.message.size) {
                            dyn::AppendSpan(error_text, e.message);
                            dyn::Append(error_text, '\n');
                        }
                        if (e.error_code) fmt::Append(error_text, "{}", *e.error_code);

                        auto max_width = imgui.Width() * 0.95f;
                        auto size = draw::GetTextSize(font, error_text, max_width);
                        auto desc_r = Rect {0, y_pos, size.x, size.y};
                        imgui.RegisterAndConvertRect(&desc_r);
                        imgui.graphics->AddText(font,
                                                font->font_size_no_scale,
                                                desc_r.pos,
                                                text_style.main_cols.reg,
                                                error_text,
                                                max_width);
                        y_pos += size.y + (f32)ErrorWindowGapAfterDesc;
                    }

                    // buttons
                    {
                        f32 x_pos = 0;
                        auto btn_w = (f32)ErrorWindowButtonW;

                        auto btn_sets = imgui::DefButton();
                        btn_sets.draw = [](IMGUI_DRAW_BUTTON_ARGS) {
                            auto col = LiveCol(imgui, UiColMap::ErrorWindowButtonBack);
                            if (imgui.IsHot(id)) col = LiveCol(imgui, UiColMap::ErrorWindowButtonBackHover);
                            if (imgui.IsActive(id))
                                col = LiveCol(imgui, UiColMap::ErrorWindowButtonBackActive);
                            auto const rounding = LiveSize(imgui, UiSizeId::CornerRounding);
                            imgui.graphics->AddRectFilled(r.Min(), r.Max(), col, rounding);

                            auto text_r = r;
                            text_r.x += text_r.h * 0.2f;
                            imgui.graphics->AddTextJustified(text_r,
                                                             str,
                                                             LiveCol(imgui, UiColMap::ErrorWindowButtonText),
                                                             TextJustification::CentredLeft);
                        };

                        bool button_added = false;

                        auto do_button = [&](String name) {
                            button_added = true;
                            bool const clicked =
                                imgui.Button(btn_sets, {x_pos, y_pos, btn_w, (f32)ErrorWindowButtonH}, name);
                            x_pos += btn_w + (f32)ErrorWindowButtonGapX;
                            return clicked;
                        };

                        if (do_button("Dismiss")) {
                            errors->RemoveError(e.id);
                            continue;
                        }
                    }

                    y_pos += (f32)ErrorWindowButtonH;

                    // divider line
                    if (it->next.Load(MemoryOrder::Relaxed) != nullptr) {
                        y_pos += (f32)ErrorWindowGapAfterDesc;
                        auto line_r = Rect {0, y_pos, imgui.Width(), 1};
                        imgui.RegisterAndConvertRect(&line_r);
                        imgui.graphics->AddLine(line_r.Min(), line_r.Max(), text_style.main_cols.reg);
                        y_pos += (f32)ErrorWindowDividerSpacingY;
                    }

                    ++num_errors;
                }
            }
        }

        // Add space to the bottom of the scroll window
        imgui.GetRegisteredAndConvertedRect({0, y_pos, 1, (f32)ErrorWindowButtonH});

        if (!num_errors) imgui.ClosePopupToLevel(0);

        imgui.EndWindow();
    }
}

void DoMetricsStandalone(Gui* g) {
    auto a = &g->plugin;
    g->gui_platform.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->gui_platform.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto popup_w = LiveSize(imgui, UiSizeId::MetricsWindowWidth);
    auto popup_h = LiveSize(imgui, UiSizeId::MetricsWindowHeight);

    auto settings = StandalonePopupSettings(g->imgui);

    Rect r;
    r.x = (f32)(int)((f32)g->gui_platform.window_size.width / 2 - popup_w / 2);
    r.y = (f32)(int)((f32)g->gui_platform.window_size.height / 2 - popup_h / 2);
    r.w = popup_w;
    r.h = popup_h;

    if (imgui.BeginWindowPopup(settings, GetStandaloneID(StandaloneWindowsMetrics), r, "MetricsModal")) {
        DoStandaloneCloseButton(g);
        f32 y_pos = 0;
        StandalonePopupHeading(g, y_pos, "Metrics");

        auto& sample_library_loader = g->plugin.shared_data.sample_library_loader;
        DoLabelLine(imgui,
                    y_pos,
                    "Number of active voices:",
                    fmt::Format(g->scratch_arena, "{}", a->processor.voice_pool.num_active_voices.Load()));
        DoLabelLine(imgui,
                    y_pos,
                    "Memory:",
                    fmt::Format(g->scratch_arena, "{} MB", MegabytesUsedBySamples(*a)));
        DoLabelLine(imgui,
                    y_pos,
                    "Memory (all instances):",
                    fmt::Format(g->scratch_arena,
                                "{} MB",
                                sample_library_loader.total_bytes_used_by_samples.Load() / (1024 * 1024)));
        DoLabelLine(imgui,
                    y_pos,
                    "Num Loaded Instruments:",
                    fmt::Format(g->scratch_arena, "{}", sample_library_loader.num_insts_loaded.Load()));
        DoLabelLine(imgui,
                    y_pos,
                    "Num Loaded Samples:",
                    fmt::Format(g->scratch_arena, "{}", sample_library_loader.num_samples_loaded.Load()));

        imgui.EndWindow();
    }
}

void DoAboutStandalone(Gui* g) {
    g->gui_platform.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->gui_platform.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto popup_w = LiveSize(imgui, UiSizeId::AboutWindowWidth);
    auto popup_h = LiveSize(imgui, UiSizeId::AboutWindowHeight);

    auto settings = StandalonePopupSettings(g->imgui);

    Rect r;
    r.x = (f32)(int)((f32)g->gui_platform.window_size.width / 2 - popup_w / 2);
    r.y = (f32)(int)((f32)g->gui_platform.window_size.height / 2 - popup_h / 2);
    r.w = popup_w;
    r.h = popup_h;

    if (imgui.BeginWindowPopup(settings, GetStandaloneID(StandaloneWindowsAbout), r, "AboutModal")) {
        DoStandaloneCloseButton(g);
        f32 y_pos = 0;
        StandalonePopupHeading(g, y_pos, "About");

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

        imgui.EndWindow();
    }
}

void DoLoadingOverlay(Gui* g) {
    g->gui_platform.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->gui_platform.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;

    auto popup_w = LiveSize(imgui, UiSizeId::LoadingOverlayBoxWidth);
    auto popup_h = LiveSize(imgui, UiSizeId::LoadingOverlayBoxHeight);

    auto settings = StandalonePopupSettings(g->imgui);

    Rect r;
    r.x = (f32)(int)((f32)g->gui_platform.window_size.width / 2 - popup_w / 2);
    r.y = (f32)(int)((f32)g->gui_platform.window_size.height / 2 - popup_h / 2);
    r.w = popup_w;
    r.h = popup_h;

    if (g->plugin.preset_is_loading ||
        FetchOrRescanPresetsFolder(g->plugin.shared_data.preset_listing,
                                   RescanMode::DontRescan,
                                   g->settings.settings.filesystem.extra_presets_scan_folders,
                                   nullptr)
            .is_loading) {
        imgui.BeginWindow(settings, r, "LoadingModal");
        f32 y_pos = 0;
        StandalonePopupHeading(g, y_pos, "Loading...", TextJustification::Centred);
        imgui.EndWindow();
    }
}

void DoInstrumentInfoStandalone(Gui* g) {
    g->gui_platform.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->gui_platform.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto popup_w = LiveSize(imgui, UiSizeId::InfoWindowWidth);
    auto popup_h = LiveSize(imgui, UiSizeId::InfoWindowHeight);

    auto settings = StandalonePopupSettings(g->imgui);

    Rect r;
    r.x = (f32)(int)((f32)g->gui_platform.window_size.width / 2 - popup_w / 2);
    r.y = (f32)(int)((f32)g->gui_platform.window_size.height / 2 - popup_h / 2);
    r.w = popup_w;
    r.h = popup_h;

    if (imgui.BeginWindowPopup(settings, GetStandaloneID(StandaloneWindowsInstInfo), r, "InstInfo")) {
        DoStandaloneCloseButton(g);
        f32 y_pos = 0;

        StandalonePopupHeading(g, y_pos, fmt::Format(g->scratch_arena, "{} - Info", g->inst_info_title));
        for (auto& i : g->inst_info)
            DoLabelLine(imgui, y_pos, i.title, i.info);

        imgui.EndWindow();
    }
}

void DoSettingsStandalone(Gui* g) {
    g->gui_platform.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->gui_platform.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto popup_w = LiveSize(imgui, UiSizeId::SettingsWindowWidth);
    auto popup_h = LiveSize(imgui, UiSizeId::SettingsWindowHeight);

    auto settings = StandalonePopupSettings(g->imgui);

    Rect r;
    r.x = (f32)(int)((f32)g->gui_platform.window_size.width / 2 - popup_w / 2);
    r.y = (f32)(int)((f32)g->gui_platform.window_size.height / 2 - popup_h / 2);
    r.w = popup_w;
    r.h = popup_h;

    if (imgui.BeginWindowPopup(settings, GetStandaloneID(StandaloneWindowsSettings), r, "Settings")) {
        f32 y_pos = 0;
        StandalonePopupHeading(g, y_pos, "Settings");
        DoStandaloneCloseButton(g);

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
            g->gui_platform.graphics_ctx->PushFont(g->icons);
            g->imgui.graphics->AddTextJustified(r,
                                                icon,
                                                !imgui.IsHot(id)
                                                    ? LiveCol(imgui, UiColMap::SettingsWindowIconButton)
                                                    : LiveCol(imgui, UiColMap::SettingsWindowIconButtonHover),
                                                TextJustification::CentredLeft,
                                                TextOverflowType::AllowOverflow,
                                                0.9f);
            g->gui_platform.graphics_ctx->PopFont();

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

            {
                auto const text = "Add"_s;
                auto const size =
                    draw::GetTextSize(imgui.graphics->context->CurrentFont(), text, imgui.Width()) +
                    f32x2 {line_height, line_height / 2};
                auto const button_r =
                    imgui.GetRegisteredAndConvertedRect({left_col_width, y_pos, size.x, size.y});
                auto const id = imgui.GetID("addlib");
                if (imgui.ButtonBehavior(button_r, id, {.left_mouse = true, .triggers_on_mouse_up = true}))
                    result.add = true;
                imgui.graphics->AddRectFilled(button_r,
                                              !imgui.IsHot(id)
                                                  ? LiveCol(imgui, UiColMap::SettingsWindowButtonBack)
                                                  : LiveCol(imgui, UiColMap::SettingsWindowButtonBackHover),
                                              rounding);
                imgui.graphics->AddRect(button_r,
                                        LiveCol(imgui, UiColMap::SettingsWindowButtonOutline),
                                        rounding);
                auto const text_r = button_r.ReducedHorizontally(path_gui_spacing);
                imgui.graphics->AddTextJustified(text_r,
                                                 text,
                                                 LiveCol(imgui, UiColMap::SettingsWindowButtonText),
                                                 TextJustification::Centred);
            }

            y_pos += line_height * 1.5f;

            return result;
        };

        {
            auto edits = do_scan_folder_gui(
                "Library scan-folders",
                "Folders that contain libraries (scanned non-recursively)",
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

        imgui.EndWindow();
    }
}

static void DoMultilineText(Gui* g, String text, f32& y_pos) {
    auto& imgui = g->imgui;
    auto font = imgui.graphics->context->CurrentFont();
    auto const line_height = imgui.graphics->context->CurrentFontSize();
    auto size = draw::GetTextSize(font, text, imgui.Width());

    auto text_r = Rect {0, y_pos, size.x, size.y};
    y_pos += size.y + line_height / 2;
    imgui.RegisterAndConvertRect(&text_r);
    imgui.graphics->AddText(font,
                            font->font_size_no_scale,
                            text_r.pos,
                            LiveCol(imgui, UiColMap::PopupItemText),
                            text,
                            imgui.Width());
}

void DoLicencesStandalone(Gui* g) {
#include "third_party_licence_text.hpp"
    static bool open[ArraySize(k_third_party_licence_texts)];

    g->gui_platform.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->gui_platform.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto popup_w = LiveSize(imgui, UiSizeId::LicencesWindowWidth);
    auto popup_h = LiveSize(imgui, UiSizeId::LicencesWindowHeight);

    auto settings = StandalonePopupSettings(g->imgui);

    Rect r;
    r.x = (f32)(int)((f32)g->gui_platform.window_size.width / 2 - popup_w / 2);
    r.y = (f32)(int)((f32)g->gui_platform.window_size.height / 2 - popup_h / 2);
    r.w = popup_w;
    r.h = popup_h;

    if (imgui.BeginWindowPopup(settings, GetStandaloneID(StandaloneWindowsLicences), r, "LicencesModal")) {
        DoStandaloneCloseButton(g);
        auto h = LiveSize(imgui, UiSizeId::MenuItemHeight);
        f32 y_pos = 0;
        StandalonePopupHeading(g, y_pos, "Licences");

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
