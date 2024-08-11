// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_layer.hpp"

#include <IconsFontAwesome5.h>

#include "framework/gui_live_edit.hpp"
#include "gui.hpp"
#include "gui_button_widgets.hpp"
#include "gui_dragger_widgets.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_envelope.hpp"
#include "gui_label_widgets.hpp"
#include "gui_peak_meter_widget.hpp"
#include "gui_standalone_popups.hpp"
#include "gui_waveform.hpp"
#include "gui_widget_compounds.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"
#include "layer_processor.hpp"
#include "plugin_instance.hpp"

namespace layer_gui {

static void LayerInstrumentMenuItems(Gui* g, LayerProcessor* layer) {
    auto const scratch_cursor = g->scratch_arena.TotalUsed();
    DEFER { g->scratch_arena.TryShrinkTotalUsed(scratch_cursor); };

    auto libs = sample_lib_server::AllLibrariesRetained(g->plugin.shared_data.sample_library_server,
                                                        g->scratch_arena);
    DEFER { sample_lib_server::ReleaseAll(libs); };

    StartFloeMenu(g);
    DEFER { EndFloeMenu(g); };

    // TODO(1.0): this is not production-ready code. We need a new powerful database-like browser GUI
    int current = 0;
    DynamicArray<String> insts {g->scratch_arena};
    DynamicArray<sample_lib::InstrumentId> inst_info {g->scratch_arena};
    dyn::Append(insts, "None");
    dyn::Append(inst_info, {});

    for (auto const i : Range(ToInt(WaveformType::Count))) {
        dyn::Append(insts, k_waveform_type_names[i]);
        dyn::Append(inst_info, {});
    }

    for (auto l : libs) {
        for (auto [key, inst_ptr] : l->insts_by_name) {
            auto const lib_id = l->Id();
            auto const inst_name = key;
            if (auto desired_sampled = layer->instrument_id.TryGet<sample_lib::InstrumentId>()) {
                if (desired_sampled->library == l->Id() && desired_sampled->inst_name == inst_name)
                    current = (int)insts.size;
            }
            dyn::Append(insts, fmt::Format(g->scratch_arena, "{}: {}", lib_id, inst_name));
            dyn::Append(inst_info,
                        sample_lib::InstrumentId {
                            .library = lib_id,
                            .inst_name = inst_name,
                        });
        }
    }

    if (DoMultipleMenuItems(g, insts, current)) {
        if (current == 0)
            LoadInstrument(g->plugin, layer->index, InstrumentId {InstrumentType::None});
        else if (current >= 1 && current <= (int)WaveformType::Count)
            LoadInstrument(g->plugin, layer->index, InstrumentId {(WaveformType)(current - 1)});
        else
            LoadInstrument(g->plugin, layer->index, InstrumentId {inst_info[(usize)current]});
    }
}

static void DoInstSelectorGUI(Gui* g, Rect r, u32 layer) {
    auto& plugin = g->plugin;
    auto& imgui = g->imgui;
    imgui.PushID("inst selector");
    DEFER { imgui.PopID(); };
    auto imgui_id = imgui.GetID((u64)layer);

    auto layer_obj = &plugin.Layer(layer);
    auto const inst_name = layer_obj->InstName();

    if (buttons::Popup(g, imgui_id, imgui_id + 1, r, inst_name, buttons::InstSelectorPopupButton(imgui))) {
        LayerInstrumentMenuItems(g, layer_obj);
        imgui.EndWindow();
    }

    if (layer_obj->instrument_id.tag == InstrumentType::None) {
        Tooltip(g, imgui_id, r, "Select the instrument for this layer"_s);
    } else {
        Tooltip(g,
                imgui_id,
                r,
                fmt::Format(g->scratch_arena,
                            "Instrument: {}\nChange or remove the instrument for this layer",
                            inst_name));
    }
}

static String GetPageTitle(PageType type) {
    switch (type) {
        case PageType::Main: return "Main";
        case PageType::Eq: return "EQ";
        case PageType::Midi: return "MIDI";
        case PageType::Lfo: return "LFO";
        case PageType::Filter: return "Filter";
        default: PanicIfReached();
    }
    return "";
}

void Layout(Gui* g,
            LayerProcessor* layer,
            LayerLayoutTempIDs& c,
            LayerLayout* layer_gui,
            f32 width,
            f32 height) {
    using enum UiSizeId;
    auto& imgui = g->imgui;
    auto& lay = g->layout;

    auto const param_popup_button_height = LiveSize(imgui, ParamPopupButtonHeight);
    auto const page_heading_height = LiveSize(imgui, Page_HeadingHeight);

    auto container = lay.CreateRootItem((LayScalar)width, (LayScalar)height, LAY_COLUMN | LAY_START);

    // selector
    {
        auto const layer_selector_box_height = LiveSize(imgui, LayerSelectorBoxHeight);
        auto const layer_selector_box_margin_l = LiveSize(imgui, LayerSelectorBoxMarginL);
        auto const layer_selector_box_margin_t = LiveSize(imgui, LayerSelectorBoxMarginT);
        auto const layer_selector_box_margin_r = LiveSize(imgui, LayerSelectorBoxMarginR);
        auto const layer_selector_box_margin_b = LiveSize(imgui, LayerSelectorBoxMarginB);
        auto const layer_selector_button_w = LiveSize(imgui, LayerSelectorButtonW);
        auto const layer_selector_box_buttons_margin_r = LiveSize(imgui, LayerSelectorBoxButtonsMarginR);

        c.selector_box =
            lay.CreateParentItem(container, 1, layer_selector_box_height, LAY_HFILL, LAY_ROW | LAY_START);
        lay.SetMargins(c.selector_box,
                       layer_selector_box_margin_l,
                       layer_selector_box_margin_t,
                       layer_selector_box_margin_r,
                       layer_selector_box_margin_b);

        c.selector_menu = lay.CreateChildItem(c.selector_box, 1, 1, LAY_FILL);
        c.selector_l = lay.CreateChildItem(c.selector_box, layer_selector_button_w, 1, LAY_VFILL);
        c.selector_r = lay.CreateChildItem(c.selector_box, layer_selector_button_w, 1, LAY_VFILL);
        c.selector_randomise = lay.CreateChildItem(c.selector_box, layer_selector_button_w, 1, LAY_VFILL);
        lay.SetRightMargin(c.selector_randomise, layer_selector_box_buttons_margin_r);
    }

    if (layer->instrument.tag == InstrumentType::None) return;

    // mixer container 1
    {
        auto const layer_mixer_container1_margin_l = LiveSize(imgui, LayerMixerContainer1MarginL);
        auto const layer_mixer_container1_margin_t = LiveSize(imgui, LayerMixerContainer1MarginT);
        auto const layer_mixer_container1_margin_r = LiveSize(imgui, LayerMixerContainer1MarginR);
        auto const layer_mixer_container1_margin_b = LiveSize(imgui, LayerMixerContainer1MarginB);
        auto subcontainer_1 = lay.CreateParentItem(container, 1, 0, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
        lay.SetMargins(subcontainer_1,
                       layer_mixer_container1_margin_l,
                       layer_mixer_container1_margin_t,
                       layer_mixer_container1_margin_r,
                       layer_mixer_container1_margin_b);

        auto const layer_volume_knob_size = LiveSize(imgui, LayerVolumeKnobSize);
        auto const layer_volume_knob_margin_r = LiveSize(imgui, LayerVolumeKnobMarginR);
        c.volume =
            lay.CreateChildItem(subcontainer_1, layer_volume_knob_size, layer_volume_knob_size, LAY_HCENTER);
        lay.SetRightMargin(c.volume, layer_volume_knob_margin_r);

        auto const layer_mute_solo_width = LiveSize(imgui, LayerMuteSoloWidth);
        auto const layer_mute_solo_height = LiveSize(imgui, LayerMuteSoloHeight);
        auto const layer_mute_solo_margin_l = LiveSize(imgui, LayerMuteSoloMarginL);
        auto const layer_mute_solo_margin_t = LiveSize(imgui, LayerMuteSoloMarginT);
        auto const layer_mute_solo_margin_r = LiveSize(imgui, LayerMuteSoloMarginR);
        auto const layer_mute_solo_margin_b = LiveSize(imgui, LayerMuteSoloMarginB);
        c.mute_solo =
            lay.CreateChildItem(subcontainer_1, layer_mute_solo_width, layer_mute_solo_height, LAY_HCENTER);
        lay.SetMargins(c.mute_solo,
                       layer_mute_solo_margin_l,
                       layer_mute_solo_margin_t,
                       layer_mute_solo_margin_r,
                       layer_mute_solo_margin_b);
    }

    // mixer container 2
    {
        auto subcontainer_2 = lay.CreateParentItem(container, 0, 0, 0, LAY_ROW | LAY_MIDDLE);
        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob1,
                                 layer->params[ToInt(LayerParamIndex::TuneSemitone)],
                                 LayerPitchMarginLR);
        auto const layer_pitch_width = LiveSize(imgui, LayerPitchWidth);
        auto const layer_pitch_height = LiveSize(imgui, LayerPitchHeight);
        auto const layer_pitch_margin_t = LiveSize(imgui, LayerPitchMarginT);
        auto const layer_pitch_margin_b = LiveSize(imgui, LayerPitchMarginB);
        lay_set_size_xy(&lay.ctx, c.knob1.control, layer_pitch_width, layer_pitch_height);
        lay.SetTopMargin(c.knob1.control, layer_pitch_margin_t);
        lay.SetBottomMargin(c.knob1.control, layer_pitch_margin_b);

        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob2,
                                 layer->params[ToInt(LayerParamIndex::TuneCents)],
                                 LayerMixerKnobGapX);
        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob3,
                                 layer->params[ToInt(LayerParamIndex::Pan)],
                                 LayerMixerKnobGapX);
    }

    auto const layer_mixer_divider_vert_margins = LiveSize(imgui, LayerMixerDividerVertMargins);
    // divider
    {
        c.divider = lay.CreateChildItem(container, 1, 1, LAY_HFILL);
        lay.SetMargins(c.divider, 0, layer_mixer_divider_vert_margins, 0, layer_mixer_divider_vert_margins);
    }

    // tabs
    {
        auto const layer_params_group_tabs_h = LiveSize(imgui, LayerParamsGroupTabsH);
        auto const layer_params_group_box_gap_x = LiveSize(imgui, LayerParamsGroupBoxGapX);
        auto const layer_params_group_tabs_gap = LiveSize(imgui, LayerParamsGroupTabsGap);
        auto tab_lay =
            lay.CreateParentItem(container, 1, layer_params_group_tabs_h, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
        lay.SetMargins(tab_lay, layer_params_group_box_gap_x, 0, layer_params_group_box_gap_x, 0);
        for (auto const i : Range(k_num_pages)) {
            auto const page_type = (PageType)i;
            auto size = draw::GetTextWidth(imgui.graphics->context->CurrentFont(), GetPageTitle(page_type));
            if (page_type == PageType::Filter || page_type == PageType::Lfo || page_type == PageType::Eq)
                size += LiveSize(imgui, LayerParamsGroupTabsIconW2);
            c.tabs[i] = lay.CreateChildItem(tab_lay,
                                            (LayScalar)(size + (f32)layer_params_group_tabs_gap),
                                            1,
                                            LAY_VFILL);
        }
    }

    // divider2
    {
        c.divider2 = lay.CreateChildItem(container, 1, 1, LAY_HFILL);
        lay.SetMargins(c.divider2, 0, layer_mixer_divider_vert_margins, 0, layer_mixer_divider_vert_margins);
    }

    {
        auto const page_heading_margin_l = LiveSize(imgui, Page_HeadingMarginL);
        auto const page_heading_margin_t = LiveSize(imgui, Page_HeadingMarginT);
        auto const page_heading_margin_b = LiveSize(imgui, Page_HeadingMarginB);
        auto set_heading_margins = [&](LayID id) {
            lay.SetMargins(id, page_heading_margin_l, page_heading_margin_t, 0, page_heading_margin_b);
        };

        auto page_container = lay.CreateParentItem(container, 1, 1, LAY_FILL, LAY_COLUMN | LAY_START);

        auto const main_envelope_h = LiveSize(imgui, Main_EnvelopeH);

        switch (layer_gui->selected_page) {
            case PageType::Main: {
                auto const main_waveform_h = LiveSize(imgui, Main_WaveformH);
                auto const main_waveform_margin_lr = LiveSize(imgui, Main_WaveformMarginLR);
                auto const main_waveform_margin_tb = LiveSize(imgui, Main_WaveformMarginTB);
                c.main.waveform = lay.CreateChildItem(page_container, 1, main_waveform_h, LAY_HFILL);
                lay.SetMargins(c.main.waveform,
                               main_waveform_margin_lr,
                               main_waveform_margin_tb,
                               main_waveform_margin_lr,
                               main_waveform_margin_tb);

                auto const main_item_margin_lr = LiveSize(imgui, Main_ItemMarginLR);
                auto const main_item_height = LiveSize(imgui, Main_ItemHeight);
                auto const main_item_gap_y = LiveSize(imgui, Main_ItemGapY);
                auto btn_container = lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW);
                lay.SetMargins(btn_container, main_item_margin_lr, 0, main_item_margin_lr, 0);
                c.main.reverse = lay.CreateChildItem(btn_container, 1, main_item_height, LAY_HFILL);
                lay.SetMargins(c.main.reverse, 0, main_item_gap_y, 0, main_item_gap_y);
                c.main.loop_mode =
                    lay.CreateChildItem(btn_container, 1, param_popup_button_height, LAY_HFILL);
                lay.SetMargins(c.main.loop_mode, 0, main_item_gap_y, 0, main_item_gap_y);

                auto const main_divider_margin_t = LiveSize(imgui, Main_DividerMarginT);
                auto const main_divider_margin_b = LiveSize(imgui, Main_DividerMarginB);
                c.main.divider = lay.CreateChildItem(page_container, 1, 1, LAY_HFILL);
                lay.SetMargins(c.main.divider, 0, main_divider_margin_t, 0, main_divider_margin_b);

                c.main.env_on = lay.CreateChildItem(page_container, 1, page_heading_height, LAY_HFILL);
                set_heading_margins(c.main.env_on);
                lay.SetBottomMargin(c.main.env_on, 0);

                auto const main_envelope_margin_lr = LiveSize(imgui, Main_EnvelopeMarginLR);
                auto const main_envelope_margin_tb = LiveSize(imgui, Main_EnvelopeMarginTB);
                c.main.envelope = lay.CreateChildItem(page_container, 1, main_envelope_h, LAY_HFILL);
                lay.SetMargins(c.main.envelope,
                               main_envelope_margin_lr,
                               main_envelope_margin_tb,
                               main_envelope_margin_lr,
                               main_envelope_margin_tb);

                break;
            }
            case PageType::Filter: {
                auto filter_heading_container =
                    lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW);
                c.filter.filter_on = lay.CreateChildItem(filter_heading_container,
                                                         1,
                                                         page_heading_height,
                                                         LAY_HFILL | LAY_TOP);
                set_heading_margins(c.filter.filter_on);
                c.filter.filter_type =
                    lay.CreateChildItem(filter_heading_container, 1, param_popup_button_height, LAY_HFILL);
                lay.SetMargins(c.filter.filter_type, 0, 0, page_heading_margin_l, 0);
                auto const filter_gap_y_before_knobs = LiveSize(imgui, Filter_GapYBeforeKnobs);
                lay.SetBottomMargin(filter_heading_container, filter_gap_y_before_knobs);

                auto filter_knobs_container =
                    lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
                LayoutParameterComponent(g,
                                         filter_knobs_container,
                                         c.filter.cutoff,
                                         layer->params[ToInt(LayerParamIndex::FilterCutoff)],
                                         Page_3KnobGapX);
                LayoutParameterComponent(g,
                                         filter_knobs_container,
                                         c.filter.reso,
                                         layer->params[ToInt(LayerParamIndex::FilterResonance)],
                                         Page_3KnobGapX);
                LayoutParameterComponent(g,
                                         filter_knobs_container,
                                         c.filter.env_amount,
                                         layer->params[ToInt(LayerParamIndex::FilterEnvAmount)],
                                         Page_3KnobGapX);

                auto const filter_envelope_margin_lr = LiveSize(imgui, Filter_EnvelopeMarginLR);
                auto const filter_envelope_margin_tb = LiveSize(imgui, Filter_EnvelopeMarginTB);
                c.filter.envelope = lay.CreateChildItem(page_container, 1, main_envelope_h, LAY_HFILL);
                lay.SetMargins(c.filter.envelope,
                               filter_envelope_margin_lr,
                               filter_envelope_margin_tb,
                               filter_envelope_margin_lr,
                               filter_envelope_margin_tb);
                break;
            }
            case PageType::Eq: {
                c.eq.on = lay.CreateChildItem(page_container, 1, page_heading_height, LAY_HFILL);
                set_heading_margins(c.eq.on);

                auto const eq_band_gap_y = LiveSize(imgui, EQ_BandGapY);
                {
                    c.eq.type[0] =
                        lay.CreateChildItem(page_container, 1, param_popup_button_height, LAY_HFILL);
                    lay.SetMargins(c.eq.type[0],
                                   page_heading_margin_l,
                                   eq_band_gap_y,
                                   page_heading_margin_l,
                                   eq_band_gap_y);

                    auto knob_container =
                        lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.freq[0],
                                             layer->params[ToInt(LayerParamIndex::EqFreq1)],
                                             Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.reso[0],
                                             layer->params[ToInt(LayerParamIndex::EqResonance1)],
                                             Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.gain[0],
                                             layer->params[ToInt(LayerParamIndex::EqGain1)],
                                             Page_3KnobGapX);
                    lay.SetBottomMargin(knob_container, eq_band_gap_y);
                }

                {
                    c.eq.type[1] =
                        lay.CreateChildItem(page_container, 1, param_popup_button_height, LAY_HFILL);
                    lay.SetMargins(c.eq.type[1],
                                   page_heading_margin_l,
                                   eq_band_gap_y,
                                   page_heading_margin_l,
                                   eq_band_gap_y);

                    auto knob_container =
                        lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.freq[1],
                                             layer->params[ToInt(LayerParamIndex::EqFreq2)],
                                             Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.reso[1],
                                             layer->params[ToInt(LayerParamIndex::EqResonance2)],
                                             Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.gain[1],
                                             layer->params[ToInt(LayerParamIndex::EqGain2)],
                                             Page_3KnobGapX);
                }

                break;
            }
            case PageType::Midi: {
                auto const midi_item_height = LiveSize(imgui, MIDI_ItemHeight);
                auto const midi_item_width = LiveSize(imgui, MIDI_ItemWidth);
                auto const midi_item_margin_lr = LiveSize(imgui, MIDI_ItemMarginLR);
                auto const midi_item_gap_y = LiveSize(imgui, MIDI_ItemGapY);
                auto layout_item_single = [&](LayID& control) {
                    control = lay.CreateChildItem(page_container, 1, midi_item_height, LAY_HFILL);
                    lay.SetMargins(control,
                                   midi_item_margin_lr,
                                   midi_item_gap_y,
                                   midi_item_margin_lr,
                                   midi_item_gap_y);
                };

                auto layout_item = [&](LayID& control, LayID& name, LayScalar height) {
                    auto parent = lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW);
                    control = lay.CreateChildItem(parent, midi_item_width, height, 0);
                    lay.SetMargins(control,
                                   midi_item_margin_lr,
                                   midi_item_gap_y,
                                   midi_item_margin_lr,
                                   midi_item_gap_y);
                    name = lay.CreateChildItem(parent, 1, height, LAY_HFILL);
                };

                layout_item(c.midi.transpose, c.midi.transpose_name, midi_item_height);
                layout_item_single(c.midi.keytrack);
                layout_item_single(c.midi.mono);
                layout_item_single(c.midi.retrig);
                auto const midi_velo_buttons_height = LiveSize(imgui, MIDI_VeloButtonsHeight);
                layout_item(c.midi.velo_buttons, c.midi.velo_name, midi_velo_buttons_height);
                break;
            }
            case PageType::Lfo: {
                c.lfo.on = lay.CreateChildItem(page_container, 1, page_heading_height, LAY_HFILL);
                set_heading_margins(c.lfo.on);

                auto const lfo_item_width = LiveSize(imgui, LFO_ItemWidth);
                auto const lfo_item_margin_l = LiveSize(imgui, LFO_ItemMarginL);
                auto const lfo_item_gap_y = LiveSize(imgui, LFO_ItemGapY);
                auto const lfo_item_margin_r = LiveSize(imgui, LFO_ItemMarginR);
                auto const lfo_gap_y_before_knobs = LiveSize(imgui, LFO_GapYBeforeKnobs);
                auto layout_item = [&](LayID& control, LayID& name) {
                    auto parent = lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW);
                    control = lay.CreateChildItem(parent, lfo_item_width, param_popup_button_height, 0);
                    lay.SetMargins(control,
                                   lfo_item_margin_l,
                                   lfo_item_gap_y,
                                   lfo_item_margin_r,
                                   lfo_item_gap_y);
                    name = lay.CreateChildItem(parent, 1, param_popup_button_height, LAY_HFILL);
                };

                layout_item(c.lfo.target, c.lfo.target_name);
                layout_item(c.lfo.shape, c.lfo.shape_name);
                layout_item(c.lfo.mode, c.lfo.mode_name);

                auto knob_container =
                    lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
                lay.SetTopMargin(knob_container, lfo_gap_y_before_knobs);

                LayoutParameterComponent(g,
                                         knob_container,
                                         c.lfo.amount,
                                         layer->params[ToInt(LayerParamIndex::LfoAmount)],
                                         Page_2KnobGapX);

                auto& rate_param =
                    layer->params[layer->params[ToInt(LayerParamIndex::LfoSyncSwitch)].ValueAsBool()
                                      ? ToInt(LayerParamIndex::LfoRateTempoSynced)
                                      : ToInt(LayerParamIndex::LfoRateHz)];
                LayoutParameterComponent(g, knob_container, c.lfo.rate, rate_param, Page_2KnobGapX, true);
                break;
            }
            case PageType::Count: PanicIfReached();
        }
    }
}

static void DrawSelectorProgressBar(imgui::Context const& imgui, Rect r, f32 load_percent) {
    auto min = r.Min();
    auto max = f32x2 {r.x + Max(4.0f, r.w * load_percent), r.Bottom()};
    auto col = LiveCol(imgui, UiColMap::LayerSelectorMenuLoading);
    auto const rounding = LiveSize(imgui, UiSizeId::CornerRounding);
    imgui.graphics->AddRectFilled(min, max, col, rounding);
}

void Draw(Gui* g,
          PluginInstance* plugin,
          Rect r,
          LayerProcessor* layer,
          LayerLayoutTempIDs& c,
          LayerLayout* layer_gui) {
    using enum UiSizeId;

    auto& lay = g->layout;
    auto& imgui = g->imgui;

    auto settings = FloeWindowSettings(imgui, [&](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto desired_lib_name = layer->LibId();
        if (!desired_lib_name) return;
        auto const get_background_uvs = [&](LibraryImages const& imgs,
                                            Rect r,
                                            imgui::Window* window,
                                            f32x2& out_min_uv,
                                            f32x2& out_max_uv) {
            auto const whole_uv = GetMaxUVToMaintainAspectRatio(*imgs.background, r.size);
            auto const left_margin = r.x - window->parent_window->bounds.x;
            auto const top_margin = r.y - window->parent_window->bounds.y;

            out_min_uv = {whole_uv.x * (left_margin / r.size.x), whole_uv.y * (top_margin / r.size.y)};
            out_max_uv = {whole_uv.x * (r.w + left_margin) / r.size.x,
                          whole_uv.y * (r.h + top_margin) / r.size.y};
        };

        auto const& r = window->bounds;

        auto background_lib =
            sample_lib_server::FindLibraryRetained(g->plugin.shared_data.sample_library_server,
                                                   *desired_lib_name);
        DEFER { background_lib.Release(); };

        auto const panel_rounding = LiveSize(imgui, UiSizeId::BlurredPanelRounding);

        if (background_lib && !g->settings.settings.gui.high_contrast_gui) {
            auto imgs = LoadLibraryBackgroundAndIconIfNeeded(g, *background_lib);
            if (imgs.blurred_background) {
                if (auto tex = g->frame_input.graphics_ctx->GetTextureFromImage(*imgs.blurred_background)) {
                    f32x2 min_uv;
                    f32x2 max_uv;
                    get_background_uvs(imgs, r, window, min_uv, max_uv);
                    imgui.graphics->AddImageRounded(*tex,
                                                    r.Min(),
                                                    r.Max(),
                                                    min_uv,
                                                    max_uv,
                                                    LiveCol(imgui, UiColMap::BlurredImageDrawColour),
                                                    panel_rounding);
                }

                {
                    int const vtx_idx_0 = imgui.graphics->vtx_buffer.size;
                    auto const pos = r.Min() + f32x2 {1, 1};
                    auto const size = f32x2 {r.w, r.h / 2} - f32x2 {2, 2};
                    imgui.graphics->AddRectFilled(pos, pos + size, 0xffffffff, panel_rounding);
                    int const vtx_idx_1 = imgui.graphics->vtx_buffer.size;
                    imgui.graphics->AddRectFilled(pos, pos + size, 0xffffffff, panel_rounding);
                    int const vtx_idx_2 = imgui.graphics->vtx_buffer.size;

                    graphics::DrawList::ShadeVertsLinearColorGradientSetAlpha(
                        imgui.graphics,
                        vtx_idx_0,
                        vtx_idx_1,
                        pos,
                        pos + f32x2 {0, size.y},
                        LiveCol(imgui, UiColMap::BlurredImageGradientOverlay),
                        0);
                    graphics::DrawList::ShadeVertsLinearColorGradientSetAlpha(
                        imgui.graphics,
                        vtx_idx_1,
                        vtx_idx_2,
                        pos + f32x2 {size.x, 0},
                        pos + f32x2 {size.x, size.y},
                        LiveCol(imgui, UiColMap::BlurredImageGradientOverlay),
                        0);
                }

                imgui.graphics->AddRect(r.Min(),
                                        r.Max(),
                                        LiveCol(imgui, UiColMap::BlurredImageBorder),
                                        panel_rounding);
            }
        }
    });
    settings.flags |= imgui::WindowFlags_NoScrollbarY;
    imgui.BeginWindow(settings, imgui.GetID(layer), r);
    DEFER { imgui.EndWindow(); };

    auto const draw_divider = [&](LayID id) {
        auto line_r = lay.GetRect(id);
        imgui.RegisterAndConvertRect(&line_r);
        imgui.graphics->AddLine({line_r.x, line_r.Bottom()},
                                {line_r.Right(), line_r.Bottom()},
                                LiveCol(imgui, UiColMap::LayerDividerLine));
    };

    // Inst selector
    {
        auto selector_left_id = imgui.GetID("SelcL");
        auto selector_right_id = imgui.GetID("SelcR");
        auto selector_menu_r = lay.GetRect(c.selector_menu);
        auto selector_left_r = lay.GetRect(c.selector_l);
        auto selector_right_r = lay.GetRect(c.selector_r);

        bool const should_highlight = false;
        // TODO(1.0): how are we going to handle the new dynamics knob changes
#if 0
        if (auto inst = layer->instrument.GetNullable<AssetReference<LoadedInstrument>>();
            inst && (*inst) &&
            (g->dynamics_slider_is_held ||
             CcControllerMovedParamRecently(g->plugin.processor, ParamIndex::MasterDynamics)) &&
            ((*inst)->instrument.flags & FloeLibrary::Instrument::HasDynamicLayers)) {
            should_highlight = true;
        }
#endif

        auto const registered_selector_box_r =
            imgui.GetRegisteredAndConvertedRect(lay.GetRect(c.selector_box));
        {
            auto const rounding = LiveSize(imgui, UiSizeId::CornerRounding);
            auto const col = should_highlight ? LiveCol(imgui, UiColMap::LayerSelectorMenuBackHighlight)
                                              : LiveCol(imgui, UiColMap::LayerSelectorMenuBack);
            imgui.graphics->AddRectFilled(registered_selector_box_r.Min(),
                                          registered_selector_box_r.Max(),
                                          col,
                                          rounding);
        }

        DoInstSelectorGUI(g, selector_menu_r, layer->index);
        if (auto percent =
                g->plugin.sample_lib_server_async_channel.instrument_loading_percents[(usize)layer->index]
                    .Load();
            percent != -1) {
            f32 const load_percent = (f32)percent / 100.0f;
            DrawSelectorProgressBar(imgui, registered_selector_box_r, load_percent);
            g->imgui.WakeupAtTimedInterval(g->redraw_counter, 0.1);
        }

        if (buttons::Button(g,
                            selector_left_id,
                            selector_left_r,
                            ICON_FA_CARET_LEFT,
                            buttons::IconButton(imgui)))
            CycleInstrument(*plugin, layer->index, CycleDirection::Backward);
        if (buttons::Button(g,
                            selector_right_id,
                            selector_right_r,
                            ICON_FA_CARET_RIGHT,
                            buttons::IconButton(imgui))) {
            CycleInstrument(*plugin, layer->index, CycleDirection::Forward);
        }
        {
            auto rand_id = imgui.GetID("Rand");
            auto rand_r = lay.GetRect(c.selector_randomise);
            if (buttons::Button(g,
                                rand_id,
                                rand_r,
                                ICON_FA_RANDOM,
                                buttons::IconButton(imgui).WithRandomiseIconScaling())) {
                LoadRandomInstrument(*plugin, layer->index, false);
            }
            Tooltip(g, rand_id, rand_r, "Load a random instrument"_s);
        }

        Tooltip(g, selector_left_id, selector_left_r, "Load the previous instrument"_s);
        Tooltip(g, selector_right_id, selector_right_r, "Load the next instrument"_s);
    }

    if (layer->instrument.tag == InstrumentType::None) return;

    // divider
    draw_divider(c.divider);

    auto const volume_knob_r = lay.GetRect(c.volume);
    // level meter
    {
        auto const layer_peak_meter_width = LiveSize(imgui, LayerPeakMeterWidth);
        auto const layer_peak_meter_height = LiveSize(imgui, LayerPeakMeterHeight);
        auto const layer_peak_meter_bottom_gap = LiveSize(imgui, LayerPeakMeterBottomGap);

        Rect const peak_meter_r {
            volume_knob_r.Centre().x - layer_peak_meter_width / 2,
            volume_knob_r.y + (volume_knob_r.h - (layer_peak_meter_height + layer_peak_meter_bottom_gap)),
            layer_peak_meter_width,
            layer_peak_meter_height - layer_peak_meter_bottom_gap};
        auto const& processor = plugin->processor.layer_processors[(usize)layer->index];
        peak_meters::PeakMeter(g, peak_meter_r, processor.peak_meter, false);
    }

    // volume
    {
        auto const volume_name_h = lay.GetRect(c.knob1.label).h;
        auto const volume_name_y_gap = LiveSize(imgui, LayerVolumeNameGapY);
        Rect const volume_name_r {volume_knob_r.x,
                                  volume_knob_r.Bottom() - volume_name_h + volume_name_y_gap,
                                  volume_knob_r.w,
                                  volume_name_h};

        KnobAndLabel(g,
                     layer->params[ToInt(LayerParamIndex::Volume)],
                     volume_knob_r,
                     volume_name_r,
                     knobs::DefaultKnob(imgui));
    }

    // mute and solo
    {
        auto mute_solo_r = lay.GetRect(c.mute_solo);
        Rect const mute_r = {mute_solo_r.x, mute_solo_r.y, mute_solo_r.w / 2, mute_solo_r.h};
        Rect const solo_r = {mute_solo_r.x + mute_solo_r.w / 2,
                             mute_solo_r.y,
                             mute_solo_r.w / 2,
                             mute_solo_r.h};

        auto const col_border = LiveCol(imgui, UiColMap::LayerMuteSoloBorder);
        auto const col_background = LiveCol(imgui, UiColMap::LayerMuteSoloBackground);
        auto const rounding = LiveSize(imgui, UiSizeId::CornerRounding);
        auto reg_mute_solo_r = imgui.GetRegisteredAndConvertedRect(mute_solo_r);
        auto reg_mute_r = imgui.GetRegisteredAndConvertedRect(mute_r);
        imgui.graphics->AddRectFilled(reg_mute_solo_r.Min(), reg_mute_solo_r.Max(), col_background, rounding);
        imgui.graphics->AddLine({reg_mute_r.Right(), reg_mute_r.y},
                                {reg_mute_r.Right(), reg_mute_r.Bottom()},
                                col_border);

        buttons::Toggle(g,
                        layer->params[ToInt(LayerParamIndex::Mute)],
                        mute_r,
                        "M",
                        buttons::IconButton(imgui));
        buttons::Toggle(g,
                        layer->params[ToInt(LayerParamIndex::Solo)],
                        solo_r,
                        "S",
                        buttons::IconButton(imgui));
    }

    // knobs
    {
        auto semitone_style = draggers::DefaultStyle(imgui);
        semitone_style.always_show_plus = true;
        draggers::Dragger(g,
                          layer->params[ToInt(LayerParamIndex::TuneSemitone)],
                          c.knob1.control,
                          semitone_style);
        labels::Label(g,
                      layer->params[ToInt(LayerParamIndex::TuneSemitone)],
                      c.knob1.label,
                      labels::ParameterCentred(imgui));

        KnobAndLabel(g,
                     layer->params[ToInt(LayerParamIndex::TuneCents)],
                     c.knob2,
                     knobs::BidirectionalKnob(imgui));
        KnobAndLabel(g, layer->params[ToInt(LayerParamIndex::Pan)], c.knob3, knobs::BidirectionalKnob(imgui));
    }

    draw_divider(c.divider2);

    // current page
    switch (layer_gui->selected_page) {
        case PageType::Main: {
            // waveform
            {
                GUIDoSampleWaveform(g, layer, lay.GetRect(c.main.waveform));

                buttons::Toggle(g,
                                layer->params[ToInt(LayerParamIndex::Reverse)],
                                c.main.reverse,
                                buttons::ParameterToggleButton(imgui));

                buttons::PopupWithItems(g,
                                        layer->params[ToInt(LayerParamIndex::LoopMode)],
                                        c.main.loop_mode,
                                        buttons::ParameterPopupButton(imgui));
            }

            draw_divider(c.main.divider);

            // env
            {
                buttons::Toggle(g,
                                layer->params[ToInt(LayerParamIndex::VolEnvOn)],
                                c.main.env_on,
                                buttons::LayerHeadingButton(imgui));
                bool const env_on = layer->params[ToInt(LayerParamIndex::VolEnvOn)].ValueAsBool();
                GUIDoEnvelope(g,
                              layer,
                              lay.GetRect(c.main.envelope),
                              !env_on,
                              {LayerParamIndex::VolumeAttack,
                               LayerParamIndex::VolumeDecay,
                               LayerParamIndex::VolumeSustain,
                               LayerParamIndex::VolumeRelease},
                              GuiEnvelopeType::Volume);
            }

            break;
        }
        case PageType::Filter: {
            bool const greyed_out = !layer->params[ToInt(LayerParamIndex::FilterOn)].ValueAsBool();
            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::FilterOn)],
                            c.filter.filter_on,
                            buttons::LayerHeadingButton(imgui));

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::FilterType)],
                                    c.filter.filter_type,
                                    buttons::ParameterPopupButton(imgui, greyed_out));

            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::FilterCutoff)],
                         c.filter.cutoff,
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::FilterResonance)],
                         c.filter.reso,
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::FilterEnvAmount)],
                         c.filter.env_amount,
                         knobs::BidirectionalKnob(imgui),
                         greyed_out);

            GUIDoEnvelope(g,
                          layer,
                          lay.GetRect(c.filter.envelope),
                          greyed_out ||
                              (layer->params[ToInt(LayerParamIndex::FilterEnvAmount)].LinearValue() == 0),
                          {LayerParamIndex::FilterAttack,
                           LayerParamIndex::FilterDecay,
                           LayerParamIndex::FilterSustain,
                           LayerParamIndex::FilterRelease},
                          GuiEnvelopeType::Filter);

            break;
        }
        case PageType::Eq: {
            bool const greyed_out = !layer->params[ToInt(LayerParamIndex::EqOn)].ValueAsBool();
            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::EqOn)],
                            lay.GetRect(c.eq.on),
                            buttons::LayerHeadingButton(imgui));

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::EqType1)],
                                    lay.GetRect(c.eq.type[0]),
                                    buttons::ParameterPopupButton(imgui, greyed_out));

            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqFreq1)],
                         c.eq.freq[0],
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqResonance1)],
                         c.eq.reso[0],
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqGain1)],
                         c.eq.gain[0],
                         knobs::BidirectionalKnob(imgui),
                         greyed_out);

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::EqType2)],
                                    lay.GetRect(c.eq.type[1]),
                                    buttons::ParameterPopupButton(imgui, greyed_out));

            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqFreq2)],
                         c.eq.freq[1],
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqResonance2)],
                         c.eq.reso[1],
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqGain2)],
                         c.eq.gain[1],
                         knobs::BidirectionalKnob(imgui),
                         greyed_out);

            break;
        }
        case PageType::Midi: {
            draggers::Dragger(g,
                              layer->params[ToInt(LayerParamIndex::MidiTranspose)],
                              c.midi.transpose,
                              draggers::DefaultStyle(imgui));
            labels::Label(g,
                          layer->params[ToInt(LayerParamIndex::MidiTranspose)],
                          c.midi.transpose_name,
                          labels::Parameter(imgui));
            {
                auto const label_id = imgui.GetID("transp");
                auto const label_r = lay.GetRect(c.midi.transpose_name);
                imgui.ButtonBehavior(imgui.GetRegisteredAndConvertedRect(label_r), label_id, {});
                Tooltip(g,
                        label_id,
                        label_r,
                        layer->params[ToInt(LayerParamIndex::MidiTranspose)].info.tooltip);
                if (imgui.IsHot(label_id)) imgui.frame_output.cursor_type = CursorType::Default;
            }

            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::Keytrack)],
                            c.midi.keytrack,
                            buttons::MidiButton(imgui));
            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::Monophonic)],
                            c.midi.mono,
                            buttons::MidiButton(imgui));
            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::CC64Retrigger)],
                            c.midi.retrig,
                            buttons::MidiButton(imgui));

            {
                static constexpr auto k_num_btns = ToInt(param_values::VelocityMappingMode::Count);
                static_assert(k_num_btns == 6, "");
                auto const btn_gap = LiveSize(imgui, MIDI_VeloButtonsSpacing);
                auto const whole_velo_r =
                    lay.GetRect(c.midi.velo_buttons).CutRight(btn_gap * 2).CutBottom(btn_gap);

                for (auto const btn_ind : Range(k_num_btns)) {
                    bool state = ToInt(layer->GetVelocityMode()) == btn_ind;
                    auto imgui_id = imgui.GetID(layer_gui::k_velo_btn_tooltips[(usize)btn_ind]);

                    Rect btn_r {whole_velo_r.x + (whole_velo_r.w / 3) * (btn_ind % 3),
                                whole_velo_r.y + (whole_velo_r.h / 2) * (f32)(int)(btn_ind / 3),
                                whole_velo_r.w / 3,
                                whole_velo_r.h / 2};

                    btn_r.x += btn_gap * (btn_ind % 3);
                    btn_r.y += btn_gap * (f32)(int)(btn_ind / 3);

                    if (buttons::Toggle(
                            g,
                            imgui_id,
                            btn_r,
                            state,
                            "",
                            buttons::VelocityButton(imgui, (param_values::VelocityMappingMode)btn_ind))) {
                        auto velo_param_id =
                            ParamIndexFromLayerParamIndex(layer->index, LayerParamIndex::VelocityMapping);
                        SetParameterValue(g->plugin.processor, velo_param_id, (f32)btn_ind, {});
                    }

                    Tooltip(g, imgui_id, btn_r, layer_gui::k_velo_btn_tooltips[(usize)btn_ind]);
                }

                labels::Label(g,
                              layer->params[ToInt(LayerParamIndex::VelocityMapping)],
                              c.midi.velo_name,
                              labels::Parameter(imgui));

                auto const label_id = imgui.GetID("velobtn");
                auto const label_r = lay.GetRect(c.midi.velo_name);
                imgui.ButtonBehavior(imgui.GetRegisteredAndConvertedRect(label_r), label_id, {});
                Tooltip(g,
                        label_id,
                        label_r,
                        "The velocity mapping switches allow you to create presets that change timbre from "
                        "low velocity to high velocity. To do this, 2 or more layers should be used, and "
                        "each layer should be given a different velocity mapping option so that the loudness "
                        "of each layer is controlled by the MIDI velocity."_s);
                if (imgui.IsHot(label_id)) imgui.frame_output.cursor_type = CursorType::Default;
            }

            break;
        }
        case PageType::Lfo: {
            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::LfoOn)],
                            c.lfo.on,
                            buttons::LayerHeadingButton(imgui));
            auto const greyed_out = !layer->params[ToInt(LayerParamIndex::LfoOn)].ValueAsBool();

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::LfoDestination)],
                                    c.lfo.target,
                                    buttons::ParameterPopupButton(imgui, greyed_out));
            labels::Label(g,
                          layer->params[ToInt(LayerParamIndex::LfoDestination)],
                          c.lfo.target_name,
                          labels::Parameter(imgui));

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::LfoRestart)],
                                    c.lfo.mode,
                                    buttons::ParameterPopupButton(imgui, greyed_out));
            labels::Label(g,
                          layer->params[ToInt(LayerParamIndex::LfoRestart)],
                          c.lfo.mode_name,
                          labels::Parameter(imgui));

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::LfoShape)],
                                    c.lfo.shape,
                                    buttons::ParameterPopupButton(imgui, greyed_out));
            labels::Label(g,
                          layer->params[ToInt(LayerParamIndex::LfoShape)],
                          c.lfo.shape_name,
                          labels::Parameter(imgui));

            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::LfoAmount)],
                         c.lfo.amount,
                         knobs::BidirectionalKnob(imgui),
                         greyed_out);

            Parameter const* rate_param;
            if (layer->params[ToInt(LayerParamIndex::LfoSyncSwitch)].ValueAsBool()) {
                rate_param = &layer->params[ToInt(LayerParamIndex::LfoRateTempoSynced)];
                buttons::PopupWithItems(g,
                                        *rate_param,
                                        c.lfo.rate.control,
                                        buttons::ParameterPopupButton(imgui, greyed_out));
            } else {
                rate_param = &layer->params[ToInt(LayerParamIndex::LfoRateHz)];
                knobs::Knob(g,
                            *rate_param,
                            c.lfo.rate.control,
                            knobs::DefaultKnob(imgui).GreyedOut(greyed_out));
            }

            auto const rate_name_r = lay.GetRect(c.lfo.rate.label);
            labels::Label(g, *rate_param, rate_name_r, labels::ParameterCentred(imgui, greyed_out));

            auto const lfo_sync_switch_width = LiveSize(imgui, LFO_SyncSwitchWidth);
            auto const lfo_sync_switch_height = LiveSize(imgui, LFO_SyncSwitchHeight);
            auto const lfo_sync_switch_gap_y = LiveSize(imgui, LFO_SyncSwitchGapY);

            Rect const sync_r {rate_name_r.x + rate_name_r.w / 2 - lfo_sync_switch_width / 2,
                               rate_name_r.Bottom() + lfo_sync_switch_gap_y,
                               lfo_sync_switch_width,
                               lfo_sync_switch_height};
            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::LfoSyncSwitch)],
                            sync_r,
                            buttons::ParameterToggleButton(imgui));

            break;
        }
        case PageType::Count: PanicIfReached();
    }

    // tabs
    for (auto const i : Range(k_num_pages)) {
        auto const page_type = (PageType)i;
        bool state = page_type == layer_gui->selected_page;
        auto const id = imgui.GetID((u64)i);
        auto const tab_r = lay.GetRect(c.tabs[i]);
        auto const name {GetPageTitle(page_type)};
        bool const has_dot =
            (page_type == PageType::Filter &&
             layer->params[ToInt(LayerParamIndex::FilterOn)].ValueAsBool()) ||
            (page_type == PageType::Lfo && layer->params[ToInt(LayerParamIndex::LfoOn)].ValueAsBool()) ||
            (page_type == PageType::Eq && layer->params[ToInt(LayerParamIndex::EqOn)].ValueAsBool());
        if (buttons::Toggle(g, id, tab_r, state, name, buttons::LayerTabButton(imgui, has_dot)))
            layer_gui->selected_page = page_type;
        Tooltip(g, id, tab_r, fmt::Format(g->scratch_arena, "Open {} tab", name));
    }

    // overlay
    auto const& layer_processor = plugin->processor.layer_processors[(usize)layer->index];
    if (layer_processor.is_silent.Load()) {
        auto pos = imgui.curr_window->unpadded_bounds.pos;
        imgui.graphics->AddRectFilled(pos, pos + imgui.Size(), LiveCol(imgui, UiColMap::LayerMutedOverlay));
    }
}

} // namespace layer_gui
