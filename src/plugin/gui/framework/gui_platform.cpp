// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_platform.hpp"

#include "os/misc.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

#include "tracy/Tracy.hpp"

bool GuiPlatform::HandleMouseWheel(f32 delta_lines) {
    mouse_scroll_in_lines += delta_lines;
    if (gui_update_requirements.wants_mouse_scroll) {
        SetStateChanged(__FUNCTION__);
        return true;
    }
    return false;
}

bool GuiPlatform::HandleMouseMoved(f32 cursor_x, f32 cursor_y) {
    bool result = false;
    cursor_pos.x = cursor_x;
    cursor_pos.y = cursor_y;

    char const* state_change_reason = "";
    if (gui_update_requirements.mouse_tracked_regions.size == 0 ||
        gui_update_requirements.wants_mouse_capture) {
        state_change_reason = "HandleMouseMoved gui_data.widgets.Size() == 0 || gui_data.wants_mouse_capture";
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
                state_change_reason = "HandleMouseMoved cursor just entered a widget";
                result = true;
                break;
            } else if (!mouse_over && item.mouse_over) {
                // cursor just left
                state_change_reason = "HandleMouseMoved cursor just left a widget";
                item.mouse_over = mouse_over;
                result = true;
                break;
            }
        }
    }

    if (result) SetStateChanged(state_change_reason);
    return result;
}

bool GuiPlatform::HandleMouseClicked(int button, bool is_down) {
    mouse_down[button] = is_down;
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

    if (result) SetStateChanged(__FUNCTION__);
    return result;
}

bool GuiPlatform::HandleDoubleLeftClick() {
    HandleMouseClicked(0, true);
    double_left_click = true;
    SetStateChanged(__FUNCTION__);
    return true;
}

bool GuiPlatform::HandleKeyPressed(KeyCodes key_code, bool is_down) {
    keys_down[key_code] = is_down;
    if (is_down) keys_pressed[key_code] = is_down;
    if (gui_update_requirements.wants_keyboard_input) {
        SetStateChanged(__FUNCTION__);
        return true;
    }
    if (gui_update_requirements.wants_just_arrow_keys &&
        (key_code == KeyCodeUpArrow || key_code == KeyCodeDownArrow || key_code == KeyCodeLeftArrow ||
         key_code == KeyCodeRightArrow)) {
        SetStateChanged(__FUNCTION__);
        return true;
    }
    return false;
}

bool GuiPlatform::HandleInputChar(int character) {
    int n = 0;
    int const* pos = input_chars;
    while (*pos++)
        n++;

    if (n + 1 < (int)ArraySize(input_chars)) {
        input_chars[n] = character;
        input_chars[n + 1] = '\0';
    }

    if (gui_update_requirements.wants_keyboard_input) {
        SetStateChanged(__FUNCTION__);
        return true;
    }
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

    if (redraw_needed) SetStateChanged(__FUNCTION__);

    return redraw_needed;
}

void GuiPlatform::Update() {
    ZoneScopedN("GUI Update");

    if (currently_updating) return;
    currently_updating = true;
    DEFER { currently_updating = false; };

    if (!platform_state_changed) SetStateChanged("OS required");
    platform_state_changed = false;

    if (!graphics_ctx) return;

    DEFER { update_count++; };

    auto start_counter = TimePoint::Now();

    if (All(cursor_pos < f32x2 {0, 0} || cursor_prev < f32x2 {0, 0})) {
        // if mouse just appeared or disappeared (negative coordinate) we cancel out movement by setting to
        // zero
        cursor_delta = {0, 0};
    } else {
        cursor_delta = cursor_pos - cursor_prev;
    }

    if (time_at_last_paint)
        delta_time = (f32)time_at_last_paint.SecondsFromNow();
    else
        delta_time = 0;

    for (usize i = 0; i < ArraySize(mouse_down); i++) {
        if (All(mouse_down[i] && last_mouse_down_point[i] != cursor_pos)) mouse_is_dragging[i] = true;
        if (!mouse_down[i]) mouse_is_dragging[i] = false;
    }
    current_time = TimePoint::Now();
    display_ratio = 1; // TODO: display ratio

    //
    //
    //
#if !PRODUCTION_BUILD
    auto update_time_counter = TimePoint::Now();
#endif
    update_guicall_count = 0;
    for (auto _ : Range(4)) {
        ZoneNamedN(repeat, "repeat", true);
        update();

        CopyMemory(mouse_down_prev, mouse_down, sizeof(mouse_down));
        CopyMemory(mouse_is_dragging_prev, mouse_is_dragging, sizeof(mouse_is_dragging));
        key_ctrl_prev = key_ctrl;
        key_shift_prev = key_shift;
        key_alt_prev = key_alt;
        ZeroMemory(input_chars, sizeof(input_chars));
        CopyMemory(keys_pressed_prev, keys_pressed, sizeof(keys_pressed));
        CopyMemory(keys_down_prev, keys_down, sizeof(keys_down));
        ZeroMemory(keys_pressed, sizeof(keys_pressed));
        double_left_click = false;
        mouse_scroll_in_lines = 0;
        dyn::Clear(clipboard_data);

        update_guicall_count++;

        if (!gui_update_requirements.requires_another_update) break;
    }

#if !PRODUCTION_BUILD
    auto update_time_ms = SecondsToMilliseconds(update_time_counter.SecondsFromNow());
#endif

    //
    //
    //

#if !PRODUCTION_BUILD
    f64 render_time_ms = 0;
#endif
    if (draw_data.cmd_lists_count) {
        ZoneNamedN(render, "render", true);
#if !PRODUCTION_BUILD
        auto time_counter_before_render = TimePoint::Now();
#endif
        auto o =
            graphics_ctx->Render(draw_data, window_size, display_ratio, Rect(0, 0, window_size.ToFloat2()));
        if (o.HasError()) logger.ErrorLn("GUI render failed: {}", o.Error());

#if !PRODUCTION_BUILD
        render_time_ms = SecondsToMilliseconds(time_counter_before_render.SecondsFromNow());
#endif
    }

    auto const diff_ms = SecondsToMilliseconds(start_counter.SecondsFromNow());
    if (diff_ms >= 35) DebugLn("{} very slow update: {} ms", __FUNCTION__, diff_ms);

#if !PRODUCTION_BUILD
    ASSERT(paint_time_counter < (int)ArraySize(paint_time_average));
    paint_time_average[paint_time_counter++] = (f32)diff_ms;
    if (paint_time_counter >= (int)ArraySize(paint_time_average)) paint_time_counter = 0;

    f32 avg_time = 0;
    for (auto const i : Range((int)ArraySize(paint_time_average)))
        avg_time += paint_time_average[i];
    avg_time /= (f32)ArraySize(paint_time_average);
    paint_average_time = avg_time;
    paint_prev_time = (f32)diff_ms;
    render_prev_time = (f32)render_time_ms;
    update_prev_time = (f32)update_time_ms;
#endif

    cursor_prev = cursor_pos;
    time_at_last_paint = TimePoint::Now();
}
