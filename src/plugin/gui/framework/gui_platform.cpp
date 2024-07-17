// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_platform.hpp"

#include "os/misc.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

#include "tracy/Tracy.hpp"

bool GuiPlatform::HandleMouseWheel(f32 delta_lines) {
    mouse_scroll_in_lines += delta_lines;
    if (gui_update_requirements.wants_mouse_scroll) return true;
    return false;
}

bool GuiPlatform::HandleMouseMoved(f32 cursor_x, f32 cursor_y) {
    bool result = false;
    cursor_pos.x = cursor_x;
    cursor_pos.y = cursor_y;

    if (gui_update_requirements.mouse_tracked_regions.size == 0 ||
        gui_update_requirements.wants_mouse_capture) {
        result = true;
    } else if (CheckForTimerRedraw()) {
        return true;
    } else {
        for (auto const i : Range(gui_update_requirements.mouse_tracked_regions.size)) {
            auto& item = gui_update_requirements.mouse_tracked_regions[i];
            bool const mouse_over = item.r.Contains({cursor_x, cursor_y});
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

bool GuiPlatform::HandleMouseClicked(u32 button, bool is_down) {
    mouse.is_down[button] = is_down;
    if (is_down) {
        time_at_mouse_down[button] = TimePoint::Now();
        last_mouse_down_point[button] = cursor_pos;
    }

    bool result = false;
    if (gui_update_requirements.mouse_tracked_regions.size == 0 ||
        gui_update_requirements.wants_mouse_capture ||
        (gui_update_requirements.wants_all_left_clicks && button == 0) ||
        (gui_update_requirements.wants_all_right_clicks && button == 1) ||
        (gui_update_requirements.wants_all_middle_clicks && button == 2)) {
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
    HandleMouseClicked(0, true);
    double_left_click = true;
    return true;
}

bool GuiPlatform::HandleKeyPressed(KeyCode key_code, bool is_down) {
    DebugLn("Key: {} {}", EnumToString(key_code), is_down);
    keys_down.SetToValue(ToInt(key_code), is_down);
    if (is_down) keys_pressed.SetToValue(ToInt(key_code), true);
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

void GuiPlatform::Update() {
    ZoneScopedN("GUI Update");

    if (currently_updating) return;
    currently_updating = true;
    DEFER { currently_updating = false; };

    if (!graphics_ctx) return;

    DEFER { update_count++; };

    auto const start_counter = TimePoint::Now();
    DEFER {
        if constexpr (!PRODUCTION_BUILD) {
            if (auto const diff_ms = SecondsToMilliseconds(start_counter.SecondsFromNow()); diff_ms >= 35)
                DebugLn("{} very slow update: {} ms", __FUNCTION__, diff_ms);
        }
    };

    if (All(cursor_pos < f32x2 {0, 0} || cursor_prev < f32x2 {0, 0})) {
        // if mouse just appeared or disappeared (negative coordinate) we cancel out movement by setting to
        // zero
        cursor_delta = {0, 0};
    } else {
        cursor_delta = cursor_pos - cursor_prev;
    }

    if (time_at_last_update)
        delta_time = (f32)time_at_last_update.SecondsFromNow();
    else
        delta_time = 0;

    for (usize button = 0; button < k_num_mouse_buttons; button++) {
        if (All(mouse.is_down[button] && last_mouse_down_point[button] != cursor_pos))
            mouse.is_dragging[button] = true;
        if (!mouse.is_down[button]) mouse.is_dragging[button] = false;
    }
    current_time = TimePoint::Now();
    display_ratio = 1; // TODO: display ratio

    //
    //
    //
    update_guicall_count = 0;
    for (auto _ : Range(4)) {
        ZoneNamedN(repeat, "repeat", true);
        update();

        mouse_prev = mouse;
        input_chars = {};
        keys_down_prev = keys_down;
        keys_pressed = {};
        double_left_click = false;
        mouse_scroll_in_lines = 0;
        dyn::Clear(clipboard_data);

        update_guicall_count++;

        if (!gui_update_requirements.requires_another_update) break;
    }

    //
    //
    //

    if (draw_data.cmd_lists_count) {
        ZoneNamedN(render, "render", true);
        auto o =
            graphics_ctx->Render(draw_data, window_size, display_ratio, Rect(0, 0, window_size.ToFloat2()));
        if (o.HasError()) logger.ErrorLn("GUI render failed: {}", o.Error());
    }

    cursor_prev = cursor_pos;
    time_at_last_update = TimePoint::Now();
}
