// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <pugl/gl.h> // on windows this includes windows.h
#include <pugl/pugl.h>
#include <test_utils.h> // pugl - bit of a hack including it this way

#include "foundation/foundation.hpp"
#include "os/undef_windows_macros.h"

#include "clap/host.h"
#include "gui/gui.hpp"
#include "gui_frame.hpp"
#include "plugin_instance.hpp"
#include "settings/settings_gui.hpp"

constexpr bool k_debug_gui_platform = false;

struct GuiPlatform {
    static constexpr uintptr_t k_timer_id = 200;

    static int g_world_counter;
    static PuglWorld* g_world;

    clap_host const& host;
    SettingsFile& settings;
    Logger& logger;
    bool realised = false;
    PuglWorld* world {};
    PuglView* view {};
    CursorType current_cursor {CursorType::Default};
    graphics::DrawContext* graphics_ctx {};
    GuiFrameResult last_result {};
    GuiFrameInput frame_state {};
    Optional<Gui> gui {};
};

// Public API
// ==========================================================================================================

enum class GuiPlatformErrorCode {
    UnknownError,
    Unsupported,
    BackendFailed,
    RegistrationFailed,
    RealizeFailed,
    SetFormatFailed,
    CreateContextFailed,
};

static ErrorCodeCategory const gui_platform_error_code {
    .category_id = "GUIP",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((GuiPlatformErrorCode)code.code) {
            case GuiPlatformErrorCode::UnknownError: str = "Unknown error"; break;
            case GuiPlatformErrorCode::Unsupported: str = "Unsupported"; break;
            case GuiPlatformErrorCode::BackendFailed: str = "Backend init failed"; break;
            case GuiPlatformErrorCode::RegistrationFailed: str = "Registration failed"; break;
            case GuiPlatformErrorCode::RealizeFailed: str = "Realize failed"; break;
            case GuiPlatformErrorCode::SetFormatFailed: str = "Set format failed"; break;
            case GuiPlatformErrorCode::CreateContextFailed: str = "Create context failed"; break;
        }
        return writer.WriteChars(str);
    },
};
inline ErrorCodeCategory const& ErrorCategoryForEnum(GuiPlatformErrorCode) { return gui_platform_error_code; }

static ErrorCodeOr<void> Required(PuglStatus status) {
    switch (status) {
        case PUGL_SUCCESS: return k_success;

        case PUGL_UNSUPPORTED: return ErrorCode {GuiPlatformErrorCode::Unsupported};
        case PUGL_FAILURE:
        case PUGL_UNKNOWN_ERROR: return ErrorCode {GuiPlatformErrorCode::UnknownError};
        case PUGL_BACKEND_FAILED: return ErrorCode {GuiPlatformErrorCode::BackendFailed};
        case PUGL_REGISTRATION_FAILED: return ErrorCode {GuiPlatformErrorCode::RegistrationFailed};
        case PUGL_REALIZE_FAILED: return ErrorCode {GuiPlatformErrorCode::RealizeFailed};
        case PUGL_SET_FORMAT_FAILED: return ErrorCode {GuiPlatformErrorCode::SetFormatFailed};
        case PUGL_CREATE_CONTEXT_FAILED: return ErrorCode {GuiPlatformErrorCode::CreateContextFailed};

        // Bugs
        case PUGL_BAD_BACKEND: Panic("Invalid or missing backend");
        case PUGL_BAD_CONFIGURATION: Panic("Invalid view configuration");
        case PUGL_BAD_PARAMETER: Panic("Invalid parameter");
        case PUGL_NO_MEMORY: Panic("Failed to allocate memory");
    }
    return k_success;
}

namespace detail {
static PuglStatus EventHandler(PuglView* view, PuglEvent const* event);
}

PUBLIC ErrorCodeOr<void> CreateView(GuiPlatform& platform, PluginInstance& plugin) {
    platform.g_world_counter++;
    if (auto const floe_host =
            (FloeClapExtensionHost const*)platform.host.get_extension(&platform.host,
                                                                      k_floe_clap_extension_id);
        floe_host != nullptr) {
        platform.world = (PuglWorld*)floe_host->pugl_world;
        ASSERT(platform.world != nullptr);
    } else if (platform.g_world_counter == 1) {
        ASSERT(platform.g_world == nullptr);
        platform.g_world = puglNewWorld(PUGL_MODULE, 0);
        if (platform.g_world == nullptr) Panic("out of memory");
        puglSetWorldString(platform.g_world, PUGL_CLASS_NAME, "Floe");
        platform.world = platform.g_world;
    } else {
        ASSERT(platform.g_world != nullptr);
        platform.world = platform.g_world;
    }

    platform.view = puglNewView(platform.world);
    if (platform.view == nullptr) Panic("out of memory");

    puglSetHandle(platform.view, &platform);
    TRY(Required(puglSetEventFunc(platform.view, detail::EventHandler)));

    // IMPROVE: we might want a DirectX backend for Windows
    TRY(Required(puglSetBackend(platform.view, puglGlBackend())));
    TRY(Required(puglSetViewHint(platform.view, PUGL_CONTEXT_VERSION_MAJOR, 3)));
    TRY(Required(puglSetViewHint(platform.view, PUGL_CONTEXT_VERSION_MINOR, 3)));
    TRY(Required(puglSetViewHint(platform.view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_COMPATIBILITY_PROFILE)));
    puglSetViewHint(platform.view, PUGL_CONTEXT_DEBUG, RUNTIME_SAFETY_CHECKS_ON);

    puglSetViewHint(platform.view, PUGL_RESIZABLE, true);
    auto const size = gui_settings::WindowSize(platform.settings.settings.gui);
    TRY(Required(puglSetSize(platform.view, size.width, size.height)));

    platform.gui.Emplace(platform.frame_state, plugin);

    return k_success;
}

PUBLIC void DestroyView(GuiPlatform& platform) {
    platform.gui.Clear();

    if (platform.realised) {
        puglStopTimer(platform.view, platform.k_timer_id);
        puglUnrealize(platform.view);
        platform.realised = false;
    }
    puglFreeView(platform.view);

    if (--platform.g_world_counter == 0) {
        if (platform.g_world) {
            puglFreeWorld(platform.g_world);
            platform.g_world = nullptr;
        }
        platform.world = nullptr;
    }
}

PUBLIC void PollAndUpdate(GuiPlatform& platform) { puglUpdate(platform.world, 0); }

PUBLIC ErrorCodeOr<void> SetParent(GuiPlatform& platform, clap_window_t const& window) {
    TRY(Required(puglSetParentWindow(platform.view, (uintptr_t)window.ptr)));
    puglSetPosition(platform.view, 0, 0);
    return k_success;
}

PUBLIC ErrorCodeOr<void> SetTransient(GuiPlatform& platform, clap_window_t const& window) {
    TRY(Required(puglSetTransientParent(platform.view, (uintptr_t)window.ptr)));
    return k_success;
}

PUBLIC ErrorCodeOr<void> SetVisible(GuiPlatform& platform, bool visible) {
    if (visible) {
        if (!platform.realised) {
            TRY(Required(puglRealize(platform.view)));
            TRY(Required(
                puglStartTimer(platform.view, platform.k_timer_id, 1.0 / (f64)k_gui_refresh_rate_hz)));
            platform.realised = true;
        }
        TRY(Required(puglShow(platform.view, PUGL_SHOW_PASSIVE)));
    } else {
        // IMRPOVE: stop update timers, make things more efficient
        TRY(Required(puglHide(platform.view)));
    }
    return k_success;
}

PUBLIC bool SetSize(GuiPlatform& platform, UiSize new_size) {
    return puglSetSize(platform.view, new_size.width, new_size.height) == PUGL_SUCCESS;
}

PUBLIC UiSize WindowSize(GuiPlatform& platform) {
    auto const frame = puglGetFrame(platform.view);
    return {frame.width, frame.height};
}

// Details
// ==========================================================================================================

namespace detail {

static bool IsUpdateNeeded(GuiPlatform& platform) {
    bool update_needed = false;

    if (platform.last_result.update_request > GuiFrameResult::UpdateRequest::Sleep) update_needed = true;

    if (platform.last_result.timed_wakeups) {
        for (usize i = 0; i < platform.last_result.timed_wakeups->size;) {
            auto& t = (*platform.last_result.timed_wakeups)[i];
            if (TimePoint::Now() >= t) {
                update_needed = true;
                dyn::Remove(*platform.last_result.timed_wakeups, i);
            } else {
                ++i;
            }
        }
    }

    return update_needed;
}

static ModifierFlags CreateModifierFlags(u32 flags) {
    ModifierFlags result {};
    if (flags & PUGL_MOD_SHIFT) result.Set(ModifierKey::Shift);
    if (flags & PUGL_MOD_CTRL) result.Set(ModifierKey::Ctrl);
    if (flags & PUGL_MOD_ALT) result.Set(ModifierKey::Alt);
    if (flags & PUGL_MOD_SUPER) result.Set(ModifierKey::Super);
    return result;
}

static bool EventWheel(GuiPlatform& platform, PuglScrollEvent const& scroll_event) {
    // IMPROVE: support horizontal scrolling
    if (scroll_event.direction != PUGL_SCROLL_UP && scroll_event.direction != PUGL_SCROLL_DOWN) return false;

    auto const delta_lines = (f32)scroll_event.dy;
    platform.frame_state.mouse_scroll_delta_in_lines += delta_lines;
    if (platform.last_result.wants_mouse_scroll) return true;
    return false;
}

static bool EventMotion(GuiPlatform& platform, PuglMotionEvent const& motion_event) {
    auto const new_cursor_pos = f32x2 {(f32)motion_event.x, (f32)motion_event.y};
    bool result = false;
    platform.frame_state.cursor_pos = new_cursor_pos;

    for (auto& btn : platform.frame_state.mouse_buttons) {
        if (btn.is_down) {
            if (!btn.is_dragging) btn.dragging_started = true;
            btn.is_dragging = true;
        }
    }

    if (platform.last_result.mouse_tracked_rects.size == 0 || platform.last_result.wants_mouse_capture) {
        result = true;
    } else if (IsUpdateNeeded(platform)) {
        return true;
    } else {
        for (auto const i : Range(platform.last_result.mouse_tracked_rects.size)) {
            auto& item = platform.last_result.mouse_tracked_rects[i];
            bool const mouse_over = item.rect.Contains(platform.frame_state.cursor_pos);
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

static Optional<MouseButton> RemapMouseButton(u32 button) {
    switch (button) {
        case 0: return MouseButton::Left;
        case 1: return MouseButton::Right;
        case 2: return MouseButton::Middle;
    }
    return nullopt;
}

static bool EventMouseButton(GuiPlatform& platform, PuglButtonEvent const& button_event, bool is_down) {
    auto const button = RemapMouseButton(button_event.button);
    if (!button) return false;

    GuiFrameInput::MouseButtonState::Event const e {
        .point = {(f32)button_event.x, (f32)button_event.y},
        .time = TimePoint::Now(),
        .modifiers = CreateModifierFlags(button_event.state),
    };

    auto& btn = platform.frame_state.mouse_buttons[ToInt(*button)];
    btn.is_down = is_down;
    if (is_down) {
        if ((e.time - btn.last_pressed_time) <= k_double_click_interval_seconds) btn.double_click = true;
        btn.last_pressed_point = e.point;
        btn.last_pressed_time = e.time;
    } else {
        if (btn.is_dragging) btn.dragging_ended = true;
        btn.is_dragging = false;
    }
    btn.presses.Append(e, platform.frame_state.event_arena);

    bool result = false;
    if (platform.last_result.mouse_tracked_rects.size == 0 || platform.last_result.wants_mouse_capture ||
        (platform.last_result.wants_all_left_clicks && button == MouseButton::Left) ||
        (platform.last_result.wants_all_right_clicks && button == MouseButton::Right) ||
        (platform.last_result.wants_all_middle_clicks && button == MouseButton::Middle)) {
        result = true;
    } else {
        for (auto const i : Range(platform.last_result.mouse_tracked_rects.size)) {
            auto& item = platform.last_result.mouse_tracked_rects[i];
            bool const mouse_over = item.rect.Contains(platform.frame_state.cursor_pos);
            if (mouse_over) {
                result = true;
                break;
            }
        }
    }

    return result;
}

static bool EventKeyRegular(GuiPlatform& platform, KeyCode key_code, bool is_down, ModifierFlags modifiers) {
    auto& key = platform.frame_state.keys[ToInt(key_code)];
    if (is_down) {
        key.presses_or_repeats.Append({modifiers}, platform.frame_state.event_arena);
        if (!key.is_down) key.presses.Append({modifiers}, platform.frame_state.event_arena);
    } else {
        key.releases.Append({modifiers}, platform.frame_state.event_arena);
    }
    key.is_down = is_down;

    if (platform.last_result.wants_keyboard_input) return true;
    if (platform.last_result.wants_just_arrow_keys &&
        (key_code == KeyCode::UpArrow || key_code == KeyCode::DownArrow || key_code == KeyCode::LeftArrow ||
         key_code == KeyCode::RightArrow)) {
        return true;
    }
    return false;
}

static Optional<KeyCode> RemapKeyCode(u32 pugl_key) {
    switch (pugl_key) {
        case PUGL_KEY_TAB: return KeyCode::Tab;
        case PUGL_KEY_LEFT: return KeyCode::LeftArrow;
        case PUGL_KEY_RIGHT: return KeyCode::RightArrow;
        case PUGL_KEY_UP: return KeyCode::UpArrow;
        case PUGL_KEY_DOWN: return KeyCode::DownArrow;
        case PUGL_KEY_PAGE_UP: return KeyCode::PageUp;
        case PUGL_KEY_PAGE_DOWN: return KeyCode::PageDown;
        case PUGL_KEY_HOME: return KeyCode::Home;
        case PUGL_KEY_END: return KeyCode::End;
        case PUGL_KEY_DELETE: return KeyCode::Delete;
        case PUGL_KEY_BACKSPACE: return KeyCode::Backspace;
        case PUGL_KEY_ENTER: return KeyCode::Enter;
        case PUGL_KEY_ESCAPE: return KeyCode::Escape;
        case PUGL_KEY_F1: return KeyCode::F1;
        case PUGL_KEY_F2: return KeyCode::F2;
        case PUGL_KEY_F3: return KeyCode::F3;
        case 'a': return KeyCode::A;
        case 'c': return KeyCode::C;
        case 'v': return KeyCode::V;
        case 'x': return KeyCode::X;
        case 'y': return KeyCode::Y;
        case 'z': return KeyCode::Z;
    }
    return nullopt;
}

static Optional<ModifierKey> RemapModKey(u32 pugl_key) {
    switch (pugl_key) {
        case PUGL_KEY_SHIFT_L:
        case PUGL_KEY_SHIFT_R: return ModifierKey::Shift;
        case PUGL_KEY_CTRL_L:
        case PUGL_KEY_CTRL_R: return ModifierKey::Ctrl;
        case PUGL_KEY_ALT_L:
        case PUGL_KEY_ALT_R: return ModifierKey::Alt;
        case PUGL_KEY_SUPER_L:
        case PUGL_KEY_SUPER_R: return ModifierKey::Super;
    }
    return nullopt;
}

static bool EventKeyModifier(GuiPlatform& platform, ModifierKey mod_key, bool is_down) {
    auto& mod = platform.frame_state.modifier_keys[ToInt(mod_key)];
    if (is_down) {
        if (!mod.is_down) mod.presses = true;
        ++mod.is_down;
    } else {
        --mod.is_down;
        if (mod.is_down == 0) mod.releases = true;
    }
    return true;
}

static bool EventKey(GuiPlatform& platform, PuglKeyEvent const& key_event, bool is_down) {
    if (auto const key_code = RemapKeyCode(key_event.key))
        return EventKeyRegular(platform, *key_code, is_down, CreateModifierFlags(key_event.state));
    else if (auto const mod_key = RemapModKey(key_event.key))
        return EventKeyModifier(platform, *mod_key, is_down);
    return false;
}

static bool EventText(GuiPlatform& platform, PuglTextEvent const& text_event) {
    dyn::Append(platform.frame_state.input_utf32_chars, text_event.character);
    if (platform.last_result.wants_keyboard_input) return true;
    return false;
}

static void CreateGraphicsContext(GuiPlatform& platform) {
    auto graphics_ctx = graphics::CreateNewDrawContext();
    auto const outcome = graphics_ctx->CreateDeviceObjects((void*)puglGetNativeView(platform.view));
    if (outcome.HasError()) {
        platform.logger.ErrorLn("Failed to create graphics context: {}", outcome.Error());
        delete graphics_ctx;
        return;
    }
    platform.graphics_ctx = graphics_ctx;
}

static void DestroyGraphicsContext(GuiPlatform& platform) {
    if (platform.graphics_ctx) {
        platform.graphics_ctx->DestroyDeviceObjects();
        platform.graphics_ctx->fonts.Clear();
        delete platform.graphics_ctx;
        platform.graphics_ctx = nullptr;
    }
}

static bool EventDataOffer(GuiPlatform& platform, PuglDataOfferEvent const& data_offer) {
    bool result = false;
    for (auto const type_index : Range(puglGetNumClipboardTypes(platform.view))) {
        auto const type = puglGetClipboardType(platform.view, type_index);
        if (NullTermStringsEqual(type, "text/plain")) {
            puglAcceptOffer(platform.view, &data_offer, type_index);
            result = true;
        }
    }
    return result;
}

static bool EventData(GuiPlatform& platform, PuglDataEvent const& data_event) {
    auto const type_index = data_event.typeIndex;
    auto const type = puglGetClipboardType(platform.view, type_index);
    if (NullTermStringsEqual(type, "text/plain")) {
        usize size = 0;
        void const* data = puglGetClipboard(platform.view, type_index, &size);
        if (data && size) {
            dyn::Assign(platform.frame_state.clipboard_text, String {(char const*)data, size});
            return true;
        }
    }
    return false;
}

static void BeginFrame(GuiFrameInput& frame_state) {
    if (All(frame_state.cursor_pos < f32x2 {0, 0} || frame_state.cursor_pos_prev < f32x2 {0, 0})) {
        // if mouse just appeared or disappeared (negative coordinate) we cancel out movement by setting
        // to zero
        frame_state.cursor_delta = {0, 0};
    } else {
        frame_state.cursor_delta = frame_state.cursor_pos - frame_state.cursor_pos_prev;
    }
    frame_state.cursor_pos_prev = frame_state.cursor_pos;

    frame_state.current_time = TimePoint::Now();

    if (frame_state.time_prev)
        frame_state.delta_time = (f32)(frame_state.current_time - frame_state.time_prev);
    else
        frame_state.delta_time = 0;
    frame_state.time_prev = frame_state.current_time;
}

static void ClearImpermanentState(GuiFrameInput& frame_state) {
    for (auto& btn : frame_state.mouse_buttons) {
        btn.dragging_started = false;
        btn.dragging_ended = false;
        btn.double_click = false;
        btn.presses.Clear();
        btn.releases.Clear();
    }

    for (auto& mod : frame_state.modifier_keys) {
        mod.presses = 0;
        mod.releases = 0;
    }

    for (auto& key : frame_state.keys) {
        key.presses.Clear();
        key.releases.Clear();
        key.presses_or_repeats.Clear();
    }

    frame_state.input_utf32_chars = {};
    frame_state.mouse_scroll_delta_in_lines = 0;
    dyn::Clear(frame_state.clipboard_text);
    frame_state.event_arena.ResetCursorAndConsolidateRegions();
    ++frame_state.update_count;
}

static void HandlePostUpdateRequests(GuiPlatform& platform) {
    if (platform.last_result.cursor_type != platform.current_cursor) {
        platform.current_cursor = platform.last_result.cursor_type;
        puglSetCursor(platform.view, ({
                          PuglCursor cursor = PUGL_CURSOR_ARROW;
                          switch (platform.last_result.cursor_type) {
                              case CursorType::Default: cursor = PUGL_CURSOR_ARROW; break;
                              case CursorType::Hand: cursor = PUGL_CURSOR_HAND; break;
                              case CursorType::IBeam: cursor = PUGL_CURSOR_CARET; break;
                              case CursorType::AllArrows: cursor = PUGL_CURSOR_ALL_SCROLL; break;
                              case CursorType::HorizontalArrows: cursor = PUGL_CURSOR_LEFT_RIGHT; break;
                              case CursorType::VerticalArrows: cursor = PUGL_CURSOR_UP_DOWN; break;
                              case CursorType::Count: break;
                          }
                          cursor;
                      }));
    }

    if (platform.last_result.wants_clipboard_text_paste) {
        // IMPORTANT: this will call into our event handler function right from here rather than queue things
        // up
        puglPaste(platform.view);
    }

    if (auto const cb = platform.last_result.set_clipboard_text; cb.size) {
        // we can re-use the 'paste' clipboard buffer even though this is a 'copy' operation
        auto& buffer = platform.frame_state.clipboard_text;
        dyn::Assign(buffer, cb);
        // IMPORTANT: pugl needs a null terminator despite also taking a size
        dyn::Append(buffer, '\0');

        puglSetClipboard(platform.view, "text/plain", buffer.data, buffer.size);
    }
}

static void UpdateAndRender(GuiPlatform& platform) {
    if (!platform.graphics_ctx) return;
    if (!puglGetVisible(platform.view)) return;

    auto const window_size = WindowSize(platform);

    platform.frame_state.graphics_ctx = platform.graphics_ctx;
    platform.frame_state.native_window = (void*)puglGetNativeView(platform.view);
    platform.frame_state.window_size = window_size;

    u32 num_repeats = 0;
    do {
        // Mostly we'd only expect 1 or 2 updates but we set a hard limit of 4 as a fallback.
        if (num_repeats++ >= 4) {
            platform.logger.WarningLn("GUI update loop repeated too many times");
            break;
        }

        ZoneNamedN(repeat, "Update", true);

        BeginFrame(platform.frame_state);

        platform.last_result = GuiUpdate(&*platform.gui);

        // clear the state ready for new events, and to ensure they're only processed once
        ClearImpermanentState(platform.frame_state);

        // it's important to do this after clearing the impermanent state because this might add new events to
        // the frame
        HandlePostUpdateRequests(platform);
    } while (platform.last_result.update_request == GuiFrameResult::UpdateRequest::ImmediatelyUpdate);

    if (platform.last_result.draw_data.draw_lists.size) {
        ZoneNamedN(render, "render", true);
        auto o = platform.graphics_ctx->Render(platform.last_result.draw_data,
                                               window_size,
                                               platform.frame_state.display_ratio,
                                               Rect(0, 0, window_size.ToFloat2()));
        if (o.HasError()) platform.logger.ErrorLn("GUI render failed: {}", o.Error());
    }
}

static PuglStatus EventHandler(PuglView* view, PuglEvent const* event) {
    if constexpr (k_debug_gui_platform)
        if (event->type != PUGL_UPDATE && event->type != PUGL_TIMER) printEvent(event, "PUGL: ", true);
    auto& platform = *(GuiPlatform*)puglGetHandle(view);

    bool post_redisplay = false;

    switch (event->type) {
        case PUGL_NOTHING: break;

        case PUGL_REALIZE: {
            puglGrabFocus(platform.view);
            CreateGraphicsContext(platform);
            break;
        }

        case PUGL_UNREALIZE: {
            DestroyGraphicsContext(platform);
            break;
        }

        // resized or moved
        case PUGL_CONFIGURE: {
            if (platform.graphics_ctx)
                platform.graphics_ctx->Resize({event->configure.width, event->configure.height});
            break;
        }

        case PUGL_UPDATE: {
            break;
        }

        case PUGL_EXPOSE: {
            UpdateAndRender(platform);
            break;
        }

        case PUGL_CLOSE: {
            auto const host_gui =
                (clap_host_gui const*)platform.host.get_extension(&platform.host, CLAP_EXT_GUI);
            if (host_gui) host_gui->closed(&platform.host, false);
            break;
        }

        case PUGL_FOCUS_IN:
        case PUGL_FOCUS_OUT: break;

        case PUGL_KEY_PRESS: {
            post_redisplay = EventKey(platform, event->key, true);
            break;
        }

        case PUGL_KEY_RELEASE: {
            post_redisplay = EventKey(platform, event->key, false);
            break;
        }

        case PUGL_TEXT: {
            post_redisplay = EventText(platform, event->text);
            break;
        }

        case PUGL_POINTER_IN: puglGrabFocus(platform.view); break;
        case PUGL_POINTER_OUT: {
            break;
        }

        case PUGL_BUTTON_PRESS:
        case PUGL_BUTTON_RELEASE: {
            post_redisplay = EventMouseButton(platform, event->button, event->type == PUGL_BUTTON_PRESS);
            break;
        }

        case PUGL_MOTION: {
            post_redisplay = EventMotion(platform, event->motion);
            break;
        }

        case PUGL_SCROLL: {
            post_redisplay = EventWheel(platform, event->scroll);
            break;
        }

        case PUGL_TIMER: {
            if (event->timer.id == platform.k_timer_id) post_redisplay = IsUpdateNeeded(platform);
            break;
        }

        case PUGL_DATA_OFFER: {
            post_redisplay = EventDataOffer(platform, event->offer);
            break;
        }

        case PUGL_DATA: {
            post_redisplay = EventData(platform, event->data);
            break;
        }

        case PUGL_CLIENT: break;

        case PUGL_LOOP_ENTER:
        case PUGL_LOOP_LEAVE: break;
    }

    if (post_redisplay) puglPostRedisplay(view);

    return PUGL_SUCCESS;
}

} // namespace detail
