// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/engine.hpp"
#include "gui/gui2_common_modal_panel.hpp"
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
};

static void InstPickerWaveformItems(GuiBoxSystem& box_system,
                                    InstPickerContext& context,
                                    InstPickerState&,
                                    Box const root) {

    for (auto const waveform_type : EnumIterator<WaveformType>()) {
        auto const item = DoBox(box_system,
                                {
                                    .parent = root,
                                    .text = k_waveform_type_names[ToInt(waveform_type)],
                                    .font = FontType::Body,
                                    .background_fill_auto_hot_active_overlay = true,
                                    .activate_on_click_button = MouseButton::Left,
                                    .activation_click_event = ActivationClickEvent::Up,
                                    .layout =
                                        {
                                            .size = {layout::k_fill_parent, 20},
                                        },
                                });
        if (item.button_fired) {
            LoadInstrument(context.engine, context.layer.index, waveform_type);
            box_system.imgui.CloseCurrentPopup();
        }
    }
}

static void InstPickerItems(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
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

    if (state.tab == InstPickerState::Tab::Waveforms) {
        InstPickerWaveformItems(box_system, context, state, root);
        return;
    }

    state.hovering_inst = InstrumentType::None;

    Optional<Optional<String>> previous_folder {};
    for (auto const& l_ptr : context.libraries) {
        auto const& lib = *l_ptr;

        if (lib.sorted_instruments.size == 0) continue;
        if (lib.file_format_specifics.tag != state.FileFormatForCurrentTab()) continue;
        if (state.selected_library_hashes.size && !Contains(state.selected_library_hashes, lib.Id().Hash()))
            continue;

        Optional<graphics::TextureHandle> lib_icon_tex {};
        if (auto const imgs = LibraryImagesFromLibraryId(context.library_images,
                                                         box_system.imgui,
                                                         lib.Id(),
                                                         context.sample_library_server,
                                                         box_system.arena);
            imgs && imgs->icon) {
            lib_icon_tex = box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(imgs->icon);
        }

        for (auto const inst_ptr : lib.sorted_instruments) {
            auto const& inst = *inst_ptr;
            auto const folder = inst.folder;

            if (state.search.size && (!ContainsCaseInsensitiveAscii(inst.name, state.search) &&
                                      !ContainsCaseInsensitiveAscii(inst.folder.ValueOr({}), state.search)))
                continue;

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

            auto const inst_id = sample_lib::InstrumentId {lib.Id(), inst.name};
            auto const is_current = context.layer.instrument_id == inst_id;

            auto const item =
                DoBox(box_system,
                      {
                          .parent = root,
                          .background_fill = is_current ? style::Colour::Highlight : style::Colour::None,
                          .background_fill_auto_hot_active_overlay = true,
                          .activate_on_click_button = MouseButton::Left,
                          .activation_click_event = ActivationClickEvent::Up,
                          .layout =
                              {
                                  .size = {layout::k_fill_parent, 20},
                                  .contents_direction = layout::Direction::Row,
                              },
                      });

            if (item.is_hot) {
                state.hovering_inst = inst_id;
                box_system.imgui.frame_output.ElevateUpdateRequest(
                    GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
            }
            if (item.button_fired) {
                if (is_current) {
                    LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
                } else {
                    LoadInstrument(context.engine,
                                   context.layer.index,
                                   sample_lib::InstrumentId {
                                       .library = lib.Id(),
                                       .inst_name = inst.name,
                                   });
                    box_system.imgui.CloseCurrentPopup();
                }
            }

            if (lib_icon_tex) {
                DoBox(box_system,
                      {
                          .parent = item,
                          .background_tex = *lib_icon_tex,
                          .layout {
                              .size = {20, 20},
                              .margins = {.r = 3},
                          },
                      });
            }

            DoBox(box_system,
                  {
                      .parent = item,
                      .text = inst.name,
                      .font = FontType::Body,
                      .layout =
                          {
                              .size = layout::k_fill_parent,
                          },
                  });
        }
    }
}

static void InstPickerFilters(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
    if (state.tab == InstPickerState::Tab::Waveforms) return;

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
        if (lib.file_format_specifics.tag != state.FileFormatForCurrentTab()) continue;

        auto const lib_id_hash = lib.Id().Hash();
        auto const is_selected = Contains(state.selected_library_hashes, lib_id_hash);

        auto const button =
            DoBox(box_system,
                  {
                      .parent = library_container,
                      .background_fill = is_selected ? style::Colour::Highlight : style::Colour::None,
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

        if (button.button_fired) {
            if (is_selected)
                dyn::RemoveValue(state.selected_library_hashes, lib_id_hash);
            else
                dyn::Append(state.selected_library_hashes, lib_id_hash);
        }

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

    constexpr auto k_spacing = 8.0f;

    // Heading
    {
        auto const heading_box = DoBox(box_system,
                                       {
                                           .parent = root,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_padding = {.lr = k_spacing, .tb = k_spacing / 2},
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Justify,
                                           },
                                       });
        DoBox(box_system,
              {
                  .parent = heading_box,
                  .text = fmt::Format(box_system.arena, "Layer {} Instrument", context.layer.index + 1),
                  .font = FontType::Heading1,
                  .size_from_text = true,
              });

        if (context.layer.instrument_id.tag != InstrumentType::None) {
            if (TextButton(box_system,
                           heading_box,
                           fmt::Format(box_system.arena, "Unload {}", context.layer.InstName()),
                           "")) {
                LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
                box_system.imgui.CloseCurrentPopup();
            }
        }
    }

    {
        auto const tab_row = DoBox(box_system,
                                   {
                                       .parent = root,
                                       .background_fill = style::Colour::Background2,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                       },
                                   });

        for (auto const tab_index : Range(ToInt(InstPickerState::Tab::Count))) {
            auto const tab =
                DoBox(box_system,
                      {
                          .parent = tab_row,
                          .background_fill = tab_index == ToInt(state.tab) ? style::Colour::Background0
                                                                           : style::Colour::None,
                          .background_fill_auto_hot_active_overlay = true,
                          .round_background_corners = 0b1100,
                          .activate_on_click_button = MouseButton::Left,
                          .activation_click_event = ActivationClickEvent::Up,
                          .layout =
                              {
                                  .size = layout::k_hug_contents,
                              },
                      });

            DoBox(box_system,
                  {
                      .parent = tab,
                      .text = ({
                          String s {};
                          switch (InstPickerState::Tab(tab_index)) {
                              case InstPickerState::Tab::FloeLibaries: s = "Floe Instruments"; break;
                              case InstPickerState::Tab::MirageLibraries: s = "Mirage Instruments"; break;
                              case InstPickerState::Tab::Waveforms: s = "Waveforms"; break;
                              case InstPickerState::Tab::Count: PanicIfReached(); break;
                          }
                          s;
                      }),
                      .text_fill =
                          tab_index == ToInt(state.tab) ? style::Colour::Text : style::Colour::Subtext0,
                      .size_from_text = true,
                      .layout =
                          {
                              .margins = {.lr = 8, .tb = 4},
                          },
                  });

            if (tab.button_fired) state.tab = InstPickerState::Tab(tab_index);
        }
    }

    constexpr auto k_instrument_list_width = 200.0f;
    constexpr auto k_filter_list_width = 200.0f;

    {
        auto const headings_row = DoBox(box_system,
                                        {
                                            .parent = root,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::Alignment::Start,
                                            },
                                        });

        DoBox(box_system,
              {
                  .parent = headings_row,
                  .text = "Instruments",
                  .font = FontType::Heading2,
                  .layout =
                      {
                          .size = {k_instrument_list_width - k_spacing, style::k_font_heading2_size},
                          .margins = {.l = k_spacing, .tb = k_spacing / 2},
                      },
              });

        // divider
        DoBox(box_system,
              {
                  .parent = headings_row,
                  .background_fill = style::Colour::Surface2,
                  .layout {
                      .size = {box_system.imgui.PixelsToVw(1), layout::k_fill_parent},
                  },
              });

        DoBox(box_system,
              {
                  .parent = headings_row,
                  .text = "Filters",
                  .font = FontType::Heading2,
                  .layout =
                      {
                          .size = {k_filter_list_width - k_spacing, style::k_font_heading2_size},
                          .margins = {.l = k_spacing, .tb = k_spacing / 2},
                      },
              });
    }

    // divider
    DoBox(box_system,
          {
              .parent = root,
              .background_fill = style::Colour::Surface2,
              .layout {
                  .size = {layout::k_fill_parent, box_system.imgui.PixelsToVw(1)},
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

    {
        auto const lhs = DoBox(box_system,
                               {
                                   .parent = main_section,
                                   .layout {
                                       .size = {200, layout::k_fill_parent},
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                   },
                               });

        {
            auto const search_box = DoBox(box_system,
                                          {
                                              .parent = lhs,
                                              .background_fill = style::Colour::Surface1,
                                              .round_background_corners = 0b1111,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .margins = {.lr = k_spacing, .t = k_spacing},
                                                  .contents_direction = layout::Direction::Row,
                                              },
                                          });
            // Icon
            DoBox(box_system,
                  {
                      .parent = search_box,
                      .text = ICON_FA_SEARCH,
                      .font = FontType::Icons,
                      .text_fill = style::Colour::Subtext0,
                      .layout {
                          .size = {20, 20},
                          .margins = {.l = 8},
                      },
                  });

            auto const initial_size = state.search.size;

            if (auto const text_input = DoBox(box_system,
                                              {
                                                  .parent = search_box,
                                                  .text = state.search,
                                                  .text_input_box = TextInputBox::SingleLine,
                                                  .text_input_cursor = style::Colour::Text,
                                                  .text_input_selection = style::Colour::Highlight,
                                                  .layout {
                                                      .size = layout::k_fill_parent,
                                                  },
                                              });
                text_input.text_input_result && text_input.text_input_result->buffer_changed) {
                dyn::Assign(state.search, text_input.text_input_result->text);
            }

            if (initial_size) {
                if (DoBox(box_system,
                          {
                              .parent = search_box,
                              .text = ICON_FA_TIMES,
                              .font = FontType::Icons,
                              .text_fill = style::Colour::Subtext0,
                              .background_fill_auto_hot_active_overlay = true,
                              .activate_on_click_button = MouseButton::Left,
                              .activation_click_event = ActivationClickEvent::Up,
                              .layout {
                                  .size = {20, 20},
                                  .margins = {.l = 8},
                              },
                          })
                        .button_fired) {
                    dyn::Clear(state.search);
                }
            }
        }

        AddPanel(box_system,
                 {
                     .run = [&context, &state](
                                GuiBoxSystem& box_system) { InstPickerItems(box_system, context, state); },
                     .data =
                         Subpanel {
                             .id = DoBox(box_system,
                                         {
                                             .parent = lhs,
                                             .layout {
                                                 .size = layout::k_fill_parent,
                                             },
                                         })
                                       .layout_id,
                             .imgui_id = (imgui::Id)SourceLocationHash(),
                             .debug_name = "items",
                         },
                 });
    }

    // Vertical divider
    DoBox(box_system,
          {
              .parent = main_section,
              .background_fill = style::Colour::Surface2,
              .layout {
                  .size = {box_system.imgui.PixelsToVw(1), layout::k_fill_parent},
              },
          });

    {
        auto const rhs = DoBox(box_system,
                               {
                                   .parent = main_section,
                                   .layout {
                                       .size = {200, layout::k_fill_parent},
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                   },
                               });

        AddPanel(box_system,
                 {
                     .run = [&context, &state](
                                GuiBoxSystem& box_system) { InstPickerFilters(box_system, context, state); },
                     .data =
                         Subpanel {
                             .id = DoBox(box_system,
                                         {
                                             .parent = rhs,
                                             .layout {
                                                 .size = layout::k_fill_parent,
                                             },
                                         })
                                       .layout_id,
                             .imgui_id = (imgui::Id)SourceLocationHash(),
                             .debug_name = "filters",
                         },
                 });
    }

    // Horizontal divider
    DoBox(box_system,
          {
              .parent = root,
              .background_fill = style::Colour::Surface2,
              .layout {
                  .size = {layout::k_fill_parent, box_system.imgui.PixelsToVw(1)},
              },
          });

    {
        auto const status_bar = DoBox(box_system,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_direction = layout::Direction::Column,
                                              .contents_align = layout::Alignment::Start,
                                          },
                                      });

        switch (state.hovering_inst.tag) {
            case InstrumentType::None: break;
            case InstrumentType::WaveformSynth: {
                auto const waveform_type = state.hovering_inst.GetFromTag<InstrumentType::WaveformSynth>();

                DoBox(box_system,
                      {
                          .parent = status_bar,
                          .text = fmt::Format(
                              box_system.arena,
                              "{} waveform. A simple waveform useful for layering with sample instruments.",
                              k_waveform_type_names[ToInt(waveform_type)]),
                          .wrap_width = k_wrap_to_parent,
                          .font = FontType::Body,
                          .size_from_text = true,
                      });

                break;
            }
            case InstrumentType::Sampler: {
                auto const sampled_inst = state.hovering_inst.GetFromTag<InstrumentType::Sampler>();

                sample_lib::Instrument const* i {};
                for (auto const& lib : context.libraries) {
                    if (lib->Id() == sampled_inst.library) {
                        auto i_ptr = lib->insts_by_name.Find(sampled_inst.inst_name);
                        if (i_ptr) {
                            i = *i_ptr;
                            break;
                        }
                    }
                }

                if (!i) break;

                DynamicArray<char> buf {box_system.arena};
                fmt::Append(buf, "{} from {} by {}.", i->name, i->library.name, i->library.author);

                if (i->description) fmt::Append(buf, " {}", i->description);

                fmt::Append(buf, "\nTags: ");
                if (i->tags.size == 0)
                    fmt::Append(buf, "None");
                else
                    for (auto const t : i->tags)
                        fmt::Append(buf, "{} ", t);

                DoBox(box_system,
                      {
                          .parent = status_bar,
                          .text = buf,
                          .wrap_width = k_wrap_to_parent,
                          .font = FontType::Body,
                          .size_from_text = true,
                      });
            }
        }
    }
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
                         if (box_system.state->pass == BoxSystemCurrentPanelState::Pass::LayoutBoxes) {
                             context.libraries =
                                 sample_lib_server::AllLibrariesRetained(context.sample_library_server,
                                                                         box_system.arena);
                             Sort(context.libraries,
                                  [](auto const& a, auto const& b) { return a->name < b->name; });
                         }

                         InstPickerPopup(box_system, context, state);

                         // Shutdown
                         if (box_system.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender)
                             sample_lib_server::ReleaseAll(context.libraries);
                     },
                 .data =
                     PopupPanel {
                         .creator_absolute_rect = absolute_button_rect,
                         .popup_imgui_id = popup_id,
                     },
             });
}
