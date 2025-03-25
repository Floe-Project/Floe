// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/gui2_common_modal_panel.hpp"
#include "gui_framework/gui_box_system.hpp"

constexpr auto k_picker_item_height = 20.0f;
constexpr auto k_picker_spacing = 8.0f;

struct PickerItemOptions {
    Box parent;
    String text;
    bool is_current;
    Optional<graphics::TextureHandle> icon;
};

PUBLIC Box DoPickerItem(GuiBoxSystem& box_system, PickerItemOptions const& options) {
    auto const item =
        DoBox(box_system,
              {
                  .parent = options.parent,
                  .background_fill = options.is_current ? style::Colour::Highlight : style::Colour::None,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .activate_on_click_button = MouseButton::Left,
                  .activation_click_event = ActivationClickEvent::Up,
                  .layout =
                      {
                          .size = {layout::k_fill_parent, k_picker_item_height},
                          .contents_direction = layout::Direction::Row,
                      },
              });

    if (options.icon) {
        DoBox(box_system,
              {
                  .parent = item,
                  .background_tex = *options.icon,
                  .layout {
                      .size = {k_picker_item_height, k_picker_item_height},
                      .margins = {.r = k_picker_spacing / 2},
                  },
              });
    }

    DoBox(box_system,
          {
              .parent = item,
              .text = options.text,
              .font = FontType::Body,
              .layout =
                  {
                      .size = layout::k_fill_parent,
                  },
          });

    return item;
}

PUBLIC Box DoPickerItemsRoot(GuiBoxSystem& box_system) {
    return DoBox(box_system,
                 {
                     .layout {
                         .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                         .contents_gap = k_picker_spacing,
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                     },
                 });
}

PUBLIC Box DoFilterButton(GuiBoxSystem& box_system,
                          Box const& parent,
                          bool is_selected,
                          Optional<graphics::TextureHandle> icon,
                          String text) {
    auto const button =
        DoBox(box_system,
              {
                  .parent = parent,
                  .background_fill = is_selected ? style::Colour::Highlight : style::Colour::None,
                  .background_fill_active = style::Colour::Highlight,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .activate_on_click_button = MouseButton::Left,
                  .activation_click_event = ActivationClickEvent::Up,
                  .layout {
                      .size = {layout::k_hug_contents, k_picker_item_height},
                      .contents_padding = {.r = k_picker_spacing / 2},
                      .contents_gap = {k_picker_spacing / 2, 0},
                  },
              });

    if (icon) {
        DoBox(box_system,
              {
                  .parent = button,
                  .background_tex = icon,
                  .layout {
                      .size = {k_picker_item_height, k_picker_item_height},
                      .margins = {.r = 3},
                  },
              });
    }

    DoBox(box_system,
          {
              .parent = button,
              .text = text,
              .font = FontType::Body,
              .text_fill = style::Colour::Text,
              .text_fill_hot = style::Colour::Text,
              .text_fill_active = style::Colour::Text,
              .size_from_text = true,
              .parent_dictates_hot_and_active = true,
              .layout =
                  {
                      .margins = {.l = icon ? 0 : k_picker_spacing / 2},
                  },
          });

    return button;
}

struct PickerItemsSectionOptions {
    Box parent;
    Optional<String> heading;
    bool heading_is_folder;
    bool multiline_contents;
};

PUBLIC Box DoPickerItemsSectionContainer(GuiBoxSystem& box_system, PickerItemsSectionOptions const& options) {
    auto const container = DoBox(box_system,
                                 {
                                     .parent = options.parent,
                                     .layout =
                                         {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_direction = layout::Direction::Column,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                         },
                                 });

    if (options.heading) {
        DynamicArrayBounded<char, 200> buf;

        String text = *options.heading;

        if (options.heading_is_folder) {
            buf = *options.heading;
            for (auto& c : buf)
                c = ToUppercaseAscii(c);
            dyn::Replace(buf, "/"_s, ": "_s);

            text = buf;
        }

        DoBox(box_system,
              {
                  .parent = container,
                  .text = text,
                  .font = FontType::Heading3,
                  .size_from_text = true,
                  .layout {
                      .margins = {.b = k_picker_spacing / 2},
                  },
              });
    }

    if (!options.multiline_contents) return container;

    return DoBox(box_system,
                 {
                     .parent = container,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_direction = layout::Direction::Row,
                         .contents_multiline = true,
                         .contents_align = layout::Alignment::Start,
                     },
                 });
}

struct PickerPopupOptions {
    struct Button {
        String text {};
        String tooltip {};
        f32 icon_scaling {};
        FunctionRef<void()> on_fired {};
    };

    struct SearchBar {
        String text {};
        FunctionRef<void(imgui::TextInputResult const&)> on_change {};
        FunctionRef<void()> on_clear {};
    };

    struct Column {
        String title {};
        f32 width {};
        Span<Button const> icon_buttons {};
        FunctionRef<void(GuiBoxSystem&)> do_items {};
    };

    String title {};
    f32 height {}; // VW

    Span<ModalTabConfig const> tab_config {};
    u32& current_tab_index;

    Column lhs {};
    Column rhs {};

    Optional<Button> lhs_top_button {};
    Optional<SearchBar> lhs_search {};

    f32 status_bar_height {};
    FunctionRef<void(GuiBoxSystem&)> on_status_bar {};
};

static void DoPickerPopup(GuiBoxSystem& box_system, PickerPopupOptions const& options) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = {layout::k_hug_contents, options.height},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    DoBox(box_system,
          {
              .parent = root,
              .text = options.title,
              .font = FontType::Heading2,
              .size_from_text = true,
              .layout {
                  .margins = {.lrtb = k_picker_spacing},
              },
          });

    {
        DoModalTabBar(box_system,
                      {
                          .parent = root,
                          .tabs = options.tab_config,
                          .current_tab_index = options.current_tab_index,
                      });
    }

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

        {
            auto const lhs_top =
                DoBox(box_system,
                      {
                          .parent = headings_row,
                          .layout {
                              .size = {options.lhs.width, layout::k_hug_contents},
                              .contents_padding = {.lr = k_picker_spacing, .tb = k_picker_spacing / 2},
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            DoBox(box_system,
                  {
                      .parent = lhs_top,
                      .text = options.lhs.title,
                      .font = FontType::Heading2,
                      .layout {
                          .size = {layout::k_fill_parent, style::k_font_heading2_size},
                      },
                  });

            for (auto const& btn : options.lhs.icon_buttons) {
                if (IconButton(box_system,
                               lhs_top,
                               btn.text,
                               btn.tooltip,
                               style::k_font_heading2_size * btn.icon_scaling,
                               style::k_font_heading2_size)
                        .button_fired) {
                    dyn::Append(box_system.state->deferred_actions, [&]() { btn.on_fired(); });
                }
            }
        }

        DoModalDivider(box_system, headings_row, DividerType::Vertical);

        {
            auto const rhs_top =
                DoBox(box_system,
                      {
                          .parent = headings_row,
                          .layout {
                              .size = {options.rhs.width, layout::k_hug_contents},
                              .contents_padding = {.lr = k_picker_spacing, .tb = k_picker_spacing / 2},
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            DoBox(box_system,
                  {
                      .parent = rhs_top,
                      .text = options.rhs.title,
                      .font = FontType::Heading2,
                      .layout {
                          .size = {layout::k_fill_parent, style::k_font_heading2_size},
                      },
                  });

            for (auto const& btn : options.lhs.icon_buttons) {
                if (IconButton(box_system,
                               rhs_top,
                               btn.text,
                               btn.tooltip,
                               style::k_font_heading2_size * btn.icon_scaling,
                               style::k_font_heading2_size)
                        .button_fired) {
                    dyn::Append(box_system.state->deferred_actions, [&]() { btn.on_fired(); });
                }
            }
        }
    }

    DoModalDivider(box_system, root, DividerType::Horizontal);

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
                                       .size = {options.lhs.width, layout::k_fill_parent},
                                       .contents_padding = {.lr = k_picker_spacing, .t = k_picker_spacing},
                                       .contents_gap = k_picker_spacing,
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                   },
                               });

        {
            if (auto const& btn = options.lhs_top_button) {
                if (TextButton(box_system, lhs, btn->text, btn->tooltip, true))
                    dyn::Append(box_system.state->deferred_actions, [&]() { btn->on_fired(); });
            }

            if (auto const& search = options.lhs_search) {
                auto const search_box =
                    DoBox(box_system,
                          {
                              .parent = lhs,
                              .background_fill = style::Colour::Background2,
                              .round_background_corners = 0b1111,
                              .layout {
                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                  .contents_padding = {.lr = k_picker_spacing / 2},
                                  .contents_direction = layout::Direction::Row,
                                  .contents_align = layout::Alignment::Start,
                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                              },
                          });

                DoBox(box_system,
                      {
                          .parent = search_box,
                          .text = ICON_FA_SEARCH,
                          .font_size = k_picker_item_height * 0.9f,
                          .font = FontType::Icons,
                          .text_fill = style::Colour::Subtext0,
                          .size_from_text = true,
                      });

                if (auto const text_input =
                        DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = search->text,
                                  .text_input_box = TextInputBox::SingleLine,
                                  .text_input_cursor = style::Colour::Text,
                                  .text_input_selection = style::Colour::Highlight,
                                  .layout {
                                      .size = {layout::k_fill_parent, k_picker_item_height},
                                  },
                              });
                    text_input.text_input_result && text_input.text_input_result->buffer_changed) {
                    dyn::Append(box_system.state->deferred_actions,
                                [&]() { search->on_change(*text_input.text_input_result); });
                }

                if (search->text.size) {
                    if (DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = ICON_FA_TIMES,
                                  .font_size = k_picker_item_height * 0.9f,
                                  .font = FontType::Icons,
                                  .text_fill = style::Colour::Subtext0,
                                  .size_from_text = true,
                                  .background_fill_auto_hot_active_overlay = true,
                                  .activate_on_click_button = MouseButton::Left,
                                  .activation_click_event = ActivationClickEvent::Up,
                              })
                            .button_fired) {
                        dyn::Append(box_system.state->deferred_actions, [&]() { search->on_clear(); });
                    }
                }
            }
        }

        AddPanel(box_system,
                 {
                     .run = [&](GuiBoxSystem& box_system) { options.lhs.do_items(box_system); },
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
                             .debug_name = "lhs",
                         },
                 });
    }

    DoModalDivider(box_system, main_section, DividerType::Vertical);

    {
        auto const rhs = DoBox(box_system,
                               {
                                   .parent = main_section,
                                   .layout {
                                       .size = {options.rhs.width, layout::k_fill_parent},
                                       .contents_padding = {.lr = k_picker_spacing, .t = k_picker_spacing},
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                   },
                               });

        AddPanel(box_system,
                 {
                     .run = [&](GuiBoxSystem& box_system) { options.rhs.do_items(box_system); },
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

    DoModalDivider(box_system, root, DividerType::Horizontal);

    AddPanel(box_system,
             {
                 .run = [&](GuiBoxSystem& box_system) { options.on_status_bar(box_system); },
                 .data =
                     Subpanel {
                         .id = DoBox(box_system,
                                     {
                                         .parent = root,
                                         .layout {
                                             .size = {layout::k_fill_parent, options.status_bar_height},
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

PUBLIC void DoPickerPopup(GuiBoxSystem& box_system,
                          imgui::Id popup_id,
                          Rect absolute_button_rect,
                          PickerPopupOptions const& options) {
    RunPanel(box_system,
             Panel {
                 .run = [&](GuiBoxSystem& box_system) { DoPickerPopup(box_system, options); },
                 .data =
                     PopupPanel {
                         .creator_absolute_rect = absolute_button_rect,
                         .popup_imgui_id = popup_id,
                     },
             });
}
