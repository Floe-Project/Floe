// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_peak_meter_widget.hpp"

#include "foundation/foundation.hpp"

#include "gui_framework/gui_live_edit.hpp"
#include "gui.hpp"
#include "processing_utils/audio_utils.hpp"

namespace peak_meters {

static void DrawPeakMeters(imgui::Context const& imgui, Rect r, f32 vl, f32 vr, bool did_clip) {
    auto const gap = LiveSize(imgui, UiSizeId::PeakMeterGap);
    auto const marker_w = LiveSize(imgui, UiSizeId::PeakMeterMarkerWidth);
    auto const marker_pad = LiveSize(imgui, UiSizeId::PeakMeterMarkerPad);
    auto padded_r = Rect {r.x + marker_w, r.y, r.w - (marker_w * 2), r.h};
    auto w = (padded_r.w / 2) - (gap / 2);

    constexpr f32 k_max_db = 10;
    constexpr f32 k_min_db = -70;

    auto const rounding = LiveSize(imgui, UiSizeId::CornerRounding);

    {
        {
            auto l_channel = padded_r;
            l_channel.w = w;
            imgui.graphics->AddRectFilled(l_channel, LiveCol(imgui, UiColMap::PeakMeterBack), rounding);
        }
        {
            auto r_channel = padded_r;
            r_channel.x += w + gap;
            r_channel.w = w;
            imgui.graphics->AddRectFilled(r_channel, LiveCol(imgui, UiColMap::PeakMeterBack), rounding);
        }

        auto draw_marker = [&](f32 db, bool bold) {
            f32 const pos = MapTo01(db, k_min_db, k_max_db);
            auto const line_y = padded_r.y + ((1 - pos) * padded_r.h);
            imgui.graphics->AddLine({r.x, line_y},
                                    {r.x + (marker_w - marker_pad), line_y},
                                    bold ? LiveCol(imgui, UiColMap::PeakMeterMarkersBold)
                                         : LiveCol(imgui, UiColMap::PeakMeterMarkers));
            imgui.graphics->AddLine({r.Right() - (marker_w - marker_pad), line_y},
                                    {r.Right(), line_y},
                                    bold ? LiveCol(imgui, UiColMap::PeakMeterMarkersBold)
                                         : LiveCol(imgui, UiColMap::PeakMeterMarkers));
        };

        draw_marker(0, true);
        draw_marker(-12, false);
        draw_marker(-24, false);
        draw_marker(-36, false);
        draw_marker(-48, false);
    }

    if (vl > 0.001f && vr > 0.001f) {
        auto const top_segment_line = padded_r.y + ((1 - MapTo01(0, k_min_db, k_max_db)) * padded_r.h);
        auto const mid_segment_line = padded_r.y + ((1 - MapTo01(-12, k_min_db, k_max_db)) * padded_r.h);

        f32 const skewed_vl = MapTo01(Clamp(AmpToDb(vl), k_min_db, k_max_db), k_min_db, k_max_db);
        f32 const skewed_vr = MapTo01(Clamp(AmpToDb(vr), k_min_db, k_max_db), k_min_db, k_max_db);

        auto l_r = padded_r;
        l_r.y = padded_r.y + (1 - Min(1.0f, skewed_vl)) * padded_r.h;
        l_r.w = w;
        l_r.SetBottomByResizing(padded_r.Bottom());

        auto r_r = padded_r;
        r_r.x += w + gap;
        r_r.y = padded_r.y + (1 - Min(1.0f, skewed_vr)) * padded_r.h;
        r_r.w = w;
        r_r.SetBottomByResizing(padded_r.Bottom());

        Array<Rect, 2> const channel_rs = {l_r, r_r};

        for (auto& chan_r : channel_rs) {
            if (chan_r.y < top_segment_line) {
                auto col = LiveCol(imgui, UiColMap::PeakMeterHighlightTop);
                if (did_clip) col = LiveCol(imgui, UiColMap::PeakMeterClipping);
                imgui.graphics->AddRectFilled(chan_r.Min(), chan_r.Max(), col);
            }

            if (chan_r.y < mid_segment_line) {
                auto col = LiveCol(imgui, UiColMap::PeakMeterHighlightMiddle);
                if (did_clip) col = LiveCol(imgui, UiColMap::PeakMeterClipping);
                auto const top = Max(chan_r.y, top_segment_line);
                imgui.graphics->AddRectFilled(f32x2 {chan_r.x, top}, chan_r.Max(), col);
            }

            auto col = LiveCol(imgui, UiColMap::PeakMeterHighlightBottom);
            if (did_clip) col = LiveCol(imgui, UiColMap::PeakMeterClipping);
            auto const top = Max(chan_r.y, mid_segment_line);
            imgui.graphics->AddRectFilled(f32x2 {chan_r.x, top}, chan_r.Max(), col, rounding, 4 | 8);
        }
    }
}

void PeakMeter(Gui* g, Rect r, StereoPeakMeter const& level, bool flash_when_clipping) {
    auto const snapshot = level.GetSnapshot();
    DrawPeakMeters(g->imgui,
                   g->imgui.GetRegisteredAndConvertedRect(r),
                   snapshot.levels[0],
                   snapshot.levels[1],
                   flash_when_clipping && level.DidClipRecently());
}
void PeakMeter(Gui* g, LayID lay_id, StereoPeakMeter const& level, bool flash_when_clipping) {
    PeakMeter(g, g->layout.GetRect(lay_id), level, flash_when_clipping);
}

} // namespace peak_meters
