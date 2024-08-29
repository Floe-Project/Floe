// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_modal_windows.hpp"

#include <IconsFontAwesome5.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/package_format.hpp"
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

    auto const text_r = imgui.GetRegisteredAndConvertedRect({x_offset, y_pos, size.x, size.y});
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

    auto button_r = imgui.GetRegisteredAndConvertedRect({x_pos, y_pos, box_width, box_height});
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

struct DoTextLineOptions {
    TextJustification justification = TextJustification::CentredLeft;
    f32 font_scaling = 1.0f;
};

static void DoTextLine(Gui* g, String text, f32& y_pos, UiColMap col_map, DoTextLineOptions options = {}) {
    auto& imgui = g->imgui;
    auto const line_height = imgui.graphics->context->CurrentFontSize();
    auto const text_r = imgui.GetRegisteredAndConvertedRect({0, y_pos, imgui.Width(), line_height});
    imgui.graphics->AddTextJustified(text_r,
                                     text,
                                     LiveCol(imgui, col_map),
                                     options.justification,
                                     TextOverflowType::AllowOverflow,
                                     options.font_scaling);
    y_pos += line_height;
}

static void DoInstallPackagesModal(Gui* g) {
    g->frame_input.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto const settings = ModalWindowSettings(g->imgui);
    auto const r =
        ModalRect(imgui, UiSizeId::InstallPackagesWindowWidth, UiSizeId::InstallPackagesWindowHeight);

    if (g->install_packages_state.state == InstallPackagesState::Installing &&
        !g->install_packages_state.installing_packages.Load(LoadMemoryOrder::Relaxed)) {
        g->install_packages_state.state = InstallPackagesState::Done;
    }

    if (imgui.BeginWindowPopup(settings, IdForModal(ModalWindowType::InstallPackages), r, "install")) {
        DEFER { imgui.EndWindow(); };
        f32 main_y_pos = 0;
        DoHeading(g, main_y_pos, "Extract and Install Floe Packages");
        DoWindowCloseButton(g);

        auto const line_height = imgui.graphics->context->CurrentFontSize();

        Rect rect {0.0f, imgui.Size()};
        rect_cut::CutTop(rect, main_y_pos);

        // main panel
        {
            auto const main_panel_rect = rect_cut::CutTop(rect, line_height * 10);

            {
                auto const main_rect_converted = imgui.WindowPosToScreenPos(main_panel_rect.pos);
                auto const w = imgui.CurrentWindow();
                imgui.graphics->PushClipRectFullScreen();
                DEFER { imgui.graphics->PopClipRect(); };
                Rect const full_width_rect = {w->unpadded_bounds.x,
                                              main_rect_converted.y,
                                              w->unpadded_bounds.w,
                                              main_panel_rect.h};
                imgui.graphics->AddRectFilled(full_width_rect.Min(),
                                              full_width_rect.Max(),
                                              LiveCol(imgui, UiColMap::ModalWindowSubContainerBackground));
            }

            auto subwindow_settings = FloeWindowSettings(imgui, [](imgui::Context const&, imgui::Window*) {});
            subwindow_settings.draw_routine_scrollbar = settings.draw_routine_scrollbar;
            subwindow_settings.pad_bottom_right = line_height / 2;
            subwindow_settings.pad_top_left = line_height / 2;
            imgui.BeginWindow(subwindow_settings, main_panel_rect, "inner");
            DEFER { imgui.EndWindow(); };

            f32 y_pos = 1; // avoid clipping glitch

            y_pos += line_height;

            switch (g->install_packages_state.state) {
                case InstallPackagesState::SelectFiles: {
                    if (DoButton(g,
                                 "Choose Zip Files",
                                 {
                                     .incrementing_y = IncrementingY {y_pos},
                                     .centre_vertically = true,
                                     .auto_width = true,
                                     .tooltip = "Select Floe zip packages to extract and install",
                                     .icon = ICON_FA_FILE_ARCHIVE,
                                     .significant = true,
                                     .white_background = true,
                                     .big_font = true,
                                 }))
                        g->OpenDialog(DialogType::InstallPackage);

                    if (g->install_packages_state.selected_package_paths.Empty())
                        DoTextLine(g,
                                   "No packages selected",
                                   y_pos,
                                   UiColMap::ModalWindowInsignificantText,
                                   {
                                       .justification = TextJustification::Centred,
                                       .font_scaling = 0.9f,
                                   });
                    else {
                        DynamicArray<char> text {g->scratch_arena};
                        dyn::Assign(text, "Selected packages: ");
                        for (auto const path : g->install_packages_state.selected_package_paths) {
                            auto const name =
                                TrimEndIfMatches(path::Filename(path), package::k_file_extension);
                            dyn::AppendSpan(text, name);
                            dyn::AppendSpan(text, ", ");
                        }
                        dyn::Resize(text, text.size - 2);

                        DoTextLine(g,
                                   text,
                                   y_pos,
                                   UiColMap::PopupItemText,
                                   {
                                       .justification = TextJustification::Centred,
                                       .font_scaling = 0.9f,
                                   });
                    }

                    y_pos += line_height * 1.5f;

                    if (DoButton(g,
                                 "Install",
                                 {
                                     .incrementing_y = IncrementingY {y_pos},
                                     .centre_vertically = true,
                                     .auto_width = true,
                                     .tooltip = "Select Floe zip packages to extract and install",
                                     .greyed_out =
                                         g->install_packages_state.selected_package_paths.Empty() ||
                                         g->install_packages_state.state != InstallPackagesState::SelectFiles,
                                     .icon = ICON_FA_ARROW_DOWN,
                                     .white_background = true,
                                     .big_font = true,
                                 })) {
                        g->install_packages_state.state = InstallPackagesState::Installing;
                        g->install_packages_state.installing_packages.Store(true, StoreMemoryOrder::Relaxed);
                        g->plugin.shared_data.thread_pool.AddJob(
                            [&state = g->install_packages_state,
                             &request_update = g->frame_input.request_update]() {
                                SleepThisThread(5000);
                                request_update.Store(true, StoreMemoryOrder::Release);
                                state.installing_packages.Store(false, StoreMemoryOrder::Release);
                            });
                    }

                    DoTextLine(g,
                               "Installs to your Floe folders",
                               y_pos,
                               UiColMap::ModalWindowInsignificantText,
                               {
                                   .justification = TextJustification::Centred,
                                   .font_scaling = 0.9f,
                               });
                    break;
                }
                case InstallPackagesState::Installing: {
                    DoTextLine(g,
                               "Installing...",
                               y_pos,
                               UiColMap::PopupItemText,
                               {TextJustification::Centred});
                    break;
                }
                case InstallPackagesState::Done: {
                    DoTextLine(g,
                               "Successfully installed libraries and presets",
                               y_pos,
                               UiColMap::PopupItemText,
                               {TextJustification::Centred});
                    y_pos += line_height;
                    if (DoButton(g,
                                 "Install More",
                                 {
                                     .incrementing_y = IncrementingY {y_pos},
                                     .centre_vertically = true,
                                     .auto_width = true,
                                     .tooltip = "Return to the package selection screen",
                                     .white_background = true,
                                 })) {
                        g->install_packages_state.selected_package_paths.Clear();
                        g->install_packages_state.arena.ResetCursorAndConsolidateRegions();
                        g->install_packages_state.state = InstallPackagesState::SelectFiles;
                        g->frame_output.ElevateUpdateRequest(
                            GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
                    }
                    break;
                }
                case InstallPackagesState::Count: PanicIfReached();
            }
        }

        // bottom panel
        {
            auto subwindow_settings = FloeWindowSettings(imgui, [](imgui::Context const&, imgui::Window*) {});
            subwindow_settings.draw_routine_scrollbar = settings.draw_routine_scrollbar;
            subwindow_settings.pad_bottom_right = line_height / 2;
            subwindow_settings.pad_top_left = line_height / 2;

            rect_cut::CutTop(rect, line_height);
            auto const bottom_left_panel = rect_cut::CutLeft(rect, rect.w / 2);
            auto const bottom_right_panel = rect;

            {
                imgui.BeginWindow(subwindow_settings, bottom_left_panel, "innerleft");
                DEFER { imgui.EndWindow(); };

                f32 y_pos = 0;
                DoTextLine(g,
                           "About Floe Packages",
                           y_pos,
                           UiColMap::PopupItemText,
                           {
                               .justification = TextJustification::Left,
                           });
                DoMultilineText(
                    g,
                    "Floe packages are zip files ending with \"floe.zip\". They can contain both libraries and presets.",
                    y_pos,
                    0,
                    UiColMap::ModalWindowInsignificantText);
                if (DoButton(g,
                             "Learn more",
                             {
                                 .incrementing_y = IncrementingY {y_pos},
                                 .auto_width = true,
                                 .tooltip = "Open the online user manual on Floe packages",
                                 .icon = ICON_FA_EXTERNAL_LINK_ALT,
                                 .insignificant = true,
                                 .white_background = true,
                             })) {
                    OpenUrlInBrowser(FLOE_PACKAGES_INFO_URL);
                }
            }

            {
                imgui.BeginWindow(subwindow_settings, bottom_right_panel, "innerright");
                DEFER { imgui.EndWindow(); };

                f32 y_pos = 0;
                DoTextLine(g,
                           "Other ways to install",
                           y_pos,
                           UiColMap::PopupItemText,
                           {
                               .justification = TextJustification::Left,
                           });
                DoMultilineText(
                    g,
                    "You can also install libraries and presets by extracting the zip and copying its contents to your Floe folders.",
                    y_pos,
                    0,
                    UiColMap::ModalWindowInsignificantText);
                if (DoButton(g,
                             "Learn more",
                             {
                                 .incrementing_y = IncrementingY {y_pos},
                                 .auto_width = true,
                                 .tooltip = "Open the online user manual on alternate installation methods",
                                 .icon = ICON_FA_EXTERNAL_LINK_ALT,
                                 .insignificant = true,
                                 .white_background = true,
                             })) {
                    OpenUrlInBrowser(FLOE_MANUAL_INSTALL_INSTRUCTIONS_URL);
                }
            }
        }
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
    DoInstallPackagesModal(g);
    DoSettingsModal(g);
    DoLicencesModal(g);
}

void OpenInstallPackagesModal(Gui* g) {
    if (g->install_packages_state.state != InstallPackagesState::Installing) {
        g->install_packages_state.selected_package_paths.Clear();
        g->install_packages_state.arena.ResetCursorAndConsolidateRegions();
        g->install_packages_state.state = InstallPackagesState::SelectFiles;
    }
    OpenModalIfNotAlready(g->imgui, ModalWindowType::InstallPackages);
}

void InstallPackagesSelectFilesDialogResults(Gui* g, Span<MutableString> paths) {
    if (g->install_packages_state.state == InstallPackagesState::Installing) return;

    for (auto const path : paths)
        if (!package::IsPathPackageFile(path)) {
            auto const err = g->plugin.error_notifications.NewError();
            err->value = {
                .title = "Not a Floe package"_s,
                .message = String(fmt::Format(g->scratch_arena,
                                              "'{}' is not a Floe package. Floe packages are zip files.",
                                              path::Filename(path))),
                .error_code = nullopt,
                .id = ThreadsafeErrorNotifications::Id("pkg ", path),
            };
            g->plugin.error_notifications.AddOrUpdateError(err);
        } else {
            g->install_packages_state.selected_package_paths.Prepend(
                g->install_packages_state.arena.Clone(path));
        }
}

void ShutdownInstallPackagesModal(InstallPackagesData& state) {
    while (state.installing_packages.Load(LoadMemoryOrder::Acquire))
        SleepThisThread(100u);
}
