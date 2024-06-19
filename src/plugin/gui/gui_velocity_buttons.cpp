// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_velocity_buttons.hpp"

#include "param_info.hpp"

static void DrawVelocityButtonBack(imgui::Context const& imgui, Rect r) {
    imgui.graphics->AddRectFilled(r.Min(), r.Max(), GMC(VelocityButton_Back));
}

static void DrawVelocityButtonTop(imgui::Context const& imgui, Rect r, imgui::Id id, bool state) {
    auto col = GMC(VelocityButton_Outline);
    r.x = (f32)(int)r.x;
    r.y = (f32)(int)r.y;
    r.w = (f32)(int)r.w;
    r.h = (f32)(int)r.h;
    if (state) {
        auto dark_r = r.Expanded(-1);
        imgui.graphics->AddRect(dark_r.Min(), dark_r.Max(), GMC(VelocityButton_OutlineActiveInner), 0, -1, 1);
        col = GMC(VelocityButton_OutlineActive);
    }
    if (imgui.IsHot(id)) col = GMC(VelocityButton_OutlineHover);
    imgui.graphics->AddRect(r.Min(), r.Max(), col);
}

imgui::DrawButton* GetVelocityButtonDrawingFunction(param_values::VelocityMappingMode button_ind) {
    switch (button_ind) {
        case param_values::VelocityMappingMode::None:
            return [](IMGUI_DRAW_BUTTON_ARGS) {
                DrawVelocityButtonBack(imgui, r);
                auto col =
                    state ? GMC(VelocityButton_GradientStrongActive) : GMC(VelocityButton_GradientStrong);
                imgui.graphics->AddRectFilled(r.Min(), r.Max(), col);
                DrawVelocityButtonTop(imgui, r, id, state);
            };
        case param_values::VelocityMappingMode::TopToBottom:
            return [](IMGUI_DRAW_BUTTON_ARGS) {
                DrawVelocityButtonBack(imgui, r);
                auto col1 =
                    state ? GMC(VelocityButton_GradientStrongActive) : GMC(VelocityButton_GradientStrong);
                auto col2 = GMC(VelocityButton_GradientWeak);
                imgui.graphics->AddRectFilledMultiColor(r.Min(), r.Max(), col1, col1, col2, col2);
                DrawVelocityButtonTop(imgui, r, id, state);
            };
        case param_values::VelocityMappingMode::BottomToTop:
            return [](IMGUI_DRAW_BUTTON_ARGS) {
                DrawVelocityButtonBack(imgui, r);
                auto col1 = GMC(VelocityButton_GradientWeak);
                auto col2 =
                    state ? GMC(VelocityButton_GradientStrongActive) : GMC(VelocityButton_GradientStrong);
                imgui.graphics->AddRectFilledMultiColor(r.Min(), r.Max(), col1, col1, col2, col2);
                DrawVelocityButtonTop(imgui, r, id, state);
            };
        case param_values::VelocityMappingMode::TopToMiddle:
            return [](IMGUI_DRAW_BUTTON_ARGS) {
                DrawVelocityButtonBack(imgui, r);
                auto fade_r = r;
                fade_r.h /= 2;
                auto col1 =
                    state ? GMC(VelocityButton_GradientStrongActive) : GMC(VelocityButton_GradientStrong);
                auto col2 = GMC(VelocityButton_GradientWeak);
                imgui.graphics->AddRectFilledMultiColor(fade_r.Min(), fade_r.Max(), col1, col1, col2, col2);
                DrawVelocityButtonTop(imgui, r, id, state);
            };
        case param_values::VelocityMappingMode::MiddleOutwards:
            return [](IMGUI_DRAW_BUTTON_ARGS) {
                DrawVelocityButtonBack(imgui, r);
                auto r1 = r;
                auto r2 = r;
                r1.h /= 2;
                r2.h /= 2;
                r2.y += r2.h;

                auto col1 = GMC(VelocityButton_GradientWeak);
                auto col2 =
                    state ? GMC(VelocityButton_GradientStrongActive) : GMC(VelocityButton_GradientStrong);
                imgui.graphics->AddRectFilledMultiColor(r1.Min(), r1.Max(), col1, col1, col2, col2);
                imgui.graphics->AddRectFilledMultiColor(r2.Min(), r2.Max(), col2, col2, col1, col1);
                DrawVelocityButtonTop(imgui, r, id, state);
            };
        case param_values::VelocityMappingMode::MiddleToBottom:
            return [](IMGUI_DRAW_BUTTON_ARGS) {
                DrawVelocityButtonBack(imgui, r);
                auto fade_r = r;
                fade_r.h /= 2;
                fade_r.y += fade_r.h;
                auto col1 = GMC(VelocityButton_GradientWeak);
                auto col2 =
                    state ? GMC(VelocityButton_GradientStrongActive) : GMC(VelocityButton_GradientStrong);
                imgui.graphics->AddRectFilledMultiColor(fade_r.Min(), fade_r.Max(), col1, col1, col2, col2);

                DrawVelocityButtonTop(imgui, r, id, state);
            };
        case param_values::VelocityMappingMode::Count: PanicIfReached(); break;
    }
    return nullptr;
}
