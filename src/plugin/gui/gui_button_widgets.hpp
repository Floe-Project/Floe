// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "framework/draw_list.hpp"
#include "gui_fwd.hpp"
#include "layout.hpp"
#include "param_info.hpp"

namespace buttons {

enum class LayoutAndSizeType {
    None,
    IconOrText,
    IconOrTextKeyboardIcon,
    IconAndText,
    IconAndTextMenuItem,
    IconAndTextSubMenuItem,
    IconAndTextMidiButton,
    IconAndTextLayerTab,
    VelocityButton
};

struct ColourSet {
    u32 reg {0};
    u32 on {0};
    u32 hot_on {0};
    u32 hot_off {0};
    u32 active_on {0};
    u32 active_off {0};
    u32 greyed_out {0};
    u32 greyed_out_on {0};
    bool grey_out_aware {false};
};

struct Style {
    static constexpr f32 k_regular_icon_scaling = 0.85f;
    static constexpr f32 k_large_icon_scaling = 1.0f;

    Style& ClosesPopups(bool state) {
        closes_popups = state;
        return *this;
    }

    Style& WithLargeIcon() {
        icon_scaling = k_regular_icon_scaling;
        return *this;
    }

    Style& WithIconScaling(f32 v) {
        icon_scaling = v;
        return *this;
    }

    Style& WithRandomiseIconScaling() {
        icon_scaling = 0.72f;
        return *this;
    }

    LayoutAndSizeType type {LayoutAndSizeType::IconOrText};
    f32 icon_scaling = 1.0f;
    f32 text_scaling = 1.0f;
    ColourSet main_cols {};
    ColourSet text_cols {}; // used if there is text as well as an icon
    ColourSet back_cols {};
    bool closes_popups {};
    bool greyed_out {};
    bool no_tooltips {};
    bool draw_with_overlay_graphics {};
    int corner_rounding_flags {~0};

    struct {
        bool add_margin_x {};
        TextOverflowType overflow_type {};
        TextJustification justification {};
        String default_icon {};
        bool capitalise {};
    } icon_or_text;

    struct {
        String on_icon {};
        String off_icon {};
        bool capitalise {};
    } icon_and_text;

    struct {
        param_values::VelocityMappingMode index {};
    } velocity_button;
};

Style IconButton();
Style SettingsWindowButton();
Style TopPanelIconButton();
Style BrowserIconButton();
Style LayerHeadingButton(u32 highlight_col = {});
Style ParameterToggleButton(u32 highlight_col = {});
Style LayerTabButton(bool has_dot);
Style ParameterPopupButton(bool greyed_out = false);
Style InstSelectorPopupButton();
Style PresetsPopupButton();
Style MidiButton();
Style PresetsBrowserFolderButton();
Style PresetsBrowserFileButton();
Style PresetsBrowserPopupButton();
Style MenuItem(bool closes_popups);
Style MenuToggleItem(bool closes_popups);
Style SubMenuItem();
PUBLIC Style LicencesFoldButton() { return MenuItem(false); }

PUBLIC Style VelocityButton(param_values::VelocityMappingMode index) {
    Style s {};
    s.type = LayoutAndSizeType::VelocityButton;
    s.velocity_button.index = index;
    return s;
}

Style EffectButtonGrabber();
Style EffectHeading(u32 back_col);

//
//
//

struct ButtonReturnObject {
    bool changed;
    imgui::Id id;
};

// Full
bool Button(Gui* g, imgui::Id id, Rect r, String str, Style const& style);
bool Toggle(Gui* g, imgui::Id id, Rect r, bool& state, String str, Style const& style);
bool Popup(Gui* g, imgui::Id button_id, imgui::Id popup_id, Rect r, String str, Style const& style);

// No imgui ID
bool Button(Gui* g, Rect r, String str, Style const& style);
bool Toggle(Gui* g, Rect r, bool& state, String str, Style const& style);
bool Popup(Gui* g, imgui::Id popup_id, Rect r, String str, Style const& style);

// Params
ButtonReturnObject Toggle(Gui* g, Parameter const& param, Rect r, String str, Style const& style);
ButtonReturnObject Toggle(Gui* g, Parameter const& param, Rect r, Style const& style);
ButtonReturnObject PopupWithItems(Gui* g, Parameter const& param, Rect r, Style const& style);

void FakeButton(Gui* g, Rect r, String str, Style const& style);

// LayID
bool Button(Gui* g, imgui::Id id, LayID lay_id, String str, Style const& style);
bool Toggle(Gui* g, imgui::Id id, LayID lay_id, bool& state, String str, Style const& style);
bool Popup(Gui* g, imgui::Id button_id, imgui::Id popup_id, LayID lay_id, String str, Style const& style);

bool Button(Gui* g, LayID lay_id, String str, Style const& style);
bool Toggle(Gui* g, LayID lay_id, bool& state, String str, Style const& style);
bool Popup(Gui* g, imgui::Id popup_id, LayID lay_id, String str, Style const& style);

ButtonReturnObject Toggle(Gui* g, Parameter const& param, LayID lay_id, String str, Style const& style);
ButtonReturnObject Toggle(Gui* g, Parameter const& param, LayID lay_id, Style const& style);
ButtonReturnObject PopupWithItems(Gui* g, Parameter const& param, LayID lay_id, Style const& style);

void FakeButton(Gui* g, LayID lay_id, String str, Style const& style);
void FakeButton(Gui* g, Rect r, String str, Style const& style);
void FakeButton(Gui* g, Rect r, String str, bool state, Style const& style);

} // namespace buttons
