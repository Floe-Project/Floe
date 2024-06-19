// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_window.hpp"

#include "framework/gui_live_edit.hpp"
#include "gui_drawing_helpers.hpp"

imgui::WindowSettings PopupWindowSettings(imgui::Context const& imgui) {
    auto res = imgui::DefPopup();
    auto const rounding = live_edit::Size(imgui, UiSizeId::PopupWindowRounding);
    res.pad_top_left = {1, rounding};
    res.pad_bottom_right = {1, rounding};
    res.draw_routine_popup_background = [rounding](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;
        draw::DropShadow(imgui, r, rounding);
        imgui.graphics->AddRectFilled(r.Min(), r.Max(), GMC(PopupWindowBack), rounding);
        imgui.graphics->AddRect(r.Min(), r.Max(), GMC(PopupWindowOutline), rounding);
    };
    res.draw_routine_scrollbar = [](IMGUI_DRAW_WINDOW_SCROLLBAR_ARGS) {
        imgui.graphics->AddRectFilled(bounds.Min(), bounds.Max(), GMC(PopupScrollbarBack));
        uint32_t handle_col = GMC(PopupScrollbarHandle);
        if (imgui.IsHotOrActive(id)) handle_col = GMC(PopupScrollbarHandleHover);
        imgui.graphics->AddRectFilled(handle_rect.Min(),
                                      handle_rect.Max(),
                                      handle_col,
                                      live_edit::Size(imgui, UiSizeId::CornerRounding));
    };
    res.scrollbar_width = live_edit::Size(imgui, UiSizeId::ScrollbarWidth);
    return res;
}

imgui::WindowSettings StandalonePopupSettings(imgui::Context const& imgui) {
    auto res = PopupWindowSettings(imgui);
    res.draw_routine_window_background = res.draw_routine_popup_background;
    res.flags = 0;
    res.pad_top_left = {live_edit::Size(imgui, UiSizeId::StandaloneWindowPadL),
                        live_edit::Size(imgui, UiSizeId::StandaloneWindowPadT)};
    res.pad_bottom_right = {live_edit::Size(imgui, UiSizeId::StandaloneWindowPadR),
                            live_edit::Size(imgui, UiSizeId::StandaloneWindowPadB)};
    return res;
}

imgui::WindowSettings
FloeWindowSettings(imgui::Context const& imgui,
                   TrivialFixedSizeFunction<48, void(IMGUI_DRAW_WINDOW_BG_ARGS)> const& draw) {
    auto wnd_settings = imgui::DefWindow();
    wnd_settings.draw_routine_window_background = draw;
    wnd_settings.pad_top_left = {0, 0};
    wnd_settings.pad_bottom_right = {0, 0};
    wnd_settings.flags = imgui::WindowFlags_NoScrollbarX;
    wnd_settings.scrollbar_width = live_edit::Size(imgui, UiSizeId::ScrollbarWidth);
    wnd_settings.draw_routine_scrollbar = [](IMGUI_DRAW_WINDOW_SCROLLBAR_ARGS) {
        auto const rounding = live_edit::Size(imgui, UiSizeId::CornerRounding);
        imgui.graphics->AddRectFilled(bounds.Min(), bounds.Max(), GMC(ScrollbarBack), rounding);
        uint32_t handle_col = GMC(ScrollbarHandle);
        if (imgui.IsHot(id))
            handle_col = GMC(ScrollbarHandleHover);
        else if (imgui.IsActive(id))
            handle_col = GMC(ScrollbarHandleActive);
        imgui.graphics->AddRectFilled(handle_rect.Min(), handle_rect.Max(), handle_col, rounding);
    };
    return wnd_settings;
}
