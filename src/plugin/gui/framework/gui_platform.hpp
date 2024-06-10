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

enum KeyCodes {
    KeyCodeTab,
    KeyCodeLeftArrow,
    KeyCodeRightArrow,
    KeyCodeUpArrow,
    KeyCodeDownArrow,
    KeyCodePageUp,
    KeyCodePageDown,
    KeyCodeHome,
    KeyCodeEnd,
    KeyCodeDelete,
    KeyCodeBackspace,
    KeyCodeEnter,
    KeyCodeEscape,
    KeyCodeA,
    KeyCodeC,
    KeyCodeV,
    KeyCodeX,
    KeyCodeY,
    KeyCodeZ,
    KeyCodeF1,
    KeyCodeF2,
    KeyCodeF3,
    KeyCodeCount
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

enum class CursorType { Default, Hand, IBeam, AllArrows, HorizontalArrows, VerticalArrows, Hidden, Count };

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
    CursorType cursor_type = CursorType::Default;
};

#define GUI_PLATFORM_ARGS                                                                                    \
    const clap_host &host, TrivialFixedSizeFunction<16, void()>&&update, Logger &logger,                     \
        SettingsFile &settings
#define GUI_PLATFORM_FORWARD_ARGS host, Move(update), logger, settings

struct GuiPlatform {
    GuiPlatform(GUI_PLATFORM_ARGS) : host(host), update(Move(update)), logger(logger), settings(settings) {}

    virtual ~GuiPlatform() {}
    virtual f32 GetDisplayRatio() { return 1; }

    virtual void* OpenWindow() = 0;
    virtual bool CloseWindow() = 0;
    virtual void* GetWindow() = 0;
    virtual void PollAndUpdate() {}
    virtual void SetParent(clap_window_t const* window) = 0;
    virtual bool SetTransient(clap_window_t const*) { return false; }
    virtual void SetVisible(bool visible) = 0;
    virtual bool SetSize(UiSize new_size) = 0;
    virtual bool SetClipboard(String mime_type, String data) = 0;
    virtual bool RequestClipboardPaste() = 0;

    void SetGUIDirty() { gui_update_requirements.mark_gui_dirty = true; }

    bool ShiftJustPressed() { return key_shift && !key_shift_prev; }
    bool ShiftJustReleased() { return !key_shift && key_shift_prev; }
    bool CtrlJustPressed() { return key_ctrl && !key_ctrl_prev; }
    bool CtrlJustReleased() { return !key_ctrl && key_ctrl_prev; }
    bool AltJustPressed() { return key_alt && !key_alt_prev; }
    bool AltJustReleased() { return !key_alt && key_alt_prev; }
    bool ContainsCursor(Rect r) { return r.Contains(cursor_pos); }
    bool IsKeyReleased(KeyCodes keycode) {
        return !keys_pressed[ToInt(keycode)] && keys_pressed_prev[ToInt(keycode)];
    }
    bool IsKeyPressed(KeyCodes keycode) { return keys_pressed[ToInt(keycode)]; }
    bool IsKeyDown(KeyCodes keycode) { return keys_down[ToInt(keycode)]; }
    bool KeyJustWentDown(KeyCodes keycode) {
        return IsKeyDown(keycode) && !keys_down_prev[ToInt(keycode)] && update_guicall_count == 0;
    }
    bool KeyJustWentUp(KeyCodes keycode) {
        return !IsKeyDown(keycode) && keys_down_prev[ToInt(keycode)] && update_guicall_count == 0;
    }

    // n for mouse button number
    bool MouseJustWentDown(int n) { return mouse_down[n] && !mouse_down_prev[n]; }
    bool MouseJustWentUp(int n) { return !mouse_down[n] && mouse_down_prev[n]; }
    bool MouseJustWentDownInRegion(int n, Rect r) { return MouseJustWentDown(n) && ContainsCursor(r); }
    bool MouseJustWentUpInRegion(int n, Rect r) { return MouseJustWentUp(n) && ContainsCursor(r); }
    bool MouseJustWentDownAndUpInRegion(int n, Rect r) {
        return MouseJustWentUpInRegion(n, r) && r.Contains(last_mouse_down_point[n]);
    }
    bool MouseIsDragging(int n) { return mouse_is_dragging[n]; }
    bool MouseJustStartedDragging(int n) { return mouse_is_dragging[n] && !mouse_is_dragging_prev[n]; }
    bool MouseJustFinishedDragging(int n) { return !mouse_down[n] && mouse_is_dragging_prev[n]; }
    f64 SecondsSinceMouseDown(int n) { return current_time - time_at_mouse_down[n]; }
    f32x2 DeltaFromMouseDown(int n) { return cursor_pos - last_mouse_down_point[n]; }

    //
    // Called by platform specific code
    // ======================================================================================================
    ErrorCodeOr<void> InitGraphics(void* object_needed_for_device);
    void WindowWasResized(UiSize new_size);
    void DestroyGraphics();

    void SetStateChanged(char const* reason) {
        platform_state_changed = true;
        platform_state_changed_reason = reason;
    }
    bool HandleMouseWheel(f32 delta);
    bool HandleMouseMoved(f32 cursor_x, f32 cursor_y);
    bool HandleMouseClicked(int button, bool is_down);
    bool HandleDoubleLeftClick();
    bool HandleKeyPressed(KeyCodes code, bool is_down);
    bool HandleInputChar(int character);
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

    bool mouse_is_dragging[3] {};
    bool mouse_is_dragging_prev[3] {};
    f32x2 last_mouse_down_point[3] {};
    TimePoint time_at_mouse_down[3] {};
    bool mouse_down[3] {};
    bool mouse_down_prev[3] {};
    bool double_left_click {};

    UiSize window_size {};
    f32 delta_time {};
    TimePoint current_time {};

    bool keys_down[KeyCodeCount] {}; // the state of the button
    bool keys_down_prev[KeyCodeCount] {}; // the state of the button
    bool keys_pressed[KeyCodeCount] {}; // indicates the a key has just be pressed (include key repeats),
                                        // reset every frame
    bool keys_pressed_prev[KeyCodeCount] {};
    bool key_ctrl {};
    bool key_shift {};
    bool key_alt {};

    DynamicArray<char> clipboard_data {PageAllocator::Instance()};

    int input_chars[16] {};

    uint64_t update_count {};
    int update_guicall_count {}; // update gui is sometimes called 2 times in one frame
    bool is_window_open {};
    f32 display_ratio {};

    bool currently_updating {};

    //
    // Internals
    // ======================================================================================================
    clap_host const& host;
    TrivialFixedSizeFunction<16, void()> update;
    Logger& logger;
    SettingsFile& settings;

#if !PRODUCTION_BUILD
    f32 paint_time_average[256] {};
    int paint_time_counter {};
    f32 paint_average_time {};
    f32 paint_prev_time {};
    f32 render_prev_time {};
    f32 update_prev_time {};
#endif
    TimePoint time_at_last_paint {};
    bool platform_state_changed {};
    char const* platform_state_changed_reason = "";
    bool key_ctrl_prev {};
    bool key_shift_prev {};
    bool key_alt_prev {};
};

GuiPlatform* CreateGuiPlatform(GUI_PLATFORM_ARGS);
void DestroyGuiPlatform(GuiPlatform* platform);
