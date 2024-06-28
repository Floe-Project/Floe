// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foundation/foundation.hpp"
#include "utils/debug/debug.hpp"

#include "clap/ext/timer-support.h"
#include "gui_platform.hpp"
#include "plugin.hpp"
#include "pugl/gl.h" // on windows this includes windows.h
#include "pugl/pugl.h"
#include "settings/settings_gui.hpp"
#include "third_party_libs/pugl/test/test_utils.h"

//
#include "os/undef_windows_macros.h"

static int g_counter = 0;
PuglWorld* g_world {};

constexpr uintptr_t k_timer_id = 200;

// TODO(1.0): go over the API docs and review usage
// TODO(1.0): add error handling
// TODO(1.0): integrate this with the clap interface, there's no need having an abstraction layer here

struct PuglPlatform : public GuiPlatform {
    PuglPlatform(GUI_PLATFORM_ARGS) : GuiPlatform(GUI_PLATFORM_FORWARD_ARGS) {}

    void* OpenWindow() override {
        g_counter++;
        if (auto const floe_host =
                (FloeClapExtensionHost const*)host.get_extension(&host, k_floe_clap_extension_id);
            floe_host != nullptr) {
            world = (PuglWorld*)floe_host->pugl_world;
            ASSERT(world != nullptr);
        } else if (g_counter == 1) {
            ASSERT(g_world == nullptr);
            g_world = puglNewWorld(PUGL_MODULE, 0);
            puglSetWorldString(g_world, PUGL_CLASS_NAME, "Floe");
            world = g_world;
        } else {
            ASSERT(g_world != nullptr);
            world = g_world;
        }

        view = puglNewView(world);
        puglSetBackend(view, puglGlBackend());
        puglSetViewHint(view, PUGL_CONTEXT_VERSION_MAJOR, 3);
        puglSetViewHint(view, PUGL_CONTEXT_VERSION_MINOR, 3);
        puglSetViewHint(view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_COMPATIBILITY_PROFILE);
        puglSetHandle(view, this);
        puglSetEventFunc(view, OnEvent);
        puglSetViewHint(view, PUGL_RESIZABLE, true);

        auto const ratio = gui_settings::CurrentAspectRatio(settings.settings.gui);
        auto const min_size = gui_settings::CreateFromWidth(500, ratio);
        auto const max_size = gui_settings::CreateFromWidth(2000, ratio);
        puglSetSizeHint(view, PUGL_DEFAULT_SIZE, window_size.width, window_size.height);
        puglSetSizeHint(view, PUGL_MIN_SIZE, min_size.width, min_size.height);
        puglSetSizeHint(view, PUGL_MAX_SIZE, max_size.width, max_size.height);
        puglSetSizeHint(view,
                        PUGL_FIXED_ASPECT,
                        ratio.width,
                        ratio.height); // TODO: what if the plugin changes shape?
        puglSetSize(view, window_size.width, window_size.height);

        puglSetViewHint(view, PUGL_CONTEXT_DEBUG, RUNTIME_SAFETY_CHECKS_ON);

        // TODO: do we need to register clap_host_posix_fs_support?

        // constexpr auto interval_ms = (u32)(1000.0 / (f64)k_gui_platform_timer_hz);
        // const auto host_timer =
        //     (const clap_host_timer_support *)host.get_extension(&host, CLAP_EXT_TIMER_SUPPORT);
        // if (host_timer) {
        //     if (!host_timer->register_timer(&host, interval_ms, &timer_id)) Panic("Failed to register
        //     timer");
        // } else {
        //     Panic("Host does not support timer extension");
        // }

        is_window_open = true;

        return view;
    }
    bool CloseWindow() override {
        if (realised) {
            puglStopTimer(view, k_timer_id);
            puglUnrealize(view);
            realised = false;
        }
        puglFreeView(view);

        is_window_open = false;
        if (--g_counter == 0) {
            if (g_world) {
                puglFreeWorld(g_world);
                g_world = nullptr;
            }
            world = nullptr;
        }
        return true;
    }
    void* GetWindow() override { return view; }
    void PollAndUpdate() override { puglUpdate(world, 0); }
    void SetParent(clap_window_t const* window) override {
        auto const status = puglSetParentWindow(view, (uintptr_t)window->ptr);
        puglSetPosition(view, 0, 0);
        if (status != PUGL_SUCCESS) {
            auto const status_error = puglStrerror(status);
            DebugLn("puglSetParentWindow failed: {}", FromNullTerminated(status_error));
            TODO("handle error");
        }
    }
    bool SetTransient(clap_window_t const* window) override {
        puglSetTransientParent(view, (uintptr_t)window->ptr);
        return true;
    }
    void SetVisible(bool visible) override {
        if (visible) {
            if (!realised) {
                if (auto const status = puglRealize(view); status != PUGL_SUCCESS) {
                    auto const status_error = puglStrerror(status);
                    DebugLn("puglRealize failed: {}", FromNullTerminated(status_error));
                    TODO("handle error");
                }
                if (auto const status = puglStartTimer(view, k_timer_id, 1.0 / (f64)k_gui_platform_timer_hz);
                    status != PUGL_SUCCESS) {
                    TODO("handle error");
                }
                realised = true;
            }
            puglShow(view, PUGL_SHOW_PASSIVE);
        } else
            puglHide(view);
    }
    bool SetSize(UiSize new_size) override {
        DebugLn("SetSize: {}x{}", new_size.width, new_size.height);
        puglSetSize(view, new_size.width, new_size.height);
        return true;
    }
    bool SetClipboard(String mime_type, String data) override {
        ArenaAllocatorWithInlineStorage<250> allocator;
        return puglSetClipboard(view, NullTerminated(mime_type, allocator), data.data, data.size) ==
               PUGL_SUCCESS;
    }
    bool RequestClipboardPaste() override { return puglPaste(view) == PUGL_SUCCESS; }

    static Optional<KeyCodes> ConvertKeyCode(u32 key) {
        switch (key) {
            case PUGL_KEY_TAB: return KeyCodeTab;
            case PUGL_KEY_LEFT: return KeyCodeLeftArrow;
            case PUGL_KEY_RIGHT: return KeyCodeRightArrow;
            case PUGL_KEY_UP: return KeyCodeUpArrow;
            case PUGL_KEY_DOWN: return KeyCodeDownArrow;
            case PUGL_KEY_PAGE_UP: return KeyCodePageUp;
            case PUGL_KEY_PAGE_DOWN: return KeyCodePageDown;
            case PUGL_KEY_HOME: return KeyCodeHome;
            case PUGL_KEY_END: return KeyCodeEnd;
            case PUGL_KEY_DELETE: return KeyCodeDelete;
            case PUGL_KEY_BACKSPACE: return KeyCodeBackspace;
            case PUGL_KEY_ENTER: return KeyCodeEnter;
            case PUGL_KEY_ESCAPE: return KeyCodeEscape;
            case PUGL_KEY_F1: return KeyCodeF1;
            case PUGL_KEY_F2: return KeyCodeF2;
            case PUGL_KEY_F3: return KeyCodeF3;
            case 'a': return KeyCodeA;
            case 'c': return KeyCodeC;
            case 'v': return KeyCodeV;
            case 'x': return KeyCodeX;
            case 'y': return KeyCodeY;
            case 'z': return KeyCodeZ;
        }
        return nullopt;
    }

    static PuglStatus OnEvent(PuglView* view, PuglEvent const* event) {
        // if (event->type != PUGL_UPDATE && event->type != PUGL_TIMER) printEvent(event, "PUGL: ", true);
        auto& platform = *(PuglPlatform*)puglGetHandle(view);
        switch (event->type) {
            case PUGL_NOTHING: break;
            case PUGL_REALIZE: {
                if (auto const o = platform.InitGraphics((void*)puglGetNativeView(view)); o.HasError())
                    TODO("handle error");
                break;
            }
            case PUGL_UNREALIZE: {
                platform.DestroyGraphics();
                break;
            }

            case PUGL_CONFIGURE: {
                auto const current_size = platform.window_size;
                if (current_size.width != event->configure.width ||
                    current_size.height != event->configure.height)
                    platform.WindowWasResized({event->configure.width, event->configure.height});
                break;
            }

            case PUGL_UPDATE: {
                break;
            }

            case PUGL_EXPOSE: {
                platform.Update();
                break;
            }

            case PUGL_CLOSE: {
                auto const host_gui =
                    (clap_host_gui const*)platform.host.get_extension(&platform.host, CLAP_EXT_GUI);
                if (host_gui) host_gui->closed(&platform.host, false);
                break;
            }
            case PUGL_FOCUS_IN:
            case PUGL_FOCUS_OUT:
            case PUGL_KEY_PRESS: {
                if (auto const key = ConvertKeyCode(event->key.key)) {
                    if (platform.HandleKeyPressed(*key, true)) puglPostRedisplay(view);
                }
                break;
            }
            case PUGL_KEY_RELEASE: {
                if (auto const key = ConvertKeyCode(event->key.key)) {
                    if (platform.HandleKeyPressed(*key, true)) puglPostRedisplay(view);
                }
                break;
            }
            case PUGL_TEXT: {
                if (platform.HandleInputChar(CheckedCast<int>(event->text.character)))
                    puglPostRedisplay(view);
                break;
            }
            case PUGL_POINTER_IN:
            case PUGL_POINTER_OUT: {
                puglPostRedisplay(view);
                break;
            }
            case PUGL_BUTTON_PRESS: {
                if (event->button.button >= 0 && event->button.button < 3) {
                    if (platform.HandleMouseClicked((int)event->button.button, true)) puglPostRedisplay(view);
                }
                break;
            }
            case PUGL_BUTTON_RELEASE: {
                if (event->button.button >= 0 && event->button.button < 3) {
                    if (platform.HandleMouseClicked((int)event->button.button, false))
                        puglPostRedisplay(view);
                }
                break;
            }

            case PUGL_MOTION: {
                if (platform.HandleMouseMoved((f32)event->motion.x, (f32)event->motion.y))
                    puglPostRedisplay(view);
                break;
            }
            case PUGL_SCROLL: {
                if (event->scroll.direction == PUGL_SCROLL_UP ||
                    event->scroll.direction == PUGL_SCROLL_DOWN) {
                    if (platform.HandleMouseWheel((f32)event->scroll.dy)) puglPostRedisplay(view);
                }
                break;
            }
            case PUGL_CLIENT:
            case PUGL_TIMER: {
                if (event->timer.id == k_timer_id) {
                    if (platform.CheckForTimerRedraw()) puglPostRedisplay(view);
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

                    dyn::Assign(platform.clipboard_data, String {(char const*)data, len});
                }
                break;
            }
            case PUGL_LOOP_ENTER:
            case PUGL_LOOP_LEAVE: break;
        }
        return PUGL_SUCCESS;
    }

    bool realised = false;
    PuglWorld* world;
    PuglView* view;
};

GuiPlatform* CreateGuiPlatform(GUI_PLATFORM_ARGS) {
    auto platform = new PuglPlatform(GUI_PLATFORM_FORWARD_ARGS);
    return platform;
}

void DestroyGuiPlatform(GuiPlatform* platform) {
    if (platform) {
        auto p = (PuglPlatform*)platform;
        delete p;
    } else {
        PanicIfReached();
    }
}
