// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_save_preset_panel.hpp"

#include "common_infrastructure/tags.hpp"

#include "engine/engine.hpp"
#include "gui/gui2_common_modal_panel.hpp"
#include "gui/gui_file_picker.hpp"

void OnEngineStateChange(SavePresetPanelState& state, Engine const& engine) {
    state.metadata = engine.state_metadata;
}

static void SavePresetPanel(GuiBoxSystem& box_system, SavePresetPanelContext&, SavePresetPanelState& state) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
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
            .parent = root,
            .text =
                "Save the current state of Floe to a preset file. Its name is determined by its file name."_s,
            .wrap_width = k_wrap_to_parent,
            .font = FontType::Body,
            .size_from_text = true,
        });

    {
        auto const author_box = DoBox(box_system,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_gap = style::k_spacing / 3,
                                              .contents_direction = layout::Direction::Row,
                                              .contents_align = layout::Alignment::Start,
                                              .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                          },
                                      });
        DoBox(box_system,
              {
                  .parent = author_box,
                  .text = "Author:"_s,
                  .font = FontType::Body,
                  .size_from_text = true,
              });

        if (auto const input = TextInput(box_system,
                                         author_box,
                                         state.metadata.author,
                                         "Creator of this preset"_s,
                                         f32x2 {200, style::k_font_body_size * 1.3f},
                                         TextInputBox::SingleLine);
            input.text_input_result && input.text_input_result->buffer_changed) {
            dyn::AssignFitInCapacity(state.metadata.author, input.text_input_result->text);
        }
    }

    {
        auto const container = DoBox(box_system,
                                     {
                                         .parent = root,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = style::k_spacing / 3,
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Start,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                         },
                                     });
        DoBox(box_system,
              {
                  .parent = container,
                  .text = "Description:"_s,
                  .font = FontType::Body,
                  .size_from_text = true,
              });

        if (auto const description_field = TextInput(box_system,
                                                     container,
                                                     state.metadata.description,
                                                     "",
                                                     f32x2 {layout::k_fill_parent, 60},
                                                     TextInputBox::MultiLine);
            description_field.text_input_result && description_field.text_input_result->buffer_changed)
            dyn::AssignFitInCapacity(state.metadata.description, description_field.text_input_result->text);
    }

    {

        for (auto const category : EnumIterator<TagCategory>()) {
            if (category == TagCategory::ReverbType) continue;

            auto const category_box =
                DoBox(box_system,
                      {
                          .parent = root,
                          .layout {
                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                              .contents_gap = style::k_spacing / 3,
                              .contents_direction = layout::Direction::Column,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                          },
                      });

            auto const info = Tags(category);
            DoBox(box_system,
                  {
                      .parent = category_box,
                      .text = fmt::FormatInline<k_max_tag_size + 3>("{}:", info.name),
                      .font = FontType::Body,
                      .size_from_text = true,
                      .layout {
                          .line_break = true,
                      },
                  });

            auto const tags_list = DoBox(box_system,
                                         {
                                             .parent = category_box,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                 .contents_gap = style::k_spacing / 2.5f,
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_multiline = true,
                                                 .contents_align = layout::Alignment::Start,
                                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                             },
                                         });

            for (auto const& tag : info.tags) {
                auto const is_selected = Contains(state.metadata.tags, tag.tag);

                auto const button = DoBox(box_system,
                                          BoxConfig {
                                              .parent = tags_list,
                                              .text = tag.tag,
                                              .font = FontType::Body,
                                              .size_from_text = true,
                                              .background_fill = is_selected ? style::Colour::Highlight
                                                                             : style::Colour::Background1,
                                              .background_fill_auto_hot_active_overlay = true,
                                              .round_background_corners = 0b1100,
                                              .activate_on_click_button = MouseButton::Left,
                                              .activation_click_event = ActivationClickEvent::Up,
                                          });

                if (button.button_fired) {
                    if (is_selected)
                        dyn::RemoveValue(state.metadata.tags, tag.tag);
                    else
                        dyn::Append(state.metadata.tags, tag.tag);
                }
            }
        }
    }
}

static void CommitMetadataToEngine(Engine& engine, SavePresetPanelState const& state) {
    engine.state_metadata = state.metadata;
}

void DoSavePresetPanel(GuiBoxSystem& box_system,
                       SavePresetPanelContext& context,
                       SavePresetPanelState& state) {
    if (!state.open) return;
    RunPanel(
        box_system,
        Panel {
            .run =
                [&context, &state](GuiBoxSystem& box_system) {
                    auto const root = DoModalRootBox(box_system);

                    DoModalHeader(box_system,
                                  {
                                      .parent = root,
                                      .title = "Save Preset",
                                      .on_close = [&state]() { state.open = false; },
                                  });

                    DoModalDivider(box_system, root, DividerType::Horizontal);

                    AddPanel(
                        box_system,
                        Panel {
                            .run =
                                [&](GuiBoxSystem& box_system) {
                                    SavePresetPanel(box_system, context, state);
                                },
                            .data =
                                Subpanel {
                                    .id =
                                        DoBox(box_system,
                                              {
                                                  .parent = root,
                                                  .layout {
                                                      .size = {layout::k_fill_parent, layout::k_fill_parent},
                                                  },
                                              })
                                            .layout_id,
                                    .imgui_id = (u32)SourceLocationHash(),
                                },

                        });

                    DoModalDivider(box_system, root, DividerType::Horizontal);

                    // bottom buttons
                    auto const button_container =
                        DoBox(box_system,
                              {
                                  .parent = root,
                                  .layout {
                                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                                      .contents_padding = {.lrtb = style::k_spacing},
                                      .contents_gap = style::k_spacing,
                                      .contents_direction = layout::Direction::Row,
                                      .contents_align = layout::Alignment::End,
                                  },
                              });

                    if (TextButton(box_system, button_container, "Cancel"_s, "Cancel and close"_s))
                        state.open = false;

                    if (auto const existing_path = context.engine.last_snapshot.name_or_path.Path()) {
                        if (TextButton(box_system,
                                       button_container,
                                       "Overwrite"_s,
                                       "Overwrite the existing preset"_s)) {
                            CommitMetadataToEngine(context.engine, state);
                            SaveCurrentStateToFile(context.engine, *existing_path);
                            state.open = false;
                        }

                        if (TextButton(box_system,
                                       button_container,
                                       "Save As New"_s,
                                       "Save the preset as a new file"_s)) {
                            CommitMetadataToEngine(context.engine, state);
                            OpenFilePickerSavePreset(context.file_picker_state,
                                                     box_system.imgui.frame_output,
                                                     context.paths);
                            state.open = false;
                        }
                    } else if (TextButton(box_system,
                                          button_container,
                                          "Save"_s,
                                          "Save the preset to a new file"_s)) {
                        CommitMetadataToEngine(context.engine, state);
                        OpenFilePickerSavePreset(context.file_picker_state,
                                                 box_system.imgui.frame_output,
                                                 context.paths);
                        state.open = false;
                    }
                },
            .data =
                ModalPanel {
                    .r = CentredRect({.pos = 0, .size = box_system.imgui.frame_input.window_size.ToFloat2()},
                                     f32x2 {box_system.imgui.VwToPixels(style::k_feedback_dialog_width),
                                            box_system.imgui.VwToPixels(style::k_feedback_dialog_height)}),
                    .imgui_id = box_system.imgui.GetID("save-preset"),
                    .on_close = [&state]() { state.open = false; },
                    .close_on_click_outside = true,
                    .darken_background = true,
                    .disable_other_interaction = true,
                },
        });
}
