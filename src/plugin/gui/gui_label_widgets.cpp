// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_label_widgets.hpp"

#include "gui.hpp"
#include "gui_editor_ui_style.hpp"
#include "param.hpp"

namespace labels {

Style CentredLeft(u32 col) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.add_margin_x = true;
    s.main_cols.reg = col;
    return s;
}

Style TopLeft(u32 col) {
    auto s = CentredLeft(col);
    s.icon_or_text.justification = TextJustification::TopLeft;
    return s;
}

Style Title(u32 col) {
    auto s = TopLeft(col);
    s.icon_or_text.justification = TextJustification::Baseline;
    return s;
}

Style BrowserHeading() { return CentredLeft(GMC(BrowserHeading)); }

Style PresetBrowserFolder() {
    auto s = CentredLeft(GMC(PresetBrowserFileFolderText));
    s.icon_or_text.justification = TextJustification::Baseline | TextJustification::Left;
    return s;
}

Style PresetBrowserFolderPath() {
    auto s = CentredLeft(GMC(PresetBrowserFileFolderTextPath));
    s.icon_or_text.justification = TextJustification::Baseline | TextJustification::Right;
    s.icon_or_text.overflow_type = TextOverflowType::ShowDotsOnLeft;
    return s;
}

Style PresetSectionHeading() { return CentredLeft(GMC(BrowserSectionHeading)); }

Style Parameter(bool greyed_out) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.capitalise = false;
    s.main_cols.reg = greyed_out ? GMC(ParameterLabelGreyedOut) : GMC(ParameterLabel);
    return s;
}

Style ParameterCentred(bool greyed_out) {
    auto s = Parameter(greyed_out);
    s.icon_or_text.justification = TextJustification::HorizontallyCentred;
    return s;
}

Style ErrorWindowLabel() {
    auto s = CentredLeft(GMC(PopupItemText));
    s.icon_or_text.add_margin_x = false;
    return s;
}

Style WaveformLoadingLabel() {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::Centred;
    s.main_cols.reg = GMCC(Waveform_, LoadingText);
    return s;
}

//
//
//
void Label(Gui* g, Rect r, String str, Style const& style) { buttons::FakeButton(g, r, str, style); }

void Label(Gui* g, ::Parameter const& param, Rect r, Style const& style) {
    Label(g, r, param.info.gui_label, style);
}

void Label(Gui* g, LayID id, String str, Style const& style) {
    Label(g, g->layout.GetRect(id), str, style);
}

void Label(Gui* g, ::Parameter const& param, LayID id, Style const& style) {
    Label(g, param, g->layout.GetRect(id), style);
}

} // namespace labels
