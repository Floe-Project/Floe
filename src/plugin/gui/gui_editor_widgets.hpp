// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

#include "framework/gui_imgui.hpp"

struct EditorGUI {
    f32 y_pos = 0;
    f32 const item_h = 19;
    imgui::Context* imgui = nullptr;
    bool alternating_back = false;
};

void EditorReset(EditorGUI* g);

Rect EditorGetFullR(EditorGUI* g);

Rect EditorGetLeftR(EditorGUI* g);

Rect EditorGetRightR(EditorGUI* g);

void EditorIncrementPos(EditorGUI* g, f32 size = 0);

void EditorText(EditorGUI* g, String text);

void EditorHeading(EditorGUI* g, String text);

void EditorLabel(EditorGUI* g, String text);
void EditorLabel(EditorGUI* g, Rect r, String text, TextJustification just);

bool EditorButton(EditorGUI* g, String button, String label);

bool EditorBeginMenu(EditorGUI* g, String label, String text, imgui::Id pop_id);

void EditorEndMenu(EditorGUI* g);

bool EditorSlider(EditorGUI* g, Rect r, imgui::Id id, f32 min, f32 max, f32& val);
bool EditorSlider(EditorGUI* g, String label, f32 min, f32 max, f32& val);

bool EditorSlider(EditorGUI* g, String label, int min, int max, int& val);
bool EditorDragger(EditorGUI* g, String label, int min, int max, int& val);

using EditorTextInputBuffer = DynamicArrayInline<char, 128>;
void EditorTextInput(EditorGUI* g, String label, EditorTextInputBuffer& buf);

bool EditorMenuItems(EditorGUI* g, Span<String const> items, int& current);
bool EditorMenu(EditorGUI* g, Rect r, Span<String const> items, int& current);

void EditorLabelAlternatingBack(EditorGUI* g, Rect r, String, bool extra_highlight);

void WriteColoursFile();
void WriteSizesFile();
void WriteColourMapFile();
void SizesGUISliders(EditorGUI* g, String search);
void ColourMapGUIMenus(EditorGUI* g, String search, String colour_search, bool show_high_contrast);
void ColoursGUISliders(EditorGUI* gui, String search);
