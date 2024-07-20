// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "draw_list.hpp"

static constexpr u8 k_gui_refresh_rate_hz = 60;

// Pugl doesn't currently (July 2024) support double clicks, so we implement it ourselves. It would be better
// to get the preferred double-click interval from the OS.
static constexpr f64 k_double_click_interval_seconds = 0.3;

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

struct GuiFrameInput {
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
        bool double_click {};
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

    auto const& Mouse(MouseButton n) const { return mouse_buttons[ToInt(n)]; }
    auto const& Key(ModifierKey n) const { return modifier_keys[ToInt(n)]; }
    auto const& Key(KeyCode n) const { return keys[ToInt(n)]; }

    graphics::DrawContext* graphics_ctx {};

    f32x2 cursor_pos {};
    f32x2 cursor_pos_prev {};
    f32x2 cursor_delta {};
    f32 mouse_scroll_delta_in_lines {};
    Array<MouseButtonState, ToInt(MouseButton::Count)> mouse_buttons {};
    Array<KeyState, ToInt(KeyCode::Count)> keys {};
    Array<ModifierKeyState, ToInt(ModifierKey::Count)> modifier_keys {};
    // may contain text from the OS clipboard if you requested it
    DynamicArray<char> clipboard_text {PageAllocator::Instance()};
    DynamicArrayInline<u32, 16> input_utf32_chars {};

    TimePoint current_time {};
    TimePoint time_prev {};
    f32 delta_time {};
    u64 update_count {};
    f32 display_ratio {1}; // TODO: do we need to set this properly for high-dpi displays?
    UiSize window_size {};
    void* native_window {}; // HWND, NSView*, etc.

    // internal
    ArenaAllocator event_arena {Malloc::Instance(), 256};
};

struct MouseTrackedRect {
    Rect rect;
    bool mouse_over;
};

enum class CursorType { Default, Hand, IBeam, AllArrows, HorizontalArrows, VerticalArrows, Count };

// Reset this at the start of each frame
struct GuiFrameResult {
    enum class Status {
        // 1. GUI will sleep until there's user iteraction or a timed wakeup fired
        Sleep,

        // 2. GUI will update at the timer (normally 60Hz)
        Animate,

        // 3. re-update the GUI instantly - as soon as the frame is done - use this sparingly for necessary
        // layout changes
        ImmediatelyUpdate,
    };

    // only sets the status if it's more important than the current status
    void IncreaseStatus(Status s) {
        if (ToInt(s) > ToInt(status)) status = s;
    }

    Status status {Status::Sleep};

    // Set this if you want to be woken up at certain times in the future. Out-of-date wakeups will be removed
    // for you.
    // Must be valid until the next frame.
    DynamicArray<TimePoint>* timed_wakeups {};

    // Rectangles that will wake up the GUI when the mouse enters/leaves it.
    // Must be valid until the next frame.
    Span<MouseTrackedRect> mouse_tracked_rects {};

    bool wants_keyboard_input = false;
    bool wants_just_arrow_keys = false;
    bool wants_mouse_capture = false;
    bool wants_mouse_scroll = false;
    bool wants_all_left_clicks = false;
    bool wants_all_right_clicks = false;
    bool wants_all_middle_clicks = false;

    CursorType cursor_type = CursorType::Default;

    // Set this if you want text from the OS clipboard, it will be given to you in an upcoming frame
    bool wants_clipboard_text_paste = false;

    // Set this to the text that you want put into the OS clipboard
    // Must be valid until the next frame.
    Span<char> set_clipboard_text {};

    // Must be valid until the next frame.
    graphics::DrawData draw_data {};
};