// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <pugl/pugl.h>

#include "foundation/foundation.hpp"

#ifdef __linux__
#include <X11/Xlib.h>
#endif

namespace detail {

int FdFromPuglWorld(PuglWorld* world) {
#ifdef __linux__
    auto display = (Display*)puglGetNativeWorld(world);
    return ConnectionNumber(display);
#else
    (void)world;
    return 0;
#endif
}

// The CLAP API says that we need to use XEMBED protocol. Pugl doesn't do that so we need to do it ourselves.
void X11SetParent(PuglView* view, uintptr parent) {
#ifdef __linux__
    auto display = (Display*)puglGetNativeWorld(puglGetWorld(view));
    auto window = (Window)puglGetNativeView(view);
    ASSERT(window);

    XReparentWindow(display, window, (Window)parent, 0, 0);
    XFlush(display);

    Atom embed_info_atom = XInternAtom(display, "_XEMBED_INFO", 0);
    constexpr u32 k_xembed_protocol_version = 0;
    constexpr u32 k_xembed_flags = 0;
    u32 embed_info_data[2] = {k_xembed_protocol_version, k_xembed_flags};
    XChangeProperty(display,
                    window,
                    embed_info_atom,
                    embed_info_atom,
                    sizeof(embed_info_data[0]) * 8,
                    PropModeReplace,
                    (u8*)embed_info_data,
                    ArraySize(embed_info_data));
#else
    (void)view;
#endif
}

} // namespace detail
