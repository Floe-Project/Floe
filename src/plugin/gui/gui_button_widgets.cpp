// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_button_widgets.hpp"

#include "gui.hpp"
#include "gui_editor_ui_style.hpp"
#include "gui_velocity_buttons.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"
#include "icons-fa/IconsFontAwesome5.h"
#include "param.hpp"

namespace buttons {

Style IconButton() {
    Style s {};
    s.type = LayoutAndSizeType::IconOrText;
    s.main_cols.reg = GMC(IconButton1Regular);
    s.main_cols.on = GMC(IconButton1On);
    s.main_cols.hot_on = GMC(IconButton1Hover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = GMC(IconButton1Active);
    s.main_cols.active_off = s.main_cols.active_on;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.justification = TextJustification::Centred;
    s.icon_scaling = Style::k_regular_icon_scaling;
    return s;
}

Style SettingsWindowButton() {
    auto s = IconButton();
    s.type = LayoutAndSizeType::IconAndText;
    s.text_cols.reg = GMC(SettingsWindowMainText);
    s.text_cols.hot_on = GMC(SettingsWindowHoveredMainText);
    s.text_cols.hot_off = s.text_cols.hot_on;
    s.text_cols.active_on = s.text_cols.reg;
    s.text_cols.active_off = s.text_cols.active_on;
    s.text_cols.on = s.text_cols.reg;
    s.main_cols.reg = GMC(SettingsWindowIconButton);
    s.main_cols.hot_on = s.main_cols.reg;
    s.main_cols.hot_off = s.main_cols.reg;
    s.main_cols.active_on = s.main_cols.reg;
    s.main_cols.active_off = s.main_cols.active_on;
    s.main_cols.on = s.main_cols.reg;
    s.icon_and_text.on_icon = ICON_FA_CHECK_SQUARE;
    s.icon_and_text.off_icon = ICON_FA_SQUARE;
    s.icon_and_text.capitalise = false;
    return s;
}

Style TopPanelIconButton() {
    auto s = IconButton();
    s.main_cols.reg = GMC(TopPanelIconButtonRegular);
    s.main_cols.on = GMC(TopPanelIconButtonOn);
    s.main_cols.hot_on = GMC(TopPanelIconButtonHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = GMC(TopPanelIconButtonActive);
    s.main_cols.active_off = s.main_cols.active_on;
    return s;
}

Style BrowserIconButton() {
    auto s = IconButton();
    s.main_cols.reg = GMC(BrowserIconButtonRegular);
    s.main_cols.on = GMC(BrowserIconButtonOn);
    s.main_cols.hot_on = GMC(BrowserIconButtonHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = GMC(BrowserIconButtonActive);
    s.main_cols.active_on = s.main_cols.active_off;
    return s;
}

Style LayerHeadingButton(u32 highlight_col) {
    Style s {};
    if (!highlight_col) highlight_col = GMCC(ToggleButton, IconOn);
    s.type = LayoutAndSizeType::IconAndText;
    s.main_cols.reg = GMCC(ToggleButton, IconOff);
    s.main_cols.on = highlight_col;
    s.main_cols.hot_off = s.main_cols.reg;
    s.main_cols.hot_on = s.main_cols.on;
    s.main_cols.active_on = s.main_cols.hot_on;
    s.main_cols.active_off = s.main_cols.active_on;
    s.text_cols.reg = GMCC(ToggleButton, TextOff);
    s.text_cols.on = GMCC(ToggleButton, TextOn);
    s.text_cols.hot_on = GMCC(ToggleButton, TextHover);
    s.text_cols.hot_off = s.text_cols.hot_on;
    s.text_cols.active_on = s.text_cols.hot_on;
    s.text_cols.active_off = s.text_cols.active_on;
    s.icon_and_text.on_icon = ICON_FA_CHECK_SQUARE;
    s.icon_and_text.off_icon = ICON_FA_SQUARE;
    s.icon_and_text.capitalise = false;
    s.icon_scaling = 0.65f;
    return s;
}

Style ParameterToggleButton(u32 highlight_col) {
    auto s = LayerHeadingButton(highlight_col);
    s.icon_and_text.on_icon = ICON_FA_TOGGLE_ON;
    s.icon_and_text.off_icon = ICON_FA_TOGGLE_OFF;
    return s;
}

Style LayerTabButton(bool has_dot) {
    Style s {};
    if (!has_dot)
        s.type = LayoutAndSizeType::IconOrText;
    else
        s.type = LayoutAndSizeType::IconAndTextLayerTab;
    s.main_cols.reg = GMC(LayerTabButtonText);
    s.main_cols.on = GMC(LayerTabButtonTextActive);
    s.main_cols.hot_on = GMC(LayerTabButtonTextHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = s.main_cols.hot_on;
    s.main_cols.active_off = s.main_cols.active_on;
    s.text_cols = s.main_cols;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.justification = TextJustification::Centred;
    s.icon_and_text.on_icon = ICON_FA_CIRCLE;
    s.icon_and_text.off_icon = s.icon_and_text.on_icon;
    s.icon_scaling = 0.20f;
    return s;
}

Style ParameterPopupButton(bool _greyed_out) {
    auto s = LayerHeadingButton();
    s.type = LayoutAndSizeType::IconOrText;
    s.main_cols.reg = GMC(MenuButtonText);
    s.main_cols.greyed_out = GMC(MenuButtonTextInactive);
    s.main_cols.greyed_out_on = s.main_cols.greyed_out;
    s.main_cols.on = s.main_cols.reg;
    s.main_cols.hot_on = GMC(MenuButtonTextHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = s.main_cols.hot_on;
    s.main_cols.active_off = s.main_cols.active_on;
    s.main_cols.grey_out_aware = true;
    s.greyed_out = _greyed_out;

    s.icon_or_text.add_margin_x = true;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.overflow_type = TextOverflowType::ShowDotsOnRight;

    s.back_cols.reg = GMC(MenuButtonBack);
    s.back_cols.on = s.back_cols.reg;
    s.back_cols.hot_on = s.back_cols.reg;
    s.back_cols.hot_off = s.back_cols.reg;
    s.back_cols.active_on = s.back_cols.hot_on;
    s.back_cols.active_off = s.back_cols.active_on;
    return s;
}

Style InstSelectorPopupButton() {
    auto s = ParameterPopupButton();
    s.main_cols.grey_out_aware = false;
    s.back_cols = {};
    return s;
}

Style PresetsPopupButton() {
    auto s = ParameterPopupButton();
    s.main_cols.grey_out_aware = false;
    s.back_cols = {};
    return s;
}

Style MidiButton() {
    auto s = ParameterToggleButton();
    s.type = LayoutAndSizeType::IconAndTextMidiButton;
    return s;
}

Style PresetsBrowserFolderButton() {
    Style s {};
    s.type = LayoutAndSizeType::IconOrText;
    s.back_cols.reg = GMC(PresetBrowserFolderButtonBackOff);
    s.back_cols.on = GMC(PresetBrowserFolderButtonBackOn);
    s.back_cols.hot_on = GMC(PresetBrowserFolderButtonBackHover);
    s.back_cols.hot_off = s.back_cols.hot_on;
    s.back_cols.active_on = GMC(PresetBrowserFolderButtonBackActive);
    s.back_cols.active_off = s.back_cols.active_on;
    s.main_cols.reg = GMC(PresetBrowserFolderButtonTextOff);
    s.main_cols.on = GMC(PresetBrowserFolderButtonTextOn);
    s.main_cols.hot_on = GMC(PresetBrowserFolderButtonTextHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = GMC(PresetBrowserFolderButtonTextActive);
    s.main_cols.active_off = s.main_cols.active_on;
    s.icon_or_text.add_margin_x = true;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.overflow_type = TextOverflowType::ShowDotsOnRight;
    return s;
}

Style PresetsBrowserFileButton() {
    Style s {};
    s.type = LayoutAndSizeType::IconOrText;
    s.back_cols.reg = GMC(PresetBrowserFileButtonBackOff);
    s.back_cols.on = GMC(PresetBrowserFileButtonBackOn);
    s.back_cols.hot_on = GMC(PresetBrowserFileButtonBackHover);
    s.back_cols.hot_off = s.back_cols.hot_on;
    s.back_cols.active_on = GMC(PresetBrowserFileButtonBackActive);
    s.back_cols.active_off = s.back_cols.active_on;
    s.main_cols.reg = GMC(PresetBrowserFileButtonTextOff);
    s.main_cols.on = GMC(PresetBrowserFileButtonTextOn);
    s.main_cols.hot_on = GMC(PresetBrowserFileButtonTextHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = GMC(PresetBrowserFileButtonTextActive);
    s.main_cols.active_off = s.main_cols.active_on;
    s.icon_or_text.add_margin_x = true;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.overflow_type = TextOverflowType::ShowDotsOnRight;
    return s;
}

Style PresetsBrowserPopupButton() {
    auto s = ParameterPopupButton();
    s.icon_or_text.overflow_type = TextOverflowType::ShowDotsOnLeft;
    s.main_cols.grey_out_aware = false;
    s.main_cols.reg = GMCC(Browser, FolderPopupButtonText);
    s.main_cols.on = s.main_cols.reg;
    s.main_cols.hot_on = GMCC(Browser, FolderPopupButtonTextHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = s.main_cols.hot_on;
    s.main_cols.active_off = s.main_cols.active_on;
    s.back_cols.reg = GMCC(Browser, FolderPopupButtonBack);
    s.back_cols.on = s.back_cols.reg;
    s.back_cols.hot_on = s.back_cols.reg;
    s.back_cols.hot_off = s.back_cols.hot_on;
    s.back_cols.active_on = s.back_cols.reg;
    s.back_cols.active_off = s.back_cols.active_on;
    return s;
}

Style MenuItem(bool _closes_popups) {
    Style s {};
    s.type = LayoutAndSizeType::IconAndTextMenuItem;
    s.closes_popups = _closes_popups;
    s.back_cols.reg = 0;
    s.back_cols.hot_on = GMC(PopupItemBackHover);
    s.back_cols.hot_off = s.back_cols.hot_on;
    s.back_cols.active_on = GMC(PopupItemBackHover);
    s.back_cols.active_off = s.back_cols.active_on;
    s.back_cols.on = GMC(PopupItemBackHover);
    s.text_cols.reg = GMC(PopupItemText);
    s.text_cols.hot_on = s.text_cols.reg;
    s.text_cols.hot_off = s.text_cols.reg;
    s.text_cols.active_on = s.text_cols.reg;
    s.text_cols.active_off = s.text_cols.active_on;
    s.text_cols.on = s.text_cols.reg;
    s.main_cols.reg = GMC(PopupItemIcon);
    s.main_cols.hot_on = s.main_cols.reg;
    s.main_cols.hot_off = s.main_cols.reg;
    s.main_cols.active_on = s.main_cols.reg;
    s.main_cols.active_off = s.main_cols.active_on;
    s.main_cols.on = s.main_cols.reg;
    s.icon_scaling = 0.7f;
    s.icon_and_text.on_icon = ICON_FA_CHECK;
    return s;
}

Style MenuToggleItem(bool _closes_popups) {
    auto s = MenuItem(_closes_popups);
    s.back_cols.on = 0;
    return s;
}

Style SubMenuItem() {
    auto s = MenuItem(false);
    s.type = LayoutAndSizeType::IconAndTextSubMenuItem;
    s.icon_and_text.on_icon = ICON_FA_CARET_RIGHT;
    s.icon_and_text.off_icon = s.icon_and_text.on_icon;
    return s;
}

Style EffectButtonGrabber() {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::CentredRight;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.default_icon = ICON_FA_ARROWS_ALT_V;
    s.icon_scaling = 0.7f;
    s.main_cols = {};
    s.main_cols.hot_on = GMC(FXButtonGripIcon);
    s.main_cols.hot_off = s.main_cols.hot_on;
    return s;
}

Style EffectHeading(u32 back_col) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::Centred;
    s.main_cols.reg = GMC(FXHeading);
    s.main_cols.active_on = s.main_cols.reg;
    s.main_cols.active_off = s.main_cols.active_on;
    s.main_cols.hot_on = s.main_cols.reg;
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.text_scaling = 1.1f;
    s.icon_or_text.add_margin_x = false;
    s.back_cols.reg = back_col;
    s.back_cols.hot_on = back_col;
    s.back_cols.hot_off = back_col;
    s.back_cols.active_on = back_col;
    s.back_cols.active_off = s.back_cols.active_on;
    s.corner_rounding_flags = 4;
    return s;
}

//
//
//
struct ScopedFont {
    ScopedFont(Gui* g, graphics::Font* font) : m_g(g) {
        if (font) {
            g->gui_platform.graphics_ctx->PushFont(font);
            pushed_font = true;
        }
    }
    ~ScopedFont() {
        if (pushed_font) m_g->gui_platform.graphics_ctx->PopFont();
    }
    Gui* m_g;
    bool pushed_font = false;
};

static u32 GetCol(Gui* g, Style const& style, ColourSet const& colours, imgui::Id id, bool state) {
    auto col = state ? colours.on : colours.reg;
    if (colours.grey_out_aware && style.greyed_out) col = state ? colours.greyed_out_on : colours.greyed_out;
    if (g->imgui.IsHot(id)) col = state ? colours.hot_on : colours.hot_off;
    if (g->imgui.IsActive(id)) col = state ? colours.active_on : colours.active_off;
    return col;
}

static bool DrawBackground(Gui* g, Style const& style, Rect r, imgui::Id id, bool state) {
    if (auto col = GetCol(g, style, style.back_cols, id, state)) {
        auto const rounding = editor::GetSize(g->imgui, UiSizeId::CornerRounding);
        g->imgui.graphics->AddRectFilled(r.Min(), r.Max(), col, rounding, style.corner_rounding_flags);
        return true;
    }
    return false;
}

static String GetTempCapitalisedString(String str) {
    static char buffer[64];
    auto const caps_str_size = Min(str.size, ArraySize(buffer));
    for (auto const i : Range(caps_str_size))
        buffer[i] = ToUppercaseAscii(str[i]);
    return {buffer, caps_str_size};
}

static void DrawKeyboardIcon(Gui* g, Style const& style, Rect r, imgui::Id id, bool state) {
    auto& s = g->imgui;
    DrawBackground(g, style, r, id, state);

    auto const white_width = editor::GetSize(s, UiSizeId::KeyboardIconWhiteWidth) / 100.0f * r.w;
    auto const white_height = editor::GetSize(s, UiSizeId::KeyboardIconWhiteHeight) / 100.0f * r.w;
    auto const rounding = editor::GetSize(s, UiSizeId::KeyboardIconRounding) / 100.0f * r.w;
    auto const black_width = editor::GetSize(s, UiSizeId::KeyboardIconBlackWidth) / 100.0f * r.w;
    auto const black_height = editor::GetSize(s, UiSizeId::KeyboardIconBlackHeight) / 100.0f * r.w;
    auto const gap = Max(1.0f, editor::GetSize(s, UiSizeId::KeyboardIconGap) / 100.0f * r.w);

    auto const total_width = white_width * 3 + gap * 2;
    auto const total_height = white_height;

    f32x2 const start_pos {r.CentreX() - total_width / 2, r.CentreY() - total_height / 2};

    auto col = GetCol(g, style, style.main_cols, id, state);

    {
        {
            Rect kr {start_pos + f32x2 {0, black_height}, f32x2 {white_width, white_height - black_height}};
            s.graphics->AddRectFilled(kr.Min(), kr.Max(), col, rounding, 4 | 8);

            kr.x += white_width + gap;
            s.graphics->AddRectFilled(kr.Min(), kr.Max(), col);

            kr.x += white_width + gap;
            s.graphics->AddRectFilled(kr.Min(), kr.Max(), col, rounding, 1 | 2);
        }

        {
            auto const white_top_width = (total_width - (black_width * 2 + gap * 4)) / 3;
            Rect kr {start_pos, f32x2 {white_top_width, white_height}};

            s.graphics->AddRectFilled(kr.Min(), kr.Max(), col, rounding, 4 | 8);

            kr.x += white_top_width + gap + black_width + gap;
            s.graphics->AddRectFilled(kr.Min(), kr.Max(), col);

            kr.x = start_pos.x + total_width - white_top_width;
            s.graphics->AddRectFilled(kr.Min(), kr.Max(), col, rounding, 1 | 2);
        }
    }
}

static void DrawIconOrText(Gui* g,
                           Style const& style,
                           Rect r,
                           imgui::Id id,
                           String str,
                           bool state,
                           bool using_icon_font) {
    auto& s = g->imgui;
    DrawBackground(g, style, r, id, state);

    if (style.icon_or_text.justification & TextJustification::Left) {
        if (style.icon_or_text.add_margin_x)
            r = r.CutLeft(editor::GetSize(s, UiSizeId::MenuButtonTextMarginL));
    } else if (style.icon_or_text.justification & TextJustification::Right) {
        if (style.icon_or_text.add_margin_x)
            r = r.CutRight(editor::GetSize(s, UiSizeId::MenuButtonTextMarginL));
    }

    if (style.icon_or_text.capitalise) str = GetTempCapitalisedString(str);
    s.graphics->AddTextJustified(r,
                                 str,
                                 GetCol(g, style, style.main_cols, id, state),
                                 style.icon_or_text.justification,
                                 style.icon_or_text.overflow_type,
                                 using_icon_font ? style.icon_scaling : style.text_scaling);
}

static void DrawIconAndTextButton(Gui* g, Style const& style, Rect r, imgui::Id id, String str, bool state) {
    auto& s = g->imgui;

    auto const icon_col = GetCol(g, style, style.main_cols, id, state);
    auto const text_col = GetCol(g, style, style.text_cols, id, state);

    DrawBackground(g, style, r, id, state);

    {
        ScopedFont const icon_font(g, g->icons);
        auto just = TextJustification::CentredLeft;
        auto btn_r = r;
        if (style.type == LayoutAndSizeType::IconAndTextLayerTab) {
            btn_r = r.WithW(editor::GetSize(s, UiSizeId::LayerParamsGroupTabsIconW));
            just = TextJustification::CentredRight;
        } else if (style.type == LayoutAndSizeType::IconAndTextMidiButton) {
            btn_r = r.WithW(editor::GetSize(s, UiSizeId::MIDI_ItemWidth));
            just = TextJustification::CentredRight;
        } else if (style.type == LayoutAndSizeType::IconAndTextMenuItem) {
            btn_r = r.WithW(editor::GetSize(s, UiSizeId::MenuItem_TickWidth))
                        .CutLeft(editor::GetSize(s, UiSizeId::MenuItem_IconMarginX));
        } else if (style.type == LayoutAndSizeType::IconAndTextSubMenuItem) {
            btn_r = r.CutLeft(r.w - editor::GetSize(s, UiSizeId::MenuItem_SubMenuArrowWidth))
                        .CutRight(editor::GetSize(s, UiSizeId::MenuItem_IconMarginX));
            just = TextJustification::CentredRight;
        }
        s.graphics->AddTextJustified(btn_r,
                                     state ? style.icon_and_text.on_icon : style.icon_and_text.off_icon,
                                     icon_col,
                                     just,
                                     TextOverflowType::AllowOverflow,
                                     style.icon_scaling);
    }

    if (style.icon_and_text.capitalise) str = GetTempCapitalisedString(str);

    auto just = TextJustification::CentredLeft;
    auto text_offset = editor::GetSize(s, UiSizeId::Page_HeadingTextOffset);
    if (style.type == LayoutAndSizeType::IconAndTextMidiButton) {
        text_offset =
            editor::GetSize(s, UiSizeId::MIDI_ItemWidth) + editor::GetSize(s, UiSizeId::MIDI_ItemMarginLR);
    } else if (style.type == LayoutAndSizeType::IconAndTextMenuItem ||
               style.type == LayoutAndSizeType::IconAndTextSubMenuItem) {
        text_offset = editor::GetSize(s, UiSizeId::MenuItem_TickWidth);
    } else if (style.type == LayoutAndSizeType::IconAndTextLayerTab) {
        text_offset = 0;
        just = TextJustification::Centred;
    }
    s.graphics->AddTextJustified(r.CutLeft(text_offset),
                                 str,
                                 text_col,
                                 just,
                                 TextOverflowType::AllowOverflow,
                                 style.text_scaling);
}

static bool ButtonInternal(Gui* g,
                           Style const& style,
                           Optional<imgui::Id> id,
                           Optional<imgui::Id> popup_id,
                           Rect r,
                           bool& state,
                           String str) {
    auto s = imgui::DefButton();
    s.window = PopupWindowSettings(g->imgui);
    s.flags.closes_popups = false;
    if (!popup_id && style.closes_popups) s.flags.closes_popups = true;

    s.draw = [g, &style](IMGUI_DRAW_BUTTON_ARGS) {
        switch (style.type) {
            case LayoutAndSizeType::IconOrTextKeyboardIcon: {
                DrawKeyboardIcon(g, style, r, id, state);
                break;
            }
            case LayoutAndSizeType::IconOrText: {
                if (!str.size) str = style.icon_or_text.default_icon;
                ScopedFont const icon_font(g, (str.size && (str[0] & 0x80)) ? g->icons : nullptr);
                DrawIconOrText(g, style, r, id, str, state, icon_font.pushed_font);
                break;
            }
            case LayoutAndSizeType::IconAndTextMenuItem:
            case LayoutAndSizeType::IconAndTextSubMenuItem:
            case LayoutAndSizeType::IconAndTextMidiButton:
            case LayoutAndSizeType::IconAndTextLayerTab:
            case LayoutAndSizeType::IconAndText: {
                DrawIconAndTextButton(g, style, r, id, str, state);
                break;
            }

            case LayoutAndSizeType::VelocityButton:
                GetVelocityButtonDrawingFunction(style.velocity_button.index)(s, r, id, str, state);
                break;
            default: PanicIfReached();
        }
    };

    if (popup_id) {
        ASSERT(id.HasValue());
        ASSERT(!style.draw_with_overlay_graphics);
        return g->imgui.PopupButton(s, r, *id, *popup_id, str);
    } else if (id) {
        ASSERT(!style.draw_with_overlay_graphics);
        return g->imgui.ToggleButton(s, r, *id, state, str);
    } else {
        if (!style.draw_with_overlay_graphics) g->imgui.RegisterAndConvertRect(&r);
        imgui::Id const fake_id = 99;
        auto graphics = g->imgui.graphics;
        if (style.draw_with_overlay_graphics) g->imgui.graphics = &g->imgui.overlay_graphics;
        s.draw(g->imgui, r, fake_id, str, state);
        g->imgui.graphics = graphics;
        return false;
    }
}

bool Toggle(Gui* g, imgui::Id id, Rect r, bool& state, String str, Style const& style) {
    return ButtonInternal(g, style, id, {}, r, state, str);
}

bool Popup(Gui* g, imgui::Id button_id, imgui::Id popup_id, Rect r, String str, Style const& style) {
    bool state = false;
    return ButtonInternal(g, style, button_id, popup_id, r, state, str);
}

bool Button(Gui* g, imgui::Id id, Rect r, String str, Style const& style) {
    bool state = false;
    return Toggle(g, id, r, state, str, style);
}

ButtonReturnObject Toggle(Gui* g, Parameter const& param, Rect r, String str, Style const& style) {
    auto const id = BeginParameterGUI(g, param, r);
    Optional<f32> val {};
    bool state = param.ValueAsBool();
    if (Toggle(g, id, r, state, str, style)) val = state ? 1.0f : 0.0f;
    EndParameterGUI(g,
                    id,
                    param,
                    r,
                    val,
                    style.no_tooltips ? ParamDisplayFlagsNoTooltip : ParamDisplayFlagsDefault);
    return {val.HasValue(), id};
}

ButtonReturnObject Toggle(Gui* g, Parameter const& param, Rect r, Style const& style) {
    return Toggle(g, param, r, param.info.gui_label, style);
}

ButtonReturnObject PopupWithItems(Gui* g, Parameter const& param, Rect r, Style const& style) {
    auto const id = BeginParameterGUI(g, param, r);
    Optional<f32> val {};
    if (Popup(g, id, id + 1, r, ParamMenuText(param.info.index, param.LinearValue()), style)) {
        auto current = param.ValueAsInt<int>();
        if (DoMultipleMenuItems(g, ParameterMenuItems(param.info.index), current)) val = (f32)current;
        g->imgui.EndWindow();
    }
    EndParameterGUI(g,
                    id,
                    param,
                    r,
                    val,
                    style.no_tooltips ? ParamDisplayFlagsNoTooltip : ParamDisplayFlagsDefault);
    return {val.HasValue(), id};
}

bool Button(Gui* g, Rect r, String str, Style const& style) {
    return Button(g, g->imgui.GetID(str), r, str, style);
}

bool Toggle(Gui* g, Rect r, bool& state, String str, Style const& style) {
    return Toggle(g, g->imgui.GetID(str), r, state, str, style);
}

bool Popup(Gui* g, imgui::Id popup_id, Rect r, String str, Style const& style) {
    return Popup(g, g->imgui.GetID(str), popup_id, r, str, style);
}

void FakeButton(Gui* g, Rect r, String str, Style const& style) { FakeButton(g, r, str, false, style); }

void FakeButton(Gui* g, Rect r, String str, bool state, Style const& style) {
    ButtonInternal(g, style, {}, {}, r, state, str);
}

bool Button(Gui* g, imgui::Id id, LayID lay_id, String str, Style const& style) {
    return Button(g, id, g->layout.GetRect(lay_id), str, style);
}
bool Toggle(Gui* g, imgui::Id id, LayID lay_id, bool& state, String str, Style const& style) {
    return Toggle(g, id, g->layout.GetRect(lay_id), state, str, style);
}
bool Popup(Gui* g, imgui::Id button_id, imgui::Id popup_id, LayID lay_id, String str, Style const& style) {
    return Popup(g, button_id, popup_id, g->layout.GetRect(lay_id), str, style);
}

bool Button(Gui* g, LayID lay_id, String str, Style const& style) {
    return Button(g, g->layout.GetRect(lay_id), str, style);
}
bool Toggle(Gui* g, LayID lay_id, bool& state, String str, Style const& style) {
    return Toggle(g, g->layout.GetRect(lay_id), state, str, style);
}
bool Popup(Gui* g, imgui::Id popup_id, LayID lay_id, String str, Style const& style) {
    return Popup(g, popup_id, g->layout.GetRect(lay_id), str, style);
}

ButtonReturnObject Toggle(Gui* g, Parameter const& param, LayID lay_id, String str, Style const& style) {
    return Toggle(g, param, g->layout.GetRect(lay_id), str, style);
}
ButtonReturnObject Toggle(Gui* g, Parameter const& param, LayID lay_id, Style const& style) {
    return Toggle(g, param, g->layout.GetRect(lay_id), style);
}
ButtonReturnObject PopupWithItems(Gui* g, Parameter const& param, LayID lay_id, Style const& style) {
    return PopupWithItems(g, param, g->layout.GetRect(lay_id), style);
}

void FakeButton(Gui* g, LayID lay_id, String str, Style const& style) {
    FakeButton(g, g->layout.GetRect(lay_id), str, style);
}
} // namespace buttons
