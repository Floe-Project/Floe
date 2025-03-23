// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/engine.hpp"
#include "gui/gui2_inst_picker_state.hpp"
#include "gui/gui_library_images.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "processor/layer_processor.hpp"

// Ephemeral
struct InstPickerContext {
    LayerProcessor& layer;
    sample_lib_server::Server& sample_library_server;
    LibraryImagesArray& library_images;
    Engine& engine;
    Span<sample_lib_server::RefCounted<sample_lib::Library>> libraries;
    sample_lib::Instrument const* hot_instrument {};
};

[[maybe_unused]] static void
InstPickerStatusBar(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
    (void)state;
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = 4},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    if (context.hot_instrument) {
        DoBox(box_system,
              {
                  .parent = root,
                  .text = context.hot_instrument->name,
                  .font = FontType::Body,
                  .layout =
                      {
                          .size = layout::k_fill_parent,
                      },
              });
    }
}

static void InstPickerItems(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
    (void)state;
    (void)context;
    (void)box_system;
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = 4},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    DoBox(box_system,
          {
              .parent = root,
              .text = "Instruments",
              .font = FontType::Heading1,
              .size_from_text = true,
              .layout =
                  {
                      .margins = {.b = 6},
                  },
          });

    Optional<Optional<String>> previous_folder {};
    for (auto const& l_ptr : context.libraries) {
        auto const& lib = *l_ptr;

        if (lib.sorted_instruments.size == 0) continue;

        for (auto const inst_ptr : lib.sorted_instruments) {
            auto const& inst = *inst_ptr;

            auto const folder = inst.folder;
            if (folder != previous_folder) {
                previous_folder = folder;

                DoBox(box_system,
                      {
                          .parent = root,
                          .text = folder ? *folder : "No Folder",
                          .font = FontType::Heading3,
                          .layout =
                              {
                                  .size = {layout::k_fill_parent, 15},
                                  .margins = {.t = 6.0f},
                              },
                      });
            }

            auto const item = DoBox(box_system,
                                    {
                                        .parent = root,
                                        .text = inst.name,
                                        .font = FontType::Body,
                                        .background_fill_auto_hot_active_overlay = true,
                                        .activate_on_click_button = MouseButton::Left,
                                        .activation_click_event = ActivationClickEvent::Up,
                                        .layout =
                                            {
                                                .size = {layout::k_fill_parent, 20},
                                            },
                                    });
            if (item.is_hot) context.hot_instrument = &inst;
            if (item.button_fired) {
                LoadInstrument(context.engine,
                               context.layer.index,
                               sample_lib::InstrumentId {
                                   .library = lib.Id(),
                                   .inst_name = inst.name,
                               });
                box_system.imgui.CloseCurrentPopup();
            }
        }
    }
}

static void InstPickerFilters(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
    (void)state;
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = 4},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    DoBox(box_system,
          {
              .parent = root,
              .text = "Filters",
              .font = FontType::Heading1,
              .size_from_text = true,
              .layout =
                  {
                      .margins = {.b = 6},
                  },
          });

    DoBox(box_system,
          {
              .parent = root,
              .text = "Libraries",
              .font = FontType::Heading3,
              .size_from_text = true,
          });

    auto const library_container = DoBox(box_system,
                                         {
                                             .parent = root,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_multiline = true,
                                                 .contents_align = layout::Alignment::Start,
                                             },
                                         });

    for (auto const& l_ptr : context.libraries) {
        auto const& lib = *l_ptr;

        if (lib.insts_by_name.size == 0) continue;

        auto const button = DoBox(box_system,
                                  {
                                      .parent = library_container,
                                      .background_fill_active = style::Colour::Highlight,
                                      .background_fill_auto_hot_active_overlay = true,
                                      .round_background_corners = 0b1111,
                                      .activate_on_click_button = MouseButton::Left,
                                      .activation_click_event = ActivationClickEvent::Up,
                                      .layout {
                                          .size = layout::k_hug_contents,
                                          .margins = {.r = 12, .b = 0.5f},
                                      },
                                  });

        if (auto const imgs = LibraryImagesFromLibraryId(context.library_images,
                                                         box_system.imgui,
                                                         lib.Id(),
                                                         context.sample_library_server,
                                                         box_system.arena);
            imgs && imgs->icon) {
            auto const tex = box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(imgs->icon);

            DoBox(box_system,
                  {
                      .parent = button,
                      .background_tex = tex,
                      .layout {
                          .size = {20, 20},
                          .margins = {.r = 3},
                      },
                  });
        }

        DoBox(box_system,
              {
                  .parent = button,
                  .text = lib.name,
                  .font = FontType::Body,
                  .text_fill = style::Colour::Text,
                  .text_fill_hot = style::Colour::Text,
                  .text_fill_active = style::Colour::Text,
                  .size_from_text = true,
                  .parent_dictates_hot_and_active = true,
              });
    }
}

static void InstPickerPopup(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
    (void)state;
    auto const root = DoBox(
        box_system,
        {
            .layout {
                .size = {layout::k_hug_contents,
                         box_system.imgui.PixelsToVw(box_system.imgui.frame_input.window_size.height * 0.9f)},
                .contents_direction = layout::Direction::Column,
                .contents_align = layout::Alignment::Start,
            },
        });

    auto const main_section = DoBox(box_system,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_hug_contents, layout::k_fill_parent},
                                            .contents_direction = layout::Direction::Row,
                                            .contents_align = layout::Alignment::Start,
                                        },
                                    });

    AddPanel(box_system,
             {
                 .run = [&context,
                         &state](GuiBoxSystem& box_system) { InstPickerItems(box_system, context, state); },
                 .data =
                     Subpanel {
                         .id = DoBox(box_system,
                                     {
                                         .parent = main_section,
                                         .layout {
                                             .size = {200, layout::k_fill_parent},
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Start,
                                         },
                                     })
                                   .layout_id,
                         .imgui_id = (imgui::Id)SourceLocationHash(),
                         .debug_name = "items",
                     },
             });

    // Vertical divider
    DoBox(box_system,
          {
              .parent = main_section,
              .background_fill = style::Colour::Surface2,
              .layout {
                  .size = {box_system.imgui.PixelsToVw(1), layout::k_fill_parent},
              },
          });

    AddPanel(box_system,
             {
                 .run = [&context,
                         &state](GuiBoxSystem& box_system) { InstPickerFilters(box_system, context, state); },
                 .data =
                     Subpanel {
                         .id = DoBox(box_system,
                                     {
                                         .parent = main_section,
                                         .layout {
                                             .size = {200, layout::k_fill_parent},
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Start,
                                         },
                                     })
                                   .layout_id,
                         .imgui_id = (imgui::Id)SourceLocationHash(),
                         .debug_name = "filters",
                     },
             });

    // Horizontal divider
    DoBox(box_system,
          {
              .parent = root,
              .background_fill = style::Colour::Surface2,
              .layout {
                  .size = {layout::k_fill_parent, box_system.imgui.PixelsToVw(1)},
              },
          });

    AddPanel(box_system,
             {
                 .run = [&context, &state](
                            GuiBoxSystem& box_system) { InstPickerStatusBar(box_system, context, state); },
                 .data =
                     Subpanel {
                         .id = DoBox(box_system,
                                     {
                                         .parent = root,
                                         .layout {
                                             .size = {layout::k_fill_parent, 50},
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Start,
                                         },
                                     })
                                   .layout_id,
                         .imgui_id = (imgui::Id)SourceLocationHash(),
                         .debug_name = "status bar",
                     },
             });
}

PUBLIC void DoInstPickerPopup(GuiBoxSystem& box_system,
                              imgui::Id popup_id,
                              Rect absolute_button_rect,
                              InstPickerContext& context,
                              InstPickerState& state) {
    RunPanel(box_system,
             Panel {
                 .run =
                     [&context, &state](GuiBoxSystem& box_system) {
                         // Setup
                         if (box_system.state->pass == BoxSystemPanelState::Pass::LayoutBoxes) {
                             context.libraries =
                                 sample_lib_server::AllLibrariesRetained(context.sample_library_server,
                                                                         box_system.arena);
                             Sort(context.libraries,
                                  [](auto const& a, auto const& b) { return a->name < b->name; });
                         }

                         InstPickerPopup(box_system, context, state);

                         // Shutdown
                         if (box_system.state->pass == BoxSystemPanelState::Pass::HandleInputAndRender)
                             sample_lib_server::ReleaseAll(context.libraries);
                     },
                 .data =
                     PopupPanel {
                         .creator_absolute_rect = absolute_button_rect,
                         .popup_imgui_id = popup_id,
                     },
             });
}
