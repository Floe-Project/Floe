// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_effects.hpp"

#include <IconsFontAwesome5.h>
#include <float.h>

#include "os/threading.hpp"

#include "descriptors/param_descriptors.hpp"
#include "engine/engine.hpp"
#include "gui.hpp"
#include "gui/gui_dragger_widgets.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_label_widgets.hpp"
#include "gui_widget_compounds.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"
#include "processor/effect.hpp"

constexpr auto k_reverb_params = ComptimeParamSearch<ComptimeParamSearchOptions {
    .modules = {ParameterModule::Effect, ParameterModule::Reverb},
    .skip = ParamIndex::ReverbOn,
}>();

constexpr auto k_phaser_params = ComptimeParamSearch<ComptimeParamSearchOptions {
    .modules = {ParameterModule::Effect, ParameterModule::Phaser},
    .skip = ParamIndex::PhaserOn,
}>();

struct EffectIDs {
    layout::Id heading;
    layout::Id divider;
    layout::Id close;

    Effect* fx;

    union {
        struct {
            LayIDPair type;
            LayIDPair amount;
        } distortion;

        struct {
            LayIDPair bits;
            LayIDPair sample_rate;
            LayIDPair wet;
            LayIDPair dry;
        } bit_crush;

        struct {
            LayIDPair threshold;
            LayIDPair ratio;
            LayIDPair gain;

            layout::Id auto_gain;
        } compressor;

        struct {
            LayIDPair type;
            LayIDPair cutoff;
            LayIDPair reso;
            bool using_gain;
            LayIDPair gain;
        } filter;

        struct {
            LayIDPair width;
        } stereo;

        struct {
            LayIDPair rate;
            LayIDPair highpass;
            LayIDPair depth;
            LayIDPair wet;
            LayIDPair dry;
        } chorus;

        struct {
            Array<LayIDPair, k_reverb_params.size> ids;
        } reverb;

        struct {
            Array<LayIDPair, k_phaser_params.size> ids;
        } phaser;

        struct {
            LayIDPair feedback;
            LayIDPair left;
            LayIDPair right;
            LayIDPair mix;
            LayIDPair filter_cutoff;
            LayIDPair filter_spread;
            LayIDPair mode;
            layout::Id sync_btn;
        } delay;

        struct {
            LayIDPair ir;
            LayIDPair highpass;
            LayIDPair wet;
            LayIDPair dry;
        } convo;
    };
};

static void ImpulseResponseMenuItems(Gui* g) {
    auto const scratch_cursor = g->scratch_arena.TotalUsed();
    DEFER { g->scratch_arena.TryShrinkTotalUsed(scratch_cursor); };

    auto libs = sample_lib_server::AllLibrariesRetained(g->shared_engine_systems.sample_library_server,
                                                        g->scratch_arena);
    DEFER { sample_lib_server::ReleaseAll(libs); };

    StartFloeMenu(g);
    DEFER { EndFloeMenu(g); };

    // TODO(1.0): this is not production-ready code. We need a new powerful database-like browser GUI
    int current = 0;
    DynamicArray<String> irs {g->scratch_arena};
    DynamicArray<sample_lib::IrId> ir_ids {g->scratch_arena};
    dyn::Append(irs, "None");
    dyn::Append(ir_ids, {});

    for (auto l : libs) {
        for (auto const ir : l->irs_by_name) {
            auto const ir_id = sample_lib::IrId {
                .library = l->Id(),
                .ir_name = ir.key,
            };

            if (g->engine.processor.convo.ir_id == ir_id) current = (int)irs.size;
            dyn::Append(irs, fmt::Format(g->scratch_arena, "{}: {}", l->name, ir.key));
            dyn::Append(ir_ids, ir_id);
        }
    }

    if (DoMultipleMenuItems(g, irs, current)) {
        if (current == 0)
            LoadConvolutionIr(g->engine, k_nullopt);
        else
            LoadConvolutionIr(g->engine, ir_ids[(usize)current]);
    }
}

static void DoImpulseResponseMenu(Gui* g, layout::Id lay_id) {
    auto r = layout::GetRect(g->layout, lay_id);

    auto id = g->imgui.GetID("Impulse");
    auto const ir_name =
        g->engine.processor.convo.ir_id ? String(g->engine.processor.convo.ir_id->ir_name) : "None"_s;
    if (buttons::Popup(g, id, id + 1, r, ir_name, buttons::ParameterPopupButton(g->imgui))) {
        ImpulseResponseMenuItems(g);
        g->imgui.EndWindow();
    }
    Tooltip(g, id, r, fmt::Format(g->scratch_arena, "Impulse: {}\n{}", ir_name.size, "Impulse response"));
}

struct FXColours {
    u32 back;
    u32 highlight;
    u32 button;
};

static FXColours GetFxCols(imgui::Context const& imgui, EffectType type) {
    using enum UiColMap;
    switch (type) {
        case EffectType::Distortion:
            return {LiveCol(imgui, DistortionBack),
                    LiveCol(imgui, DistortionHighlight),
                    LiveCol(imgui, DistortionButton)};
        case EffectType::BitCrush:
            return {LiveCol(imgui, BitCrushBack),
                    LiveCol(imgui, BitCrushHighlight),
                    LiveCol(imgui, BitCrushButton)};
        case EffectType::Compressor:
            return {LiveCol(imgui, CompressorBack),
                    LiveCol(imgui, CompressorHighlight),
                    LiveCol(imgui, CompressorButton)};
        case EffectType::FilterEffect:
            return {LiveCol(imgui, FilterBack),
                    LiveCol(imgui, FilterHighlight),
                    LiveCol(imgui, FilterButton)};
        case EffectType::StereoWiden:
            return {LiveCol(imgui, StereoBack),
                    LiveCol(imgui, StereoHighlight),
                    LiveCol(imgui, StereoButton)};
        case EffectType::Chorus:
            return {LiveCol(imgui, ChorusBack),
                    LiveCol(imgui, ChorusHighlight),
                    LiveCol(imgui, ChorusButton)};
        case EffectType::Reverb:
            return {LiveCol(imgui, ReverbBack),
                    LiveCol(imgui, ReverbHighlight),
                    LiveCol(imgui, ReverbButton)};
        case EffectType::Delay:
            return {LiveCol(imgui, DelayBack), LiveCol(imgui, DelayHighlight), LiveCol(imgui, DelayButton)};
        case EffectType::ConvolutionReverb:
            return {LiveCol(imgui, ConvolutionBack),
                    LiveCol(imgui, ConvolutionHighlight),
                    LiveCol(imgui, ConvolutionButton)};
        case EffectType::Phaser:
            return {LiveCol(imgui, PhaserBack),
                    LiveCol(imgui, PhaserHighlight),
                    LiveCol(imgui, PhaserButton)};
        case EffectType::Count: PanicIfReached();
    }
    return {};
}

void DoEffectsWindow(Gui* g, Rect r) {
    using enum UiSizeId;
    auto& imgui = g->imgui;
    auto& lay = g->layout;
    auto& engine = g->engine;

    auto const fx_divider_margin_b = LiveSize(imgui, FXDividerMarginB);
    auto const fx_param_button_height = LiveSize(imgui, FXParamButtonHeight);
    auto const corner_rounding = LiveSize(imgui, CornerRounding);

    auto settings = FloeWindowSettings(imgui, [](IMGUI_DRAW_WINDOW_BG_ARGS) {});
    settings.flags |= imgui::WindowFlags_AlwaysDrawScrollY;
    settings.pad_top_left = {LiveSize(imgui, FXWindowPadL), LiveSize(imgui, FXWindowPadT)};
    settings.pad_bottom_right = {LiveSize(imgui, FXWindowPadR), LiveSize(imgui, FXWindowPadB)};
    imgui.BeginWindow(settings, r, "Effects");
    DEFER { imgui.EndWindow(); };
    DEFER { layout::ResetContext(lay); };

    layout::Id switches[k_num_effect_types];
    for (auto& s : switches)
        s = layout::k_invalid_id;
    layout::Id switches_bottom_divider;
    DynamicArrayBounded<EffectIDs, k_num_effect_types> effects;

    auto& dragging_fx_unit = g->dragging_fx_unit;

    //
    //
    //
    auto const root_width = imgui.Width();
    auto effects_root = layout::CreateItem(lay,
                                           {
                                               .size = imgui.Size(),
                                               .contents_direction = layout::Direction::Column,
                                               .contents_align = layout::JustifyContent::Start,
                                           });

    int const switches_left_col_size = k_num_effect_types / 2 + (k_num_effect_types % 2);

    {
        auto const fx_switch_board_margin_l = LiveSize(imgui, FXSwitchBoardMarginL);
        auto const fx_switch_board_margin_r = LiveSize(imgui, FXSwitchBoardMarginR);

        auto switches_container =
            layout::CreateItem(lay,
                               {
                                   .parent = effects_root,
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .margins = {.l = fx_switch_board_margin_l,
                                               .r = fx_switch_board_margin_r,
                                               .t = LiveSize(imgui, FXSwitchBoardMarginT),
                                               .b = LiveSize(imgui, FXSwitchBoardMarginB)},
                                   .contents_direction = layout::Direction::Row,
                               });

        auto left = layout::CreateItem(lay,
                                       {
                                           .parent = switches_container,
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_direction = layout::Direction::Column,

                                       });
        auto right = layout::CreateItem(lay,
                                        {
                                            .parent = switches_container,
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_direction = layout::Direction::Column,
                                        });

        auto const fx_switch_board_item_height = LiveSize(imgui, FXSwitchBoardItemHeight);
        for (auto const i : Range(k_num_effect_types)) {
            auto const parent = (i < switches_left_col_size) ? left : right;
            switches[i] = layout::CreateItem(
                lay,
                {
                    .parent = parent,
                    .size = {(root_width / 2) - fx_switch_board_margin_l - fx_switch_board_margin_r,
                             fx_switch_board_item_height},
                });
        }
    }

    switches_bottom_divider = layout::CreateItem(lay,
                                                 {
                                                     .parent = effects_root,
                                                     .size = {layout::k_fill_parent, 1},
                                                     .margins = {.b = fx_divider_margin_b},
                                                 });

    auto heading_font = g->fira_sans;

    auto get_heading_size = [&](String name) {
        auto size = heading_font->CalcTextSizeA(heading_font->font_size_no_scale *
                                                    buttons::EffectHeading(imgui, 0).text_scaling,
                                                FLT_MAX,
                                                0.0f,
                                                name);
        f32 const epsilon = 2;
        return f32x2 {Round(size.x + epsilon) + LiveSize(imgui, FXHeadingExtraWidth),
                      LiveSize(imgui, FXHeadingH)};
    };

    auto create_fx_ids = [&](Effect& fx, layout::Id* heading_container_out) {
        EffectIDs ids;
        ids.fx = &fx;

        auto master_heading_container =
            layout::CreateItem(lay,
                               {
                                   .parent = effects_root,
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::JustifyContent::Start,
                               });

        auto const heading_size = get_heading_size(k_effect_info[ToInt(fx.type)].name);
        ids.heading = layout::CreateItem(
            lay,
            {
                .parent = master_heading_container,
                .size = {heading_size.x, heading_size.y},
                .margins = {.l = LiveSize(imgui, FXHeadingL), .r = LiveSize(imgui, FXHeadingR)},
                .anchor = layout::Anchor::Left | layout::Anchor::Top,
            });

        auto heading_container =
            layout::CreateItem(lay,
                               {
                                   .parent = master_heading_container,
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::JustifyContent::End,
                               });

        ids.close = layout::CreateItem(
            lay,
            {
                .parent = master_heading_container,
                .size = {LiveSize(imgui, FXCloseButtonWidth), LiveSize(imgui, FXCloseButtonHeight)},
            });

        if (heading_container_out) *heading_container_out = heading_container;
        return ids;
    };

    auto const divider_options = layout::ItemOptions {
        .parent = effects_root,
        .size = {layout::k_fill_parent, 1},
        .margins = {.t = LiveSize(imgui, FXDividerMarginT), .b = fx_divider_margin_b},
    };

    auto const param_container_options = layout::ItemOptions {
        .parent = effects_root,
        .size = {layout::k_fill_parent, layout::k_hug_contents},
        .contents_direction = layout::Direction::Row,
        .contents_multiline = true,
        .contents_align = layout::JustifyContent::Middle,
    };

    auto create_subcontainer = [&](layout::Id parent) {
        return layout::CreateItem(lay,
                                  {
                                      .parent = parent,
                                      .size = layout::k_hug_contents,
                                      .contents_direction = layout::Direction::Row,
                                  });
    };

    auto layout_all = [&](Span<LayIDPair> ids, Span<ParamIndex const> params) {
        auto param_container = layout::CreateItem(lay, param_container_options);

        Optional<layout::Id> container {};
        u8 previous_group = 0;
        for (auto const i : Range(ids.size)) {
            auto const& info = k_param_descriptors[ToInt(params[i])];
            layout::Id inner_container = param_container;
            if (info.grouping_within_module != 0) {
                if (!container || info.grouping_within_module != previous_group)
                    container = create_subcontainer(param_container);
                inner_container = *container;
                previous_group = info.grouping_within_module;
            }
            LayoutParameterComponent(g, inner_container, ids[i], engine.processor.params[ToInt(params[i])]);
        }
    };

    auto ordered_effects =
        DecodeEffectsArray(engine.processor.desired_effects_order.Load(LoadMemoryOrder::Relaxed),
                           engine.processor.effects_ordered_by_type);

    for (auto fx : ordered_effects) {
        if (!EffectIsOn(engine.processor.params, fx)) continue;

        switch (fx->type) {
            case EffectType::Distortion: {
                auto ids = create_fx_ids(engine.processor.distortion, nullptr);
                auto param_container = layout::CreateItem(lay, param_container_options);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.distortion.type,
                                         engine.processor.params[ToInt(ParamIndex::DistortionType)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.distortion.amount,
                                         engine.processor.params[ToInt(ParamIndex::DistortionDrive)]);

                ids.divider = layout::CreateItem(lay, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::BitCrush: {
                auto ids = create_fx_ids(engine.processor.bit_crush, nullptr);
                auto param_container = layout::CreateItem(lay, param_container_options);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.bit_crush.bits,
                                         engine.processor.params[ToInt(ParamIndex::BitCrushBits)]);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.bit_crush.sample_rate,
                                         engine.processor.params[ToInt(ParamIndex::BitCrushBitRate)]);

                auto mix_container = create_subcontainer(param_container);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.bit_crush.wet,
                                         engine.processor.params[ToInt(ParamIndex::BitCrushWet)]);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.bit_crush.dry,
                                         engine.processor.params[ToInt(ParamIndex::BitCrushDry)]);

                ids.divider = layout::CreateItem(lay, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Compressor: {
                layout::Id heading_container;
                auto ids = create_fx_ids(engine.processor.compressor, &heading_container);
                auto param_container = layout::CreateItem(lay, param_container_options);

                ids.compressor.auto_gain = layout::CreateItem(
                    lay,
                    {
                        .parent = heading_container,
                        .size = {LiveSize(imgui, FXCompressorAutoGainWidth), fx_param_button_height},
                    });

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.compressor.threshold,
                                         engine.processor.params[ToInt(ParamIndex::CompressorThreshold)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.compressor.ratio,
                                         engine.processor.params[ToInt(ParamIndex::CompressorRatio)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.compressor.gain,
                                         engine.processor.params[ToInt(ParamIndex::CompressorGain)]);

                ids.divider = layout::CreateItem(lay, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::FilterEffect: {
                auto ids = create_fx_ids(engine.processor.filter_effect, nullptr);
                auto param_container = layout::CreateItem(lay, param_container_options);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.filter.type,
                                         engine.processor.params[ToInt(ParamIndex::FilterType)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.filter.cutoff,
                                         engine.processor.params[ToInt(ParamIndex::FilterCutoff)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.filter.reso,
                                         engine.processor.params[ToInt(ParamIndex::FilterResonance)]);
                ids.filter.using_gain =
                    engine.processor.filter_effect.IsUsingGainParam(engine.processor.params);
                if (ids.filter.using_gain) {
                    LayoutParameterComponent(g,
                                             param_container,
                                             ids.filter.gain,
                                             engine.processor.params[ToInt(ParamIndex::FilterGain)]);
                }

                ids.divider = layout::CreateItem(lay, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::StereoWiden: {
                auto ids = create_fx_ids(engine.processor.stereo_widen, nullptr);
                auto param_container = layout::CreateItem(lay, param_container_options);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.stereo.width,
                                         engine.processor.params[ToInt(ParamIndex::StereoWidenWidth)]);

                ids.divider = layout::CreateItem(lay, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Chorus: {
                auto ids = create_fx_ids(engine.processor.chorus, nullptr);
                auto param_container = layout::CreateItem(lay, param_container_options);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.chorus.rate,
                                         engine.processor.params[ToInt(ParamIndex::ChorusRate)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.chorus.highpass,
                                         engine.processor.params[ToInt(ParamIndex::ChorusHighpass)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.chorus.depth,
                                         engine.processor.params[ToInt(ParamIndex::ChorusDepth)]);

                auto mix_container = create_subcontainer(param_container);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.chorus.wet,
                                         engine.processor.params[ToInt(ParamIndex::ChorusWet)]);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.chorus.dry,
                                         engine.processor.params[ToInt(ParamIndex::ChorusDry)]);

                ids.divider = layout::CreateItem(lay, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Reverb: {
                auto ids = create_fx_ids(engine.processor.reverb, nullptr);

                layout_all(ids.reverb.ids, k_reverb_params);

                ids.divider = layout::CreateItem(lay, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Phaser: {
                auto ids = create_fx_ids(engine.processor.phaser, nullptr);

                layout_all(ids.phaser.ids, k_phaser_params);

                ids.divider = layout::CreateItem(lay, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Delay: {
                layout::Id heading_container;
                auto ids = create_fx_ids(engine.processor.delay, &heading_container);
                auto param_container = layout::CreateItem(lay, param_container_options);

                ids.delay.sync_btn = layout::CreateItem(
                    lay,
                    {
                        .parent = heading_container,
                        .size = {LiveSize(imgui, FXDelaySyncBtnWidth), fx_param_button_height},
                    });

                auto left = &engine.processor.params[ToInt(ParamIndex::DelayTimeSyncedL)];
                auto right = &engine.processor.params[ToInt(ParamIndex::DelayTimeSyncedR)];
                if (!engine.processor.params[ToInt(ParamIndex::DelayTimeSyncSwitch)].ValueAsBool()) {
                    left = &engine.processor.params[ToInt(ParamIndex::DelayTimeLMs)];
                    right = &engine.processor.params[ToInt(ParamIndex::DelayTimeRMs)];
                }
                LayoutParameterComponent(g, param_container, ids.delay.left, *left, k_nullopt, false, true);
                LayoutParameterComponent(g, param_container, ids.delay.right, *right, k_nullopt, false, true);
                {
                    LayoutParameterComponent(g,
                                             param_container,
                                             ids.delay.feedback,
                                             engine.processor.params[ToInt(ParamIndex::DelayFeedback)]);
                }
                {
                    auto const id =
                        LayoutParameterComponent(g,
                                                 param_container,
                                                 ids.delay.mode,
                                                 engine.processor.params[ToInt(ParamIndex::DelayMode)]);
                    layout::SetBehave(lay, id, layout::flags::LineBreak);
                }
                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.delay.filter_cutoff,
                    engine.processor.params[ToInt(ParamIndex::DelayFilterCutoffSemitones)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.delay.filter_spread,
                                         engine.processor.params[ToInt(ParamIndex::DelayFilterSpread)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.delay.mix,
                                         engine.processor.params[ToInt(ParamIndex::DelayMix)]);

                ids.divider = layout::CreateItem(lay, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::ConvolutionReverb: {
                auto ids = create_fx_ids(engine.processor.convo, nullptr);
                auto param_container = layout::CreateItem(lay, param_container_options);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.convo.ir.control,
                                         ids.convo.ir.label,
                                         LayoutType::Effect,
                                         k_nullopt,
                                         true);

                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.convo.highpass,
                    engine.processor.params[ToInt(ParamIndex::ConvolutionReverbHighpass)]);

                auto mix_container = create_subcontainer(param_container);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.convo.wet,
                                         engine.processor.params[ToInt(ParamIndex::ConvolutionReverbWet)]);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.convo.dry,
                                         engine.processor.params[ToInt(ParamIndex::ConvolutionReverbDry)]);

                ids.divider = layout::CreateItem(lay, divider_options);
                dyn::Append(effects, ids);
            } break;

            case EffectType::Count: PanicIfReached(); break;
        }
    }

    //
    //
    //
    layout::RunContext(lay);

    layout::Id closest_divider = layout::k_invalid_id;
    if (dragging_fx_unit && imgui.HoveredWindow() == imgui.CurrentWindow()) {
        f32 const rel_y_pos = imgui.ScreenPosToWindowPos(imgui.frame_input.cursor_pos).y;
        f32 distance = Abs(layout::GetRect(lay, switches_bottom_divider).y - rel_y_pos);
        closest_divider = switches_bottom_divider;
        usize closest_slot = 0;
        auto const original_slot = FindSlotInEffects(ordered_effects, dragging_fx_unit->fx);

        for (auto ids : effects) {
            if (f32 const d = Abs(layout::GetRect(lay, ids.divider).y - rel_y_pos); d < distance) {
                distance = d;
                closest_divider = ids.divider;
                closest_slot = FindSlotInEffects(ordered_effects, ids.fx) + 1;
                if (closest_slot > original_slot) --closest_slot;
            }
        }

        ASSERT(closest_slot <= ordered_effects.size);
        if (dragging_fx_unit->drop_slot != closest_slot)
            imgui.frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
        dragging_fx_unit->drop_slot = closest_slot;
    }

    auto const draw_divider = [&](layout::Id id) {
        auto const room_at_scroll_window_bottom = imgui.PointsToPixels(15);
        auto const line_r =
            imgui.GetRegisteredAndConvertedRect(layout::GetRect(lay, id).WithH(room_at_scroll_window_bottom));
        imgui.graphics->AddLine(line_r.TopLeft(),
                                line_r.TopRight(),
                                (id == closest_divider) ? LiveCol(imgui, UiColMap::FXDividerLineDropZone)
                                                        : LiveCol(imgui, UiColMap::FXDividerLine));
    };

    auto const fx_knob_joining_line_thickness = LiveSize(imgui, FXKnobJoiningLineThickness);
    auto const fx_knob_joining_line_pad_lr = LiveSize(imgui, FXKnobJoiningLinePadLR);
    auto const draw_knob_joining_line = [&](layout::Id knob1, layout::Id knob2) {
        auto r1 = imgui.GetRegisteredAndConvertedRect(layout::GetRect(lay, knob1));
        auto r2 = imgui.GetRegisteredAndConvertedRect(layout::GetRect(lay, knob2));
        f32x2 const start {r1.Right() + fx_knob_joining_line_pad_lr,
                           r1.CentreY() - fx_knob_joining_line_thickness / 2};
        f32x2 const end {r2.x - fx_knob_joining_line_pad_lr, start.y};
        imgui.graphics->AddLine(start,
                                end,
                                LiveCol(imgui, UiColMap::FXKnobJoiningLine),
                                fx_knob_joining_line_thickness);
    };

    auto const do_all_ids = [&](Span<LayIDPair> ids, Span<ParamIndex const> params, FXColours cols) {
        for (auto const i : Range(ids.size))
            KnobAndLabel(g,
                         engine.processor.params[ToInt(params[i])],
                         ids[i],
                         knobs::DefaultKnob(imgui, cols.highlight));

        u8 previous_group = 0;
        for (auto const i : Range(ids.size)) {
            auto const& info = k_param_descriptors[ToInt(params[i])];
            if (info.grouping_within_module != 0) {
                if (info.grouping_within_module == previous_group)
                    draw_knob_joining_line(ids[i - 1].control, ids[i].control);
                previous_group = info.grouping_within_module;
            }
        }
    };

    draw_divider(switches_bottom_divider);

    for (auto ids : effects) {
        imgui.PushID((u64)ids.fx->type);
        DEFER { imgui.PopID(); };

        draw_divider(ids.divider);
        if (dragging_fx_unit && dragging_fx_unit->fx == ids.fx) continue;

        auto const do_heading = [&](Effect& fx, u32 col) {
            {
                auto const id = imgui.GetID("heading");
                auto const r = layout::GetRect(lay, ids.heading);
                buttons::Button(g,
                                id,
                                r,
                                k_effect_info[ToInt(fx.type)].name,
                                buttons::EffectHeading(imgui, col));

                if (imgui.WasJustActivated(id)) {
                    dragging_fx_unit = DraggingFX {id, &fx, FindSlotInEffects(ordered_effects, &fx), {}};
                    imgui.frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
                }

                if (imgui.IsHotOrActive(id)) g->frame_output.cursor_type = CursorType::AllArrows;
                Tooltip(g,
                        id,
                        r,
                        fmt::Format(g->scratch_arena, "{}", k_effect_info[ToInt(fx.type)].description));
            }

            {
                auto const close_id = imgui.GetID("close");
                auto const r = layout::GetRect(lay, ids.close);
                if (buttons::Button(g,
                                    close_id,
                                    r,
                                    ICON_FA_TIMES,
                                    buttons::IconButton(imgui).WithIconScaling(0.7f))) {
                    SetParameterValue(engine.processor, k_effect_info[ToInt(fx.type)].on_param_index, 0, {});
                }
                Tooltip(g,
                        close_id,
                        r,
                        fmt::Format(g->scratch_arena, "Remove {}", k_effect_info[ToInt(fx.type)].name));
            }
        };

        switch (ids.fx->type) {
            case EffectType::Distortion: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.distortion, cols.back);
                auto& d = ids.distortion;

                buttons::PopupWithItems(g,
                                        engine.processor.params[ToInt(ParamIndex::DistortionType)],
                                        d.type.control,
                                        buttons::ParameterPopupButton(imgui));
                labels::Label(g,
                              engine.processor.params[ToInt(ParamIndex::DistortionType)],
                              d.type.label,
                              labels::ParameterCentred(imgui));

                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::DistortionDrive)],
                             d.amount,
                             knobs::DefaultKnob(imgui, cols.highlight));
                break;
            }
            case EffectType::BitCrush: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.bit_crush, cols.back);
                auto& b = ids.bit_crush;

                draggers::Dragger(g,
                                  engine.processor.params[ToInt(ParamIndex::BitCrushBits)],
                                  b.bits.control,
                                  draggers::DefaultStyle(imgui));
                labels::Label(g,
                              engine.processor.params[ToInt(ParamIndex::BitCrushBits)],
                              b.bits.label,
                              labels::ParameterCentred(imgui));

                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::BitCrushBitRate)],
                             b.sample_rate,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::BitCrushWet)],
                             b.wet,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::BitCrushDry)],
                             b.dry,
                             knobs::DefaultKnob(imgui, cols.highlight));

                draw_knob_joining_line(b.wet.control, b.dry.control);
                break;
            }
            case EffectType::Compressor: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.compressor, cols.back);
                auto& b = ids.compressor;

                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::CompressorThreshold)],
                             b.threshold,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::CompressorRatio)],
                             b.ratio,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::CompressorGain)],
                             b.gain,
                             knobs::BidirectionalKnob(imgui, cols.highlight));

                buttons::Toggle(g,
                                engine.processor.params[ToInt(ParamIndex::CompressorAutoGain)],
                                b.auto_gain,
                                buttons::ParameterToggleButton(imgui, cols.highlight));
                break;
            }
            case EffectType::FilterEffect: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.filter_effect, cols.back);
                auto& f = ids.filter;

                buttons::PopupWithItems(g,
                                        engine.processor.params[ToInt(ParamIndex::FilterType)],
                                        f.type.control,
                                        buttons::ParameterPopupButton(imgui));
                labels::Label(g,
                              engine.processor.params[ToInt(ParamIndex::FilterType)],
                              f.type.label,
                              labels::ParameterCentred(imgui));

                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::FilterCutoff)],
                             f.cutoff,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::FilterResonance)],
                             f.reso,
                             knobs::DefaultKnob(imgui, cols.highlight));
                if (f.using_gain) {
                    KnobAndLabel(g,
                                 engine.processor.params[ToInt(ParamIndex::FilterGain)],
                                 f.gain,
                                 knobs::DefaultKnob(imgui, cols.highlight));
                }
                break;
            }
            case EffectType::StereoWiden: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.stereo_widen, cols.back);
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::StereoWidenWidth)],
                             ids.stereo.width,
                             knobs::BidirectionalKnob(imgui, cols.highlight));
                break;
            }
            case EffectType::Chorus: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.chorus, cols.back);
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::ChorusRate)],
                             ids.chorus.rate,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::ChorusDepth)],
                             ids.chorus.depth,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::ChorusHighpass)],
                             ids.chorus.highpass,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::ChorusWet)],
                             ids.chorus.wet,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::ChorusDry)],
                             ids.chorus.dry,
                             knobs::DefaultKnob(imgui, cols.highlight));
                draw_knob_joining_line(ids.chorus.wet.control, ids.chorus.dry.control);
                break;
            }

            case EffectType::Reverb: {
                auto const cols = GetFxCols(imgui, ids.fx->type);
                do_heading(engine.processor.reverb, cols.back);
                do_all_ids(ids.reverb.ids, k_reverb_params, cols);
                break;
            }

            case EffectType::Phaser: {
                auto const cols = GetFxCols(imgui, ids.fx->type);
                do_heading(engine.processor.phaser, cols.back);
                do_all_ids(ids.phaser.ids, k_phaser_params, cols);
                break;
            }

            case EffectType::Delay: {
                auto const cols = GetFxCols(imgui, ids.fx->type);
                do_heading(engine.processor.delay, cols.back);
                auto const knob_style = knobs::DefaultKnob(imgui, cols.highlight);

                if (engine.processor.params[ToInt(ParamIndex::DelayTimeSyncSwitch)].ValueAsBool()) {
                    buttons::PopupWithItems(g,
                                            engine.processor.params[ToInt(ParamIndex::DelayTimeSyncedL)],
                                            ids.delay.left.control,
                                            buttons::ParameterPopupButton(imgui));
                    buttons::PopupWithItems(g,
                                            engine.processor.params[ToInt(ParamIndex::DelayTimeSyncedR)],
                                            ids.delay.right.control,
                                            buttons::ParameterPopupButton(imgui));
                    labels::Label(g,
                                  engine.processor.params[ToInt(ParamIndex::DelayTimeSyncedL)],
                                  ids.delay.left.label,
                                  labels::ParameterCentred(imgui));
                    labels::Label(g,
                                  engine.processor.params[ToInt(ParamIndex::DelayTimeSyncedR)],
                                  ids.delay.right.label,
                                  labels::ParameterCentred(imgui));
                } else {
                    KnobAndLabel(g,
                                 engine.processor.params[ToInt(ParamIndex::DelayTimeLMs)],
                                 ids.delay.left,
                                 knob_style);
                    KnobAndLabel(g,
                                 engine.processor.params[ToInt(ParamIndex::DelayTimeRMs)],
                                 ids.delay.right,
                                 knob_style);
                }
                draw_knob_joining_line(ids.delay.left.control, ids.delay.right.control);

                buttons::Toggle(g,
                                engine.processor.params[ToInt(ParamIndex::DelayTimeSyncSwitch)],
                                ids.delay.sync_btn,
                                buttons::ParameterToggleButton(imgui, cols.highlight));

                buttons::PopupWithItems(g,
                                        engine.processor.params[ToInt(ParamIndex::DelayMode)],
                                        ids.delay.mode.control,
                                        buttons::ParameterPopupButton(imgui));
                labels::Label(g,
                              engine.processor.params[ToInt(ParamIndex::DelayMode)],
                              ids.delay.mode.label,
                              labels::ParameterCentred(imgui));

                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::DelayFeedback)],
                             ids.delay.feedback,
                             knob_style);
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::DelayMix)],
                             ids.delay.mix,
                             knob_style);
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::DelayFilterCutoffSemitones)],
                             ids.delay.filter_cutoff,
                             knob_style);
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::DelayFilterSpread)],
                             ids.delay.filter_spread,
                             knob_style);
                draw_knob_joining_line(ids.delay.filter_cutoff.control, ids.delay.filter_spread.control);

                break;
            }

            case EffectType::ConvolutionReverb: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.convo, cols.back);

                DoImpulseResponseMenu(g, ids.convo.ir.control);
                labels::Label(g, ids.convo.ir.label, "Impulse", labels::ParameterCentred(imgui));

                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::ConvolutionReverbHighpass)],
                             ids.convo.highpass,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::ConvolutionReverbWet)],
                             ids.convo.wet,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.params[ToInt(ParamIndex::ConvolutionReverbDry)],
                             ids.convo.dry,
                             knobs::DefaultKnob(imgui, cols.highlight));

                draw_knob_joining_line(ids.convo.wet.control, ids.convo.dry.control);
                break;
            }
            case EffectType::Count: PanicIfReached(); break;
        }
    }

    if (dragging_fx_unit) {
        g->frame_output.cursor_type = CursorType::AllArrows;
        {
            auto style = buttons::EffectHeading(
                imgui,
                colours::ChangeBrightness(GetFxCols(imgui, dragging_fx_unit->fx->type).back | 0xff000000,
                                          0.7f));
            style.draw_with_overlay_graphics = true;

            auto const text = k_effect_info[ToInt(dragging_fx_unit->fx->type)].name;
            Rect btn_r {.pos = g->frame_input.cursor_pos, .size = get_heading_size(text)};
            btn_r.pos += {btn_r.h, 0};
            buttons::FakeButton(g, btn_r, text, style);
        }

        {
            auto const space_around_cursor = 100.0f;
            Rect spacer_r;
            spacer_r.pos = g->frame_input.cursor_pos;
            spacer_r.y -= space_around_cursor / 2;
            spacer_r.w = 1;
            spacer_r.h = space_around_cursor;

            auto wnd = imgui.CurrentWindow();
            if (!Rect::DoRectsIntersect(spacer_r, wnd->clipping_rect.ReducedVertically(spacer_r.h))) {
                bool const going_up = g->frame_input.cursor_pos.y < wnd->clipping_rect.CentreY();

                auto const d = 100.0f * g->frame_input.delta_time;
                imgui.WakeupAtTimedInterval(g->redraw_counter, 0.016);

                imgui.SetYScroll(wnd,
                                 Clamp(wnd->scroll_offset.y + (going_up ? -d : d), 0.0f, wnd->scroll_max.y));
            }
        }
    }

    bool effects_order_changed = false;

    if (dragging_fx_unit && imgui.WasJustDeactivated(dragging_fx_unit->id)) {
        MoveEffectToNewSlot(ordered_effects, dragging_fx_unit->fx, dragging_fx_unit->drop_slot);
        effects_order_changed = true;
        dragging_fx_unit.Clear();
    }

    {
        auto const fx_switch_board_number_width = LiveSize(imgui, FXSwitchBoardNumberWidth);
        auto const fx_switch_board_grab_region_width = LiveSize(imgui, FXSwitchBoardGrabRegionWidth);

        auto& dragging_fx = g->dragging_fx_switch;
        usize fx_index = 0;
        for (auto const slot : Range(k_num_effect_types)) {
            auto const whole_r = layout::GetRect(lay, switches[slot]);
            auto const number_r = whole_r.WithW(fx_switch_board_number_width);
            auto const slot_r = whole_r.CutLeft(fx_switch_board_number_width);
            auto const converted_slot_r = imgui.GetRegisteredAndConvertedRect(slot_r);
            auto const grabber_r = slot_r.CutLeft(slot_r.w - fx_switch_board_grab_region_width);

            labels::Label(g,
                          number_r,
                          fmt::Format(g->scratch_arena, "{}", slot + 1),
                          labels::Parameter(imgui));

            if (dragging_fx &&
                (converted_slot_r.Contains(imgui.frame_input.cursor_pos) || dragging_fx->drop_slot == slot)) {
                if (dragging_fx->drop_slot != slot)
                    imgui.frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
                dragging_fx->drop_slot = slot;
                imgui.graphics->AddRectFilled(converted_slot_r.Min(),
                                              converted_slot_r.Max(),
                                              LiveCol(imgui, UiColMap::FXButtonDropZone),
                                              corner_rounding);
            } else {
                auto fx = ordered_effects[fx_index++];
                if (dragging_fx && fx == dragging_fx->fx) fx = ordered_effects[fx_index++];

                auto style = buttons::ParameterToggleButton(imgui, GetFxCols(imgui, fx->type).button);
                style.no_tooltips = true;
                auto [changed, id] = buttons::Toggle(
                    g,
                    engine.processor.params[ToInt(k_effect_info[ToInt(fx->type)].on_param_index)],
                    slot_r,
                    k_effect_info[ToInt(fx->type)].name,
                    style);

                {
                    auto grabber_style = buttons::EffectButtonGrabber(imgui);
                    if (imgui.IsHot(id)) grabber_style.main_cols.reg = grabber_style.main_cols.hot_on;
                    buttons::FakeButton(g, grabber_r, {}, grabber_style);

                    auto converted_grabber_r = imgui.GetRegisteredAndConvertedRect(grabber_r);
                    imgui.RegisterRegionForMouseTracking(&converted_grabber_r);

                    if (converted_grabber_r.Contains(g->frame_input.cursor_pos))
                        g->frame_output.cursor_type = CursorType::AllArrows;
                }

                if (imgui.IsActive(id) && !dragging_fx) {
                    auto const click_pos = g->frame_input.mouse_buttons[0].last_pressed_point;
                    auto const current_pos = g->frame_input.cursor_pos;
                    auto const delta = current_pos - click_pos;

                    constexpr f32 k_wiggle_room = 3;
                    if (Sqrt(delta.x * delta.x + delta.y * delta.y) > k_wiggle_room) {
                        dragging_fx =
                            DraggingFX {id, fx, slot, g->frame_input.cursor_pos - converted_slot_r.pos};
                    }
                }
            }
        }

        if (dragging_fx) {
            auto style =
                buttons::ParameterToggleButton(imgui, GetFxCols(imgui, dragging_fx->fx->type).button);
            style.draw_with_overlay_graphics = true;

            Rect btn_r {layout::GetRect(lay, switches[0])};
            btn_r.pos = imgui.frame_input.cursor_pos - dragging_fx->relative_grab_point;

            auto active_fx = dragging_fx->fx;
            buttons::FakeButton(g,
                                btn_r,
                                k_effect_info[ToInt(active_fx->type)].name,
                                EffectIsOn(engine.processor.params, active_fx),
                                style);
            g->frame_output.cursor_type = CursorType::AllArrows;
        }

        if (dragging_fx && imgui.WasJustDeactivated(dragging_fx->id)) {
            MoveEffectToNewSlot(ordered_effects, dragging_fx->fx, dragging_fx->drop_slot);
            effects_order_changed = true;
            dragging_fx.Clear();
        }
    }

    if (effects_order_changed) {
        engine.processor.desired_effects_order.Store(EncodeEffectsArray(ordered_effects),
                                                     StoreMemoryOrder::Release);
        engine.processor.events_for_audio_thread.Push(EventForAudioThreadType::FxOrderChanged);
    }
}
