// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_platform.hpp"

#if !OS_LINUX
int detail::FdFromPuglWorld(PuglWorld*) { return 0; }
void detail::X11SetParent(PuglView*, uintptr) {}
#endif
