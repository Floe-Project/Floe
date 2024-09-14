// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

#include "framework/gui_live_edit.hpp"
#include "gui_fwd.hpp"
#include "gui_knob_widgets.hpp"
#include "infos/param_info.hpp"

struct LayIDPair {
    LayID control;
    LayID label;
};

enum class LayoutType { Generic, Layer, Effect };

LayID LayoutParameterComponent(Gui* g,
                               LayID parent,
                               LayID& param_layid,
                               LayID& name,
                               LayoutType type,
                               Optional<ParamIndex> index_for_menu_items,
                               bool is_convo_ir,
                               Optional<UiSizeId> size_index_for_gapx = {},
                               bool set_gapx_independent_of_size = false,
                               bool set_bottom_gap_independent_of_size = false);

LayID LayoutParameterComponent(Gui* g,
                               LayID parent,
                               LayID& slider,
                               LayID& name,
                               Parameter const& param,
                               Optional<UiSizeId> size_index_for_gapx = {},
                               bool set_gapx_independent_of_size = false,
                               bool set_bottom_gap_independent_of_size = false);

LayID LayoutParameterComponent(Gui* g,
                               LayID parent,
                               LayIDPair& ids,
                               Parameter const& param,
                               Optional<UiSizeId> size_index_for_gapx = {},
                               bool set_gapx_independent_of_size = false,
                               bool set_bottom_gap_independent_of_size = false);

bool KnobAndLabel(Gui* g,
                  Parameter const& param,
                  Rect knob_r,
                  Rect label_r,
                  knobs::Style const& style,
                  bool greyed_out = false);

bool KnobAndLabel(Gui* g,
                  Parameter const& param,
                  LayIDPair ids,
                  knobs::Style const& style,
                  bool greyed_out = false);
