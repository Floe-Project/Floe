// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui.hpp"
#include "gui_editor_ui_style.hpp"
#include "gui_label_widgets.hpp"
#include "gui_widget_helpers.hpp"

struct PopupMenuItems {
    PopupMenuItems(Gui* _g, Span<String const> _items) {
        m_items = _items;
        m_g = _g;

        m_g->imgui.graphics->context->PushFont(m_g->roboto_small);
        m_h = editor::GetSize(m_g->imgui, UiSizeId::MenuItemHeight);

        m_div_gap_x = editor::GetSize(m_g->imgui, UiSizeId::MenuItemDividerGapX);
        m_div_h = editor::GetSize(m_g->imgui, UiSizeId::MenuItemDividerH);

        m_w = MenuItemWidth(m_g, m_items);
        m_y_pos = 0;
    }
    ~PopupMenuItems() { m_g->imgui.graphics->context->PopFont(); }

    void DoFakeButton(String text) {
        labels::Label(m_g, GetItemRect(), text, labels::FakeMenuItem());
        m_y_pos += m_h;
    }

    bool DoButton(String text, String tooltip = {}, bool closes_popup = true) {
        bool result = false;
        auto id = m_g->imgui.GetID(text);
        auto r = GetItemRect();
        if (buttons::Button(m_g, id, r, text, buttons::MenuItem(closes_popup))) result = true;
        m_y_pos += m_h;
        if (tooltip.size) Tooltip(m_g, id, r, tooltip);

        return result;
    }
    bool DoButton(int index, String tooltip = {}) { return DoButton(m_items[(usize)index], tooltip); }

    bool DoToggleButton(String text, bool& state, String tooltip = {}, imgui::Id id = {}) {
        bool result = false;
        if (!id) id = m_g->imgui.GetID(text);
        auto r = GetItemRect();
        if (buttons::Toggle(m_g, id, r, state, text, buttons::MenuToggleItem(true))) result = true;
        m_y_pos += m_h;
        if (tooltip.size) Tooltip(m_g, id, r, tooltip);
        return result;
    }

    bool DoToggleButton(int index, bool& state, String tooltip = {}, imgui::Id id = {}) {
        return DoToggleButton(m_items[(usize)index], state, tooltip, id);
    }

    bool DoSubMenuButton(String text, imgui::Id popup_id) {
        bool result = false;
        if (buttons::Popup(m_g, popup_id, GetItemRect(), text, buttons::SubMenuItem())) result = true;
        m_y_pos += m_h;
        return result;
    }
    bool DoSubMenuButton(int index, imgui::Id popup_id) {
        return DoSubMenuButton(m_items[(usize)index], popup_id);
    }

    bool DoMultipleMenuItems(int& current) {
        int clicked = -1;
        for (auto const i : Range(m_items.size)) {
            auto str = m_items[i];
            bool state = (int)i == current;
            if (DoToggleButton(str, state)) clicked = (int)i;
        }
        if (clicked != -1 && current != clicked) {
            current = clicked;
            return true;
        }
        return false;
    }

    void Divider() {
        Rect div_r = {m_div_gap_x, m_y_pos + (m_div_h / 2), m_g->imgui.Width() - 2 * m_div_gap_x, 1};
        m_g->imgui.RegisterAndConvertRect(&div_r);
        m_g->imgui.graphics->AddRectFilled(div_r.Min(), div_r.Max(), GMC(PopupItemDivider));
        m_y_pos += m_div_h;
    }

    Rect GetLastItemRect() { return m_item_rect; }

  private:
    Rect GetItemRect() {
        m_item_rect = {0, m_y_pos, m_w, m_h};
        return m_item_rect;
    }

    Rect m_item_rect {};
    Gui* m_g;
    Span<String const> m_items;
    f32 m_div_gap_x, m_div_h;
    f32 m_w, m_h;
    f32 m_y_pos;
};
