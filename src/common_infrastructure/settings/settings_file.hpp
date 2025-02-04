// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "foundation/foundation.hpp"

// Code related to reading the default settings file.

namespace ini {

PUBLIC Optional<String> ValueIfKeyMatches(String line, String key) {
    if (StartsWithSpan(line, key)) {
        auto l = line;
        l.RemovePrefix(key.size);
        l = WhitespaceStrippedStart(l);
        if (l.size && l[0] == '=') {
            l.RemovePrefix(1);
            l = WhitespaceStripped(l);
            if (l.size) return l;
        }
    }
    return k_nullopt;
}

PUBLIC bool SetIfMatching(String line, String key, bool& value) {
    if (auto value_string = ValueIfKeyMatches(line, key)) {
        value = IsEqualToCaseInsensitiveAscii(value_string.Value(), "true"_s);
        return true;
    }
    return false;
}

PUBLIC bool SetIfMatching(String line, String key, String& value) {
    if (auto value_string = ValueIfKeyMatches(line, key)) {
        value = *value_string;
        return true;
    }
    return false;
}

template <Integral Type>
requires(!Same<Type, bool>)
PUBLIC bool SetIfMatching(String line, String key, Type& value) {
    if (auto value_string = ValueIfKeyMatches(line, key)) {
        if (auto o = ParseInt(*value_string, ParseIntBase::Decimal); o.HasValue()) value = (Type)o.Value();
        return true;
    }
    return false;
}

enum class KeyType : u32 {
    CcToParamIdMap,
    ExtraLibrariesFolder,
    ExtraPresetsFolder,
    LibrariesInstallLocation,
    PresetsInstallLocation,
    GuiKeyboardOctave,
    HighContrastGui,
    PresetsRandomMode,
    ShowKeyboard,
    ShowTooltips,
    WindowWidth,
    OnlineReportingDisabled,
    Count,
};

constexpr String Key(KeyType k) {
    switch (k) {
        case KeyType::CcToParamIdMap: return "cc_to_param_id_map"_s;
        case KeyType::ExtraLibrariesFolder: return "extra_libraries_folder"_s;
        case KeyType::ExtraPresetsFolder: return "extra_presets_folder"_s;
        case KeyType::LibrariesInstallLocation: return "libraries_install_location"_s;
        case KeyType::PresetsInstallLocation: return "presets_install_location"_s;
        case KeyType::GuiKeyboardOctave: return "gui_keyboard_octave"_s;
        case KeyType::HighContrastGui: return "high_contrast_gui"_s;
        case KeyType::PresetsRandomMode: return "presets_random_mode"_s;
        case KeyType::ShowKeyboard: return "show_keyboard"_s;
        case KeyType::ShowTooltips: return "show_tooltips"_s;
        case KeyType::WindowWidth: return "window_width"_s;
        case KeyType::OnlineReportingDisabled: return "online_reporting_disabled"_s;
        case KeyType::Count: PanicIfReached();
    }
}

} // namespace ini

// A one-time read of the settings file to get the data.
bool IsOnlineReportingDisabled();
