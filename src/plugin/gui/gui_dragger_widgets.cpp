// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_dragger_widgets.hpp"

#include "gui.hpp"
#include "gui_editor_ui_style.hpp"
#include "gui_widget_helpers.hpp"
#include "icons-fa/IconsFontAwesome5.h"

namespace draggers {

Style DefaultStyle() {
    Style s {};
    s.background = GMC(Dragger1Back);
    s.text = GMC(TextInputText);
    s.selection_back = GMC(TextInputSelection);
    s.cursor = GMC(TextInputCursor);
    s.button_style = buttons::IconButton();
    return s;
}

bool Dragger(Gui* g, imgui::Id id, Rect r, int min, int max, int& value, Style const& style) {
    auto settings = imgui::DefTextInputDraggerInt();
    settings.slider_settings.flags = {.slower_with_shift = true, .default_on_ctrl = true};
    settings.slider_settings.sensitivity = style.sensitivity;
    settings.format = style.always_show_plus ? "{+}"_s : "{}"_s;
    settings.slider_settings.draw = [](IMGUI_DRAW_SLIDER_ARGS) {};
    settings.text_input_settings.draw = [&style](IMGUI_DRAW_TEXT_INPUT_ARGS) {
        if (result->HasSelection()) {
            auto selection_r = result->GetSelectionRect();
            s.graphics->AddRectFilled(selection_r.Min(), selection_r.Max(), style.selection_back);
        }

        if (result->show_cursor) {
            auto cursor_r = result->GetCursorRect();
            s.graphics->AddRectFilled(cursor_r.Min(), cursor_r.Max(), style.cursor);
        }

        s.graphics->AddText(result->GetTextPos(), style.text, text);
    };
    settings.text_input_settings.text_flags.centre_align = true;
    return g->imgui.TextInputDraggerInt(settings, r, id, min, max, value);
}

bool Dragger(Gui* g, Parameter const& param, Rect r, Style const& style) {
    auto id = BeginParameterGUI(g, param, r);

    auto& imgui = g->imgui;

    auto result = param.ValueAsInt<int>();
    auto const btn_w = editor::GetSize(imgui, UiSizeId::ParamIntButtonSize);

    Rect left_r = r;
    left_r.w = btn_w;

    Rect right_r = r;
    right_r.x = r.x + r.w - btn_w;
    right_r.w = btn_w;

    Rect dragger_r = r;
    dragger_r.x += btn_w;
    dragger_r.w -= btn_w * 2;

    // draw it around the whole thing, not just the dragger
    if (style.background) {
        auto const converted_r = imgui.GetRegisteredAndConvertedRect(r);
        imgui.graphics->AddRectFilled(converted_r.Min(),
                                      converted_r.Max(),
                                      style.background,
                                      editor::GetSize(imgui, UiSizeId::CornerRounding));
    }

    bool changed = Dragger(g,
                           id,
                           dragger_r,
                           (int)param.info.linear_range.min,
                           (int)param.info.linear_range.max,
                           result,
                           style);

    auto const left_id = id - 4;
    auto const right_id = id + 4;
    if (buttons::Button(g, left_id, left_r, ICON_FA_CARET_LEFT, style.button_style)) {
        result = Max((int)param.info.linear_range.min, result - 1);
        changed = true;
    }
    if (buttons::Button(g, right_id, right_r, ICON_FA_CARET_RIGHT, style.button_style)) {
        result = Min((int)param.info.linear_range.max, result + 1);
        changed = true;
    }
    Tooltip(g, left_id, left_r, "Decrement the value"_s);
    Tooltip(g, right_id, right_r, "Increment the value"_s);

    EndParameterGUI(g,
                    id,
                    param,
                    r,
                    changed ? Optional<f32>((f32)result) : nullopt,
                    ParamDisplayFlagsNoValuePopup);

    return changed;
}

bool Dragger(Gui* g, imgui::Id id, LayID lay_id, int min, int max, int& value, Style const& style) {
    return Dragger(g, id, g->layout.GetRect(lay_id), min, max, value, style);
}
bool Dragger(Gui* g, Parameter const& param, LayID lay_id, Style const& style) {
    return Dragger(g, param, g->layout.GetRect(lay_id), style);
}

} // namespace draggers
