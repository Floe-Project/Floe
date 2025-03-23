// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/gui_imgui.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct LibraryImages {
    sample_lib::LibraryId library_id {};
    Optional<graphics::ImageID> icon {};
    Optional<graphics::ImageID> background {};
    Optional<graphics::ImageID> blurred_background {};
    bool icon_missing {};
    bool background_missing {};
    bool reload {};
};

using LibraryImagesArray = DynamicArray<LibraryImages>;

Optional<LibraryImages> LibraryImagesFromLibraryId(LibraryImagesArray& array,
                                                   imgui::Context& imgui,
                                                   sample_lib::LibraryIdRef const& library_id,
                                                   sample_lib_server::Server& server,
                                                   ArenaAllocator& scratch_arena);
