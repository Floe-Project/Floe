// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <pugl/pugl.h>

#ifdef __linux__
#include <X11/Xlib.h>
#endif

int FdFromPuglWorld(PuglWorld* world) {
#ifdef __linux__
    auto display = (Display*)puglGetNativeWorld(world);
    return ConnectionNumber(display);
#else
    (void)world;
    return 0;
#endif
}
