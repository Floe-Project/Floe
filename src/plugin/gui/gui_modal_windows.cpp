// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_modal_windows.hpp"

#include <IconsFontAwesome5.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common_infrastructure/paths.hpp"

#include "engine/engine.hpp"
#include "engine/package_installation.hpp"
#include "gui.hpp"
#include "gui/gui_button_widgets.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_framework/draw_list.hpp"
#include "gui_framework/gui_frame.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_label_widgets.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"
#include "presets/presets_folder.hpp"
#include "processor/layer_processor.hpp"
#include "sample_lib_server/sample_library_server.hpp"
#include "settings/settings_file.hpp"
#include "settings/settings_filesystem.hpp"
#include "settings/settings_gui.hpp"

static imgui::Id IdForModal(ModalWindowType type) { return (imgui::Id)(ToInt(type) + 1000); }

static void DoMultilineText(Gui* g,
                            String text,
                            f32& y_pos,
                            f32 x_offset = 0,
                            UiColMap col_map = UiColMap::PopupItemText) {
    auto& imgui = g->imgui;
    auto font = imgui.graphics->context->CurrentFont();
    auto const line_height = imgui.graphics->context->CurrentFontSize();

    // IMPORTANT: if the string is very long, it needs to be word-wrapped manually by including newlines in
    // the text. This is necessary because our text rendering system is bad at doing huge amounts of
    // word-wrapping. It still renders text that isn't visible unless there's no word-wrapping, in which case
    // it's does skip rendering off-screen text.
    f32 const wrap_width = text.size < 10000 ? Max(imgui.Width() - x_offset, 0.0f) : 0.0f;

    auto const size = draw::GetTextSize(font, text, wrap_width);

    auto const text_r = imgui.GetRegisteredAndConvertedRect({.xywh {x_offset, y_pos, size.x, size.y}});
    imgui.graphics
        ->AddText(font, font->font_size_no_scale, text_r.pos, LiveCol(imgui, col_map), text, wrap_width);

    y_pos += size.y + line_height / 2;
}

struct IncrementingY {
    f32& y;
};

struct DoButtonArgs {
    Optional<IncrementingY> incrementing_y;
    Optional<f32> y;
    f32 x_offset;
    bool centre_vertically;
    bool auto_width;
    f32 width;
    String tooltip;
    bool greyed_out;
    String icon;
    bool significant;
    bool insignificant;
    bool white_background;
    bool big_font;
};

static bool DoButton(Gui* g, String button_text, DoButtonArgs args) {
    auto& imgui = g->imgui;

    if (args.big_font) imgui.graphics->context->PushFont(g->mada);
    DEFER {
        if (args.big_font) imgui.graphics->context->PopFont();
    };

    auto const line_height = imgui.graphics->context->CurrentFontSize();
    auto const rounding = LiveSize(g->imgui, UiSizeId::CornerRounding);
    auto const icon_scaling = 0.8f;
    auto const icon_size = line_height * icon_scaling;
    auto const box_padding = line_height * 0.4f;
    auto const gap_between_icon_and_text = box_padding;

    auto const y_pos = args.incrementing_y ? args.incrementing_y->y : args.y.ValueOr(0);

    auto const text_width =
        draw::GetTextSize(imgui.graphics->context->CurrentFont(), button_text, imgui.Width()).x;

    auto const content_width = text_width + (args.icon.size ? icon_size + gap_between_icon_and_text : 0);

    auto const box_width = (args.auto_width) ? (content_width + box_padding * 2) : args.width;
    auto const box_height = line_height * 1.5f;

    auto x_pos = args.x_offset;
    if (args.centre_vertically) x_pos = (imgui.Width() - box_width) / 2;

    auto button_r = imgui.GetRegisteredAndConvertedRect({.xywh {x_pos, y_pos, box_width, box_height}});
    auto const id = imgui.GetID(button_text);

    bool result = false;
    if (!args.greyed_out)
        result = imgui.ButtonBehavior(button_r, id, {.left_mouse = true, .triggers_on_mouse_up = true});

    imgui.graphics->AddRectFilled(
        button_r,
        LiveCol(imgui,
                !imgui.IsHot(id)
                    ? (args.white_background ? UiColMap::PopupWindowBack : UiColMap::ModalWindowButtonBack)
                    : UiColMap::ModalWindowButtonBackHover),
        rounding);

    if (!args.greyed_out)
        imgui.graphics->AddRect(button_r,
                                LiveCol(imgui,
                                        args.significant ? UiColMap::ModalWindowButtonOutlineSignificant
                                                         : UiColMap::ModalWindowButtonOutline),
                                rounding);

    auto const required_padding = (box_width - content_width) / 2;
    rect_cut::CutLeft(button_r, required_padding);
    rect_cut::CutRight(button_r, required_padding);

    if (args.icon.size) {
        imgui.graphics->context->PushFont(g->icons);
        DEFER { imgui.graphics->context->PopFont(); };

        auto const icon_r = rect_cut::CutLeft(button_r, icon_size);
        rect_cut::CutLeft(button_r, gap_between_icon_and_text);

        imgui.graphics->AddTextJustified(icon_r,
                                         args.icon,
                                         LiveCol(imgui,
                                                 args.greyed_out ? UiColMap::ModalWindowButtonTextInactive
                                                                 : UiColMap::ModalWindowButtonIcon),
                                         TextJustification::CentredLeft,
                                         TextOverflowType::AllowOverflow,
                                         icon_scaling);
    }

    imgui.graphics->AddTextJustified(
        button_r,
        button_text,
        LiveCol(imgui,
                args.greyed_out ? UiColMap::ModalWindowButtonTextInactive
                                : (args.insignificant ? UiColMap::ModalWindowInsignificantText
                                                      : UiColMap::ModalWindowButtonText)),
        TextJustification::CentredLeft);

    if (args.tooltip.size) Tooltip(g, id, button_r, args.tooltip, true);

    if (args.incrementing_y) args.incrementing_y->y += box_height;
    return result;
}

static bool DoButton(Gui* g, String button_text, f32& y_pos, f32 x_offset) {
    return DoButton(g,
                    button_text,
                    {
                        .incrementing_y = IncrementingY {y_pos},
                        .x_offset = x_offset,
                        .auto_width = true,
                    });
}

static void DoLabelLine(imgui::Context& imgui, f32& y_pos, String label, String value) {
    auto const line_height = imgui.graphics->context->CurrentFontSize();
    auto const text_r = imgui.GetRegisteredAndConvertedRect({.xywh {0, y_pos, imgui.Width(), line_height}});
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
    auto const r = imgui.GetRegisteredAndConvertedRect({.xywh {0, y_pos, imgui.Width(), window_title_h}});
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

static Rect ModalRect(imgui::Context const& imgui, f32 width, f32 height) {
    auto const size = f32x2 {width, height};
    Rect r;
    r.pos = imgui.frame_input.window_size.ToFloat2() / 2 - size / 2; // centre
    r.size = size;
    return r;
}

static Rect ModalRect(imgui::Context const& imgui, UiSizeId width_id, UiSizeId height_id) {
    return ModalRect(imgui, LiveSize(imgui, width_id), LiveSize(imgui, height_id));
}

static void DoErrorsModal(Gui* g) {
    g->frame_input.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;

    auto const r = ModalRect(imgui, UiSizeId::ErrorWindowWidth, UiSizeId::ErrorWindowHeight);
    auto const settings = ModalWindowSettings(g->imgui);

    auto font = imgui.graphics->context->CurrentFont();

    if (imgui.BeginWindowPopup(settings, IdForModal(ModalWindowType::LoadError), r, "ErrorModal")) {
        DEFER { imgui.EndWindow(); };

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
                 Array {&g->engine.error_notifications, &g->shared_engine_systems.error_notifications}) {
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
                                      {.xywh {0, y_pos, imgui.Width(), (f32)error_window_item_h}},
                                      e.title,
                                      text_style);
                        imgui.graphics->context->PopFont();

                        y_pos += (f32)error_window_item_h;
                    }

                    // desc
                    {
                        DynamicArray<char> error_text {g->scratch_arena};
                        if (e.error_code) fmt::Append(error_text, "{u}.", *e.error_code);
                        if (e.message.size) {
                            dyn::Append(error_text, '\n');
                            dyn::AppendSpan(error_text, e.message);
                        }

                        auto const max_width = imgui.Width() * 0.95f;
                        auto const size = draw::GetTextSize(font, error_text, max_width);
                        auto desc_r = Rect {.x = 0, .y = y_pos, .w = size.x, .h = size.y};
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
                        auto line_r = Rect {.x = 0, .y = y_pos, .w = imgui.Width(), .h = 1};
                        imgui.RegisterAndConvertRect(&line_r);
                        imgui.graphics->AddLine(line_r.Min(), line_r.Max(), text_style.main_cols.reg);
                        y_pos += (f32)error_window_divider_spacing_y;
                    }

                    ++num_errors;
                }
            }
        }

        // Add space to the bottom of the scroll window
        imgui.GetRegisteredAndConvertedRect(
            {.xywh {0, y_pos, 1, imgui.graphics->context->CurrentFontSize()}});

        if (!num_errors) imgui.ClosePopupToLevel(0);
    }
}

static void DoMetricsModal(Gui* g) {
    auto a = &g->engine;
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

        auto& sample_library_server = g->shared_engine_systems.sample_library_server;
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

        DoLabelLine(imgui, y_pos, "Name:", "Floe");

        DoLabelLine(
            imgui,
            y_pos,
            "Version:",
            fmt::Format(g->scratch_arena, "{}{}", FLOE_VERSION_STRING, PRODUCTION_BUILD ? "" : " Debug"));
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

    if (g->engine.pending_state_change ||
        FetchOrRescanPresetsFolder(
            g->shared_engine_systems.preset_listing,
            RescanMode::DontRescan,
            g->settings.settings.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)],
            nullptr)
            .is_loading) {
        imgui.BeginWindow(settings, r, "LoadingModal");
        DEFER { imgui.EndWindow(); };

        f32 y_pos = 0;
        DoHeading(g, y_pos, "Loading...", TextJustification::Centred);
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
        imgui.BeginWindow(subwindow_settings,
                          {.xywh {0, y_pos, imgui.Width(), imgui.Height() - y_pos}},
                          "inner");
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
                                {.xywh {left_col_width, y_pos, right_col_width, line_height}},
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
                imgui.GetRegisteredAndConvertedRect({.xywh {0, y_pos, imgui.Width(), line_height * 2}});
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
                imgui.GetRegisteredAndConvertedRect({.xywh {0, y_pos, imgui.Width(), line_height * 2}});
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
            auto box_r = imgui.GetRegisteredAndConvertedRect(
                {.xywh {left_col_width, y_pos, right_col_width, line_height}});
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
                if (new_width > 0 && new_width <= UINT16_MAX)
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
            auto const info_text_r = imgui.GetRegisteredAndConvertedRect(
                {.xywh {left_col_width, y_pos, right_col_width, line_height}});
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

        auto do_scan_folder_gui =
            [&](String title, String subheading, Span<String> extra_paths, String always_scanned_path) {
                imgui.PushID(title);
                DEFER { imgui.PopID(); };

                FolderEdits result {};
                do_lhs_title(title);
                do_rhs_subheading(subheading);
                auto const box_r = imgui.GetRegisteredAndConvertedRect(
                    {.xywh {left_col_width,
                            y_pos,
                            right_col_width,
                            path_gui_height * (f32)Max(1u, (u32)(extra_paths.size + 1))}});

                g->imgui.graphics->AddRectFilled(box_r,
                                                 LiveCol(imgui, UiColMap::SettingsWindowPathBackground),
                                                 rounding);

                u32 pos = 0;
                for (auto paths : Array {Array {always_scanned_path}.Items(), extra_paths}) {
                    Optional<u32> remove_index {};
                    for (auto [index, path] : Enumerate<u32>(paths)) {
                        imgui.PushID(pos);
                        DEFER { imgui.PopID(); };
                        auto const path_r = Rect {.x = box_r.x,
                                                  .y = box_r.y + ((f32)pos * path_gui_height),
                                                  .w = right_col_width,
                                                  .h = path_gui_height};
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
                g->settings.settings.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)],
                g->shared_engine_systems.paths.always_scanned_folder[ToInt(ScanFolderType::Libraries)]);

            if (edits.remove)
                filesystem_settings::RemoveScanFolder(g->settings, ScanFolderType::Libraries, *edits.remove);
            if (edits.add) g->OpenDialog(DialogType::AddNewLibraryScanFolder);
        }
        y_pos += line_height * 1.5f;

        {
            auto edits = do_scan_folder_gui(
                "Preset scan-folders",
                "Folders that contain presets (scanned recursively)",
                g->settings.settings.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)],
                g->shared_engine_systems.paths.always_scanned_folder[ToInt(ScanFolderType::Presets)]);

            if (edits.remove)
                filesystem_settings::RemoveScanFolder(g->settings, ScanFolderType::Presets, *edits.remove);
            if (edits.add) g->OpenDialog(DialogType::AddNewPresetsScanFolder);
        }

        // Add whitespace at bottom of scrolling
        imgui.GetRegisteredAndConvertedRect({.xywh {0, y_pos, imgui.Width(), line_height}});
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
                                                 {.xywh {0, y_pos, imgui.Width(), h}},
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

// ===============================================================================================================
// ===============================================================================================================
// ===============================================================================================================
// ===============================================================================================================

struct Widget {
    layout::Id layout_id;
    imgui::Id imgui_id;
    bool32 is_hot : 1 = false;
    bool32 is_active : 1 = false;
    bool32 button_fired : 1 = false;
};

struct UiBuilder;

using PanelFunction = TrivialFixedSizeFunction<16, void(UiBuilder&)>;

enum class PanelType {
    Subpanel,
    Modal,
    Popup,
};

struct Subpanel {
    layout::Id id;
    imgui::Id imgui_id;
};

struct ModalPanel {
    Rect r;
    imgui::Id imgui_id;
    TrivialFixedSizeFunction<8, void()> on_close;
    bool close_on_click_outside;
    bool darken_background;
    bool disable_other_interaction;
    bool auto_height;
};

struct PopupPanel {
    layout::Id creator_layout_id;
    imgui::Id popup_imgui_id;
};

using PanelUnion = TaggedUnion<PanelType,
                               TypeAndTag<Subpanel, PanelType::Subpanel>,
                               TypeAndTag<ModalPanel, PanelType::Modal>,
                               TypeAndTag<PopupPanel, PanelType::Popup>>;

struct Panel {
    PanelFunction run;
    PanelUnion data;

    // internal, filled by the layout system
    Optional<Rect> rect {};
    Panel* next {};
    Panel* first_child {};
};

struct UiBuilder {
    enum class State {
        Layout,
        Do,
    };

    ArenaAllocator& arena;
    imgui::Context& imgui;
    Fonts& fonts;
    layout::Context& layout;
    void* user_data;

    Panel* current_panel {};
    u32 widget_counter {};

    State state {State::Layout};
    DynamicArray<Widget> widgets {arena};

    f32 const scrollbar_width = imgui.PointsToPixels(8);
    f32 const scrollbar_padding = imgui.PointsToPixels(style::k_scrollbar_rhs_space);
    imgui::DrawWindowScrollbar* const draw_scrollbar = [](IMGUI_DRAW_WINDOW_SCROLLBAR_ARGS) {
        u32 handle_col = style::Col(style::Colour::Surface1);
        if (imgui.IsHotOrActive(id)) handle_col = style::Col(style::Colour::Surface2);
        imgui.graphics->AddRectFilled(handle_rect.Min(),
                                      handle_rect.Max(),
                                      handle_col,
                                      imgui.PointsToPixels(4));
    };

    imgui::DrawWindowBackground* const draw_window = [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto const rounding = imgui.PointsToPixels(style::k_panel_rounding);
        auto r = window->unpadded_bounds;
        draw::DropShadow(imgui, r, rounding);
        imgui.graphics->AddRectFilled(r, style::Col(style::Colour::Background0), rounding);
    };

    imgui::WindowSettings const regular_window_settings {
        .scrollbar_padding = scrollbar_padding,
        .scrollbar_width = scrollbar_width,
        .draw_routine_scrollbar = draw_scrollbar,
    };

    imgui::WindowSettings const popup_settings {
        .flags =
            imgui::WindowFlags_AutoWidth | imgui::WindowFlags_AutoHeight | imgui::WindowFlags_AutoPosition,
        .pad_top_left = {1, imgui.PointsToPixels(style::k_panel_rounding)},
        .pad_bottom_right = {1, imgui.PointsToPixels(style::k_panel_rounding)},
        .scrollbar_padding = scrollbar_padding,
        .scrollbar_padding_top = 0,
        .scrollbar_width = scrollbar_width,
        .draw_routine_scrollbar = draw_scrollbar,
        .draw_routine_popup_background = draw_window,
    };

    imgui::WindowSettings const modal_window_settings {
        .flags = imgui::WindowFlags_NoScrollbarX,
        .scrollbar_padding = scrollbar_padding,
        .scrollbar_width = scrollbar_width,
        .draw_routine_scrollbar = draw_scrollbar,
        .draw_routine_window_background = draw_window,
    };
};

static void AddPanel(UiBuilder& builder, Panel panel) {
    if (builder.state == UiBuilder::State::Do) {
        auto p = builder.arena.New<Panel>(panel);
        if (builder.current_panel->first_child)
            builder.current_panel->first_child->next = p;
        else
            builder.current_panel->first_child = p;
    }
}

static void Run(UiBuilder& builder, Panel* panel) {
    if (!panel) return;

    switch (panel->data.tag) {
        case PanelType::Subpanel: {
            auto const& subpanel = panel->data.Get<Subpanel>();
            builder.imgui.BeginWindow(builder.regular_window_settings, subpanel.imgui_id, *panel->rect);
            break;
        }
        case PanelType::Modal: {
            auto const& modal = panel->data.Get<ModalPanel>();

            if (modal.disable_other_interaction) {
                imgui::WindowSettings const invis_sets {
                    .draw_routine_window_background =
                        [darken = modal.darken_background](IMGUI_DRAW_WINDOW_BG_ARGS) {
                            if (!darken) return;
                            auto r = window->unpadded_bounds;
                            imgui.graphics->AddRectFilled(r.Min(), r.Max(), 0x6c0f0d0d);
                        },
                };
                builder.imgui.BeginWindow(invis_sets, {.pos = 0, .size = builder.imgui.Size()}, "invisible");
                DEFER { builder.imgui.EndWindow(); };
                auto invis_window = builder.imgui.CurrentWindow();

                if (modal.close_on_click_outside) {
                    if (builder.imgui.IsWindowHovered(invis_window)) {
                        builder.imgui.frame_output.cursor_type = CursorType::Hand;
                        if (builder.imgui.frame_input.Mouse(MouseButton::Left).presses.size) modal.on_close();
                    }
                }
            }

            auto settings = builder.modal_window_settings;
            if (modal.auto_height) settings.flags |= imgui::WindowFlags_AutoHeight;

            builder.imgui.BeginWindow(settings, modal.imgui_id, modal.r);
            break;
        }
        case PanelType::Popup: {
            if (!builder.imgui.BeginWindowPopup(builder.popup_settings,
                                                panel->data.Get<PopupPanel>().popup_imgui_id,
                                                *panel->rect,
                                                "popup")) {
                return;
            }
            break;
        }
    }

    {
        builder.current_panel = panel;
        dyn::Clear(builder.widgets);

        builder.widget_counter = 0;
        builder.state = UiBuilder::State::Layout;
        panel->run(builder);

        layout::RunContext(builder.layout);

        builder.widget_counter = 0;
        builder.state = UiBuilder::State::Do;
        panel->run(builder);
    }

    // Fill in the rect of new panels so we can reuse the layout system.
    // New panels can be identified because they have no rect.
    for (auto p = panel->first_child; p != nullptr; p = p->next) {
        if (p->rect) continue;
        switch (p->data.tag) {
            case PanelType::Subpanel: {
                auto data = p->data.Get<Subpanel>();
                p->rect = layout::GetRect(builder.layout, data.id);
                break;
            }
            case PanelType::Modal: {
                break;
            }
            case PanelType::Popup: {
                auto data = p->data.Get<PopupPanel>();
                p->rect = layout::GetRect(builder.layout, data.creator_layout_id);
                // we now have a relative position of the creator of the popup (usually a button). We
                // need to convert it to screen space. When we run the panel, the imgui system will
                // take this button rect and find a place for the popup below/right of it.
                p->rect->pos = builder.imgui.WindowPosToScreenPos(p->rect->pos);
                break;
            }
        }
    }

    layout::ResetContext(builder.layout);

    for (auto p = panel->first_child; p != nullptr; p = p->next)
        Run(builder, p);

    builder.imgui.EndWindow();

    Run(builder, panel->next);
}

static void RunPanel(UiBuilder builder, Panel initial_panel) {
    auto panel = builder.arena.New<Panel>(initial_panel);
    Run(builder, panel);
}

enum class ActivationClickEvent : u32 { None, Down, Up, Count };

enum class TextAlignX : u32 { Left, Centre, Right, Count };
enum class TextAlignY : u32 { Top, Centre, Bottom, Count };

static f32x2 AlignWithin(Rect container, f32x2 size, TextAlignX align_x, TextAlignY align_y) {
    f32x2 result = container.Min();
    if (align_x == TextAlignX::Centre)
        result.x += (container.w - size.x) / 2;
    else if (align_x == TextAlignX::Right)
        result.x += container.w - size.x;

    if (align_y == TextAlignY::Centre)
        result.y += (container.h - size.y) / 2;
    else if (align_y == TextAlignY::Bottom)
        result.y += container.h - size.y;

    return result;
}

struct WidgetOptions {
    Optional<Widget> parent {};

    String text {};
    f32 font_size = 0; // zero == use default for font
    f32 wrap = 0; // zero == no wrap
    FontType font : NumBitsNeededToStore(ToInt(FontType::Count)) {FontType::Body};
    style::Colour text_fill : style::k_colour_bits = style::Colour::Text;
    style::Colour text_fill_hot : style::k_colour_bits = style::Colour::Text;
    style::Colour text_fill_active : style::k_colour_bits = style::Colour::Text;
    bool32 size_from_text : 1 = false;
    TextAlignX text_align_x : NumBitsNeededToStore(ToInt(TextAlignX::Count)) = TextAlignX::Left;
    TextAlignY text_align_y : NumBitsNeededToStore(ToInt(TextAlignY::Count)) = TextAlignY::Top;

    style::Colour background_fill : style::k_colour_bits = style::Colour::None;
    style::Colour background_fill_hot : style::k_colour_bits = style::Colour::None;
    style::Colour background_fill_active : style::k_colour_bits = style::Colour::None;
    bool32 background_fill_auto_hot_active_overlay : 1 = false;

    style::Colour border : style::k_colour_bits = style::Colour::None;
    style::Colour border_hot : style::k_colour_bits = style::Colour::None;
    style::Colour border_active : style::k_colour_bits = style::Colour::None;
    bool32 border_auto_hot_active_overlay : 1 = false;

    // 4 bits, clockwise from top-left: top-left, top-right, bottom-right, bottom-left, set using 0b0001 etc.
    u32 round_background_corners : 4 = 0;

    MouseButton activate_on_click_button
        : NumBitsNeededToStore(ToInt(MouseButton::Count)) = MouseButton::Left;
    ActivationClickEvent activation_click_event
        : NumBitsNeededToStore(ToInt(ActivationClickEvent::Count)) = ActivationClickEvent::None;
    bool32 parent_dictates_hot_and_active : 1 = false;
    u8 extra_margin_for_mouse_events = 0;

    layout::ItemOptions layout {};
};

Widget CreateWidget(UiBuilder& builder, WidgetOptions const& options) {
    auto const widget_index = builder.widget_counter++;

    switch (builder.state) {
        case UiBuilder::State::Layout: {
            auto const widget = Widget {
                .layout_id = layout::CreateItem(
                    builder.layout,
                    ({
                        layout::ItemOptions layout = options.layout;

                        if (options.parent) [[likely]]
                            layout.parent = options.parent->layout_id;

                        layout.size =
                            Max(builder.imgui.pixels_per_point * layout.size, f32x2(layout::k_fill_parent));

                        layout.margins.lrtb *= builder.imgui.pixels_per_point;
                        layout.contents_gap *= builder.imgui.pixels_per_point;
                        layout.contents_padding.lrtb *= builder.imgui.pixels_per_point;

                        if (options.size_from_text) {
                            auto font = builder.fonts[ToInt(options.font)];
                            auto const font_size = options.font_size != 0
                                                       ? builder.imgui.PointsToPixels(options.font_size)
                                                       : font->font_size_no_scale;
                            layout.size = font->CalcTextSizeA(font_size, FLT_MAX, options.wrap, options.text);
                        }

                        layout;
                    })),
                .imgui_id = widget_index,
            };

            dyn::Append(builder.widgets, widget);

            return widget;
        }
        case UiBuilder::State::Do: {
            auto& widget = builder.widgets[widget_index];
            auto const rect = builder.imgui.GetRegisteredAndConvertedRect(
                layout::GetRect(builder.layout, widget.layout_id));
            auto const mouse_rect =
                rect.Expanded(builder.imgui.PointsToPixels(options.extra_margin_for_mouse_events));

            if (options.activation_click_event != ActivationClickEvent::None) {
                imgui::ButtonFlags button_flags {
                    .left_mouse = options.activate_on_click_button == MouseButton::Left,
                    .right_mouse = options.activate_on_click_button == MouseButton::Right,
                    .middle_mouse = options.activate_on_click_button == MouseButton::Middle,
                    .triggers_on_mouse_down = options.activation_click_event == ActivationClickEvent::Down,
                    .triggers_on_mouse_up = options.activation_click_event == ActivationClickEvent::Up,
                };
                widget.imgui_id = builder.imgui.GetID((usize)widget_index);
                widget.button_fired = builder.imgui.ButtonBehavior(mouse_rect, widget.imgui_id, button_flags);
                widget.is_active = builder.imgui.IsActive(widget.imgui_id);
                widget.is_hot = builder.imgui.IsHot(widget.imgui_id);
            }
            bool32 const is_active =
                options.parent_dictates_hot_and_active ? options.parent->is_active : widget.is_active;
            bool32 const is_hot =
                options.parent_dictates_hot_and_active ? options.parent->is_hot : widget.is_hot;

            if (auto const background_fill = ({
                    style::Colour c {};
                    if (options.background_fill_auto_hot_active_overlay)
                        c = options.background_fill;
                    else if (is_active)
                        c = options.background_fill_active;
                    else if (is_hot)
                        c = options.background_fill_hot;
                    else
                        c = options.background_fill;
                    c;
                });
                background_fill != style::Colour::None || options.background_fill_auto_hot_active_overlay) {

                auto r = rect;
                // if we normally don't show a background, then we can assume that hot/active colours are
                // exclusively for the mouse so we should use the mouse rect
                if (options.background_fill == style::Colour::None) r = mouse_rect;

                auto const rounding = options.round_background_corners
                                          ? builder.imgui.PointsToPixels(style::k_button_rounding)
                                          : 0;

                u32 col_u32 = style::Col(background_fill);
                if (options.background_fill_auto_hot_active_overlay) {
                    if (is_hot)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_hot_white_overlay)
                                          : style::k_auto_hot_white_overlay;
                    else if (is_active)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_active_white_overlay)
                                          : style::k_auto_active_white_overlay;
                }

                builder.imgui.graphics->AddRectFilled(r, col_u32, rounding, options.round_background_corners);
            }

            if (auto const border = ({
                    style::Colour c {};
                    if (options.border_auto_hot_active_overlay)
                        c = options.border;
                    else if (is_active)
                        c = options.border_active;
                    else if (is_hot)
                        c = options.border_hot;
                    else
                        c = options.border;
                    c;
                });
                border != style::Colour::None || options.border_auto_hot_active_overlay) {

                auto r = rect;
                if (options.border == style::Colour::None) r = mouse_rect;

                auto const rounding = options.round_background_corners
                                          ? builder.imgui.PointsToPixels(style::k_button_rounding)
                                          : 0;

                u32 col_u32 = style::Col(border);
                if (options.border_auto_hot_active_overlay) {
                    if (is_hot)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_hot_white_overlay)
                                          : style::k_auto_hot_white_overlay;
                    else if (is_active)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_active_white_overlay)
                                          : style::k_auto_active_white_overlay;
                }

                builder.imgui.graphics->AddRect(r, col_u32, rounding, options.round_background_corners);
            }

            if (options.text.size) {
                auto font = builder.fonts[ToInt(options.font)];
                auto const font_size = options.font_size != 0
                                           ? builder.imgui.PointsToPixels(options.font_size)
                                           : font->font_size_no_scale;
                if (options.text_align_x != TextAlignX::Left || options.text_align_y != TextAlignY::Top) {
                    auto const text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0, options.text);
                    auto const text_pos =
                        AlignWithin(rect, text_size, options.text_align_x, options.text_align_y);
                    builder.imgui.graphics->AddText(font,
                                                    font_size,
                                                    text_pos,
                                                    style::Col(is_hot      ? options.text_fill_hot
                                                               : is_active ? options.text_fill_active
                                                                           : options.text_fill),
                                                    options.text,
                                                    options.wrap);
                } else {
                    builder.imgui.graphics->AddText(font,
                                                    font_size,
                                                    rect.pos,
                                                    style::Col(is_hot      ? options.text_fill_hot
                                                               : is_active ? options.text_fill_active
                                                                           : options.text_fill),
                                                    options.text,
                                                    options.wrap);
                }
            }

            return widget;
        }
    }

    return {};
}

static void SettingsLhsTextWidget(UiBuilder& builder, Widget parent, String text) {
    CreateWidget(builder,
                 {
                     .parent = parent,
                     .text = text,
                     .font = FontType::Body,
                     .layout {
                         .size = {style::k_settings_lhs_width,
                                  builder.imgui.PixelsToPoints(
                                      builder.fonts[ToInt(FontType::Body)]->font_size_no_scale)},
                     },
                 });
}

static void SettingsRhsText(UiBuilder& builder, Widget parent, String text) {
    CreateWidget(builder,
                 {
                     .parent = parent,
                     .text = text,
                     .font = FontType::Body,
                     .text_fill = style::Colour::Subtext0,
                     .size_from_text = true,
                 });
}

static bool SettingsTextButton(UiBuilder& builder, Widget parent, String text) {
    auto const button = CreateWidget(
        builder,
        {
            .parent = parent,
            .background_fill = style::Colour::Background2,
            .background_fill_auto_hot_active_overlay = true,
            .round_background_corners = 0b1111,
            .activate_on_click_button = MouseButton::Left,
            .activation_click_event = ActivationClickEvent::Up,
            .layout {
                .size = layout::k_hug_contents,
                .contents_padding = {.lr = style::k_button_padding_x, .tb = style::k_button_padding_y},
            },
        });

    CreateWidget(builder,
                 {
                     .parent = button,
                     .text = text,
                     .font = FontType::Body,
                     .size_from_text = true,
                 });

    return button.button_fired;
}

static Widget SettingsMenuButton(UiBuilder& builder, Widget parent, String text) {
    auto const button = CreateWidget(
        builder,
        {
            .parent = parent,
            .background_fill = style::Colour::Background2,
            .background_fill_auto_hot_active_overlay = true,
            .round_background_corners = 0b1111,
            .activate_on_click_button = MouseButton::Left,
            .activation_click_event = ActivationClickEvent::Up,
            .layout {
                .size = {layout::k_fill_parent, layout::k_hug_contents},
                .contents_padding = {.lr = style::k_button_padding_x, .tb = style::k_button_padding_y},
                .contents_align = layout::Alignment::Justify,
            },
        });

    CreateWidget(builder,
                 {
                     .parent = button,
                     .text = text,
                     .font = FontType::Body,
                     .size_from_text = true,
                 });

    CreateWidget(builder,
                 {
                     .parent = button,
                     .text = ICON_FA_CARET_DOWN,
                     .font = FontType::Icons,
                     .size_from_text = true,
                 });

    return button;
}

static bool SettingsCheckboxButton(UiBuilder& builder, Widget parent, String text, bool state) {
    auto const button = CreateWidget(builder,
                                     {
                                         .parent = parent,
                                         .activate_on_click_button = MouseButton::Left,
                                         .activation_click_event = ActivationClickEvent::Up,
                                         .layout {
                                             .size = {layout::k_hug_contents, layout::k_hug_contents},
                                             .contents_gap = style::k_settings_medium_gap,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Start,
                                         },
                                     });

    CreateWidget(builder,
                 {
                     .parent = button,
                     .text = state ? ICON_FA_CHECK : ""_s,
                     .font = FontType::SmallIcons,
                     .text_fill = style::Colour::Text,
                     .text_fill_hot = style::Colour::Text,
                     .text_fill_active = style::Colour::Text,
                     .text_align_x = TextAlignX::Centre,
                     .text_align_y = TextAlignY::Centre,
                     .background_fill = style::Colour::Background2,
                     .background_fill_auto_hot_active_overlay = true,
                     .border = style::Colour::Overlay0,
                     .border_auto_hot_active_overlay = true,
                     .round_background_corners = 0b1111,
                     .parent_dictates_hot_and_active = true,
                     .layout {
                         .size = style::k_settings_icon_button_size,
                     },
                 });
    CreateWidget(builder,
                 {
                     .parent = button,
                     .text = text,
                     .size_from_text = true,
                 });

    return button.button_fired;
}

static Widget SettingsRow(UiBuilder& builder, Widget parent) {
    return CreateWidget(builder,
                        {
                            .parent = parent,
                            .layout {
                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                .contents_direction = layout::Direction::Row,
                                .contents_align = layout::Alignment::Start,
                                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                            },
                        });
}

static Widget SettingsRhsColumn(UiBuilder& builder, Widget parent, f32 gap) {
    return CreateWidget(builder,
                        {
                            .parent = parent,
                            .layout {
                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                .contents_gap = gap,
                                .contents_direction = layout::Direction::Column,
                                .contents_align = layout::Alignment::Start,
                                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                            },
                        });
}

struct FolderSelectorResult {
    bool delete_pressed;
    bool open_pressed;
};
static FolderSelectorResult
SettingsFolderSelector(UiBuilder& builder, Widget parent, String path, String subtext, bool deletable) {
    auto const container = CreateWidget(builder,
                                        {
                                            .parent = parent,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_gap = style::k_settings_small_gap,
                                                .contents_direction = layout::Direction::Column,
                                                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                            },
                                        });

    auto const path_container = CreateWidget(
        builder,
        {
            .parent = container,
            .background_fill = style::Colour::Background1,
            .round_background_corners = 0b1111,
            .layout {
                .size = {layout::k_fill_parent, layout::k_hug_contents},
                .contents_padding = {.lr = style::k_button_padding_x, .tb = style::k_button_padding_y},
                .contents_direction = layout::Direction::Row,
                .contents_align = layout::Alignment::Justify,
            },
        });
    CreateWidget(builder,
                 {
                     .parent = path_container,
                     .text = path,
                     .font = FontType::Body,
                     .size_from_text = true,
                 });
    auto const icon_button_container =
        CreateWidget(builder,
                     {
                         .parent = path_container,
                         .layout {
                             .size = {layout::k_hug_contents, layout::k_hug_contents},
                             .contents_gap = style::k_settings_small_gap,
                             .contents_direction = layout::Direction::Row,
                         },
                     });

    FolderSelectorResult result {};
    if (deletable) {
        result.delete_pressed = CreateWidget(builder,
                                             {
                                                 .parent = icon_button_container,
                                                 .text = ICON_FA_TRASH_ALT,
                                                 .font = FontType::Icons,
                                                 .text_fill = style::Colour::Subtext0,
                                                 .text_fill_hot = style::Colour::Subtext0,
                                                 .text_fill_active = style::Colour::Subtext0,
                                                 .size_from_text = true,
                                                 .background_fill_auto_hot_active_overlay = true,
                                                 .round_background_corners = 0b1111,
                                                 .activate_on_click_button = MouseButton::Left,
                                                 .activation_click_event = ActivationClickEvent::Up,
                                                 .extra_margin_for_mouse_events = 2,
                                             })
                                    .button_fired;
    }
    result.open_pressed = CreateWidget(builder,
                                       {
                                           .parent = icon_button_container,
                                           .text = ICON_FA_EXTERNAL_LINK_ALT,
                                           .font = FontType::Icons,
                                           .text_fill = style::Colour::Subtext0,
                                           .text_fill_hot = style::Colour::Subtext0,
                                           .text_fill_active = style::Colour::Subtext0,
                                           .size_from_text = true,
                                           .background_fill_auto_hot_active_overlay = true,
                                           .round_background_corners = 0b1111,
                                           .activate_on_click_button = MouseButton::Left,
                                           .activation_click_event = ActivationClickEvent::Up,
                                           .extra_margin_for_mouse_events = 2,
                                       })
                              .button_fired;

    if (subtext.size) SettingsRhsText(builder, container, subtext);

    return result;
}

struct SettingsPanelContext {
    SettingsFile& settings;
    sample_lib_server::Server& sample_lib_server;
    package::InstallJobs& package_install_jobs;
    ThreadPool& thread_pool;
};

static void SetFolderSubtext(DynamicArrayBounded<char, 200>& out,
                             String dir,
                             bool is_default,
                             ScanFolderType type,
                             sample_lib_server::Server& server) {
    dyn::Clear(out);
    switch (type) {
        case ScanFolderType::Libraries: {
            u32 num_libs = 0;
            for (auto& l_node : server.libraries) {
                if (auto l = l_node.TryScoped()) {
                    if (path::IsWithinDirectory(l->lib->path, dir)) ++num_libs;
                }
            }

            if (is_default) dyn::AppendSpan(out, "Default. ");
            dyn::AppendSpan(out, "Contains ");
            if (num_libs < 1000 && out.size + 4 < out.Capacity())
                out.size += fmt::IntToString(num_libs, out.data + out.size);
            else if (num_libs)
                dyn::AppendSpan(out, "many");
            else
                dyn::AppendSpan(out, "no"_s);
            fmt::Append(out, " sample librar{}", num_libs == 1 ? "y" : "ies");
            break;
        }
        case ScanFolderType::Presets: {
            if (is_default) dyn::AppendSpan(out, "Default.");
            // IMPROVE: show number of presets in folder
            break;
        }
        case ScanFolderType::Count: break;
    }
}

static bool AddExtraScanFolderDialog(UiBuilder& builder, ScanFolderType type, bool set_as_install_location) {
    auto& context = *(SettingsPanelContext*)builder.user_data;
    Optional<String> default_folder {};
    if (auto const extra_paths = context.settings.settings.filesystem.extra_scan_folders[ToInt(type)];
        extra_paths.size)
        default_folder = extra_paths[0];

    if (auto const o = FilesystemDialog({
            .type = DialogArguments::Type::SelectFolder,
            .allocator = builder.arena,
            .title = fmt::Format(builder.arena, "Select {} Folder", ({
                                     String s {};
                                     switch (type) {
                                         case ScanFolderType::Libraries: s = "Libraries"; break;
                                         case ScanFolderType::Presets: s = "Presets"; break;
                                         case ScanFolderType::Count: PanicIfReached();
                                     }
                                     s;
                                 })),
            .default_path = default_folder,
            .filters = {},
            .parent_window = builder.imgui.frame_input.native_window,
        });
        o.HasValue()) {
        if (auto const paths = o.Value(); paths.size) {
            filesystem_settings::AddScanFolder(context.settings, type, paths[0]);
            if (set_as_install_location)
                filesystem_settings::SetInstallLocation(context.settings, type, paths[0]);
            return true;
        }
    } else {
        g_log.Error(k_gui_log_module, "Failed to create dialog: {}", o.Error());
    }
    return false;
}

static void FolderSettingsPanel(UiBuilder& builder) {
    auto& context = *(SettingsPanelContext*)builder.user_data;
    sample_lib_server::RequestScanningOfUnscannedFolders(context.sample_lib_server);

    auto const root = CreateWidget(builder,
                                   {
                                       .layout {
                                           .size = builder.imgui.PixelsToPoints(builder.imgui.Size()),
                                           .contents_padding = {.lrtb = style::k_spacing},
                                           .contents_gap = style::k_settings_large_gap,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                       },
                                   });

    for (auto const scan_folder_type : Range(ToInt(ScanFolderType::Count))) {
        auto const row = SettingsRow(builder, root);
        SettingsLhsTextWidget(builder, row, ({
                                  String s;
                                  switch ((ScanFolderType)scan_folder_type) {
                                      case ScanFolderType::Libraries: s = "Sample library folders"; break;
                                      case ScanFolderType::Presets: s = "Preset folders"; break;
                                      case ScanFolderType::Count: PanicIfReached();
                                  }
                                  s;
                              }));

        auto const rhs_column = SettingsRhsColumn(builder, row, style::k_settings_medium_gap);

        DynamicArrayBounded<char, 200> subtext_buffer {};

        {
            auto const dir = context.settings.paths.always_scanned_folder[scan_folder_type];
            SetFolderSubtext(subtext_buffer,
                             dir,
                             true,
                             (ScanFolderType)scan_folder_type,
                             context.sample_lib_server);
            if (auto const o = SettingsFolderSelector(builder, rhs_column, dir, subtext_buffer, false);
                o.open_pressed)
                OpenFolderInFileBrowser(dir);
        }

        Optional<String> to_remove {};
        for (auto const dir : context.settings.settings.filesystem.extra_scan_folders[scan_folder_type]) {
            SetFolderSubtext(subtext_buffer,
                             dir,
                             false,
                             (ScanFolderType)scan_folder_type,
                             context.sample_lib_server);
            if (auto const o = SettingsFolderSelector(builder, rhs_column, dir, subtext_buffer, true);
                o.open_pressed || o.delete_pressed) {
                if (o.open_pressed) OpenFolderInFileBrowser(dir);
                if (o.delete_pressed) to_remove = dir;
            }
        }
        if (to_remove)
            filesystem_settings::RemoveScanFolder(context.settings,
                                                  (ScanFolderType)scan_folder_type,
                                                  *to_remove);

        if (context.settings.settings.filesystem.extra_scan_folders[scan_folder_type].size !=
                k_max_extra_scan_folders &&
            SettingsTextButton(builder, rhs_column, "Add folder")) {
            AddExtraScanFolderDialog(builder, (ScanFolderType)scan_folder_type, false);
        }
    }
}

static void InstallLocationMenu(UiBuilder& builder, ScanFolderType scan_folder_type) {
    auto& context = *(SettingsPanelContext*)builder.user_data;

    sample_lib_server::RequestScanningOfUnscannedFolders(context.sample_lib_server);

    auto const root = CreateWidget(builder,
                                   {
                                       .layout {
                                           .size = layout::k_hug_contents,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                       },
                                   });

    DynamicArrayBounded<char, 200> subtext_buffer {};

    auto const menu_item = [&](String path, String subtext) {
        auto const item = CreateWidget(builder,
                                       {
                                           .parent = root,
                                           .background_fill_auto_hot_active_overlay = true,
                                           .activate_on_click_button = MouseButton::Left,
                                           .activation_click_event = ActivationClickEvent::Up,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_direction = layout::Direction::Row,
                                           },
                                       });

        if (item.button_fired) {
            filesystem_settings::SetInstallLocation(context.settings, scan_folder_type, path);
            builder.imgui.CloseTopPopupOnly();
        }

        auto const current_install_location =
            context.settings.settings.filesystem.install_location[ToInt(scan_folder_type)];

        CreateWidget(builder,
                     {
                         .parent = item,
                         .text = path == current_install_location ? String(ICON_FA_CHECK) : "",
                         .font = FontType::Icons,
                         .text_fill = style::Colour::Subtext0,
                         .layout {
                             .size = style::k_settings_icon_button_size,
                             .margins {.l = style::k_menu_item_padding_x},
                         },

                     });

        auto const text_container =
            CreateWidget(builder,
                         {
                             .parent = item,
                             .layout {
                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                 .contents_padding = {.lr = style::k_menu_item_padding_x,
                                                      .tb = style::k_menu_item_padding_y},
                                 .contents_direction = layout::Direction::Column,
                                 .contents_align = layout::Alignment::Start,
                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                             },
                         });
        CreateWidget(builder,
                     {
                         .parent = text_container,
                         .text = path,
                         .font = FontType::Body,
                         .size_from_text = true,
                     });
        CreateWidget(builder,
                     {
                         .parent = text_container,
                         .text = subtext,
                         .text_fill = style::Colour::Subtext0,
                         .size_from_text = true,
                     });
    };

    {
        auto const dir = context.settings.paths.always_scanned_folder[ToInt(scan_folder_type)];
        SetFolderSubtext(subtext_buffer, dir, true, scan_folder_type, context.sample_lib_server);
        menu_item(dir, subtext_buffer);
    }

    for (auto const dir : context.settings.settings.filesystem.extra_scan_folders[ToInt(scan_folder_type)]) {
        SetFolderSubtext(subtext_buffer, dir, false, scan_folder_type, context.sample_lib_server);
        menu_item(dir, subtext_buffer);
    }

    CreateWidget(builder,
                 {
                     .parent = root,
                     .background_fill = style::Colour::Overlay0,
                     .layout {
                         .size = {layout::k_fill_parent, builder.imgui.PixelsToPoints(1)},
                         .margins {.tb = style::k_menu_item_padding_y},
                     },
                 });

    auto const add_button = CreateWidget(builder,
                                         {
                                             .parent = root,
                                             .background_fill_auto_hot_active_overlay = true,
                                             .activate_on_click_button = MouseButton::Left,
                                             .activation_click_event = ActivationClickEvent::Up,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                 .contents_padding = {.l = style::k_menu_item_padding_x * 2 +
                                                                           style::k_settings_icon_button_size,
                                                                      .r = style::k_menu_item_padding_x,
                                                                      .tb = style::k_menu_item_padding_y},
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::Start,
                                             },
                                         });
    CreateWidget(builder,
                 {
                     .parent = add_button,
                     .text = "Add folder",
                     .size_from_text = true,
                 });

    if (add_button.button_fired)
        if (AddExtraScanFolderDialog(builder, scan_folder_type, true)) builder.imgui.CloseTopPopupOnly();
}

static void PackagesSettingsPanel(UiBuilder& builder) {
    auto& context = *(SettingsPanelContext*)builder.user_data;
    auto const root = CreateWidget(builder,
                                   {
                                       .layout {
                                           .size = builder.imgui.PixelsToPoints(builder.imgui.Size()),
                                           .contents_padding = {.lrtb = style::k_spacing},
                                           .contents_gap = style::k_settings_medium_gap,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                       },
                                   });

    for (auto const scan_folder_type : Range(ToInt(ScanFolderType::Count))) {
        auto const row = SettingsRow(builder, root);
        SettingsLhsTextWidget(builder, row, ({
                                  String s;
                                  switch ((ScanFolderType)scan_folder_type) {
                                      case ScanFolderType::Libraries:
                                          s = "Sample library install folder";
                                          break;
                                      case ScanFolderType::Presets: s = "Preset install folder"; break;
                                      case ScanFolderType::Count: PanicIfReached();
                                  }
                                  s;
                              }));

        auto const popup_id = builder.imgui.GetID(scan_folder_type);

        String menu_text = context.settings.settings.filesystem.install_location[scan_folder_type];
        if (auto const default_dir = context.settings.paths.always_scanned_folder[scan_folder_type];
            menu_text == default_dir) {
            menu_text = "Default";
        }

        auto const btn = SettingsMenuButton(builder, row, menu_text);
        if (btn.button_fired) builder.imgui.OpenPopup(popup_id, btn.imgui_id);

        AddPanel(builder,
                 Panel {
                     .run =
                         [scan_folder_type](UiBuilder& builder) {
                             InstallLocationMenu(builder, (ScanFolderType)scan_folder_type);
                         },
                     .data =
                         PopupPanel {
                             .creator_layout_id = btn.layout_id,
                             .popup_imgui_id = popup_id,
                         },
                 });
    }

    {
        auto const row = SettingsRow(builder, root);
        SettingsLhsTextWidget(builder, row, "Install");
        auto const rhs = SettingsRhsColumn(builder, row, style::k_settings_small_gap);
        SettingsRhsText(builder, rhs, "Install libraries and presets from a '.floe.zip' file");
        if (!context.package_install_jobs.Full() && SettingsTextButton(builder, rhs, "Install package")) {
            auto downloads_folder =
                KnownDirectory(builder.arena, KnownDirectoryType::Downloads, {.create = false});
            if (auto const o = FilesystemDialog({
                    .type = DialogArguments::Type::OpenFile,
                    .allocator = builder.arena,
                    .title = "Select Floe Package",
                    .default_path = downloads_folder,
                    .filters = ArrayT<DialogArguments::FileFilter>({
                        {
                            .description = "Floe Package"_s,
                            .wildcard_filter = "*.floe.zip"_s,
                        },
                    }),
                    .parent_window = builder.imgui.frame_input.native_window,
                });
                o.HasValue()) {
                for (auto const path : o.Value()) {
                    package::AddJob(context.package_install_jobs,
                                    path,
                                    context.settings,
                                    context.thread_pool,
                                    builder.arena,
                                    context.sample_lib_server);
                }
            } else {
                g_log.Error(k_gui_log_module, "Failed to create dialog: {}", o.Error());
            }
        }
    }
}

static void AppearanceSettingsPanel(UiBuilder& builder) {
    auto& context = *(SettingsPanelContext*)builder.user_data;

    auto const root = CreateWidget(builder,
                                   {
                                       .layout {
                                           .size = builder.imgui.PixelsToPoints(builder.imgui.Size()),
                                           .contents_padding = {.lrtb = style::k_spacing},
                                           .contents_gap = style::k_settings_medium_gap,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                       },
                                   });

    {
        auto const row = SettingsRow(builder, root);

        SettingsLhsTextWidget(builder, row, "GUI size");

        auto const button_container =
            CreateWidget(builder,
                         {
                             .parent = row,
                             .background_fill = style::Colour::Background2,
                             .round_background_corners = 0b1111,
                             .layout {
                                 .size = {layout::k_hug_contents, layout::k_hug_contents},
                                 .contents_direction = layout::Direction::Row,
                                 .contents_align = layout::Alignment::Start,
                             },
                         });

        Optional<int> width_change {};

        if (CreateWidget(builder,
                         {
                             .parent = button_container,
                             .text = ICON_FA_CARET_DOWN,
                             .font = FontType::Icons,
                             .text_align_x = TextAlignX::Centre,
                             .text_align_y = TextAlignY::Centre,
                             .background_fill_auto_hot_active_overlay = true,
                             .round_background_corners = 0b1001,
                             .activate_on_click_button = MouseButton::Left,
                             .activation_click_event = ActivationClickEvent::Up,
                             .layout {
                                 .size = style::k_settings_icon_button_size,
                             },
                         })
                .button_fired) {
            width_change = -1;
        }
        CreateWidget(builder,
                     {
                         .parent = button_container,
                         .background_fill = style::Colour::Surface2,
                         .layout {
                             .size = {builder.imgui.PixelsToPoints(1), layout::k_fill_parent},
                         },
                     });
        if (CreateWidget(builder,
                         {
                             .parent = button_container,
                             .text = ICON_FA_CARET_UP,
                             .font = FontType::Icons,
                             .text_align_x = TextAlignX::Centre,
                             .text_align_y = TextAlignY::Centre,
                             .background_fill_auto_hot_active_overlay = true,
                             .round_background_corners = 0b0110,
                             .activate_on_click_button = MouseButton::Left,
                             .activation_click_event = ActivationClickEvent::Up,
                             .layout {
                                 .size = style::k_settings_icon_button_size,
                             },
                         })
                .button_fired) {
            width_change = 1;
        }

        if (width_change) {
            auto const new_width = (int)context.settings.settings.gui.window_width + *width_change * 110;
            if (new_width > 0 && new_width < UINT16_MAX)
                gui_settings::SetWindowSize(context.settings.settings.gui,
                                            context.settings.tracking,
                                            (u16)new_width);
        }
    }

    {
        auto const options_row = SettingsRow(builder, root);

        SettingsLhsTextWidget(builder, options_row, "Options");
        auto const options_rhs_column = SettingsRhsColumn(builder, options_row, style::k_settings_small_gap);

        if (SettingsCheckboxButton(builder,
                                   options_rhs_column,
                                   "Show tooltips",
                                   context.settings.settings.gui.show_tooltips)) {
            context.settings.settings.gui.show_tooltips = !context.settings.settings.gui.show_tooltips;
            context.settings.tracking.changed = true;
        }

        {
            bool state = context.settings.settings.gui.show_keyboard;
            if (SettingsCheckboxButton(builder, options_rhs_column, "Show keyboard", state))
                gui_settings::SetShowKeyboard(context.settings.settings.gui,
                                              context.settings.tracking,
                                              !state);
        }

        if (SettingsCheckboxButton(builder,
                                   options_rhs_column,
                                   "High contrast GUI",
                                   context.settings.settings.gui.high_contrast_gui)) {
            context.settings.settings.gui.high_contrast_gui =
                !context.settings.settings.gui.high_contrast_gui;
            context.settings.tracking.changed = true;
        }
    }
}

static void NewSettingsWindow(UiBuilder& builder) {
    auto const root = CreateWidget(builder,
                                   {
                                       .layout {
                                           .size = builder.imgui.PixelsToPoints(builder.imgui.Size()),
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                       },
                                   });

    auto const title_container = CreateWidget(builder,
                                              {
                                                  .parent = root,
                                                  .layout {
                                                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                      .contents_padding = {.lrtb = style::k_spacing},
                                                      .contents_direction = layout::Direction::Row,
                                                      .contents_align = layout::Alignment::Justify,
                                                  },
                                              });

    CreateWidget(builder,
                 {
                     .parent = title_container,
                     .text = "Settings",
                     .font = FontType::Heading1,
                     .size_from_text = true,
                 });

    if (auto const close = CreateWidget(builder,
                                        {
                                            .parent = title_container,
                                            .text = ICON_FA_TIMES,
                                            .font = FontType::Icons,
                                            .size_from_text = true,
                                            .background_fill_auto_hot_active_overlay = true,
                                            .round_background_corners = 0b1111,
                                            .activate_on_click_button = MouseButton::Left,
                                            .activation_click_event = ActivationClickEvent::Up,
                                            .extra_margin_for_mouse_events = 8,
                                        });
        close.button_fired) {
        builder.current_panel->data.Get<ModalPanel>().on_close();
    }

    auto const divider_options = WidgetOptions {
        .parent = root,
        .background_fill = style::Colour::Surface2,
        .layout {
            .size = {layout::k_fill_parent, builder.imgui.PixelsToPoints(1)},
        },
    };

    CreateWidget(builder, divider_options);

    auto const tab_container = CreateWidget(builder,
                                            {
                                                .parent = root,
                                                .background_fill = style::Colour::Background1,
                                                .layout {
                                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                    .contents_direction = layout::Direction::Row,
                                                    .contents_align = layout::Alignment::Start,
                                                },
                                            });

    enum class Tab {
        Appearance,
        Folders,
        Packages,
        Count,
    };

    static Tab current_tab = Tab::Appearance;

    auto do_tab = [&](String icon, String text, Tab tab) {
        bool const is_current = tab == current_tab;
        auto const appearance_tab = CreateWidget(
            builder,
            {
                .parent = tab_container,
                .background_fill_auto_hot_active_overlay = true,
                .round_background_corners = 0b1111,
                .activate_on_click_button = MouseButton::Left,
                .activation_click_event = !is_current ? ActivationClickEvent::Up : ActivationClickEvent::None,
                .layout {
                    .size = {layout::k_hug_contents, layout::k_hug_contents},
                    .contents_padding = {.lr = style::k_spacing, .tb = 4},
                    .contents_gap = 5,
                    .contents_direction = layout::Direction::Row,
                },
            });
        if (appearance_tab.button_fired) current_tab = tab;

        CreateWidget(builder,
                     {
                         .parent = appearance_tab,
                         .text = icon,
                         .font = FontType::Icons,
                         .text_fill = is_current ? style::Colour::Subtext0 : style::Colour::Surface2,
                         .size_from_text = true,
                     });
        CreateWidget(builder,
                     {
                         .parent = appearance_tab,
                         .text = text,
                         .text_fill = is_current ? style::Colour::Text : style::Colour::Subtext0,
                         .size_from_text = true,
                     });
    };

    do_tab(ICON_FA_PAINT_BRUSH, "Appearance", Tab::Appearance);
    do_tab(ICON_FA_FOLDER_OPEN, "Folders", Tab::Folders);
    do_tab(ICON_FA_BOX_OPEN, "Packages", Tab::Packages);

    CreateWidget(builder, divider_options);

    AddPanel(builder,
             Panel {
                 .run = ({
                     PanelFunction f;
                     switch (current_tab) {
                         case Tab::Appearance: f = AppearanceSettingsPanel; break;
                         case Tab::Folders: f = FolderSettingsPanel; break;
                         case Tab::Packages: f = PackagesSettingsPanel; break;
                         case Tab::Count: PanicIfReached();
                     }
                     f;
                 }),
                 .data =
                     Subpanel {
                         .id = CreateWidget(builder,
                                            {
                                                .parent = root,
                                                .layout {
                                                    .size = {layout::k_fill_parent, layout::k_fill_parent},
                                                },
                                            })
                                   .layout_id,
                         .imgui_id = builder.imgui.GetID((u64)current_tab + 999999),
                     },
             });
}

static void NotificationsPanel(UiBuilder& builder, Notifications& notifications) {
    auto const root = CreateWidget(
        builder,
        {
            .layout {
                .size = {builder.imgui.PixelsToPoints(builder.imgui.Width()), layout::k_hug_contents},
                .contents_gap = style::k_spacing,
                .contents_direction = layout::Direction::Column,
                .contents_align = layout::Alignment::Start,
            },
        });

    for (auto it = notifications.begin(); it != notifications.end();) {
        auto const& n = *it;
        auto next = it;
        ++next;
        DEFER { it = next; };

        auto const config = n.get_diplay_info(builder.arena);

        auto const notification =
            CreateWidget(builder,
                         {
                             .parent = root,
                             .background_fill = style::Colour::Background0,
                             .round_background_corners = 0b1111,
                             .layout {
                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                 .contents_padding = {.lrtb = style::k_spacing},
                                 .contents_gap = style::k_spacing,
                                 .contents_direction = layout::Direction::Column,
                                 .contents_align = layout::Alignment::Start,
                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                             },
                         });
        auto const title_container =
            CreateWidget(builder,
                         {
                             .parent = notification,
                             .layout {
                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                 .contents_direction = layout::Direction::Row,
                                 .contents_align = layout::Alignment::Justify,
                             },
                         });

        auto const lhs_container =
            CreateWidget(builder,
                         {
                             .parent = title_container,
                             .layout {
                                 .size = {layout::k_hug_contents, layout::k_hug_contents},
                                 .contents_gap = 8,
                                 .contents_direction = layout::Direction::Row,
                                 .contents_align = layout::Alignment::Start,
                             },
                         });

        if (config.icon != NotificationDisplayInfo::IconType::None) {
            CreateWidget(builder,
                         {
                             .parent = lhs_container,
                             .text = ({
                                 String str {};
                                 switch (config.icon) {
                                     case NotificationDisplayInfo::IconType::None: PanicIfReached();
                                     case NotificationDisplayInfo::IconType::Info: str = ICON_FA_INFO; break;
                                     case NotificationDisplayInfo::IconType::Success:
                                         str = ICON_FA_CHECK;
                                         break;
                                     case NotificationDisplayInfo::IconType::Error:
                                         str = ICON_FA_EXCLAMATION_TRIANGLE;
                                         break;
                                 }
                                 str;
                             }),
                             .font = FontType::Icons,
                             .text_fill = ({
                                 style::Colour c {};
                                 switch (config.icon) {
                                     case NotificationDisplayInfo::IconType::None: PanicIfReached();
                                     case NotificationDisplayInfo::IconType::Info:
                                         c = style::Colour::Subtext1;
                                         break;
                                     case NotificationDisplayInfo::IconType::Success:
                                         c = style::Colour::Green;
                                         break;
                                     case NotificationDisplayInfo::IconType::Error:
                                         c = style::Colour::Red;
                                         break;
                                 }
                                 c;
                             }),
                             .size_from_text = true,
                         });
        }

        CreateWidget(builder,
                     {
                         .parent = lhs_container,
                         .text = config.title,
                         .font = FontType::Body,
                         .size_from_text = true,
                     });

        if (config.dismissable) {
            if (CreateWidget(builder,
                             {
                                 .parent = title_container,
                                 .text = ICON_FA_TIMES,
                                 .font = FontType::Icons,
                                 .size_from_text = true,
                                 .background_fill_auto_hot_active_overlay = true,
                                 .round_background_corners = 0b1111,
                                 .activate_on_click_button = MouseButton::Left,
                                 .activation_click_event = ActivationClickEvent::Up,
                                 .extra_margin_for_mouse_events = 8,
                             })
                    .button_fired) {
                next = notifications.Remove(it);
            }
        }

        if (config.message.size) {
            // TODO: support word wrap. We will have to add an API to the layout system to allow for
            // calculating a height only once the width is known.
            CreateWidget(builder,
                         {
                             .parent = notification,
                             .text = config.message,
                             .wrap = builder.imgui.PixelsToPoints(builder.imgui.Width() - 20),
                             .font = FontType::Body,
                             .size_from_text = true,
                         });
        }
    }
}

static void InstallationOptionAskUserPretext(DynamicArrayBounded<char, 256>& out,
                                             String component_name,
                                             package::ExistingInstallationStatus const& status) {
    ASSERT(package::UserInputIsRequired(status));

    String format {};
    if (status.modified_since_installed == package::ExistingInstallationStatus::Modified) {
        switch (status.version_difference) {
            case package::ExistingInstallationStatus::InstalledIsNewer:
                format =
                    "A newer version of {} is already installed but its files have been modified since it was installed.";
                break;
            case package::ExistingInstallationStatus::InstalledIsOlder:
                format =
                    "An older version of {} is already installed but its files have been modified since it was installed.";
                break;
            case package::ExistingInstallationStatus::Equal:
                format = "{} is already installed but its files have been modified since it was installed.";
                break;
        }
    } else {
        // We don't know if the package has been modified or not so we just ask the user what to do without
        // any explaination.
        switch (status.version_difference) {
            case package::ExistingInstallationStatus::InstalledIsNewer:
                format = "A newer version of {} is already installed.";
                break;
            case package::ExistingInstallationStatus::InstalledIsOlder:
                format = "An older version of {} is already installed.";
                break;
            case package::ExistingInstallationStatus::Equal: format = "{} is already installed."; break;
        }
    }

    fmt::Append(out, format, component_name);
}

static void PackageInstallAlertsPanel(UiBuilder& builder, package::InstallJobs& package_install_jobs) {
    auto const root = CreateWidget(builder,
                                   {
                                       .layout {
                                           .size = builder.imgui.PixelsToPoints(builder.imgui.Size()),
                                           .contents_padding = {.lrtb = style::k_spacing},
                                           .contents_gap = style::k_spacing,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                       },
                                   });
    for (auto& job : package_install_jobs) {
        auto const state = job.state.Load(LoadMemoryOrder::Acquire);
        if (state != package::InstallJob::State::AwaitingUserInput) continue;

        for (auto& component : job.components) {
            if (!package::UserInputIsRequired(component.existing_installation_status)) continue;

            //
            auto const container =
                CreateWidget(builder,
                             {
                                 .parent = root,
                                 .layout {
                                     .size = {layout::k_fill_parent, layout::k_hug_contents},
                                     .contents_direction = layout::Direction::Column,
                                     .contents_align = layout::Alignment::Start,
                                     .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                 },
                             });

            // TODO: fix these messy visuals

            DynamicArrayBounded<char, 256> text {};
            InstallationOptionAskUserPretext(text,
                                             path::FilenameWithoutExtension(component.component.path),
                                             component.existing_installation_status);

            CreateWidget(builder,
                         {
                             .parent = container,
                             .text = text,
                             .font = FontType::Body,
                             .size_from_text = true,
                         });

            auto const button_row =
                CreateWidget(builder,
                             {
                                 .parent = container,
                                 .layout {
                                     .size = {layout::k_fill_parent, layout::k_hug_contents},
                                     .contents_gap = style::k_spacing,
                                     .contents_direction = layout::Direction::Row,
                                     .contents_align = layout::Alignment::End,
                                 },
                             });

            if (SettingsTextButton(builder, button_row, "Skip"))
                component.user_decision = package::InstallJob::UserDecision::Skip;
            if (SettingsTextButton(builder, button_row, "Overwrite"))
                component.user_decision = package::InstallJob::UserDecision::Overwrite;
        }
    }
}

static void DoNewSettingsModal(Gui* g) {
    SettingsPanelContext context {
        .settings = g->settings,
        .sample_lib_server = g->shared_engine_systems.sample_library_server,
        .package_install_jobs = g->engine.package_install_jobs,
        .thread_pool = g->shared_engine_systems.thread_pool,
    };

    if (g->settings2_open)
        RunPanel(
            UiBuilder {
                .arena = g->scratch_arena,
                .imgui = g->imgui,
                .fonts = g->fonts,
                .layout = g->layout,
                .user_data = &context,
            },
            Panel {
                .run = NewSettingsWindow,
                .data =
                    ModalPanel {
                        .r = ModalRect(g->imgui,
                                       UiSizeId::SettingsWindowWidth,
                                       UiSizeId::SettingsWindowHeight),
                        .imgui_id = g->imgui.GetID("new settings"),
                        .on_close = [g]() { g->settings2_open = false; },
                        .close_on_click_outside = true,
                        .darken_background = true,
                        .disable_other_interaction = true,
                    },
            });

    if (!g->notifications.Empty()) {
        auto const width_px = g->imgui.PointsToPixels(style::k_notification_panel_width);
        auto const spacing = g->imgui.PixelsToPoints(style::k_spacing);

        RunPanel(
            UiBuilder {
                .arena = g->scratch_arena,
                .imgui = g->imgui,
                .fonts = g->fonts,
                .layout = g->layout,
                .user_data = nullptr,
            },
            Panel {
                .run = [g](UiBuilder& b) { NotificationsPanel(b, g->notifications); },
                .data =
                    ModalPanel {
                        .r {
                            .x = g->imgui.Width() - width_px - spacing,
                            .y = spacing,
                            .w = width_px,
                            .h = 4,
                        },
                        .imgui_id = g->imgui.GetID("notifications"),
                        .on_close = []() {},
                        .close_on_click_outside = false,
                        .darken_background = false,
                        .disable_other_interaction = false,
                        .auto_height = true,
                    },
            });
    }

    constexpr u64 k_installing_packages_notif_id = HashComptime("installing packages notification");
    if (!g->engine.package_install_jobs.Empty()) {
        if (!g->notifications.Find(k_installing_packages_notif_id)) {
            *g->notifications.AppendUninitalisedOverwrite() = {
                .get_diplay_info = [&jobs = g->engine.package_install_jobs](
                                       ArenaAllocator& scratch_arena) -> NotificationDisplayInfo {
                    NotificationDisplayInfo c {};
                    c.icon = NotificationDisplayInfo::IconType::Info;
                    c.dismissable = false;
                    if (!jobs.Empty())
                        c.title = fmt::Format(scratch_arena,
                                              "Installing {}{}",
                                              path::FilenameWithoutExtension(jobs.First().path),
                                              jobs.ContainsMoreThanOne() ? " and others" : "");
                    return c;
                },
                .id = k_installing_packages_notif_id,
            };
        }

        bool user_input_needed = false;

        for (auto it = g->engine.package_install_jobs.begin(); it != g->engine.package_install_jobs.end();) {
            auto& job = *it;
            auto next = it;
            ++next;
            DEFER { it = next; };

            auto const state = job.state.Load(LoadMemoryOrder::Acquire);
            switch (state) {
                case package::InstallJob::State::Installing: break;

                case package::InstallJob::State::DoneError: {
                    auto err = g->engine.error_notifications.NewError();
                    err->value = {
                        .message = String(job.error_log.buffer),
                        .id = HashComptime("package install error"),
                    };
                    fmt::Assign(err->value.title,
                                "Failed to install {}",
                                path::FilenameWithoutExtension(job.path));
                    g->engine.error_notifications.AddOrUpdateError(err);
                    next = package::RemoveJob(g->engine.package_install_jobs, it);
                    break;
                }
                case package::InstallJob::State::DoneSuccess: {
                    DynamicArrayBounded<char, k_notification_buffer_size - 16> buffer {};
                    u8 num_truncated = 0;
                    for (auto [index, component] : Enumerate(job.components)) {
                        if (!num_truncated) {
                            if (!dyn::AppendSpan(
                                    buffer,
                                    fmt::Format(g->scratch_arena,
                                                "{} {} {}\n",
                                                path::FilenameWithoutExtension(component.component.path),
                                                package::ComponentTypeString(component.component.type),
                                                package::ActionTaken(component))))
                                num_truncated = 1;
                        } else if (num_truncated != LargestRepresentableValue<decltype(num_truncated)>())
                            ++num_truncated;
                    }

                    *g->notifications.AppendUninitalisedOverwrite() = {
                        .get_diplay_info = [buffer, num_truncated](
                                               ArenaAllocator& scratch_arena) -> NotificationDisplayInfo {
                            NotificationDisplayInfo c {};
                            c.icon = NotificationDisplayInfo::IconType::Success;
                            c.dismissable = true;
                            c.title = "Installation Complete";
                            if (num_truncated == 0) {
                                c.message = buffer;
                            } else {
                                c.message =
                                    fmt::Format(scratch_arena, "{}\n... and {} more", buffer, num_truncated);
                            }
                            return c;
                        },
                        .id = HashComptime("package install success"),
                    };

                    next = package::RemoveJob(g->engine.package_install_jobs, it);
                    break;
                }

                case package::InstallJob::State::AwaitingUserInput: {
                    bool all_descisions_made = true;
                    for (auto& component : job.components) {
                        if (package::UserInputIsRequired(component.existing_installation_status) &&
                            component.user_decision == package::InstallJob::UserDecision::Unknown) {
                            all_descisions_made = false;
                            break;
                        }
                    }

                    if (all_descisions_made)
                        package::AllUserInputReceived(job, context.thread_pool);
                    else
                        user_input_needed = true;

                    break;
                }
            }
        }

        if (user_input_needed) {
            RunPanel(
                UiBuilder {
                    .arena = g->scratch_arena,
                    .imgui = g->imgui,
                    .fonts = g->fonts,
                    .layout = g->layout,
                    .user_data = &context,
                },
                Panel {
                    .run =
                        [g](UiBuilder& b) { PackageInstallAlertsPanel(b, g->engine.package_install_jobs); },
                    .data =
                        ModalPanel {
                            .r = ModalRect(g->imgui,
                                           g->imgui.PointsToPixels(style::k_intall_dialog_width),
                                           g->imgui.PointsToPixels(style::k_intall_dialog_height)),
                            .imgui_id = g->imgui.GetID("install alerts"),
                            .on_close = {},
                            .close_on_click_outside = false,
                            .darken_background = true,
                            .disable_other_interaction = true,
                            .auto_height = false,
                        },
                });
        }
    } else {
        g->notifications.Remove(g->notifications.Find(k_installing_packages_notif_id));
    }
}

// ===============================================================================================================
// ===============================================================================================================
// ===============================================================================================================
// ===============================================================================================================

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
    DoSettingsModal(g);
    DoLicencesModal(g);

    DoNewSettingsModal(g);
}
