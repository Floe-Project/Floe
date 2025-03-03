// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

enum class FinalBinaryType {
    Clap,
    Standalone,
    Vst3,
    Packager,
    WindowsInstaller,
    WindowsUninstaller,
    AuV2,
    Tests,
    DocsPreprocessor,
};

constexpr String ToString(FinalBinaryType type) {
    switch (type) {
        case FinalBinaryType::Clap: return "clap"_s;
        case FinalBinaryType::Standalone: return "standalone"_s;
        case FinalBinaryType::Vst3: return "vst3"_s;
        case FinalBinaryType::Packager: return "packager"_s;
        case FinalBinaryType::WindowsInstaller: return "windows_installer"_s;
        case FinalBinaryType::WindowsUninstaller: return "windows_uninstaller"_s;
        case FinalBinaryType::AuV2: return "au_v2"_s;
        case FinalBinaryType::Tests: return "tests"_s;
        case FinalBinaryType::DocsPreprocessor: return "docs_preprocessor"_s;
    }
    PanicIfReached();
}

extern FinalBinaryType const g_final_binary_type;
