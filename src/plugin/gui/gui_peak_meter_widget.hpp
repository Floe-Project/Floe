// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui_fwd.hpp"
#include "layout.hpp"
#include "processing_utils/peak_meter.hpp"

namespace peak_meters {

void PeakMeter(Gui* g, Rect r, StereoPeakMeter const& level, bool flash_when_clipping);
void PeakMeter(Gui* g, LayID lay_id, StereoPeakMeter const& level, bool flash_when_clipping);

} // namespace peak_meters
