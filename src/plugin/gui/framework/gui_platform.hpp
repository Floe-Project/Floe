// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <pugl/gl.h> // on windows this includes windows.h
#include <pugl/pugl.h>
#include <test_utils.h> // pugl - bit of a hack including it this way

#include "foundation/foundation.hpp"
#include "os/undef_windows_macros.h"
#include "utils/debug/debug.hpp"

#include "clap/host.h"
#include "gui/gui.hpp"
#include "gui_frame.hpp"
#include "plugin_instance.hpp"
#include "settings/settings_gui.hpp"

// TODO: go over the API docs and review usage
// TODO: add error handling
// TODO: refactor, use free functions, one contained function for each event, maybe pull out Gui struct

struct GuiPlatform {
    GuiPlatform(clap_host const& host, SettingsFile& settings, Logger& logger)
        : host(host)
        , settings(settings)
        , logger(logger) {}

    bool DestroyView() {
        gui.Clear();

        if (realised) {
            puglStopTimer(view, k_timer_id);
            puglUnrealize(view);
            realised = false;
        }
        puglFreeView(view);

        if (--g_world_counter == 0) {
            if (g_world) {
                puglFreeWorld(g_world);
                g_world = nullptr;
            }
            world = nullptr;
        }
        return true;
    }

    void PollAndUpdate() { puglUpdate(world, 0); }

    void SetParent(clap_window_t const* window) {
        auto const status = puglSetParentWindow(view, (uintptr_t)window->ptr);
        puglSetPosition(view, 0, 0);
        if (status != PUGL_SUCCESS) {
            auto const status_error = puglStrerror(status);
            DebugLn("puglSetParentWindow failed: {}", FromNullTerminated(status_error));
            TODO("handle error");
        }
    }

    bool SetTransient(clap_window_t const* window) {
        puglSetTransientParent(view, (uintptr_t)window->ptr);
        return true;
    }

    void SetVisible(bool visible) {
        if (visible) {
            if (!realised) {
                if (auto const status = puglRealize(view); status != PUGL_SUCCESS) {
                    auto const status_error = puglStrerror(status);
                    DebugLn("puglRealize failed: {}", FromNullTerminated(status_error));
                    TODO("handle error");
                }
                if (auto const status = puglStartTimer(view, k_timer_id, 1.0 / (f64)k_gui_refresh_rate_hz);
                    status != PUGL_SUCCESS) {
                    TODO("handle error");
                }
                realised = true;
            }
            puglShow(view, PUGL_SHOW_PASSIVE);
        } else
            puglHide(view);
    }

    bool SetSize(UiSize new_size) {
        DebugLn("SetSize: {}x{}", new_size.width, new_size.height);
        puglSetSize(view, new_size.width, new_size.height);
        return true;
    }

    bool SetClipboard(String mime_type, String data) {
        ArenaAllocatorWithInlineStorage<250> allocator;
        return puglSetClipboard(view, NullTerminated(mime_type, allocator), data.data, data.size) ==
               PUGL_SUCCESS;
    }

    UiSize WindowSize() {
        auto const frame = puglGetFrame(view);
        return {frame.width, frame.height};
    }

    static Optional<KeyCode> ConvertKeyCode(u32 key) {
        switch (key) {
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

    static Optional<ModifierKey> ModKey(u32 key) {
        switch (key) {
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

    static Optional<MouseButton> ConvertMouseButton(u32 button) {
        switch (button) {
            case 0: return MouseButton::Left;
            case 1: return MouseButton::Right;
            case 2: return MouseButton::Middle;
        }
        return nullopt;
    }

    static ModifierFlags ConvertModifierFlags(u32 flags) {
        ModifierFlags result {};
        if (flags & PUGL_MOD_SHIFT) result.Set(ModifierKey::Shift);
        if (flags & PUGL_MOD_CTRL) result.Set(ModifierKey::Ctrl);
        if (flags & PUGL_MOD_ALT) result.Set(ModifierKey::Alt);
        if (flags & PUGL_MOD_SUPER) result.Set(ModifierKey::Super);
        return result;
    }

    static PuglStatus OnEvent(PuglView* view, PuglEvent const* event) {
        if (event->type != PUGL_UPDATE && event->type != PUGL_TIMER) printEvent(event, "PUGL: ", true);
        auto& self = *(GuiPlatform*)puglGetHandle(view);

        // I'm not sure if pugl handles this for us or not, but let's just be safe. On Windows at least, it's
        // possible to receive events from within an event if you're doing certain, blocking operations that
        // trigger the event loop to pump again.
        if (self.processing_events) return PUGL_SUCCESS;
        self.processing_events = true;
        DEFER { self.processing_events = false; };

        switch (event->type) {
            case PUGL_NOTHING: break;

            case PUGL_REALIZE: {
                ZoneNamed("PUGL_REALIZE", true);
                self.graphics_ctx = graphics::CreateNewDrawContext();
                auto const outcome = self.graphics_ctx->CreateDeviceObjects((void*)puglGetNativeView(view));
                ASSERT(!outcome.HasError()); // TODO: handle error

                break;
            }
            case PUGL_UNREALIZE: {
                ZoneNamed("PUGL_UNREALIZE", true);
                if (self.graphics_ctx) {
                    self.graphics_ctx->DestroyDeviceObjects();
                    self.graphics_ctx->fonts.Clear();
                    delete self.graphics_ctx;
                    self.graphics_ctx = nullptr;
                }

                break;
            }

            // resized or moved
            case PUGL_CONFIGURE: {
                if (self.graphics_ctx)
                    self.graphics_ctx->Resize({event->configure.width, event->configure.height});
                break;
            }

            case PUGL_UPDATE: {
                break;
            }

            case PUGL_EXPOSE: {
                ZoneNamed("PUGL_EXPOSE", true);
                if (!self.graphics_ctx) break;

                auto const window_size = self.WindowSize();

                self.frame_state.graphics_ctx = self.graphics_ctx;
                self.frame_state.native_window = (void*)puglGetNativeView(self.view);
                self.frame_state.window_size = window_size;

                // Mostly we'd only expect 1 or 2 updates but we set a hard limit of 4 as a fallback.
                for (auto _ : Range(4)) {
                    ZoneNamedN(repeat, "Update", true);

                    self.BeginFrame();
                    self.last_result = GUIUpdate(&*self.gui);

                    if (self.last_result.cursor_type != self.current_cursor) {
                        self.current_cursor = self.last_result.cursor_type;
                        puglSetCursor(view, ({
                                          PuglCursor cursor = PUGL_CURSOR_ARROW;
                                          switch (self.last_result.cursor_type) {
                                              case CursorType::Default: cursor = PUGL_CURSOR_ARROW; break;
                                              case CursorType::Hand: cursor = PUGL_CURSOR_HAND; break;
                                              case CursorType::IBeam: cursor = PUGL_CURSOR_CARET; break;
                                              case CursorType::AllArrows:
                                                  cursor = PUGL_CURSOR_ALL_SCROLL;
                                                  break;
                                              case CursorType::HorizontalArrows:
                                                  cursor = PUGL_CURSOR_LEFT_RIGHT;
                                                  break;
                                              case CursorType::VerticalArrows:
                                                  cursor = PUGL_CURSOR_UP_DOWN;
                                                  break;
                                              case CursorType::Count: break;
                                          }
                                          cursor;
                                      }));
                    }

                    if (self.last_result.wants_clipboard_text_paste) puglPaste(view);

                    if (auto cb = self.last_result.set_clipboard_text; cb.size)
                        puglSetClipboard(view, "text/plain", cb.data, cb.size);

                    self.EndFrame();

                    if (self.last_result.status != GuiFrameResult::Status::ImmediatelyUpdate) break;
                }

                if (self.last_result.draw_data.draw_lists.size) {
                    ZoneNamedN(render, "render", true);
                    auto o = self.graphics_ctx->Render(self.last_result.draw_data,
                                                       window_size,
                                                       self.frame_state.display_ratio,
                                                       Rect(0, 0, window_size.ToFloat2()));
                    if (o.HasError()) self.logger.ErrorLn("GUI render failed: {}", o.Error());
                }

                break;
            }

            case PUGL_CLOSE: {
                auto const host_gui = (clap_host_gui const*)self.host.get_extension(&self.host, CLAP_EXT_GUI);
                if (host_gui) host_gui->closed(&self.host, false);
                break;
            }

            case PUGL_FOCUS_IN:
            case PUGL_FOCUS_OUT: break;

            case PUGL_KEY_PRESS: {
                if (auto const key = ConvertKeyCode(event->key.key)) {
                    if (self.HandleKeyPressed(*key, ConvertModifierFlags(event->key.state), true))
                        puglPostRedisplay(view);
                } else if (auto mod_key = self.ModKey(event->key.key)) {
                    auto& mod = self.frame_state.modifier_keys[ToInt(*mod_key)];
                    if (!mod.is_down) mod.presses = true;
                    ++mod.is_down;
                }
                break;
            }

            case PUGL_KEY_RELEASE: {
                if (auto const key = ConvertKeyCode(event->key.key)) {
                    if (self.HandleKeyPressed(*key, ConvertModifierFlags(event->key.state), false))
                        puglPostRedisplay(view);
                } else if (auto mod_key = self.ModKey(event->key.key)) {
                    auto& mod = self.frame_state.modifier_keys[ToInt(*mod_key)];
                    --mod.is_down;
                    if (mod.is_down == 0) mod.releases = true;
                }
                break;
            }

            case PUGL_TEXT: {
                if (self.HandleInputChar(event->text.character)) puglPostRedisplay(view);
                break;
            }

            case PUGL_POINTER_IN:
            case PUGL_POINTER_OUT: {
                puglPostRedisplay(view);
                break;
            }

            case PUGL_BUTTON_PRESS:
            case PUGL_BUTTON_RELEASE: {
                if (auto const button = ConvertMouseButton(event->button.button)) {
                    GuiFrameInput::MouseButtonState::Event const e {
                        .point = {(f32)event->button.x, (f32)event->button.y},
                        .time = TimePoint::Now(),
                        .modifiers = ConvertModifierFlags(event->button.state),
                    };
                    if (self.HandleMouseClicked(*button, e, event->type == PUGL_BUTTON_PRESS))
                        puglPostRedisplay(view);
                }
                break;
            }

            case PUGL_MOTION: {
                if (self.HandleMouseMoved({(f32)event->motion.x, (f32)event->motion.y}))
                    puglPostRedisplay(view);
                break;
            }

            case PUGL_SCROLL: {
                if (event->scroll.direction == PUGL_SCROLL_UP ||
                    event->scroll.direction == PUGL_SCROLL_DOWN) {
                    if (self.HandleMouseWheel((f32)event->scroll.dy)) puglPostRedisplay(view);
                }
                break;
            }

            case PUGL_CLIENT:
            case PUGL_TIMER: {
                if (event->timer.id == k_timer_id) {
                    if (self.IsUpdateNeeded()) puglPostRedisplay(view);
                }
                break;
            }

            case PUGL_DATA_OFFER: {
                u32 const num_types = puglGetNumClipboardTypes(view);
                for (auto const t : Range(num_types)) {
                    char const* type = puglGetClipboardType(view, t);
                    if (NullTermStringsEqual(type, "text/plain")) puglAcceptOffer(view, &event->offer, t);
                }
                break;
            }

            case PUGL_DATA: {
                uint32_t const type_index = event->data.typeIndex;

                char const* type = puglGetClipboardType(view, type_index);

                if (NullTermStringsEqual(type, "text/plain")) {
                    size_t len = 0;
                    void const* data = puglGetClipboard(view, type_index, &len);

                    dyn::Assign(self.frame_state.clipboard_text, String {(char const*)data, len});
                }
                break;
            }

            case PUGL_LOOP_ENTER:
            case PUGL_LOOP_LEAVE: break;
        }
        return PUGL_SUCCESS;
    }

    bool HandleMouseWheel(f32 delta_lines) {
        frame_state.mouse_scroll_delta_in_lines += delta_lines;
        if (last_result.wants_mouse_scroll) return true;
        return false;
    }

    bool HandleMouseMoved(f32x2 new_cursor_pos) {
        bool result = false;
        frame_state.cursor_pos = new_cursor_pos;

        for (auto& btn : frame_state.mouse_buttons) {
            if (btn.is_down) {
                if (!btn.is_dragging) btn.dragging_started = true;
                btn.is_dragging = true;
            }
        }

        if (last_result.mouse_tracked_rects.size == 0 || last_result.wants_mouse_capture) {
            result = true;
        } else if (IsUpdateNeeded()) {
            return true;
        } else {
            for (auto const i : Range(last_result.mouse_tracked_rects.size)) {
                auto& item = last_result.mouse_tracked_rects[i];
                bool const mouse_over = item.rect.Contains(frame_state.cursor_pos);
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

    bool HandleMouseClicked(MouseButton button, GuiFrameInput::MouseButtonState::Event event, bool is_down) {
        auto& btn = frame_state.mouse_buttons[ToInt(button)];
        btn.is_down = is_down;
        if (is_down) {
            if ((event.time - btn.last_pressed_time) <= k_double_click_interval_seconds)
                btn.double_click = true;
            btn.last_pressed_point = event.point;
            btn.last_pressed_time = event.time;
        } else {
            if (btn.is_dragging) btn.dragging_ended = true;
            btn.is_dragging = false;
        }
        btn.presses.Append(event, frame_state.event_arena);

        bool result = false;
        if (last_result.mouse_tracked_rects.size == 0 || last_result.wants_mouse_capture ||
            (last_result.wants_all_left_clicks && button == MouseButton::Left) ||
            (last_result.wants_all_right_clicks && button == MouseButton::Right) ||
            (last_result.wants_all_middle_clicks && button == MouseButton::Middle)) {
            result = true;
        } else {
            for (auto const i : Range(last_result.mouse_tracked_rects.size)) {
                auto& item = last_result.mouse_tracked_rects[i];
                bool const mouse_over = item.rect.Contains(frame_state.cursor_pos);
                if (mouse_over) {
                    result = true;
                    break;
                }
            }
        }

        return result;
    }

    bool HandleKeyPressed(KeyCode key_code, ModifierFlags modifiers, bool is_down) {
        auto& key = frame_state.keys[ToInt(key_code)];
        if (is_down) {
            key.presses_or_repeats.Append({modifiers}, frame_state.event_arena);
            if (!key.is_down) key.presses.Append({modifiers}, frame_state.event_arena);
        } else {
            key.releases.Append({modifiers}, frame_state.event_arena);
        }
        key.is_down = is_down;

        if (last_result.wants_keyboard_input) return true;
        if (last_result.wants_just_arrow_keys &&
            (key_code == KeyCode::UpArrow || key_code == KeyCode::DownArrow ||
             key_code == KeyCode::LeftArrow || key_code == KeyCode::RightArrow)) {
            return true;
        }
        return false;
    }

    bool HandleInputChar(u32 utf32_codepoint) {
        dyn::Append(frame_state.input_utf32_chars, utf32_codepoint);
        if (last_result.wants_keyboard_input) return true;
        return false;
    }

    bool IsUpdateNeeded() {
        bool update_needed = false;

        if (last_result.status > GuiFrameResult::Status::Sleep) update_needed = true;

        if (last_result.timed_wakeups) {
            for (usize i = 0; i < last_result.timed_wakeups->size;) {
                auto& t = (*last_result.timed_wakeups)[i];
                if (TimePoint::Now() >= t) {
                    update_needed = true;
                    dyn::Remove(*last_result.timed_wakeups, i);
                } else {
                    ++i;
                }
            }
        }

        return update_needed;
    }

    void BeginFrame() {
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

    void EndFrame() {
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

    static constexpr uintptr_t k_timer_id = 200;

    static int g_world_counter;
    static PuglWorld* g_world;

    clap_host const& host;
    SettingsFile& settings;
    Logger& logger;
    bool realised = false;
    PuglWorld* world;
    PuglView* view;
    bool processing_events = false;
    CursorType current_cursor {CursorType::Default};
    graphics::DrawContext* graphics_ctx {};
    GuiFrameResult last_result;
    GuiFrameInput frame_state;
    Optional<Gui> gui;
};

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
    TRY(Required(puglSetEventFunc(platform.view, GuiPlatform::OnEvent)));

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
