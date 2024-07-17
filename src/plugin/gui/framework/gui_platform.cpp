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

bool GuiPlatform::HandleMouseMoved(f32 cursor_x, f32 cursor_y) {
    bool result = false;
    cursor_pos.x = cursor_x;
    cursor_pos.y = cursor_y;

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

bool GuiPlatform::HandleMouseClicked(MouseButton button, bool is_down) {
    mouse_buttons[ToInt(button)].is_down = is_down;
    if (is_down) {
        mouse_buttons[ToInt(button)].pressed = true;
        mouse_buttons[ToInt(button)].pressed_point = cursor_pos;
        mouse_buttons[ToInt(button)].pressed_time = TimePoint::Now();
    } else {
        mouse_buttons[ToInt(button)].released = true;
        if (mouse_buttons[ToInt(button)].is_dragging) mouse_buttons[ToInt(button)].dragging_ended = true;
        mouse_buttons[ToInt(button)].is_dragging = false;
    }

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
    HandleMouseClicked(MouseButton::Left, true);
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

    if (!graphics_ctx) return;

    DEFER { update_count++; };

    auto const start_counter = TimePoint::Now();
    DEFER {
        if constexpr (!PRODUCTION_BUILD) {
            if (auto const diff_ms = SecondsToMilliseconds(start_counter.SecondsFromNow()); diff_ms >= 35)
                DebugLn("{} very slow update: {} ms", __FUNCTION__, diff_ms);
        }
    };

    if (All(cursor_pos < f32x2 {0, 0} || cursor_pos_prev < f32x2 {0, 0})) {
        // if mouse just appeared or disappeared (negative coordinate) we cancel out movement by setting to
        // zero
        cursor_delta = {0, 0};
    } else {
        cursor_delta = cursor_pos - cursor_pos_prev;
    }

    if (time_at_last_update)
        delta_time = (f32)time_at_last_update.SecondsFromNow();
    else
        delta_time = 0;

    current_time = TimePoint::Now();
    display_ratio = 1; // TODO: display ratio

    //
    //
    //
    update_guicall_count = 0;
    for (auto _ : Range(4)) {
        ZoneNamedN(repeat, "repeat", true);

        for (auto const& btn : mouse_buttons) {
            DebugLn("Mouse button[{}]: down: {}, pressed: {}, released: {}",
                    &btn - mouse_buttons.data,
                    btn.is_down,
                    btn.pressed,
                    btn.released);
        }

        update();

        input_chars = {};
        for (auto& btn : mouse_buttons) {
            btn.pressed = false;
            btn.released = false;
            btn.dragging_started = false;
            btn.dragging_ended = false;
        }

        for (auto& mod : modifier_keys) {
            mod.pressed = false;
            mod.released = false;
        }

        keys_down_prev = keys_down;
        keys_pressed = {};
        double_left_click = false;
        mouse_scroll_delta_in_lines = 0;
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

    cursor_pos_prev = cursor_pos;
    time_at_last_update = TimePoint::Now();
}
