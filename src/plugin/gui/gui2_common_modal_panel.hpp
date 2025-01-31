// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui_framework/gui_box_system.hpp"

// Creates the root container for a panel
static Box DoModalRootBox(GuiBoxSystem& box_system) {
    return DoBox(box_system,
                 {
                     .layout {
                         .size = box_system.imgui.PixelsToPoints(box_system.imgui.Size()),
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                     },
                 });
}

// Configuration structs for panel components
struct ModalHeaderConfig {
    Box parent;
    String title;
    TrivialFunctionRef<void()> on_close;
};

struct ModalTabConfig {
    String icon;
    String text;
};

struct ModalTabBarConfig {
    Box parent;
    Span<ModalTabConfig const> tabs;
    u32& current_tab_index;
};

// Creates a standard panel header with title and close button
static Box DoModalHeader(GuiBoxSystem& box_system, ModalHeaderConfig const& config) {
    auto const title_container = DoBox(box_system,
                                       {
                                           .parent = config.parent,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_padding = {.lrtb = style::k_spacing},
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Justify,
                                           },
                                       });

    DoBox(box_system,
          {
              .parent = title_container,
              .text = config.title,
              .font = FontType::Heading1,
              .size_from_text = true,
          });

    if (auto const close = DoBox(box_system,
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
        config.on_close();
    }

    return title_container;
}

// Creates a horizontal divider line
static Box DoModalDivider(GuiBoxSystem& box_system, Box parent) {
    return DoBox(box_system,
                 {
                     .parent = parent,
                     .background_fill = style::Colour::Surface2,
                     .layout {
                         .size = {layout::k_fill_parent, box_system.imgui.PixelsToPoints(1)},
                     },
                 });
}

// Creates a tab bar with configurable tabs
static Box DoModalTabBar(GuiBoxSystem& box_system, ModalTabBarConfig const& config) {
    auto const tab_container = DoBox(box_system,
                                     {
                                         .parent = config.parent,
                                         .background_fill = style::Colour::Background1,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Start,
                                         },
                                     });

    for (auto const [i, tab] : Enumerate<u32>(config.tabs)) {
        bool const is_current = i == config.current_tab_index;

        auto const tab_box = DoBox(
            box_system,
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

        if (tab_box.button_fired) config.current_tab_index = i;

        DoBox(box_system,
              {
                  .parent = tab_box,
                  .text = tab.icon,
                  .font = FontType::Icons,
                  .text_fill = is_current ? style::Colour::Subtext0 : style::Colour::Surface2,
                  .size_from_text = true,
              });
        DoBox(box_system,
              {
                  .parent = tab_box,
                  .text = tab.text,
                  .text_fill = is_current ? style::Colour::Text : style::Colour::Subtext0,
                  .size_from_text = true,
              });
    }

    return tab_container;
}

struct ModalConfig {
    String title;
    TrivialFunctionRef<void()> on_close;
    Span<ModalTabConfig const> tabs;
    u32& current_tab_index;
};

// High-level function that creates a complete modal layout
static Box DoModal(GuiBoxSystem& box_system, ModalConfig const& config) {
    auto const root = DoModalRootBox(box_system);

    DoModalHeader(box_system,
                  {
                      .parent = root,
                      .title = config.title,
                      .on_close = config.on_close,
                  });

    DoModalDivider(box_system, root);

    DoModalTabBar(box_system,
                  {
                      .parent = root,
                      .tabs = config.tabs,
                      .current_tab_index = config.current_tab_index,
                  });

    DoModalDivider(box_system, root);

    return root;
}
