// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui_fwd.hpp"
#include "param_info.hpp"
#include "plugin_instance.hpp"
#include "processing/smoothed_value.hpp"

enum class GuiEnvelopeType { Volume, Filter, Count };

struct GuiEnvelopeCursor {
    SmoothedValueFilter smoother {};
    u64 marker_id {UINT64_MAX};
};

void GUIDoEnvelope(Gui* g,
                   PluginInstance::Layer* layer,
                   Rect r,
                   bool greyed_out,
                   Array<LayerParamIndex, 4> adsr_layer_params,
                   GuiEnvelopeType type);
