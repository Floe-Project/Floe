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
        bool double_click {}; // cleared every frame
        bool is_dragging {};
        bool dragging_started {}; // cleared every frame
        bool dragging_ended {}; // cleared every frame
    };

    struct KeyState {
        struct Event {
            ModifierFlags modifiers;
        };

        bool is_down;
        ArenaStack<Event> presses_or_repeats; // key-down or repeats since last frame, cleared every frame
        ArenaStack<Event> presses; // key-down events since last frame, cleared every frame
        ArenaStack<Event> releases; // key-up events since last frame, cleared every frame
    };

    auto const& Mouse(MouseButton n) const { return mouse_buttons[ToInt(n)]; }
    auto const& Key(ModifierKey n) const { return modifier_keys[ToInt(n)]; }
    auto const& Key(KeyCode n) const { return keys[ToInt(n)]; }

    void Reset() {
        cursor_pos = {};
        cursor_pos_prev = {};
        cursor_delta = {};
        mouse_scroll_delta_in_lines = {};
        mouse_buttons = {};
        modifier_keys = {};
        keys = {};
        dyn::Clear(clipboard_text);
        dyn::Clear(input_utf32_chars);
    }

    graphics::DrawContext* graphics_ctx {};

    f32x2 cursor_pos {};
    f32x2 cursor_pos_prev {};
    f32x2 cursor_delta {};
    f32 mouse_scroll_delta_in_lines {};
    Array<MouseButtonState, ToInt(MouseButton::Count)> mouse_buttons {};
    Array<KeyState, ToInt(KeyCode::Count)> keys {};
    Array<bool, ToInt(ModifierKey::Count)> modifier_keys {};
    // may contain text from the OS clipboard if you requested it
    DynamicArray<char> clipboard_text {PageAllocator::Instance()};
    DynamicArrayBounded<u32, 16> input_utf32_chars {};
    ArenaStack<String> file_picker_results {};

    TimePoint current_time {};
    TimePoint time_prev {};
    f32 delta_time {};
    u64 update_count {};
    UiSize window_size {};
    void* native_window {}; // HWND, NSView*, etc.
    void* pugl_view {}; // PuglView* for the current frame

    Atomic<bool> request_update {false};

    // internal
    ArenaAllocator event_arena {Malloc::Instance(), 256};
};

struct MouseTrackedRect {
    Rect rect;
    bool mouse_over;
};

enum class CursorType { Default, Hand, IBeam, AllArrows, HorizontalArrows, VerticalArrows, Count };

struct FilePickerDialogOptions {
    enum class Type { SaveFile, OpenFile, SelectFolder };
    struct FileFilter {
        String description;
        String wildcard_filter;
    };

    Type type {Type::OpenFile};
    String title {"Select File"};
    Optional<String> default_path {}; // folder and file
    Span<FileFilter const> filters {};
    bool allow_multiple_selection {};
};

// Fill this struct every frame to instruct the caller about the GUI's needs.
struct GuiFrameResult {
    enum class UpdateRequest {
        // 1. GUI will sleep until there's user iteraction or a timed wakeup fired
        Sleep,

        // 2. GUI will update at the timer (normally 60Hz)
        Animate,

        // 3. re-update the GUI instantly - as soon as the frame is done - use this sparingly for necessary
        // layout changes
        ImmediatelyUpdate,
    };

    // only sets the status if it's more important than the current status
    void ElevateUpdateRequest(UpdateRequest r) {
        if (ToInt(r) > ToInt(update_request)) update_request = r;
    }

    UpdateRequest update_request {UpdateRequest::Sleep};

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

    // Set this to the cursor that you want
    CursorType cursor_type = CursorType::Default;

    // Set this if you want text from the OS clipboard, it will be given to you in an upcoming frame
    bool wants_clipboard_text_paste = false;

    // Set this to the text that you want put into the OS clipboard
    // Must be valid until the next frame.
    Span<char> set_clipboard_text {};

    // Set this to request a file picker dialog be opened. Rejected if a dialog is already open. The
    // application owns the memory, not the framework. The memory must persist until the next frame.
    Optional<FilePickerDialogOptions> file_picker_dialog {};

    // Must be valid until the next frame.
    graphics::DrawData draw_data {};
};
