// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/gui_imgui.hpp"

struct Gui;

enum class ModalWindowType {
    About,
    Licences,
    Metrics,
    LoadError,
    Settings,
    Count,
};

void OpenModalIfNotAlready(imgui::Context& imgui, ModalWindowType type);
void DoModalWindows(Gui* g);

struct NotificationDisplayInfo {
    enum class IconType : u8 { None, Info, Success, Error };
    String title {};
    String message {};
    bool dismissable = true;
    IconType icon = IconType::None;
};

constexpr usize k_notification_buffer_size = 400;

struct Notification {
    // This function is called every time the notification is displayed. It allows for changing the
    // notification text on-the-fly rather than caching a string once. The function object also has plenty
    // of space if data does need to be cached.
    TrivialFixedSizeFunction<k_notification_buffer_size,
                             NotificationDisplayInfo(ArenaAllocator& scratch_arena)>
        get_diplay_info;
    u64 id;
};

struct Notifications : BoundedList<Notification, 10> {
    Notification* Find(u64 id) {
        for (auto& n : *this)
            if (n.id == id) return &n;
        return nullptr;
    }
};
