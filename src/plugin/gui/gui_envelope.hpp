// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui_fwd.hpp"
#include "param_info.hpp"
#include "processing_engine/layer_processor.hpp"
#include "processing_utils/smoothed_value.hpp"

enum class GuiEnvelopeType { Volume, Filter, Count };

struct GuiEnvelopeCursor {
    SmoothedValueFilter smoother {};
    u64 marker_id {(u64)-1};
};

void GUIDoEnvelope(Gui* g,
                   LayerProcessor* layer,
                   Rect r,
                   bool greyed_out,
                   Array<LayerParamIndex, 4> adsr_layer_params,
                   GuiEnvelopeType type);
