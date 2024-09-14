// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome5.h>

#include "engine/engine.hpp"
#include "framework/gui_live_edit.hpp"
#include "gui.hpp"
#include "gui_button_widgets.hpp"
#include "gui_dragger_widgets.hpp"
#include "gui_keyboard.hpp"
#include "gui_widget_helpers.hpp"

void BotPanel(Gui* g) {
    auto& imgui = g->imgui;
    auto& lay = g->layout;
    auto& engine = g->engine;

    auto const slider_w = LiveSize(imgui, UiSizeId::MidiKeyboardSlider);
    auto const control_w = LiveSize(imgui, UiSizeId::MidiKeyboardControlWidth);
    auto const button_h = LiveSize(imgui, UiSizeId::MidiKeyboardButtonSize);
    auto const button_ygap = LiveSize(imgui, UiSizeId::MidiKeyboardButtonYGap);

    auto root = lay.CreateRootItem((LayScalar)imgui.Width(),
                                   (LayScalar)imgui.Height(),
                                   LayContainRow | LayContainLayout | LayContainStart);
    auto controls = lay.CreateParentItem(root,
                                         control_w,
                                         (LayScalar)(imgui.Height() * 0.9f),
                                         0,
                                         LayContainRow | LayContainStart);
    auto oct = lay.CreateParentItem(controls,
                                    (LayScalar)(slider_w * 1.5f),
                                    0,
                                    LayBehaveVfill,
                                    LayContainColumn | LayContainMiddle);
    auto oct_up = lay.CreateChildItem(oct, 0, button_h, LayBehaveHfill);
    auto oct_text = lay.CreateChildItem(oct, 0, button_h, LayBehaveHfill);
    lay.SetMargins(oct_text, 0, button_ygap, 0, button_ygap);
    auto oct_dn = lay.CreateChildItem(oct, 0, button_h, LayBehaveHfill);
    auto keyboard = lay.CreateChildItem(root, 0, 0, LayBehaveFill);

    lay.PerformLayout();
    Rect const keyboard_r = lay.GetRect(keyboard);
    Rect const oct_up_r = lay.GetRect(oct_up);
    Rect const oct_dn_r = lay.GetRect(oct_dn);
    Rect const oct_text_r = lay.GetRect(oct_text);
    lay.Reset();

    auto up_id = imgui.GetID("Up");
    ;
    auto dn_id = imgui.GetID("Dn");
    auto& settings = g->settings.settings.gui;
    if (buttons::Button(g, up_id, oct_up_r, ICON_FA_CARET_UP, buttons::IconButton(imgui))) {
        int const new_octave = Min(settings.keyboard_octave + 1, k_octave_highest);
        settings.keyboard_octave = new_octave;
        g->settings.tracking.changed = true;
    }
    if (buttons::Button(g, dn_id, oct_dn_r, ICON_FA_CARET_DOWN, buttons::IconButton(imgui))) {
        int const new_octave = Max(settings.keyboard_octave - 1, k_octave_lowest);
        settings.keyboard_octave = new_octave;
        g->settings.tracking.changed = true;
    }
    Tooltip(g, up_id, oct_up_r, "GUI Keyboard Octave Up"_s);
    Tooltip(g, dn_id, oct_dn_r, "GUI Keyboard Octave Down"_s);

    auto oct_text_id = imgui.GetID("Oct");
    if (draggers::Dragger(g,
                          oct_text_id,
                          oct_text_r,
                          k_octave_lowest,
                          k_octave_highest,
                          settings.keyboard_octave,
                          draggers::DefaultStyle(imgui).WithNoBackground().WithSensitivity(500))) {
        g->settings.tracking.changed = true;
    }
    Tooltip(g, oct_text_id, oct_text_r, "GUI Keyboard Octave - Double Click To Edit"_s);

    if (auto key = KeyboardGui(g, keyboard_r, settings.keyboard_octave)) {
        if (key->is_down)
            engine.processor.events_for_audio_thread.Push(
                GuiNoteClicked {.key = key->note, .velocity = key->velocity});
        else
            engine.processor.events_for_audio_thread.Push(GuiNoteClickReleased {.key = key->note});
        engine.host.request_process(&engine.host);
    }
}
