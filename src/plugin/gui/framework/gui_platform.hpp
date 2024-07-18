// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "draw_list.hpp"

// TODO: rename this file

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

// TODO: new name
struct GuiUpdateRequirements {
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

// TODO: needs a new name. GuiContext, UpdateInfo, GuiUpdateArgs, GuiUpdateInfo, GuiUpdateState, GuiState
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

    void SetGUIDirty() { gui_update_requirements.mark_gui_dirty = true; }

    MouseButtonState& Mouse(MouseButton n) { return mouse_buttons[ToInt(n)]; }
    ModifierKeyState& Key(ModifierKey n) { return modifier_keys[ToInt(n)]; }
    KeyState& Key(KeyCode n) { return keys[ToInt(n)]; }

    // Called by platform specific code
    bool HandleMouseWheel(f32 delta);
    bool HandleMouseMoved(f32x2 cursor_pos);
    bool HandleMouseClicked(MouseButton button, MouseButtonState::Event event, bool is_down);
    bool HandleDoubleLeftClick();
    bool HandleKeyPressed(KeyCode code, ModifierFlags modifiers, bool is_down);
    bool HandleInputChar(u32 utf32_codepoint);
    bool CheckForTimerRedraw();
    void BeginUpdate();
    void EndUpdate();

    // In: graphics/drawing API
    graphics::DrawContext* graphics_ctx {};

    // Out: result of the update call
    GuiUpdateRequirements gui_update_requirements {};
    graphics::DrawData draw_data {};

    // In: user input
    f32x2 cursor_pos {};
    f32x2 cursor_pos_prev {};
    f32x2 cursor_delta {};
    f32 mouse_scroll_delta_in_lines {};
    Array<MouseButtonState, ToInt(MouseButton::Count)> mouse_buttons {};
    bool double_left_click {};
    Array<KeyState, ToInt(KeyCode::Count)> keys {};
    Array<ModifierKeyState, ToInt(ModifierKey::Count)> modifier_keys {};
    // may contain text from the clipboard - following from a wants_clipboard_paste request
    DynamicArray<char> clipboard_data {PageAllocator::Instance()};
    DynamicArrayInline<u32, 16> input_chars {};

    // In: frame info
    TimePoint current_time {};
    TimePoint time_prev {};
    f32 delta_time {};
    u64 update_count {};
    f32 display_ratio {};
    UiSize window_size {};
    void* native_window {}; // HWND, NSView*, etc.

    // internal
    ArenaAllocator event_arena {Malloc::Instance(), 256};
};
