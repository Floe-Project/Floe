// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/error_reporting.hpp"

#include "gui/gui2_feedback_panel_state.hpp"
#include "gui/gui2_notifications.hpp"
#include "gui2_common_modal_panel.hpp"

struct FeedbackPanelContext {
    Notifications& notifications;
};

static void
FeedbackPanel(GuiBoxSystem& box_system, FeedbackPanelContext& context, FeedbackPanelState& state) {
    auto const root = DoModalRootBox(box_system);

    DoModalHeader(box_system,
                  {
                      .parent = root,
                      .title = "Share Feedback",
                      .on_close = [&state]() { state.open = false; },
                  });

    DoModalDivider(box_system, root);

    auto const panel = DoBox(box_system,
                             {
                                 .parent = root,
                                 .layout {
                                     .size = {layout::k_fill_parent, layout::k_fill_parent},
                                     .contents_padding = {.lrtb = style::k_spacing},
                                     .contents_gap = style::k_spacing,
                                     .contents_direction = layout::Direction::Column,
                                     .contents_align = layout::Alignment::Start,
                                     .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                 },
                             });

    DoBox(
        box_system,
        {
            .parent = panel,
            .text =
                "Help us improve - share bug reports, feature requests, or any other feedback to make Floe better."_s,
            .wrap_width = k_wrap_to_parent,
            .font = FontType::Body,
            .size_from_text = true,
        });

    DoBox(box_system,
          {
              .parent = panel,
              .text = "Description:"_s,
              .font = FontType::Body,
              .size_from_text = true,
          });

    auto field_config = BoxConfig {
        .parent = panel,
        .text = state.description,
        .font = FontType::Body,
        .text_fill = style::Colour::Text,
        .text_fill_hot = style::Colour::Text,
        .text_fill_active = style::Colour::Text,
        .background_fill = style::Colour::Background2,
        .background_fill_hot = style::Colour::Background2,
        .background_fill_active = style::Colour::Background2,
        .border = style::Colour::Overlay0,
        .border_hot = style::Colour::Overlay1,
        .border_active = style::Colour::Highlight,
        .round_background_corners = 0b1111,
        .text_input_box = TextInputBox::MultiLine,
        .text_input_cursor = style::Colour::Text,
        .text_input_selection = style::Colour::Highlight,
        .layout {
            .size = {layout::k_fill_parent, 90},
        },
    };

    auto const description_field = DoBox(box_system, field_config);
    if (description_field.text_input_result && description_field.text_input_result->buffer_changed)
        dyn::Assign(state.description, description_field.text_input_result->text);

    DoBox(box_system,
          {
              .parent = panel,
              .text = "Email (optional):"_s,
              .font = FontType::Body,
              .size_from_text = true,
          });

    field_config.text = state.email;
    field_config.layout.size = {layout::k_fill_parent, 30};
    field_config.text_input_box = TextInputBox::SingleLine;

    auto const email_field = DoBox(box_system, field_config);
    if (email_field.text_input_result && email_field.text_input_result->buffer_changed)
        dyn::Assign(state.email, email_field.text_input_result->text);

    if (CheckboxButton(box_system, panel, "Include anonymous diagnostic data"_s, state.send_diagnostic_data))
        state.send_diagnostic_data = !state.send_diagnostic_data;

    if (TextButton(box_system, panel, "Submit", {})) {
        auto const return_code = ReportFeedback(state.description,
                                                state.email.size ? Optional<String> {state.email} : k_nullopt,
                                                state.send_diagnostic_data);
        String notification_message = {};
        auto icon = NotificationDisplayInfo::IconType::Success;
        switch (return_code) {
            case ReportFeedbackReturnCode::Success: {
                notification_message = "Feedback submitted successfully"_s;
                dyn::Clear(state.description);
                dyn::Clear(state.email);
                state.open = false;
                break;
            }
            case ReportFeedbackReturnCode::InvalidEmail: {
                notification_message = "Invalid email address"_s;
                icon = NotificationDisplayInfo::IconType::Error;
                break;
            }
            case ReportFeedbackReturnCode::Busy: {
                notification_message = "Feedback submission already in progress"_s;
                icon = NotificationDisplayInfo::IconType::Error;
                break;
            }
            case ReportFeedbackReturnCode::DescriptionTooLong: {
                notification_message = "Description too long"_s;
                icon = NotificationDisplayInfo::IconType::Error;
                break;
            }
            case ReportFeedbackReturnCode::DescriptionEmpty: {
                notification_message = "Description cannot be empty"_s;
                icon = NotificationDisplayInfo::IconType::Error;
                break;
            }
        }
        *context.notifications.AppendUninitalisedOverwrite() = {
            .get_diplay_info = [icon,
                                title = notification_message](ArenaAllocator&) -> NotificationDisplayInfo {
                return {
                    .title = title,
                    .dismissable = true,
                    .icon = icon,
                };
            },
            .id = HashComptime(__FILE__ STRINGIFY(__LINE__)),
        };
    }
}

PUBLIC void
DoFeedbackPanel(GuiBoxSystem& box_system, FeedbackPanelContext& context, FeedbackPanelState& state) {
    if (!state.open) return;
    RunPanel(
        box_system,
        Panel {
            .run = [&context, &state](GuiBoxSystem& b) { FeedbackPanel(b, context, state); },
            .data =
                ModalPanel {
                    .r = CentredRect({.pos = 0, .size = box_system.imgui.frame_input.window_size.ToFloat2()},
                                     f32x2 {box_system.imgui.VwToPixels(style::k_feedback_dialog_width),
                                            box_system.imgui.VwToPixels(style::k_feedback_dialog_height)}),
                    .imgui_id = box_system.imgui.GetID("new info"),
                    .on_close = [&state]() { state.open = false; },
                    .close_on_click_outside = true,
                    .darken_background = true,
                    .disable_other_interaction = true,
                },
        });
}
