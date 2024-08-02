// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef __linux__
#include <X11/Xlib.h>
#include <pugl/pugl.h>

int FdFromPuglWorld(PuglWorld* world) {
    auto display = (Display*)puglGetNativeWorld(world);
    return ConnectionNumber(display);
}
#endif
