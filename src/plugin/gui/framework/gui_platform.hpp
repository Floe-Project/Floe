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
    Count,
};

enum class ModifierKey : u32 {
    Shift,
    Ctrl,
    Alt, // 'Option' on macOS
    Super, // 'Cmd' on macOS, else Super/Windows-key
    Count,

    // alias
    Modifier = IS_MACOS ? Super : Ctrl,
};

struct ModifierFlags {
    bool Get(ModifierKey k) const { return flags & (1 << ToInt(k)); }
    void Set(ModifierKey k) { flags |= (1 << ToInt(k)); }
    u8 flags;
};

enum class MouseButton : u32 { Left, Right, Middle, Count };

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

struct GuiPlatform {
    struct MouseButtonState {
        struct Event {
            f32x2 point {};
            TimePoint time {};
            ModifierFlags modifiers {};
        };

        ArenaStack<Event> presses {}; // mouse-down events since last frame, cleared every frame
        ArenaStack<Event> releases {}; // mouse-up events since last frame, cleared every frame
        f32x2 last_pressed_point {}; // the last known point where the mouse was pressed
        TimePoint last_pressed_time {}; // the last known time when the mouse was pressed
        bool is_down {}; // current state
        bool is_dragging {};
        bool dragging_started {}; // cleared every frame
        bool dragging_ended {}; // cleared every frame
    };

    struct ModifierKeyState {
        u8 is_down; // we use an int to incr/decr because modifier keys can have both left and right keys
        u8 presses; // key-down events since last frame, zeroed every frame
        u8 releases; // key-up events since last frame, zeroed every frame
    };

    struct KeyState {
        struct Event {
            ModifierFlags modifiers;
        };

        bool is_down;
        ArenaStack<Event> presses_or_repeats; // key-down or repeats since last frame, cleared every frame
        ArenaStack<Event> presses; // key-down events since last frame, zeroed every frame
        ArenaStack<Event> releases; // key-up events since last frame, zeroed every frame
    };

    GuiPlatform(TrivialFixedSizeFunction<16, void()>&& update, Logger& logger)
        : update(Move(update))
        , logger(logger) {}

    void SetGUIDirty() { gui_update_requirements.mark_gui_dirty = true; }

    bool ContainsCursor(Rect r) { return r.Contains(cursor_pos); }
    bool KeyJustWentDown(KeyCode keycode) { return keys[ToInt(keycode)].presses.size; } // TODO: remove
    bool KeyJustWentUp(KeyCode keycode) { return keys[ToInt(keycode)].releases.size; } // TODO: remove

    bool MouseJustWentDownInRegion(MouseButton n, Rect r) {
        return mouse_buttons[ToInt(n)].presses.size && ContainsCursor(r);
    }
    bool MouseJustWentUpInRegion(MouseButton n, Rect r) {
        return mouse_buttons[ToInt(n)].releases.size && ContainsCursor(r);
    }
    bool MouseJustWentDownAndUpInRegion(MouseButton n, Rect r) {
        return MouseJustWentUpInRegion(n, r) && r.Contains(mouse_buttons[ToInt(n)].last_pressed_point);
    }
    f64 SecondsSinceMouseDown(MouseButton n) {
        return current_time - mouse_buttons[ToInt(n)].last_pressed_time;
    }
    f32x2 DeltaFromMouseDown(MouseButton n) {
        return cursor_pos - mouse_buttons[ToInt(n)].last_pressed_point;
    }

    MouseButtonState& Mouse(MouseButton n) { return mouse_buttons[ToInt(n)]; }
    ModifierKeyState& Key(ModifierKey n) { return modifier_keys[ToInt(n)]; }
    KeyState& Key(KeyCode n) { return keys[ToInt(n)]; }

    //
    // Called by platform specific code
    // ======================================================================================================
    bool HandleMouseWheel(f32 delta);
    bool HandleMouseMoved(f32 cursor_x, f32 cursor_y);
    bool HandleMouseClicked(MouseButton button, MouseButtonState::Event event, bool is_down);
    bool HandleDoubleLeftClick();
    bool HandleKeyPressed(KeyCode code, ModifierFlags modifiers, bool is_down);
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
    f32x2 cursor_pos_prev {};
    f32x2 cursor_delta {};
    f32 mouse_scroll_delta_in_lines {};
    Array<MouseButtonState, ToInt(MouseButton::Count)> mouse_buttons {};
    bool double_left_click {};

    TimePoint current_time {};
    TimePoint time_prev {};
    f32 delta_time {};

    Array<KeyState, ToInt(KeyCode::Count)> keys {};
    Array<ModifierKeyState, ToInt(ModifierKey::Count)> modifier_keys {};

    // may contain text from the clipboard - following from a wants_clipboard_paste request
    DynamicArray<char> clipboard_data {PageAllocator::Instance()};

    DynamicArrayInline<u32, 16> input_chars {};

    uint64_t update_count {};
    int update_guicall_count {}; // update() is sometimes called 2 times in one frame

    f32 display_ratio {};
    UiSize window_size {};

    void* native_window {};

    //
    // Internals
    // ======================================================================================================
    TrivialFixedSizeFunction<16, void()> update;
    Logger& logger;
    ArenaAllocator event_arena {Malloc::Instance(), 256};
};
