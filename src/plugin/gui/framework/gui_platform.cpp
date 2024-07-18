// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_platform.hpp"

#include "os/misc.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

#include "tracy/Tracy.hpp"

bool GuiPlatform::HandleMouseWheel(f32 delta_lines) {
    mouse_scroll_delta_in_lines += delta_lines;
    if (gui_update_requirements.wants_mouse_scroll) return true;
    return false;
}

bool GuiPlatform::HandleMouseMoved(f32x2 new_cursor_pos) {
    bool result = false;
    cursor_pos = new_cursor_pos;

    for (auto& btn : mouse_buttons) {
        if (btn.is_down) {
            if (!btn.is_dragging) btn.dragging_started = true;
            btn.is_dragging = true;
        }
    }

    if (gui_update_requirements.mouse_tracked_regions.size == 0 ||
        gui_update_requirements.wants_mouse_capture) {
        result = true;
    } else if (CheckForTimerRedraw()) {
        return true;
    } else {
        for (auto const i : Range(gui_update_requirements.mouse_tracked_regions.size)) {
            auto& item = gui_update_requirements.mouse_tracked_regions[i];
            bool const mouse_over = item.r.Contains(cursor_pos);
            if (mouse_over && !item.mouse_over) {
                // cursor just entered
                item.mouse_over = mouse_over;
                result = true;
                break;
            } else if (!mouse_over && item.mouse_over) {
                // cursor just left
                item.mouse_over = mouse_over;
                result = true;
                break;
            }
        }
    }

    return result;
}

bool GuiPlatform::HandleMouseClicked(MouseButton button, MouseButtonState::Event event, bool is_down) {
    auto& btn = mouse_buttons[ToInt(button)];
    btn.is_down = is_down;
    if (is_down) {
        btn.last_pressed_point = event.point;
    } else {
        if (btn.is_dragging) btn.dragging_ended = true;
        btn.is_dragging = false;
    }
    btn.presses.Append(event, event_arena);

    bool result = false;
    if (gui_update_requirements.mouse_tracked_regions.size == 0 ||
        gui_update_requirements.wants_mouse_capture ||
        (gui_update_requirements.wants_all_left_clicks && button == MouseButton::Left) ||
        (gui_update_requirements.wants_all_right_clicks && button == MouseButton::Right) ||
        (gui_update_requirements.wants_all_middle_clicks && button == MouseButton::Middle)) {
        result = true;
    } else {
        for (auto const i : Range(gui_update_requirements.mouse_tracked_regions.size)) {
            auto& item = gui_update_requirements.mouse_tracked_regions[i];
            bool const mouse_over = item.r.Contains(cursor_pos);
            if (mouse_over) {
                result = true;
                break;
            }
        }
    }

    return result;
}

bool GuiPlatform::HandleDoubleLeftClick() {
    HandleMouseClicked(MouseButton::Left, {}, true); // TODO: we have no event
    double_left_click = true;
    return true;
}

bool GuiPlatform::HandleKeyPressed(KeyCode key_code, ModifierFlags modifiers, bool is_down) {
    auto& key = keys[ToInt(key_code)];
    if (is_down) {
        key.presses_or_repeats.Append({modifiers}, event_arena);
        if (!key.is_down) key.presses.Append({modifiers}, event_arena);
    } else {
        key.releases.Append({modifiers}, event_arena);
    }
    key.is_down = is_down;

    if (gui_update_requirements.wants_keyboard_input) return true;
    if (gui_update_requirements.wants_just_arrow_keys &&
        (key_code == KeyCode::UpArrow || key_code == KeyCode::DownArrow || key_code == KeyCode::LeftArrow ||
         key_code == KeyCode::RightArrow)) {
        return true;
    }
    return false;
}

bool GuiPlatform::HandleInputChar(u32 utf32_codepoint) {
    dyn::Append(input_chars, utf32_codepoint);
    if (gui_update_requirements.wants_keyboard_input) return true;
    return false;
}

bool GuiPlatform::CheckForTimerRedraw() {
    bool redraw_needed = false;

    if (Exchange(gui_update_requirements.mark_gui_dirty, false)) redraw_needed = true;

    for (usize i = 0; i < gui_update_requirements.redraw_times.size;) {
        auto& t = gui_update_requirements.redraw_times[i];
        if (TimePoint::Now() >= t.time) {
            redraw_needed = true;
            dyn::Remove(gui_update_requirements.redraw_times, i);
        } else {
            ++i;
        }
    }

    return redraw_needed;
}

void GuiPlatform::BeginUpdate() {
    gui_update_requirements.requires_another_update = false;
    dyn::Clear(gui_update_requirements.mouse_tracked_regions);
    gui_update_requirements.wants_just_arrow_keys = false;
    gui_update_requirements.wants_keyboard_input = false;
    gui_update_requirements.wants_mouse_capture = false;
    gui_update_requirements.wants_mouse_scroll = false;
    gui_update_requirements.wants_all_left_clicks = false;
    gui_update_requirements.wants_all_right_clicks = false;
    gui_update_requirements.wants_all_middle_clicks = false;
    gui_update_requirements.requires_another_update = false;
    gui_update_requirements.cursor_type = CursorType::Default;

    display_ratio = 1; // TODO: display ratio

    if (All(cursor_pos < f32x2 {0, 0} || cursor_pos_prev < f32x2 {0, 0})) {
        // if mouse just appeared or disappeared (negative coordinate) we cancel out movement by setting
        // to zero
        cursor_delta = {0, 0};
    } else {
        cursor_delta = cursor_pos - cursor_pos_prev;
    }
    cursor_pos_prev = cursor_pos;

    current_time = TimePoint::Now();

    if (time_prev)
        delta_time = (f32)(current_time - time_prev);
    else
        delta_time = 0;
    time_prev = current_time;
}

void GuiPlatform::EndUpdate() {
    for (auto& btn : mouse_buttons) {
        btn.dragging_started = false;
        btn.dragging_ended = false;
        btn.presses.Clear();
        btn.releases.Clear();
    }

    for (auto& mod : modifier_keys) {
        mod.presses = 0;
        mod.releases = 0;
    }

    for (auto& key : keys) {
        key.presses.Clear();
        key.releases.Clear();
        key.presses_or_repeats.Clear();
    }

    input_chars = {};
    double_left_click = false;
    mouse_scroll_delta_in_lines = 0;
    dyn::Clear(clipboard_data);
    event_arena.ResetCursorAndConsolidateRegions();
    ++update_count;
}
