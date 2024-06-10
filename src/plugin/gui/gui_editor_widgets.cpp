// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_editor_widgets.hpp"

#include "foundation/foundation.hpp"

#include "gui_window.hpp"

void EditorReset(EditorGUI* g) {
    g->y_pos = 0;
    g->alternating_back = false;
}

Rect EditorGetFullR(EditorGUI* g) { return Rect {0, g->y_pos, g->imgui->Width(), g->item_h - 1}; }

Rect EditorGetLeftR(EditorGUI* g) { return Rect {0, g->y_pos, g->imgui->Width() / 2, g->item_h - 1}; }

Rect EditorGetRightR(EditorGUI* g) {
    auto w = g->imgui->Width() / 2;
    return Rect {w, g->y_pos, w, g->item_h - 1};
}

void EditorIncrementPos(EditorGUI* g, f32 size) { g->y_pos += (size != 0) ? size : g->item_h; }

void EditorText(EditorGUI* g, String text) {
    g->imgui->Text(imgui::DefText(), EditorGetFullR(g), text);
    EditorIncrementPos(g);
}

void EditorHeading(EditorGUI* g, String text) {
    if (g->y_pos != 0) // don't do this for the first item
        g->y_pos += g->item_h;

    auto r = EditorGetFullR(g);
    auto back_r = r;
    g->imgui->RegisterAndConvertRect(&back_r);
    g->imgui->graphics->AddRectFilled(back_r.Min(), back_r.Max(), 0x50ffffff);
    g->imgui->Text(imgui::DefText(), r, text);
    // g->imgui->graphics->AddLine(r.BottomLeft(), r.BottomRight(), 0xffffffff);

    g->y_pos += g->item_h * 1.1f;
}

void EditorLabel(EditorGUI* g, Rect r, String text, TextJustification just) {
    g->imgui->graphics->AddTextJustified(g->imgui->GetRegisteredAndConvertedRect(r.CutRight(4)),
                                         text,
                                         imgui::DefText().col,
                                         just,
                                         TextOverflowType::ShowDotsOnRight);
}

void EditorLabel(EditorGUI* g, String text) {
    EditorLabel(g, EditorGetLeftR(g), text, TextJustification::CentredRight);
}

bool EditorButton(EditorGUI* g, String button, String label) {
    g->imgui->Text(imgui::DefText(), EditorGetLeftR(g), label);

    bool const result = g->imgui->Button(imgui::DefButton(), EditorGetRightR(g), button);
    EditorIncrementPos(g);
    return result;
}

bool EditorBeginMenu(EditorGUI* g, String label, String text, imgui::Id pop_id) {
    EditorLabel(g, label);
    auto btn_id = g->imgui->GetID(label);
    auto sets = imgui::DefButton();
    sets.window = PopupWindowSettings(*g->imgui);
    bool const res = g->imgui->PopupButton(sets, EditorGetRightR(g), btn_id, pop_id, text);
    EditorIncrementPos(g);
    return res;
}

void EditorEndMenu(EditorGUI* g) { g->imgui->EndWindow(); }

bool EditorSlider(EditorGUI* g, Rect r, imgui::Id id, f32 min, f32 max, f32& val) {
    return g->imgui->SliderRange(imgui::DefSlider(), r, id, min, max, val, min);
}

bool EditorSlider(EditorGUI* g, String label, f32 min, f32 max, f32& val) {
    ArenaAllocatorWithInlineStorage<100> allocator;
    EditorLabel(g, fmt::Format(allocator, "{} ({.2})", label, val));
    auto res = EditorSlider(g, EditorGetRightR(g), g->imgui->GetID(label), min, max, val);
    EditorIncrementPos(g);
    return res;
}

bool EditorSlider(EditorGUI* g, String label, int min, int max, int& val) {
    ArenaAllocatorWithInlineStorage<100> allocator;
    EditorLabel(g, fmt::Format(allocator, "{} ({})", label, val));
    auto id = g->imgui->GetID(label);
    auto fval = (f32)val;
    auto res =
        g->imgui->SliderRange(imgui::DefSlider(), EditorGetRightR(g), id, (f32)min, (f32)max, fval, (f32)min);
    if (res) val = (int)fval;
    EditorIncrementPos(g);
    return res;
}

bool EditorDragger(EditorGUI* g, String label, int min, int max, int& val) {
    ArenaAllocatorWithInlineStorage<100> allocator;
    EditorLabel(g, fmt::Format(allocator, "{} ({})", label, val));
    auto id = g->imgui->GetID(label);
    auto sets = imgui::DefTextInputDraggerInt();
    sets.slider_settings.sensitivity /= 6;
    bool const res = g->imgui->TextInputDraggerInt(sets, EditorGetRightR(g), id, min, max, val);
    EditorIncrementPos(g);
    return res;
}

void EditorTextInput(EditorGUI* g, String label, EditorTextInputBuffer& buf) {
    EditorLabel(g, label);
    auto const r = EditorGetRightR(g);
    auto const id = g->imgui->GetID(label);
    auto sets = imgui::DefTextInput();
    auto const result = g->imgui->TextInput(sets, r, id, buf);
    if (result.buffer_changed) dyn::Assign(buf, result.text);

    EditorIncrementPos(g);
}

bool EditorMenuItems(EditorGUI* g, Span<String const> items, int& current) {
    auto w = g->imgui->LargestStringWidth(4, items);
    auto h = g->item_h;

    auto item_set = imgui::DefButton();
    item_set.flags.closes_popups = true;

    int clicked = -1;
    for (auto const i : Range(items.size)) {
        bool selected = (int)i == current;
        if (g->imgui->ToggleButton(item_set,
                                   {0, h * (f32)i, w, h},
                                   g->imgui->GetID(items[i]),
                                   selected,
                                   items[i]))
            clicked = (int)i;
    }
    if (clicked != -1 && current != clicked) {
        current = clicked;
        return true;
    }
    return false;
}

bool EditorMenu(EditorGUI* g, Rect r, Span<String const> items, int& current) {
    auto sets = imgui::DefButtonPopup();
    auto id = g->imgui->GetID(items.data);
    auto curr_text = items[(usize)current];
    bool result = false;
    if (g->imgui->PopupButton(sets, r, id, id + 1, curr_text)) {
        result = EditorMenuItems(g, items, current) || result;
        g->imgui->EndWindow();
    }
    return result;
}

void EditorLabelAlternatingBack(EditorGUI* g, Rect r, String text, bool extra_highlight) {
    if (g->alternating_back || extra_highlight) {
        auto reg = r;
        g->imgui->RegisterAndConvertRect(&reg);
        auto col = 0x15ffffffu;
        if (extra_highlight) col = 0x35ffbfbfu;
        g->imgui->graphics->AddRectFilled(reg.Min(), reg.Max(), col);
    }
    g->alternating_back = !g->alternating_back;
    EditorLabel(g, r, text, TextJustification::CentredLeft);
}
