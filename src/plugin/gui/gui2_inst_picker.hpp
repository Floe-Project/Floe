// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/engine.hpp"
#include "gui/gui2_common_modal_panel.hpp"
#include "gui/gui2_inst_picker_state.hpp"
#include "gui/gui_library_images.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "processor/layer_processor.hpp"

// Ephemeral
struct InstPickerContext {
    void Init(ArenaAllocator& arena) {
        libraries = sample_lib_server::AllLibrariesRetained(sample_library_server, arena);
        Sort(libraries, [](auto const& a, auto const& b) { return a->name < b->name; });

        for (auto const& l : libraries) {
            if (l->file_format_specifics.tag == sample_lib::FileFormat::Mdata) {
                has_mirage_libraries = true;
                break;
            }
        }
    }
    void Deinit() { sample_lib_server::ReleaseAll(libraries); }

    LayerProcessor& layer;
    sample_lib_server::Server& sample_library_server;
    LibraryImagesArray& library_images;
    Engine& engine;

    Span<sample_lib_server::RefCounted<sample_lib::Library>> libraries;
    sample_lib::Instrument const* hovering_inst {};
    sample_lib::Library const* hovering_lib {};
    Optional<WaveformType> waveform_type_hovering {};
    bool has_mirage_libraries {};
    Optional<Set<String>> all_tags {};
};

struct InstrumentCursor {
    bool operator==(InstrumentCursor const& o) const = default;
    usize lib_index;
    usize inst_index;
};

enum class IterateInstrumentDirection { Forward, Backward };

void LoadAdjacentInstrument(InstPickerContext const& context,
                            InstPickerState& state,
                            IterateInstrumentDirection direction,
                            bool picker_gui_is_open);

void LoadRandomInstrument(InstPickerContext const& context, InstPickerState& state, bool picker_gui_is_open);

void DoInstPickerPopup(GuiBoxSystem& box_system,
                       imgui::Id popup_id,
                       Rect absolute_button_rect,
                       InstPickerContext& context,
                       InstPickerState& state);
