// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_window.hpp"

#include "gui_drawing_helpers.hpp"
#include "gui_editor_ui_style.hpp"

imgui::WindowSettings PopupWindowSettings(imgui::Context const& imgui) {
    auto res = imgui::DefPopup();
    auto const rounding = editor::GetSize(imgui, UiSizeId::PopupWindowRounding);
    res.pad_top_left = {1, rounding};
    res.pad_bottom_right = {1, rounding};
    res.draw_routine_popup_background = [rounding](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;
        draw::DropShadow(s, r, rounding);
        s.graphics->AddRectFilled(r.Min(), r.Max(), GMC(PopupWindowBack), rounding);
        s.graphics->AddRect(r.Min(), r.Max(), GMC(PopupWindowOutline), rounding);
    };
    res.draw_routine_scrollbar = [](IMGUI_DRAW_WINDOW_SCROLLBAR_ARGS) {
        s.graphics->AddRectFilled(bounds.Min(), bounds.Max(), GMC(PopupScrollbarBack));
        uint32_t handle_col = GMC(PopupScrollbarHandle);
        if (s.IsHotOrActive(id)) handle_col = GMC(PopupScrollbarHandleHover);
        s.graphics->AddRectFilled(handle_rect.Min(),
                                  handle_rect.Max(),
                                  handle_col,
                                  editor::GetSize(s, UiSizeId::CornerRounding));
    };
    res.scrollbar_width = editor::GetSize(imgui, UiSizeId::ScrollbarWidth);
    return res;
}

imgui::WindowSettings StandalonePopupSettings(imgui::Context const& imgui) {
    auto res = PopupWindowSettings(imgui);
    res.draw_routine_window_background = res.draw_routine_popup_background;
    res.flags = 0;
    res.pad_top_left = {editor::GetSize(imgui, UiSizeId::StandaloneWindowPadL),
                        editor::GetSize(imgui, UiSizeId::StandaloneWindowPadT)};
    res.pad_bottom_right = {editor::GetSize(imgui, UiSizeId::StandaloneWindowPadR),
                            editor::GetSize(imgui, UiSizeId::StandaloneWindowPadB)};
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
    wnd_settings.scrollbar_width = editor::GetSize(imgui, UiSizeId::ScrollbarWidth);
    wnd_settings.draw_routine_scrollbar = [](IMGUI_DRAW_WINDOW_SCROLLBAR_ARGS) {
        const auto rounding = editor::GetSize(s, UiSizeId::CornerRounding);
        s.graphics->AddRectFilled(bounds.Min(), bounds.Max(), GMC(ScrollbarBack), rounding);
        uint32_t handle_col = GMC(ScrollbarHandle);
        if (s.IsHot(id))
            handle_col = GMC(ScrollbarHandleHover);
        else if (s.IsActive(id))
            handle_col = GMC(ScrollbarHandleActive);
        s.graphics->AddRectFilled(handle_rect.Min(), handle_rect.Max(), handle_col, rounding);
    };
    return wnd_settings;
}
