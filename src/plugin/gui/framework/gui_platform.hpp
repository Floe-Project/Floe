// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/logger/logger.hpp"

#include "clap/ext/gui.h"
#include "draw_list.hpp"
#include "settings/settings_file.hpp"

static constexpr int k_gui_platform_timer_hz = 60;

enum class KeyCode : u32 {
    Tab,
    LeftArrow,
    RightArrow,
    UpArrow,
    DownArrow,
    PageUp,
    PageDown,
    Home,
    End,
    Delete,
    Backspace,
    Enter,
    Escape,
    A,
    C,
    V,
    X,
    Y,
    Z,
    F1,
    F2,
    F3,
    Ctrl,
    Shift,
    Alt,
    Count
};

struct MouseTrackedRegion {
    Rect r;
    bool mouse_over;
};

struct RedrawTime {
    TimePoint time;
    char const* debug_name;
    bool operator==(RedrawTime const& other) { return time.Raw() == other.time.Raw(); }
};

enum class CursorType { Default, Hand, IBeam, AllArrows, HorizontalArrows, VerticalArrows, Count };

struct GuiUpdateRequirements {
    void Reset() {
        dyn::Clear(mouse_tracked_regions);
        wants_just_arrow_keys = false;
        wants_keyboard_input = false;
        wants_mouse_capture = false;
        wants_mouse_scroll = false;
        wants_all_left_clicks = false;
        wants_all_right_clicks = false;
        wants_all_middle_clicks = false;
        requires_another_update = false;
        cursor_type = CursorType::Default;
    }

    // IMPROVE: use arena
    DynamicArray<MouseTrackedRegion> mouse_tracked_regions {Malloc::Instance()};
    DynamicArray<RedrawTime> redraw_times {Malloc::Instance()};
    bool mark_gui_dirty = false;
    bool wants_keyboard_input = false;
    bool wants_just_arrow_keys = false;
    bool wants_mouse_capture = false;
    bool wants_mouse_scroll = false;
    bool wants_all_left_clicks = false;
    bool wants_all_right_clicks = false;
    bool wants_all_middle_clicks = false;
    bool requires_another_update = false;
    bool wants_clipboard_paste = false; // set this if you'd like to recieve text from the clipboard
    CursorType cursor_type = CursorType::Default;
    DynamicArray<char> set_clipboard_text {
        Malloc::Instance()}; // set this to the text that you want put into the OS clipboard
};

constexpr u32 k_num_mouse_buttons = 3;

struct GuiPlatform {
    GuiPlatform(TrivialFixedSizeFunction<16, void()>&& update, Logger& logger)
        : update(Move(update))
        , logger(logger) {}

    virtual ~GuiPlatform() {}

    void SetGUIDirty() { gui_update_requirements.mark_gui_dirty = true; }

    bool ContainsCursor(Rect r) { return r.Contains(cursor_pos); }
    bool IsKeyPressed(KeyCode keycode) { return keys_pressed.Get(ToInt(keycode)); }
    bool IsKeyDown(KeyCode keycode) { return keys_down.Get(ToInt(keycode)); }
    bool KeyJustWentDown(KeyCode keycode) {
        return IsKeyDown(keycode) && !keys_down_prev.Get(ToInt(keycode)) && update_guicall_count == 0;
    }
    bool KeyJustWentUp(KeyCode keycode) {
        return !IsKeyDown(keycode) && keys_down_prev.Get(ToInt(keycode)) && update_guicall_count == 0;
    }

    // n for mouse button number
    bool MouseJustWentDown(u32 n) { return mouse.is_down[n] && !mouse_prev.is_down[n]; }
    bool MouseJustWentUp(u32 n) { return !mouse.is_down[n] && mouse_prev.is_down[n]; }
    bool MouseJustWentDownInRegion(u32 n, Rect r) { return MouseJustWentDown(n) && ContainsCursor(r); }
    bool MouseJustWentUpInRegion(u32 n, Rect r) { return MouseJustWentUp(n) && ContainsCursor(r); }
    bool MouseJustWentDownAndUpInRegion(u32 n, Rect r) {
        return MouseJustWentUpInRegion(n, r) && r.Contains(last_mouse_down_point[n]);
    }
    bool MouseIsDragging(u32 n) { return mouse.is_dragging[n]; }
    bool MouseJustStartedDragging(u32 n) { return mouse.is_dragging[n] && !mouse_prev.is_dragging[n]; }
    bool MouseJustFinishedDragging(u32 n) { return !mouse.is_down[n] && mouse_prev.is_dragging[n]; }
    f64 SecondsSinceMouseDown(u32 n) { return current_time - time_at_mouse_down[n]; }
    f32x2 DeltaFromMouseDown(u32 n) { return cursor_pos - last_mouse_down_point[n]; }

    //
    // Called by platform specific code
    // ======================================================================================================
    bool HandleMouseWheel(f32 delta);
    bool HandleMouseMoved(f32 cursor_x, f32 cursor_y);
    bool HandleMouseClicked(u32 button, bool is_down);
    bool HandleDoubleLeftClick();
    bool HandleKeyPressed(KeyCode code, bool is_down);
    bool HandleInputChar(u32 utf32_codepoint);
    bool CheckForTimerRedraw();

    void Update();

    //
    // Write to these every update
    // ======================================================================================================
    GuiUpdateRequirements gui_update_requirements {};
    graphics::DrawData draw_data {};
    graphics::DrawContext* graphics_ctx {};

    //
    // Read these at any time when the window is open
    // ======================================================================================================
    f32x2 cursor_pos {};
    f32x2 cursor_delta {};
    f32x2 cursor_prev {};
    f32 mouse_scroll_in_lines {};

    // TODO: We need to track presses and releases rather than just the current state because at the moment
    // it's technically possible that a click-and-release could occur between frames and we'd miss it.
    struct MouseState {
        Array<bool, k_num_mouse_buttons> is_down {}; // the current state of the button
        Array<bool, k_num_mouse_buttons> pressed {}; // indicates a key has been pressed since last frame
        Array<bool, k_num_mouse_buttons> is_dragging {};
    };

    MouseState mouse {};
    MouseState mouse_prev {};
    Array<f32x2, 3> last_mouse_down_point {};
    Array<TimePoint, 3> time_at_mouse_down {};

    bool double_left_click {};

    UiSize window_size {};
    f32 delta_time {};
    TimePoint current_time {};

    Bitset<ToInt(KeyCode::Count)> keys_down {}; // the state of the key
    Bitset<ToInt(KeyCode::Count)> keys_down_prev {};

    Bitset<ToInt(KeyCode::Count)> keys_pressed {}; // indicates the a key has just be pressed (include
                                                   // key repeats), reset every frame

    // may contain text from the clipboard - following from a wants_clipboard_paste request
    DynamicArray<char> clipboard_data {PageAllocator::Instance()};

    DynamicArrayInline<u32, 16> input_chars {};

    uint64_t update_count {};
    int update_guicall_count {}; // update gui is sometimes called 2 times in one frame
    bool is_window_open {};
    f32 display_ratio {};

    bool currently_updating {};

    void* native_window {};

    //
    // Internals
    // ======================================================================================================
    TrivialFixedSizeFunction<16, void()> update;
    Logger& logger;

    TimePoint time_at_last_update {};
};
