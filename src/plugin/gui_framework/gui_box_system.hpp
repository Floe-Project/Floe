// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "fonts.hpp"
#include "gui/gui_drawing_helpers.hpp"
#include "gui_imgui.hpp"
#include "layout.hpp"
#include "style.hpp"

// GUI Box System (working prototype)
//
//
// This is a new GUI system that we intend to use universally. For now only a couple of parts use it.
//
// This API is a mostly a wrapper on top of the existing Gui systems. When we do the GUI overhaul the
// underlying systems will improve makes some aspects of this API better.
//
// It's an IMGUI system. No state is shared across frames, but within each frame we create a tree of boxes and
// perform flexbox-like layout on them. This 2-pass approach (1. layout, 2. handle input + render) is
// transparent to the user of this API. They just define layout, input-handling and rendering all in the same
// place.
//
// An overview of the system:
// - Panels corresspond to the Windows in our current imgui system, accessing some functionality from them:
//   auto-sizing, 'popup' functionality and scrollbars. In the future we might not need panels to be separate
//   things but for now they are. They contain a set of boxes and optionally subpanels. Each panel has a
//   'panel function'. This is where everything happens. In a panel function you can add other panels - these
//   will be run after the current panel.
// - Boxes are the basic building block of the system. Boxes are configured using a bit BoxConfig struct.
//   Designated initialisers are great and this whole system relies on them.
//
// IMPORTANT: you must have the same boxes in the same order within every frame. For example if you are
// getting data from an external function that may produce different results based on when it's called, and
// building boxes based on it, cache the data and use that.
//
// The flexbox-like layout system is in layout.hpp.
//

struct GuiBoxSystem;

using PanelFunction = TrivialFixedSizeFunction<16, void(GuiBoxSystem&)>;

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
    bool transparent_panel;
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

struct Box {
    layout::Id layout_id;
    imgui::Id imgui_id;
    bool32 is_hot : 1 = false;
    bool32 is_active : 1 = false;
    bool32 button_fired : 1 = false;
    imgui::TextInputResult const* text_input_result {};
};

struct GuiBoxSystem {
    enum class State {
        LayoutBoxes,
        HandleInputAndRender,
    };

    struct WordWrappedText {
        layout::Id id;
        String text;
        graphics::Font* font;
        f32 font_size;
    };

    ArenaAllocator& arena;
    imgui::Context& imgui;
    Fonts& fonts;
    layout::Context& layout;
    bool show_tooltips;

    Panel* current_panel {};
    u32 box_counter {};

    State state {State::LayoutBoxes};
    DynamicArray<Box> boxes {arena};
    DynamicArray<WordWrappedText> word_wrapped_texts {arena};
    bool mouse_down_on_modal_background = false;
    imgui::TextInputResult last_text_input_result {};

    f32 const scrollbar_width = imgui.VwToPixels(8);
    f32 const scrollbar_padding = imgui.VwToPixels(style::k_scrollbar_rhs_space);
    imgui::DrawWindowScrollbar* const draw_scrollbar = [](IMGUI_DRAW_WINDOW_SCROLLBAR_ARGS) {
        u32 handle_col = style::Col(style::Colour::Surface1);
        if (imgui.IsHotOrActive(id)) handle_col = style::Col(style::Colour::Surface2);
        imgui.graphics->AddRectFilled(handle_rect.Min(), handle_rect.Max(), handle_col, imgui.VwToPixels(4));
    };

    imgui::DrawWindowBackground* const draw_window = [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto const rounding = imgui.VwToPixels(style::k_panel_rounding);
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
        .pad_top_left = {1, imgui.VwToPixels(style::k_panel_rounding)},
        .pad_bottom_right = {1, imgui.VwToPixels(style::k_panel_rounding)},
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

PUBLIC f32 HeightOfWrappedText(GuiBoxSystem& box_system, layout::Id id, f32 width) {
    for (auto& t : box_system.word_wrapped_texts)
        if (id == t.id) return t.font->CalcTextSizeA(t.font_size, FLT_MAX, width, t.text)[1];
    return 0;
}

PUBLIC void AddPanel(GuiBoxSystem& box_system, Panel panel) {
    if (box_system.state == GuiBoxSystem::State::HandleInputAndRender) {
        auto p = box_system.arena.New<Panel>(panel);
        if (box_system.current_panel->first_child)
            box_system.current_panel->first_child->next = p;
        else
            box_system.current_panel->first_child = p;
    }
}

PUBLIC void Run(GuiBoxSystem& builder, Panel* panel) {
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
                        if (builder.imgui.frame_input.Mouse(MouseButton::Left).presses.size) {
                            [[maybe_unused]] int b = 0;
                            modal.on_close();
                        }
                    }
                }
            }

            auto settings = builder.modal_window_settings;
            if (modal.auto_height) settings.flags |= imgui::WindowFlags_AutoHeight;
            if (modal.transparent_panel) settings.draw_routine_window_background = {};

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
        dyn::Clear(builder.boxes);
        dyn::Clear(builder.word_wrapped_texts);

        builder.box_counter = 0;
        builder.state = GuiBoxSystem::State::LayoutBoxes;
        panel->run(builder);

        builder.layout.item_height_from_width_calculation = [&builder](layout::Id id, f32 width) {
            return HeightOfWrappedText(builder, id, width);
        };

        layout::RunContext(builder.layout);

        builder.box_counter = 0;
        builder.state = GuiBoxSystem::State::HandleInputAndRender;
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

PUBLIC void RunPanel(GuiBoxSystem& builder, Panel initial_panel) {
    auto panel = builder.arena.New<Panel>(initial_panel);
    Run(builder, panel);
}

enum class ActivationClickEvent : u32 { None, Down, Up, Count };

enum class TextAlignX : u32 { Left, Centre, Right, Count };
enum class TextAlignY : u32 { Top, Centre, Bottom, Count };

PUBLIC f32x2 AlignWithin(Rect container, f32x2 size, TextAlignX align_x, TextAlignY align_y) {
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

constexpr f32 k_no_wrap = 0;
constexpr f32 k_wrap_to_parent = -1; // set size_from_text = true
constexpr f32 k_default_font_size = 0;

enum class TextInputBox : u32 { None, SingleLine, MultiLine, Count };

struct BoxConfig {
    Optional<Box> parent {};

    String text {};
    f32 font_size = k_default_font_size; // see k_default_font_size
    f32 wrap_width = k_no_wrap; // see k_no_wrap and k_wrap_to_parent
    FontType font : NumBitsNeededToStore(ToInt(FontType::Count)) {FontType::Body};
    style::Colour text_fill : style::k_colour_bits = style::Colour::Text;
    style::Colour text_fill_hot : style::k_colour_bits = style::Colour::Text;
    style::Colour text_fill_active : style::k_colour_bits = style::Colour::Text;
    bool32 size_from_text : 1 = false; // sets layout.size for you
    TextAlignX text_align_x : NumBitsNeededToStore(ToInt(TextAlignX::Count)) = TextAlignX::Left;
    TextAlignY text_align_y : NumBitsNeededToStore(ToInt(TextAlignY::Count)) = TextAlignY::Top;

    style::Colour background_fill : style::k_colour_bits = style::Colour::None;
    style::Colour background_fill_hot : style::k_colour_bits = style::Colour::None;
    style::Colour background_fill_active : style::k_colour_bits = style::Colour::None;
    bool32 background_fill_auto_hot_active_overlay : 1 = false;
    bool32 drop_shadow : 1 = false;

    style::Colour border : style::k_colour_bits = style::Colour::None;
    style::Colour border_hot : style::k_colour_bits = style::Colour::None;
    style::Colour border_active : style::k_colour_bits = style::Colour::None;
    bool32 border_auto_hot_active_overlay : 1 = false;

    // 4 bits, clockwise from top-left: top-left, top-right, bottom-right, bottom-left, set using 0b0001 etc.
    u32 round_background_corners : 4 = 0;

    TextInputBox text_input_box : NumBitsNeededToStore(ToInt(TextInputBox::Count)) = TextInputBox::None;
    style::Colour text_input_cursor : style::k_colour_bits = style::Colour::Text;
    style::Colour text_input_selection : style::k_colour_bits = style::Colour::Highlight;

    MouseButton activate_on_click_button
        : NumBitsNeededToStore(ToInt(MouseButton::Count)) = MouseButton::Left;
    ActivationClickEvent activation_click_event
        : NumBitsNeededToStore(ToInt(ActivationClickEvent::Count)) = ActivationClickEvent::None;
    bool32 parent_dictates_hot_and_active : 1 = false;
    u8 extra_margin_for_mouse_events = 0;

    layout::ItemOptions layout {};

    String tooltip {};
};

static bool Tooltip(GuiBoxSystem& builder, imgui::Id id, Rect r, String str) {
    if (!builder.show_tooltips) return false;

    auto& imgui = builder.imgui;
    if (imgui.WasJustMadeHot(id))
        imgui.AddTimedWakeup(imgui.frame_input.current_time + style::k_tooltip_open_delay, "Tooltip");

    auto hot_seconds = imgui.SecondsSpentHot();
    if (imgui.IsHot(id) && hot_seconds >= style::k_tooltip_open_delay) {
        builder.imgui.graphics->context->PushFont(builder.fonts[ToInt(FontType::Body)]);
        DEFER { builder.imgui.graphics->context->PopFont(); };

        auto const font = imgui.overlay_graphics.context->CurrentFont();
        auto const pad_x = imgui.VwToPixels(style::k_tooltip_pad_x);
        auto const pad_y = imgui.VwToPixels(style::k_tooltip_pad_y);

        auto text_size = draw::GetTextSize(font, str, imgui.VwToPixels(style::k_tooltip_max_width));

        Rect popup_r;
        popup_r.x = r.x;
        popup_r.y = r.y + r.h;
        popup_r.w = text_size.x + pad_x * 2;
        popup_r.h = text_size.y + pad_y * 2;

        auto const cursor_pos = imgui.frame_input.cursor_pos;

        // shift the x so that it's centred on the cursor
        popup_r.x = cursor_pos.x - popup_r.w / 2;

        popup_r.pos = imgui::BestPopupPos(popup_r, r, imgui.frame_input.window_size.ToFloat2(), false);

        f32x2 text_start;
        text_start.x = popup_r.x + pad_x;
        text_start.y = popup_r.y + pad_y;

        draw::DropShadow(imgui, popup_r);
        imgui.overlay_graphics.AddRectFilled(popup_r.Min(),
                                             popup_r.Max(),
                                             style::Col(style::Colour::Background0),
                                             style::k_tooltip_rounding);
        imgui.overlay_graphics.AddText(font,
                                       font->font_size,
                                       text_start,
                                       style::Col(style::Colour::Text),
                                       str,
                                       text_size.x + 1);
        return true;
    }
    return false;
}

PUBLIC Box DoBox(GuiBoxSystem& builder, BoxConfig const& config) {
    auto const box_index = builder.box_counter++;
    auto const font = builder.fonts[ToInt(config.font)];
    auto const font_size =
        config.font_size != 0 ? builder.imgui.VwToPixels(config.font_size) : font->font_size;

    // IMPORTANT: if the string is very long, it needs to be word-wrapped manually by including newlines in
    // the text. This is necessary because our text rendering system is bad at doing huge amounts of
    // word-wrapping. It still renders text that isn't visible unless there's no word-wrapping, in which case
    // it's does skip rendering off-screen text.
    f32 const wrap_width = config.text.size < 10000 ? config.wrap_width : k_no_wrap;

    switch (builder.state) {
        case GuiBoxSystem::State::LayoutBoxes: {
            auto const box = Box {
                .layout_id = layout::CreateItem(
                    builder.layout,
                    ({
                        layout::ItemOptions layout = config.layout;

                        if (config.parent) [[likely]]
                            layout.parent = config.parent->layout_id;

                        layout.size =
                            Max(builder.imgui.pixels_per_vw * layout.size, f32x2(layout::k_fill_parent));

                        layout.margins.lrtb *= builder.imgui.pixels_per_vw;
                        layout.contents_gap *= builder.imgui.pixels_per_vw;
                        layout.contents_padding.lrtb *= builder.imgui.pixels_per_vw;

                        if (config.size_from_text) {
                            if (wrap_width != k_wrap_to_parent)
                                layout.size =
                                    font->CalcTextSizeA(font_size, FLT_MAX, wrap_width, config.text);
                            else {
                                // We can't know the text size until we know the parent width.
                                layout.size = {layout::k_fill_parent, 1};
                                layout.set_item_height_after_width_calculated = true;
                            }
                        }

                        layout;
                    })),
                .imgui_id = {},
            };

            if (config.size_from_text && wrap_width == k_wrap_to_parent) {
                dyn::Append(builder.word_wrapped_texts,
                            {
                                .id = box.layout_id,
                                .text = builder.arena.Clone(config.text),
                                .font = font,
                                .font_size = font_size,
                            });
            }

            dyn::Append(builder.boxes, box);

            return box;
        }
        case GuiBoxSystem::State::HandleInputAndRender: {
            auto& box = builder.boxes[box_index];
            auto const rect =
                builder.imgui.GetRegisteredAndConvertedRect(layout::GetRect(builder.layout, box.layout_id));
            auto const mouse_rect =
                rect.Expanded(builder.imgui.VwToPixels(config.extra_margin_for_mouse_events));

            if (config.activation_click_event != ActivationClickEvent::None) {
                imgui::ButtonFlags button_flags {
                    .left_mouse = config.activate_on_click_button == MouseButton::Left,
                    .right_mouse = config.activate_on_click_button == MouseButton::Right,
                    .middle_mouse = config.activate_on_click_button == MouseButton::Middle,
                    .triggers_on_mouse_down = config.activation_click_event == ActivationClickEvent::Down,
                    .triggers_on_mouse_up = config.activation_click_event == ActivationClickEvent::Up,
                };
                box.imgui_id = builder.imgui.GetID((usize)box_index);
                box.button_fired = builder.imgui.ButtonBehavior(mouse_rect, box.imgui_id, button_flags);
                box.is_active = builder.imgui.IsActive(box.imgui_id);
                box.is_hot = builder.imgui.IsHot(box.imgui_id);
            }
            if (config.text_input_box != TextInputBox::None) {
                box.imgui_id = builder.imgui.GetID((usize)box_index);
                builder.last_text_input_result = builder.imgui.TextInput(
                    mouse_rect,
                    box.imgui_id,
                    config.text,
                    config.text_input_box == TextInputBox::MultiLine
                        ? imgui::TextInputFlags {.multiline = true, .multiline_wordwrap_hack = true}
                        : imgui::TextInputFlags {},
                    {.left_mouse = true, .triggers_on_mouse_down = true},
                    false);
                box.is_active = builder.imgui.TextInputHasFocus(box.imgui_id);
                box.is_hot = builder.imgui.IsHot(box.imgui_id);
                box.text_input_result = &builder.last_text_input_result;
            }
            bool32 const is_active =
                config.parent_dictates_hot_and_active ? config.parent->is_active : box.is_active;
            bool32 const is_hot = config.parent_dictates_hot_and_active ? config.parent->is_hot : box.is_hot;

            if (auto const background_fill = ({
                    style::Colour c {};
                    if (config.background_fill_auto_hot_active_overlay)
                        c = config.background_fill;
                    else if (is_active)
                        c = config.background_fill_active;
                    else if (is_hot)
                        c = config.background_fill_hot;
                    else
                        c = config.background_fill;
                    c;
                });
                background_fill != style::Colour::None || config.background_fill_auto_hot_active_overlay) {

                auto r = rect;
                // if we normally don't show a background, then we can assume that hot/active colours are
                // exclusively for the mouse so we should use the mouse rect
                if (config.background_fill == style::Colour::None) r = mouse_rect;

                auto const rounding =
                    config.round_background_corners ? builder.imgui.VwToPixels(style::k_button_rounding) : 0;

                u32 col_u32 = style::Col(background_fill);
                if (config.background_fill_auto_hot_active_overlay) {
                    if (is_hot)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_hot_white_overlay)
                                          : style::k_auto_hot_white_overlay;
                    else if (is_active)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_active_white_overlay)
                                          : style::k_auto_active_white_overlay;
                }

                if (config.drop_shadow) draw::DropShadow(builder.imgui, r, rounding);
                builder.imgui.graphics->AddRectFilled(r, col_u32, rounding, config.round_background_corners);
            }

            if (auto const border = ({
                    style::Colour c {};
                    if (config.border_auto_hot_active_overlay)
                        c = config.border;
                    else if (is_active)
                        c = config.border_active;
                    else if (is_hot)
                        c = config.border_hot;
                    else
                        c = config.border;
                    c;
                });
                border != style::Colour::None || config.border_auto_hot_active_overlay) {

                auto r = rect;
                if (config.border == style::Colour::None) r = mouse_rect;

                auto const rounding =
                    config.round_background_corners ? builder.imgui.VwToPixels(style::k_button_rounding) : 0;

                u32 col_u32 = style::Col(border);
                if (config.border_auto_hot_active_overlay) {
                    if (is_hot)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_hot_white_overlay)
                                          : style::k_auto_hot_white_overlay;
                    else if (is_active)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_active_white_overlay)
                                          : style::k_auto_active_white_overlay;
                }

                builder.imgui.graphics->AddRect(r, col_u32, rounding, config.round_background_corners);
            }

            if (config.text.size && config.text_input_box == TextInputBox::None) {
                if (config.text_align_x != TextAlignX::Left || config.text_align_y != TextAlignY::Top) {
                    auto const text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0, config.text);
                    auto const text_pos =
                        AlignWithin(rect, text_size, config.text_align_x, config.text_align_y);
                    builder.imgui.graphics->AddText(font,
                                                    font_size,
                                                    text_pos,
                                                    style::Col(is_hot      ? config.text_fill_hot
                                                               : is_active ? config.text_fill_active
                                                                           : config.text_fill),
                                                    config.text,
                                                    wrap_width == k_wrap_to_parent ? rect.w : wrap_width);
                } else {
                    builder.imgui.graphics->AddText(font,
                                                    font_size,
                                                    rect.pos,
                                                    style::Col(is_hot      ? config.text_fill_hot
                                                               : is_active ? config.text_fill_active
                                                                           : config.text_fill),
                                                    config.text,
                                                    wrap_width == k_wrap_to_parent ? rect.w : wrap_width);
                }
            }

            if (config.text_input_box != TextInputBox::None) {
                auto input_result = box.text_input_result;
                ASSERT(input_result);

                if (input_result->HasSelection()) {
                    imgui::TextInputResult::SelectionIterator it {*builder.imgui.graphics->context};
                    while (auto const r = input_result->NextSelectionRect(it))
                        builder.imgui.graphics->AddRectFilled(
                            *r,
                            colours::WithAlpha(style::Col(config.text_input_selection), 100));
                }

                if (input_result->show_cursor) {
                    auto cursor_r = input_result->GetCursorRect();
                    builder.imgui.graphics->AddRectFilled(cursor_r.Min(),
                                                          cursor_r.Max(),
                                                          style::Col(config.text_input_cursor));
                }

                builder.imgui.graphics->AddText(input_result->GetTextPos(),
                                                style::Col(config.text_fill),
                                                input_result->text);
            }

            if (config.tooltip.size) Tooltip(builder, box.imgui_id, rect, config.tooltip);

            return box;
        }
    }

    return {};
}

// =================================================================================================================
// Helpers
PUBLIC Rect CentredRect(Rect container, f32x2 size) {
    return {
        .pos = container.pos + (container.size - size) / 2,
        .size = size,
    };
}
