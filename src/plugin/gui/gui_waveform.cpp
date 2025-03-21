// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_waveform.hpp"

#include <IconsFontAwesome5.h>

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/loop_modes.hpp"
#include "gui.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_label_widgets.hpp"
#include "gui_widget_helpers.hpp"
#include "processor/layer_processor.hpp"
#include "processor/sample_processing.hpp"

static void GUIDoSampleWaveformOverlay(Gui* g, LayerProcessor* layer, Rect r, Rect waveform_r) {
    if (layer->inst.tag == InstrumentType::WaveformSynth) return;

    auto& engine = g->engine;
    auto& imgui = g->imgui;

    auto const handle_height = LiveSize(imgui, UiSizeId::Main_WaveformHandleHeight);
    auto const handle_width = LiveSize(imgui, UiSizeId::Main_WaveformHandleWidth);
    auto const epsilon = 0.001f;
    auto const slider_sensitivity = 400.0f;

    using namespace loop_and_reverse_flags;

    auto const reverse = layer->params[ToInt(LayerParamIndex::Reverse)].ValueAsBool();
    auto const desired_loop_mode =
        layer->params[ToInt(LayerParamIndex::LoopMode)].ValueAsInt<param_values::LoopMode>();
    auto const mode = ActualLoopBehaviour(layer->instrument, desired_loop_mode);

    auto const extra_grabbing_room_x = handle_width;
    auto const extra_grabbing_room_towards_centre = r.h / 3;
    auto const extra_grabbing_room_away_from_centre = r.h / 6;

    enum class HandleType { LoopStart, LoopEnd, Offset, Xfade };
    enum class HandleDirection { Left, Right };

    // Loop points and xfade
    Rect start_line;
    Rect start_handle;
    Rect end_line;
    Rect end_handle;
    Rect xfade_line;
    Rect xfade_handle;
    Rect loop_region_r;

    Rect const& left_line = reverse ? end_line : start_line;
    Rect const& right_line = reverse ? start_line : end_line;

    bool draw_xfade = false;
    bool draw_xfade_as_inactive = false;
    f32 loop_xfade_size {};

    auto const start_id = imgui.GetID("loop start");
    auto const end_id = imgui.GetID("loop end");
    auto const xfade_id = imgui.GetID("loop xfade");
    auto const loop_region_id = imgui.GetID("region");

    auto draw_handle = [&](Rect r, imgui::Id id, HandleType type, bool inactive) {
        u32 back_col;
        u32 back_hover_col;
        u32 text_col;
        back_col = LiveCol(imgui, UiColMap::Waveform_LoopHandle);
        back_hover_col = LiveCol(imgui, UiColMap::Waveform_LoopHandleHover);
        text_col = LiveCol(imgui, UiColMap::Waveform_LoopHandleText);

        String text {};
        HandleDirection handle_direction {HandleDirection::Left};
        switch (type) {
            case HandleType::LoopEnd: {
                handle_direction = reverse ? HandleDirection::Left : HandleDirection::Right;
                text = reverse ? ICON_FA_REDO_ALT : ICON_FA_UNDO_ALT;
                break;
            }
            case HandleType::LoopStart: {
                text = reverse ? ICON_FA_UNDO_ALT : ICON_FA_REDO_ALT;
                handle_direction = reverse ? HandleDirection::Right : HandleDirection::Left;
                break;
            }
            case HandleType::Offset: {
                text = ICON_FA_CARET_RIGHT;
                handle_direction = HandleDirection::Left;
                back_col = LiveCol(imgui, UiColMap::Waveform_OffsetHandle);
                back_hover_col = LiveCol(imgui, UiColMap::Waveform_OffsetHandleHover);
                text_col = LiveCol(imgui, UiColMap::Waveform_OffsetHandleText);
                break;
            }
            case HandleType::Xfade: {
                text = ICON_FA_BURN;
                handle_direction = mode.value.mode == sample_lib::Loop::Mode::Standard
                                       ? (reverse ? HandleDirection::Left : HandleDirection::Right)
                                       : HandleDirection::Right;
                back_col = inactive ? LiveCol(imgui, UiColMap::Waveform_XfadeHandleInactive)
                                    : LiveCol(imgui, UiColMap::Waveform_XfadeHandle);
                back_hover_col = LiveCol(imgui, UiColMap::Waveform_XfadeHandleHover);
                text_col = LiveCol(imgui, UiColMap::Waveform_XfadeHandleText);
                break;
            }
        }

        u32 rounding_corners = 0;
        switch (handle_direction) {
            case HandleDirection::Left: {
                rounding_corners = 1 | 8;
                break;
            }
            case HandleDirection::Right: {
                rounding_corners = 2 | 4;
                break;
            }
        }

        imgui.graphics->AddRectFilled(r.Min(),
                                      r.Max(),
                                      imgui.IsHotOrActive(id) ? back_hover_col : back_col,
                                      6,
                                      (int)rounding_corners);
        if (g->icons) g->frame_input.graphics_ctx->PushFont(g->icons);
        imgui.graphics->AddTextJustified(r,
                                         text,
                                         text_col,
                                         TextJustification::Centred,
                                         TextOverflowType::AllowOverflow,
                                         0.5f);
        if (g->icons) g->frame_input.graphics_ctx->PopFont();
    };

    auto do_handle_slider = [&](imgui::Id id,
                                Span<ParamIndex const> params,
                                Optional<ParamIndex> tooltip_param,
                                Rect grabber_r,
                                f32 value,
                                f32 default_val,
                                bool invert_slider,
                                FunctionRef<void(f32)> callback) {
        auto const grabber_unregistered = grabber_r;
        if (tooltip_param) MidiLearnMenu(g, *tooltip_param, grabber_unregistered);
        imgui.RegisterAndConvertRect(&grabber_r);

        bool const changed =
            imgui.SliderRangeBehavior(grabber_r,
                                      id,
                                      invert_slider ? 1.0f : 0.0f,
                                      invert_slider ? 0.0f : 1.0f,
                                      value,
                                      default_val,
                                      slider_sensitivity,
                                      {.slower_with_shift = true, .default_on_modifer = true});

        if (imgui.IsHotOrActive(id)) {
            imgui.frame_output.cursor_type = CursorType::HorizontalArrows;
            if (imgui.frame_input.Mouse(MouseButton::Left).double_click)
                g->param_text_editor_to_open = params[0];
        }

        if (imgui.WasJustActivated(id))
            for (auto p : params)
                ParameterJustStartedMoving(engine.processor, p);
        if (changed) callback(value);
        if (imgui.WasJustDeactivated(id))
            for (auto p : params)
                ParameterJustStoppedMoving(engine.processor, p);

        if (tooltip_param) {
            auto& param_obj = engine.processor.params[(usize)*tooltip_param];
            ParameterValuePopup(g, param_obj, id, grabber_unregistered);
            DoParameterTooltipIfNeeded(g, param_obj, id, grabber_unregistered);
        }
    };

    if (mode.value.editable) {
        auto const loop_start = layer->params[ToInt(LayerParamIndex::LoopStart)].LinearValue();
        auto const loop_end = Max(layer->params[ToInt(LayerParamIndex::LoopEnd)].LinearValue(), loop_start);
        auto const raw_crossfade_size = layer->params[ToInt(LayerParamIndex::LoopCrossfade)].LinearValue();
        loop_xfade_size =
            ClampCrossfadeSize<f32>(raw_crossfade_size, loop_start, loop_end, 1.0f, *mode.value.mode) * r.w;
        auto loop_start_pos = loop_start * r.w;
        auto loop_end_pos = loop_end * r.w;
        auto loop_xfade_line_pos = loop_end_pos - loop_xfade_size;
        if (mode.value.mode == sample_lib::Loop::Mode::Standard) {
            if (reverse) {
                loop_start_pos = r.w - loop_start_pos;
                loop_end_pos = r.w - loop_end_pos;
                loop_xfade_line_pos = loop_end_pos + loop_xfade_size;
            }
        } else if (!reverse) {
            loop_xfade_line_pos = loop_end_pos + loop_xfade_size;
        } else {
            loop_start_pos = r.w - loop_start_pos;
            loop_end_pos = r.w - loop_end_pos;
            loop_xfade_line_pos = loop_start_pos + loop_xfade_size;
        }

        auto const xfade_active = loop_start != 0 && (loop_end - loop_start) != 0;
        draw_xfade_as_inactive = !xfade_active;

        auto const xfade_param_id =
            ParamIndexFromLayerParamIndex(layer->index, LayerParamIndex::LoopCrossfade);
        auto const start_param_id = ParamIndexFromLayerParamIndex(layer->index, LayerParamIndex::LoopStart);
        auto const end_param_id = ParamIndexFromLayerParamIndex(layer->index, LayerParamIndex::LoopEnd);

        auto set_xfade_size_if_needed = [&]() {
            auto xfade = engine.processor.params[ToInt(xfade_param_id)].LinearValue();
            auto clamped_xfade = ClampCrossfadeSize(
                xfade,
                loop_start,
                Max(layer->params[ToInt(LayerParamIndex::LoopEnd)].LinearValue(), loop_start),
                1.0f,
                *mode.value.mode);
            if (xfade > clamped_xfade) {
                SetParameterValue(engine.processor,
                                  xfade_param_id,
                                  clamped_xfade,
                                  {.host_should_not_record = true});
            }
        };

        // start
        {
            auto const& param = engine.processor.params[ToInt(start_param_id)];

            start_line = waveform_r.WithXW(waveform_r.x + loop_start_pos, 1);
            start_handle = {.xywh {start_line.Right() - handle_width, r.y, handle_width, handle_height}};
            if (reverse) start_handle.x += handle_width - start_line.w;

            auto grabber = start_handle;
            grabber.y -= extra_grabbing_room_away_from_centre;
            grabber.h += extra_grabbing_room_away_from_centre + extra_grabbing_room_towards_centre;
            grabber.w += extra_grabbing_room_x;
            if (!reverse) grabber.x -= extra_grabbing_room_x;

            ParamIndex const params[] = {start_param_id, xfade_param_id};
            do_handle_slider(start_id,
                             params,
                             start_param_id,
                             grabber,
                             param.LinearValue(),
                             param.DefaultLinearValue(),
                             reverse,
                             [&](f32 val) {
                                 val = Max(0.0f, Min(loop_end - epsilon, val));
                                 SetParameterValue(engine.processor, start_param_id, val, {});
                                 set_xfade_size_if_needed();
                             });

            imgui.RegisterAndConvertRect(&start_line);
            imgui.RegisterAndConvertRect(&start_handle);
        };

        // end
        {
            auto const& param = engine.processor.params[ToInt(end_param_id)];

            end_line = waveform_r.WithXW(waveform_r.x + loop_end_pos, 1);
            end_handle = {.xywh {end_line.x, r.y, handle_width, handle_height}};
            if (reverse) end_handle.x -= handle_width - end_line.w;

            auto grabber = end_handle;
            grabber.w += extra_grabbing_room_x;
            grabber.y -= extra_grabbing_room_away_from_centre;
            grabber.h += extra_grabbing_room_away_from_centre + extra_grabbing_room_towards_centre;
            if (reverse) grabber.x -= extra_grabbing_room_x;

            ParamIndex const params[] = {end_param_id, xfade_param_id};
            do_handle_slider(end_id,
                             params,
                             end_param_id,
                             grabber,
                             param.LinearValue(),
                             param.DefaultLinearValue(),
                             reverse,
                             [&](f32 value) {
                                 value = Min(1.0f, Max(loop_start + epsilon, value));
                                 SetParameterValue(engine.processor, end_param_id, value, {});
                                 set_xfade_size_if_needed();
                             });

            imgui.RegisterAndConvertRect(&end_line);
            imgui.RegisterAndConvertRect(&end_handle);
        };

        // region
        {
            loop_region_r =
                Rect::FromMinMax({waveform_r.x + Min(loop_start_pos, loop_end_pos), waveform_r.y},
                                 {waveform_r.x + Max(loop_start_pos, loop_end_pos), waveform_r.Bottom()});

            if (!(loop_start == 0 && loop_end == 1)) {
                ParamIndex const params[] = {start_param_id, end_param_id, xfade_param_id};
                do_handle_slider(loop_region_id,
                                 params,
                                 {},
                                 loop_region_r,
                                 loop_start,
                                 loop_start,
                                 reverse,
                                 [&](f32 value) {
                                     f32 delta = value - loop_start;
                                     if (loop_end + delta > 1.0f) delta = 1.0f - loop_end;

                                     auto new_start = loop_start + delta;
                                     auto new_end = loop_end + delta;

                                     if (new_start != loop_start || new_end != loop_end) {
                                         SetParameterValue(engine.processor, start_param_id, new_start, {});
                                         SetParameterValue(engine.processor, end_param_id, new_end, {});
                                         set_xfade_size_if_needed();
                                     }
                                 });
            }
            imgui.RegisterAndConvertRect(&loop_region_r);
        }

        // xfade
        {
            auto const& param = engine.processor.params[ToInt(xfade_param_id)];

            xfade_line = waveform_r.WithXW(waveform_r.x + loop_xfade_line_pos, 1);
            xfade_handle = {.xywh {xfade_line.x, waveform_r.y + handle_height, handle_width, handle_height}};
            if (reverse && mode.value.mode == sample_lib::Loop::Mode::Standard)
                xfade_handle.x -= handle_width - xfade_line.w;

            auto grabber = xfade_handle;
            grabber.w += extra_grabbing_room_x;
            if (reverse && mode.value.mode == sample_lib::Loop::Mode::Standard)
                grabber.x -= extra_grabbing_room_x;

            if (xfade_active) {
                bool const invert = mode.value.mode == sample_lib::Loop::Mode::Standard ? !reverse : false;

                do_handle_slider(xfade_id,
                                 {&xfade_param_id, 1},
                                 xfade_param_id,
                                 grabber,
                                 param.LinearValue(),
                                 param.DefaultLinearValue(),
                                 invert,
                                 [&](f32 value) {
                                     value = ClampCrossfadeSize<f32>(value,
                                                                     loop_start - epsilon,
                                                                     loop_end + epsilon,
                                                                     1.0f,
                                                                     *mode.value.mode);
                                     SetParameterValue(engine.processor, xfade_param_id, value, {});
                                 });
            }

            imgui.RegisterAndConvertRect(&xfade_line);
            imgui.RegisterAndConvertRect(&xfade_handle);
            draw_xfade = true;
        }
    }

    // offset
    Rect offs_handle;
    auto const offs_imgui_id = imgui.GetID("offset");
    {
        auto const sample_offset = layer->params[ToInt(LayerParamIndex::SampleOffset)].LinearValue();
        auto const param_id = ParamIndexFromLayerParamIndex(layer->index, LayerParamIndex::SampleOffset);
        auto const& param = engine.processor.params[ToInt(param_id)];

        auto sample_offset_r = waveform_r.WithW(waveform_r.w * sample_offset);
        offs_handle = {.xywh {sample_offset_r.Right() - handle_width,
                              waveform_r.Bottom() - handle_height,
                              handle_width,
                              handle_height}};
        auto grabber = offs_handle;
        grabber.y -= extra_grabbing_room_towards_centre;
        grabber.h += extra_grabbing_room_towards_centre + extra_grabbing_room_away_from_centre;
        grabber.w += extra_grabbing_room_x;
        grabber.x -= extra_grabbing_room_x;

        do_handle_slider(offs_imgui_id,
                         {&param_id, 1},
                         param_id,
                         grabber,
                         param.LinearValue(),
                         param.DefaultLinearValue(),
                         false,
                         [&](f32 value) { SetParameterValue(engine.processor, param_id, value, {}); });

        imgui.RegisterAndConvertRect(&offs_handle);
        imgui.RegisterAndConvertRect(&sample_offset_r);

        imgui.graphics->AddRectFilled(sample_offset_r.Min(),
                                      sample_offset_r.Max(),
                                      LiveCol(imgui, UiColMap::Waveform_SampleOffset));
        imgui.graphics->AddRectFilled(f32x2 {sample_offset_r.Right() - 1, sample_offset_r.y},
                                      sample_offset_r.Max(),
                                      imgui.IsHotOrActive(offs_imgui_id)
                                          ? LiveCol(imgui, UiColMap::Waveform_OffsetHandleHover)
                                          : LiveCol(imgui, UiColMap::Waveform_OffsetHandle));
    }

    // drawing
    if (mode.value.editable) {
        auto other_xfade_line = start_line.WithPos(start_line.TopRight() +
                                                   f32x2 {reverse ? loop_xfade_size : -loop_xfade_size, 0});
        if (mode.value.mode == sample_lib::Loop::Mode::PingPong)
            other_xfade_line = left_line.WithPos(left_line.TopRight() - f32x2 {loop_xfade_size, 0});

        if (draw_xfade && loop_xfade_size > 0.01f) {
            if (mode.value.mode == sample_lib::Loop::Mode::Standard) {
                imgui.graphics->AddLine(xfade_line.Min(),
                                        end_line.BottomLeft(),
                                        LiveCol(imgui, UiColMap::Waveform_XFade));
                imgui.graphics->AddLine(other_xfade_line.BottomLeft(),
                                        start_line.TopLeft(),
                                        LiveCol(imgui, UiColMap::Waveform_XFade));
            } else {
                imgui.graphics->AddLine(other_xfade_line.BottomLeft(),
                                        left_line.TopLeft(),
                                        LiveCol(imgui, UiColMap::Waveform_XFade));
                imgui.graphics->AddLine(right_line.TopRight(),
                                        xfade_line.BottomLeft(),
                                        LiveCol(imgui, UiColMap::Waveform_XFade));
            }
        }

        auto const region_active = imgui.IsHot(loop_region_id) || imgui.IsActive(loop_region_id);
        if (!region_active && loop_xfade_size > 0.01f && draw_xfade) {
            if (mode.value.mode == sample_lib::Loop::Mode::Standard) {
                f32x2 const points[] = {start_line.TopLeft(),
                                        xfade_line.TopLeft(),
                                        end_line.BottomRight(),
                                        start_line.BottomLeft()};
                imgui.graphics->AddConvexPolyFilled(points,
                                                    (int)ArraySize(points),
                                                    LiveCol(imgui, UiColMap::Waveform_RegionOverlay),
                                                    true);
            } else {
                f32x2 const points[] = {other_xfade_line.BottomLeft(),
                                        left_line.TopLeft(),
                                        right_line.TopLeft(),
                                        xfade_line.BottomRight()};
                imgui.graphics->AddConvexPolyFilled(points,
                                                    (int)ArraySize(points),
                                                    LiveCol(imgui, UiColMap::Waveform_RegionOverlay),
                                                    true);
            }
        } else {
            imgui.graphics->AddRectFilled(loop_region_r.Min(),
                                          loop_region_r.Max(),
                                          region_active
                                              ? LiveCol(imgui, UiColMap::Waveform_RegionOverlayHover)
                                              : LiveCol(imgui, UiColMap::Waveform_RegionOverlay));
        }

        imgui.graphics->AddRectFilled(start_line.Min(),
                                      start_line.Max(),
                                      imgui.IsHotOrActive(start_id)
                                          ? LiveCol(imgui, UiColMap::Waveform_LoopHandleHover)
                                          : LiveCol(imgui, UiColMap::Waveform_LoopHandle));
        imgui.graphics->AddRectFilled(end_line.Min(),
                                      end_line.Max(),
                                      imgui.IsHotOrActive(end_id)
                                          ? LiveCol(imgui, UiColMap::Waveform_LoopHandleHover)
                                          : LiveCol(imgui, UiColMap::Waveform_LoopHandle));
        if (draw_xfade && loop_xfade_size > 0.01f) {
            imgui.graphics->AddRectFilled(xfade_line.Min(),
                                          xfade_line.Max(),
                                          imgui.IsHotOrActive(xfade_id)
                                              ? LiveCol(imgui, UiColMap::Waveform_XfadeHandleHover)
                                              : LiveCol(imgui, UiColMap::Waveform_XfadeHandle));
        }

        draw_handle(start_handle, start_id, HandleType::LoopStart, false);
        draw_handle(end_handle, end_id, HandleType::LoopEnd, false);
        if (draw_xfade) draw_handle(xfade_handle, xfade_id, HandleType::Xfade, draw_xfade_as_inactive);
    }
    draw_handle(offs_handle, offs_imgui_id, HandleType::Offset, false);

    // cursors
    if (engine.processor.voice_pool.num_active_voices.Load(LoadMemoryOrder::Relaxed)) {
        auto& voice_waveform_markers =
            engine.processor.voice_pool.voice_waveform_markers_for_gui.Consume().data;
        for (auto const voice_index : Range(k_num_voices)) {
            auto const marker = voice_waveform_markers[voice_index];
            if (!marker.intensity || marker.layer_index != layer->index) continue;

            f32 position = (f32)marker.position / (f32)UINT16_MAX;
            f32 const intensity = (f32)marker.intensity / (f32)UINT16_MAX;
            if (reverse) position = 1 - position;

            f32x2 cursor_pos {Round(waveform_r.x + position * waveform_r.w), waveform_r.y};
            cursor_pos = imgui.WindowPosToScreenPos(cursor_pos);
            draw::VoiceMarkerLine(imgui,
                                  cursor_pos,
                                  waveform_r.h,
                                  imgui.WindowPosToScreenPos(waveform_r.pos).x,
                                  {},
                                  intensity);
            g->frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::Animate);
        }
    }

    if (g->param_text_editor_to_open) {
        ParamIndex const waveform_params[] = {
            layer->params[ToInt(LayerParamIndex::LoopStart)].info.index,
            layer->params[ToInt(LayerParamIndex::LoopEnd)].info.index,
            layer->params[ToInt(LayerParamIndex::LoopCrossfade)].info.index,
            layer->params[ToInt(LayerParamIndex::SampleOffset)].info.index,
        };
        auto const cut = r.w / 3;
        HandleShowingTextEditorForParams(g, r.CutLeft(cut).CutRight(cut), waveform_params);
    }
}

void GUIDoSampleWaveform(Gui* g, LayerProcessor* layer, Rect r) {
    auto& imgui = g->imgui;
    auto reg_r = r;
    g->imgui.RegisterAndConvertRect(&reg_r);
    g->imgui.PushID((uintptr)layer);
    DEFER { g->imgui.PopID(); };

    auto const rounding = LiveSize(g->imgui, UiSizeId::CornerRounding);

    auto waveform_r_unreg = r;
    auto waveform_r = g->imgui.GetRegisteredAndConvertedRect(waveform_r_unreg);
    g->imgui.graphics->AddRectFilled(waveform_r.Min(),
                                     waveform_r.Max(),
                                     LiveCol(imgui, UiColMap::Waveform_LoopBack),
                                     rounding);

    bool is_loading = false;
    if (g->engine.sample_lib_server_async_channel.instrument_loading_percents[(usize)layer->index].Load(
            LoadMemoryOrder::Relaxed) != -1) {
        labels::Label(g, r, "Loading...", labels::WaveformLoadingLabel(g->imgui));
        is_loading = true;
    } else if (layer->instrument_id.tag != InstrumentType::None) {
        auto const offset = layer->params[ToInt(LayerParamIndex::SampleOffset)].LinearValue();
        auto const loop_start = layer->params[ToInt(LayerParamIndex::LoopStart)].LinearValue();
        auto const reverse = layer->params[ToInt(LayerParamIndex::Reverse)].ValueAsBool();
        auto const loop_end = Max(layer->params[ToInt(LayerParamIndex::LoopEnd)].LinearValue(), loop_start);
        using namespace loop_and_reverse_flags;
        auto const loop_mode =
            layer->params[ToInt(LayerParamIndex::LoopMode)].ValueAsInt<param_values::LoopMode>();
        bool const loop_points_editable = ActualLoopBehaviour(layer->instrument, loop_mode).value.editable;

        struct Range {
            f32x2 lo;
            f32x2 hi;
        };

        Range loop_section_uv;
        Range whole_section_uv;
        Range offset_section_uv;

        whole_section_uv.lo = {offset, 0};
        whole_section_uv.hi = {1, 1};
        offset_section_uv.lo = {0, 0};
        offset_section_uv.hi = {offset, 1};
        loop_section_uv.lo = {loop_start, 0};
        loop_section_uv.hi = {loop_start + (loop_end - loop_start), 1};
        if (reverse) {
            whole_section_uv.lo.x = 1.0f - whole_section_uv.lo.x;
            whole_section_uv.hi.x = 1.0f - whole_section_uv.hi.x;
            offset_section_uv.lo.x = 1.0f - offset_section_uv.lo.x;
            offset_section_uv.hi.x = 1.0f - offset_section_uv.hi.x;
        }

        // Fix issue where texture subtley begins to tile when we don't want it
        waveform_r.x = Round(waveform_r.x);
        waveform_r.y = Round(waveform_r.y);
        waveform_r.w = Round(waveform_r.w);
        waveform_r.h = Round(waveform_r.h);
        r.w = Round(r.w);

        WaveformAudioSource waveform_source {WaveformAudioSourceType::Sine};
        switch (layer->instrument.tag) {
            case InstrumentType::None: PanicIfReached(); break;
            case InstrumentType::Sampler: {
                auto sampled_inst = layer->instrument.TryGetFromTag<InstrumentType::Sampler>();
                waveform_source = (*sampled_inst)->file_for_gui_waveform;
                break;
            }
            case InstrumentType::WaveformSynth: {
                auto waveform_type = layer->instrument.GetFromTag<InstrumentType::WaveformSynth>();
                switch (waveform_type) {
                    case WaveformType::Sine: waveform_source = WaveformAudioSourceType::Sine; break;
                    case WaveformType::WhiteNoiseStereo:
                    case WaveformType::WhiteNoiseMono:
                        waveform_source = WaveformAudioSourceType::WhiteNoise;
                        break;
                    default:
                }
                break;
            }
        }

        auto tex = g->waveforms.FetchOrCreate(*g->frame_input.graphics_ctx,
                                              g->scratch_arena,
                                              waveform_source,
                                              r.w,
                                              r.h);
        if (tex.HasValue()) {
            g->imgui.graphics->AddImage(tex.Value(),
                                        waveform_r.Min() + f32x2 {offset * r.w, 0},
                                        waveform_r.Max(),
                                        whole_section_uv.lo,
                                        whole_section_uv.hi,
                                        (!loop_points_editable)
                                            ? LiveCol(imgui, UiColMap::Waveform_LoopWaveformLoop)
                                            : LiveCol(imgui, UiColMap::Waveform_LoopWaveform));

            if ((loop_end - loop_start) != 0 && loop_points_editable) {
                g->imgui.graphics->AddImage(
                    tex.Value(),
                    waveform_r.Min() + f32x2 {r.w * (reverse ? (1.0f - loop_start) : loop_start), 0},
                    waveform_r.Max() - f32x2 {waveform_r.w * (reverse ? loop_end : (1.0f - loop_end)), 0},
                    loop_section_uv.lo,
                    loop_section_uv.hi,
                    LiveCol(imgui, UiColMap::Waveform_LoopWaveformLoop));
            }

            if (offset != 0) {
                g->imgui.graphics->AddImage(tex.Value(),
                                            waveform_r.Min(),
                                            waveform_r.Max() - f32x2 {r.w * (1.0f - offset), 0},
                                            offset_section_uv.lo,
                                            offset_section_uv.hi,
                                            LiveCol(imgui, UiColMap::Waveform_LoopWaveformOffset));
            }
        }
    }

    if (!is_loading) GUIDoSampleWaveformOverlay(g, layer, r, waveform_r_unreg);
}
