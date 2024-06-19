// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_layer.hpp"

#include "gui.hpp"
#include "gui_button_widgets.hpp"
#include "gui_dragger_widgets.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_editor_ui_style.hpp"
#include "gui_envelope.hpp"
#include "gui_label_widgets.hpp"
#include "gui_peak_meter_widget.hpp"
#include "gui_standalone_popups.hpp"
#include "gui_waveform.hpp"
#include "gui_widget_compounds.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"
#include "icons-fa/IconsFontAwesome5.h"
#include "layer_processor.hpp"
#include "plugin_instance.hpp"

namespace layer_gui {

static void LayerInstrumentMenuItems(Gui* g, PluginInstance::Layer* layer) {
    auto const scratch_cursor = g->scratch_arena.TotalUsed();
    DEFER { g->scratch_arena.TryShrinkTotalUsed(scratch_cursor); };

    auto libs = g->plugin.shared_data.available_libraries.AllRetained(g->scratch_arena);
    DEFER { sample_lib_loader::ReleaseAll(libs); };

    StartFloeMenu(g);
    DEFER { EndFloeMenu(g); };

    // TODO: this is not production-ready code. The plan is to have an instrument selector based on sqlite
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
            auto const lib_name = l->name;
            auto const inst_name = key;
            if (auto desired_sampled = layer->desired_instrument.TryGet<sample_lib::InstrumentId>()) {
                if (desired_sampled->library_name == lib_name && desired_sampled->inst_name == inst_name)
                    current = (int)insts.size;
            }
            // if (lib_name == "Arctic Strings" || lib_name == "Terracotta" ||
            //     lib_name == "Music Box Suite Free" || lib_name == "Wraith Demo" ||
            //     lib_name == "New Wraith Demo") {
            dyn::Append(insts, fmt::Format(g->scratch_arena, "{}: {}", lib_name, inst_name));
            dyn::Append(inst_info,
                        sample_lib::InstrumentId {
                            .library_name = lib_name,
                            .inst_name = inst_name,
                        });
            // }
        }
    }

    if (DoMultipleMenuItems(g, insts, current)) {
        if (current == 0)
            auto _ = SetInstrument(g->plugin, layer->index, InstrumentId {InstrumentType::None});
        else if (current >= 1 && current <= (int)WaveformType::Count)
            auto _ = SetInstrument(g->plugin, layer->index, InstrumentId {(WaveformType)(current - 1)});
        else
            auto _ = SetInstrument(g->plugin, layer->index, InstrumentId {inst_info[(usize)current]});
    }
}

static void DoInstSelectorGUI(Gui* g, Rect r, u32 layer) {
    auto& plugin = g->plugin;
    auto& imgui = g->imgui;
    imgui.PushID("inst selector");
    DEFER { imgui.PopID(); };
    auto imgui_id = imgui.GetID((u64)layer);

    auto layer_obj = &plugin.layers[layer];
    auto const inst_name = layer_obj->InstName();

    if (buttons::Popup(g, imgui_id, imgui_id + 1, r, inst_name, buttons::InstSelectorPopupButton(imgui))) {
        LayerInstrumentMenuItems(g, layer_obj);
        imgui.EndWindow();
    }

    if (layer_obj->desired_instrument.tag == InstrumentType::None) {
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
            PluginInstance::Layer* layer,
            LayerLayoutTempIDs& c,
            LayerLayout* layer_gui,
            f32 width,
            f32 height) {
    auto& imgui = g->imgui;
    auto& lay = g->layout;

#define GUI_SIZE(cat, n, v, u) [[maybe_unused]] const auto cat##n = editor::GetSize(imgui, UiSizeId::cat##n);
#include SIZES_DEF_FILENAME
#undef GUI_SIZE

    auto container = lay.CreateRootItem((LayScalar)width, (LayScalar)height, LAY_COLUMN | LAY_START);

    // selector
    {
        c.selector_box =
            lay.CreateParentItem(container, 1, LayerSelectorBoxHeight, LAY_HFILL, LAY_ROW | LAY_START);
        lay.SetMargins(c.selector_box,
                       LayerSelectorBoxMarginL,
                       LayerSelectorBoxMarginT,
                       LayerSelectorBoxMarginR,
                       LayerSelectorBoxMarginB);

        c.selector_menu = lay.CreateChildItem(c.selector_box, 1, 1, LAY_FILL);
        c.selector_l = lay.CreateChildItem(c.selector_box, LayerSelectorButtonW, 1, LAY_VFILL);
        c.selector_r = lay.CreateChildItem(c.selector_box, LayerSelectorButtonW, 1, LAY_VFILL);
        c.selector_randomise = lay.CreateChildItem(c.selector_box, LayerSelectorButtonW, 1, LAY_VFILL);
        lay.SetRightMargin(c.selector_randomise, LayerSelectorBoxButtonsMarginR);
    }

    if (layer->instrument.tag == InstrumentType::None) return;

    // mixer container 1
    {
        auto subcontainer_1 = lay.CreateParentItem(container, 1, 0, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
        lay.SetMargins(subcontainer_1,
                       LayerMixerContainer1MarginL,
                       LayerMixerContainer1MarginT,
                       LayerMixerContainer1MarginR,
                       LayerMixerContainer1MarginB);

        c.volume = lay.CreateChildItem(subcontainer_1, LayerVolumeKnobSize, LayerVolumeKnobSize, LAY_HCENTER);
        lay.SetRightMargin(c.volume, LayerVolumeKnobMarginR);
        c.mute_solo =
            lay.CreateChildItem(subcontainer_1, LayerMuteSoloWidth, LayerMuteSoloHeight, LAY_HCENTER);
        lay.SetMargins(c.mute_solo,
                       LayerMuteSoloMarginL,
                       LayerMuteSoloMarginT,
                       LayerMuteSoloMarginR,
                       LayerMuteSoloMarginB);
    }

    // mixer container 2
    {
        auto subcontainer_2 = lay.CreateParentItem(container, 0, 0, 0, LAY_ROW | LAY_MIDDLE);
        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob1,
                                 layer->processor.params[ToInt(LayerParamIndex::TuneSemitone)],
                                 UiSizeId::LayerPitchMarginLR);
        lay_set_size_xy(&lay.ctx, c.knob1.control, LayerPitchWidth, LayerPitchHeight);
        lay.SetTopMargin(c.knob1.control, LayerPitchMarginT);
        lay.SetBottomMargin(c.knob1.control, LayerPitchMarginB);

        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob2,
                                 layer->processor.params[ToInt(LayerParamIndex::TuneCents)],
                                 UiSizeId::LayerMixerKnobGapX);
        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob3,
                                 layer->processor.params[ToInt(LayerParamIndex::Pan)],
                                 UiSizeId::LayerMixerKnobGapX);
    }

    // divider
    {
        c.divider = lay.CreateChildItem(container, 1, 1, LAY_HFILL);
        lay.SetMargins(c.divider, 0, LayerMixerDividerVertMargins, 0, LayerMixerDividerVertMargins);
    }

    // tabs
    {
        auto tab_lay =
            lay.CreateParentItem(container, 1, LayerParamsGroupTabsH, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
        lay.SetMargins(tab_lay, LayerParamsGroupBoxGapX, 0, LayerParamsGroupBoxGapX, 0);
        for (auto const i : Range(k_num_pages)) {
            auto const page_type = (PageType)i;
            auto size = draw::GetTextWidth(imgui.graphics->context->CurrentFont(), GetPageTitle(page_type));
            if (page_type == PageType::Filter || page_type == PageType::Lfo || page_type == PageType::Eq)
                size += editor::GetSize(imgui, UiSizeId::LayerParamsGroupTabsIconW2);
            c.tabs[i] =
                lay.CreateChildItem(tab_lay, (LayScalar)(size + (f32)LayerParamsGroupTabsGap), 1, LAY_VFILL);
        }
    }

    // divider2
    {
        c.divider2 = lay.CreateChildItem(container, 1, 1, LAY_HFILL);
        lay.SetMargins(c.divider2, 0, LayerMixerDividerVertMargins, 0, LayerMixerDividerVertMargins);
    }

    {
        auto set_heading_margins = [&](LayID id) {
            lay.SetMargins(id, Page_HeadingMarginL, Page_HeadingMarginT, 0, Page_HeadingMarginB);
        };

        auto page_container = lay.CreateParentItem(container, 1, 1, LAY_FILL, LAY_COLUMN | LAY_START);

        switch (layer_gui->selected_page) {
            case PageType::Main: {
                c.main.waveform = lay.CreateChildItem(page_container, 1, Main_WaveformH, LAY_HFILL);
                lay.SetMargins(c.main.waveform,
                               Main_WaveformMarginLR,
                               Main_WaveformMarginTB,
                               Main_WaveformMarginLR,
                               Main_WaveformMarginTB);

                auto btn_container = lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW);
                lay.SetMargins(btn_container, Main_ItemMarginLR, 0, Main_ItemMarginLR, 0);
                c.main.reverse = lay.CreateChildItem(btn_container, 1, Main_ItemHeight, LAY_HFILL);
                lay.SetMargins(c.main.reverse, 0, Main_ItemGapY, 0, Main_ItemGapY);
                c.main.loop_mode = lay.CreateChildItem(btn_container, 1, ParamPopupButtonHeight, LAY_HFILL);
                lay.SetMargins(c.main.loop_mode, 0, Main_ItemGapY, 0, Main_ItemGapY);

                c.main.divider = lay.CreateChildItem(page_container, 1, 1, LAY_HFILL);
                lay.SetMargins(c.main.divider, 0, Main_DividerMarginT, 0, Main_DividerMarginB);

                c.main.env_on = lay.CreateChildItem(page_container, 1, Page_HeadingHeight, LAY_HFILL);
                set_heading_margins(c.main.env_on);
                lay.SetBottomMargin(c.main.env_on, 0);

                c.main.envelope = lay.CreateChildItem(page_container, 1, Main_EnvelopeH, LAY_HFILL);
                lay.SetMargins(c.main.envelope,
                               Main_EnvelopeMarginLR,
                               Main_EnvelopeMarginTB,
                               Main_EnvelopeMarginLR,
                               Main_EnvelopeMarginTB);

                break;
            }
            case PageType::Filter: {
                auto filter_heading_container =
                    lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW);
                c.filter.filter_on =
                    lay.CreateChildItem(filter_heading_container, 1, Page_HeadingHeight, LAY_HFILL | LAY_TOP);
                set_heading_margins(c.filter.filter_on);
                c.filter.filter_type =
                    lay.CreateChildItem(filter_heading_container, 1, ParamPopupButtonHeight, LAY_HFILL);
                lay.SetMargins(c.filter.filter_type, 0, 0, Page_HeadingMarginL, 0);
                lay.SetBottomMargin(filter_heading_container, Filter_GapYBeforeKnobs);

                auto filter_knobs_container =
                    lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
                LayoutParameterComponent(g,
                                         filter_knobs_container,
                                         c.filter.cutoff,
                                         layer->processor.params[ToInt(LayerParamIndex::FilterCutoff)],
                                         UiSizeId::Page_3KnobGapX);
                LayoutParameterComponent(g,
                                         filter_knobs_container,
                                         c.filter.reso,
                                         layer->processor.params[ToInt(LayerParamIndex::FilterResonance)],
                                         UiSizeId::Page_3KnobGapX);
                LayoutParameterComponent(g,
                                         filter_knobs_container,
                                         c.filter.env_amount,
                                         layer->processor.params[ToInt(LayerParamIndex::FilterEnvAmount)],
                                         UiSizeId::Page_3KnobGapX);

                c.filter.envelope = lay.CreateChildItem(page_container, 1, Main_EnvelopeH, LAY_HFILL);
                lay.SetMargins(c.filter.envelope,
                               Filter_EnvelopeMarginLR,
                               Filter_EnvelopeMarginTB,
                               Filter_EnvelopeMarginLR,
                               Filter_EnvelopeMarginTB);
                break;
            }
            case PageType::Eq: {
                c.eq.on = lay.CreateChildItem(page_container, 1, Page_HeadingHeight, LAY_HFILL);
                set_heading_margins(c.eq.on);

                {
                    c.eq.type[0] = lay.CreateChildItem(page_container, 1, ParamPopupButtonHeight, LAY_HFILL);
                    lay.SetMargins(c.eq.type[0],
                                   Page_HeadingMarginL,
                                   EQ_BandGapY,
                                   Page_HeadingMarginL,
                                   EQ_BandGapY);

                    auto knob_container =
                        lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.freq[0],
                                             layer->processor.params[ToInt(LayerParamIndex::EqFreq1)],
                                             UiSizeId::Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.reso[0],
                                             layer->processor.params[ToInt(LayerParamIndex::EqResonance1)],
                                             UiSizeId::Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.gain[0],
                                             layer->processor.params[ToInt(LayerParamIndex::EqGain1)],
                                             UiSizeId::Page_3KnobGapX);
                    lay.SetBottomMargin(knob_container, EQ_BandGapY);
                }

                {
                    c.eq.type[1] = lay.CreateChildItem(page_container, 1, ParamPopupButtonHeight, LAY_HFILL);
                    lay.SetMargins(c.eq.type[1],
                                   Page_HeadingMarginL,
                                   EQ_BandGapY,
                                   Page_HeadingMarginL,
                                   EQ_BandGapY);

                    auto knob_container =
                        lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.freq[1],
                                             layer->processor.params[ToInt(LayerParamIndex::EqFreq2)],
                                             UiSizeId::Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.reso[1],
                                             layer->processor.params[ToInt(LayerParamIndex::EqResonance2)],
                                             UiSizeId::Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.gain[1],
                                             layer->processor.params[ToInt(LayerParamIndex::EqGain2)],
                                             UiSizeId::Page_3KnobGapX);
                }

                break;
            }
            case PageType::Midi: {
                auto layout_item_single = [&](LayID& control) {
                    control = lay.CreateChildItem(page_container, 1, MIDI_ItemHeight, LAY_HFILL);
                    lay.SetMargins(control,
                                   MIDI_ItemMarginLR,
                                   MIDI_ItemGapY,
                                   MIDI_ItemMarginLR,
                                   MIDI_ItemGapY);
                };

                auto layout_item = [&](LayID& control, LayID& name, LayScalar height) {
                    auto parent = lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW);
                    control = lay.CreateChildItem(parent, MIDI_ItemWidth, height, 0);
                    lay.SetMargins(control,
                                   MIDI_ItemMarginLR,
                                   MIDI_ItemGapY,
                                   MIDI_ItemMarginLR,
                                   MIDI_ItemGapY);
                    name = lay.CreateChildItem(parent, 1, height, LAY_HFILL);
                };

                layout_item(c.midi.transpose, c.midi.transpose_name, MIDI_ItemHeight);
                layout_item_single(c.midi.keytrack);
                layout_item_single(c.midi.mono);
                layout_item_single(c.midi.retrig);
                layout_item(c.midi.velo_buttons, c.midi.velo_name, MIDI_VeloButtonsHeight);
                break;
            }
            case PageType::Lfo: {
                c.lfo.on = lay.CreateChildItem(page_container, 1, Page_HeadingHeight, LAY_HFILL);
                set_heading_margins(c.lfo.on);

                auto layout_item = [&](LayID& control, LayID& name) {
                    auto parent = lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW);
                    control = lay.CreateChildItem(parent, LFO_ItemWidth, ParamPopupButtonHeight, 0);
                    lay.SetMargins(control, LFO_ItemMarginL, LFO_ItemGapY, LFO_ItemMarginR, LFO_ItemGapY);
                    name = lay.CreateChildItem(parent, 1, ParamPopupButtonHeight, LAY_HFILL);
                };

                layout_item(c.lfo.target, c.lfo.target_name);
                layout_item(c.lfo.shape, c.lfo.shape_name);
                layout_item(c.lfo.mode, c.lfo.mode_name);

                auto knob_container =
                    lay.CreateParentItem(page_container, 1, 0, LAY_HFILL, LAY_ROW | LAY_MIDDLE);
                lay.SetTopMargin(knob_container, LFO_GapYBeforeKnobs);

                LayoutParameterComponent(g,
                                         knob_container,
                                         c.lfo.amount,
                                         layer->processor.params[ToInt(LayerParamIndex::LfoAmount)],
                                         UiSizeId::Page_2KnobGapX);

                auto& rate_param =
                    layer->processor
                        .params[layer->processor.params[ToInt(LayerParamIndex::LfoSyncSwitch)].ValueAsBool()
                                    ? ToInt(LayerParamIndex::LfoRateTempoSynced)
                                    : ToInt(LayerParamIndex::LfoRateHz)];
                LayoutParameterComponent(g,
                                         knob_container,
                                         c.lfo.rate,
                                         rate_param,
                                         UiSizeId::Page_2KnobGapX,
                                         true);
                break;
            }
            case PageType::Count: PanicIfReached();
        }
    }
}

static void DrawSelectorProgressBar(imgui::Context const& imgui, Rect r, f32 load_percent) {
    auto min = r.Min();
    auto max = f32x2 {r.x + Max(4.0f, r.w * load_percent), r.Bottom()};
    auto col = GMC(LayerSelectorMenuLoading);
    auto const rounding = editor::GetSize(imgui, UiSizeId::CornerRounding);
    imgui.graphics->AddRectFilled(min, max, col, rounding);
}

void Draw(Gui* g,
          PluginInstance* plugin,
          Rect r,
          PluginInstance::Layer* layer,
          LayerLayoutTempIDs& c,
          LayerLayout* layer_gui) {
    auto& lay = g->layout;
    auto& imgui = g->imgui;

#define GUI_SIZE(cat, n, v, u) [[maybe_unused]] const auto cat##n = editor::GetSize(imgui, UiSizeId::cat##n);
#include SIZES_DEF_FILENAME
#undef GUI_SIZE

    auto settings = FloeWindowSettings(imgui, [&](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto desired_lib_name = layer->LibName();
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

        auto background_lib = g->plugin.shared_data.available_libraries.FindRetained(*desired_lib_name);
        DEFER { background_lib.Release(); };

        auto const panel_rounding = editor::GetSize(imgui, UiSizeId::BlurredPanelRounding);

        if (background_lib && !g->settings.settings.gui.high_contrast_gui) {
            auto imgs = LoadLibraryBackgroundAndIconIfNeeded(g, *background_lib);
            if (imgs.blurred_background) {
                if (auto tex = g->gui_platform.graphics_ctx->GetTextureFromImage(*imgs.blurred_background)) {
                    f32x2 min_uv;
                    f32x2 max_uv;
                    get_background_uvs(imgs, r, window, min_uv, max_uv);
                    imgui.graphics->AddImageRounded(*tex,
                                                    r.Min(),
                                                    r.Max(),
                                                    min_uv,
                                                    max_uv,
                                                    GMC(BlurredImageDrawColour),
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
                        GMC(BlurredImageGradientOverlay),
                        0);
                    graphics::DrawList::ShadeVertsLinearColorGradientSetAlpha(
                        imgui.graphics,
                        vtx_idx_1,
                        vtx_idx_2,
                        pos + f32x2 {size.x, 0},
                        pos + f32x2 {size.x, size.y},
                        GMC(BlurredImageGradientOverlay),
                        0);
                }

                imgui.graphics->AddRect(r.Min(), r.Max(), GMC(BlurredImageBorder), panel_rounding);
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
                                GMC(LayerDividerLine));
    };

    // Inst selector
    {
        auto selector_left_id = imgui.GetID("SelcL");
        auto selector_right_id = imgui.GetID("SelcR");
        auto selector_menu_r = lay.GetRect(c.selector_menu);
        auto selector_left_r = lay.GetRect(c.selector_l);
        auto selector_right_r = lay.GetRect(c.selector_r);

        bool const should_highlight = false;
        // TODO: how are we going to handle the new dynamics knob changes
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
            auto const rounding = editor::GetSize(imgui, UiSizeId::CornerRounding);
            auto const col =
                should_highlight ? GMC(LayerSelectorMenuBackHighlight) : GMC(LayerSelectorMenuBack);
            imgui.graphics->AddRectFilled(registered_selector_box_r.Min(),
                                          registered_selector_box_r.Max(),
                                          col,
                                          rounding);
        }

        DoInstSelectorGUI(g, selector_menu_r, layer->index);
        if (auto percent =
                g->plugin.sample_lib_loader_connection.instrument_loading_percents[(usize)layer->index]
                    .Load();
            percent != -1) {
            f32 const load_percent = (f32)percent / 100.0f;
            DrawSelectorProgressBar(imgui, registered_selector_box_r, load_percent);
            g->imgui.RedrawAtIntervalSeconds(g->redraw_counter, 0.1);
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
        Rect const peak_meter_r {volume_knob_r.Centre().x - LayerPeakMeterWidth / 2,
                                 volume_knob_r.y +
                                     (volume_knob_r.h - (LayerPeakMeterHeight + LayerPeakMeterBottomGap)),
                                 LayerPeakMeterWidth,
                                 LayerPeakMeterHeight - LayerPeakMeterBottomGap};
        auto const& processor = plugin->processor.layer_processors[(usize)layer->index];
        peak_meters::PeakMeter(g, peak_meter_r, processor.peak_meter, false);
    }

    // volume
    {
        auto const volume_name_h = lay.GetRect(c.knob1.label).h;
        auto const volume_name_y_gap = LayerVolumeNameGapY;
        Rect const volume_name_r {volume_knob_r.x,
                                  volume_knob_r.Bottom() - volume_name_h + volume_name_y_gap,
                                  volume_knob_r.w,
                                  volume_name_h};

        KnobAndLabel(g,
                     layer->processor.params[ToInt(LayerParamIndex::Volume)],
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

        auto const col_border = GMC(LayerMuteSoloBorder);
        auto const col_background = GMC(LayerMuteSoloBackground);
        auto const rounding = editor::GetSize(imgui, UiSizeId::CornerRounding);
        auto reg_mute_solo_r = imgui.GetRegisteredAndConvertedRect(mute_solo_r);
        auto reg_mute_r = imgui.GetRegisteredAndConvertedRect(mute_r);
        imgui.graphics->AddRectFilled(reg_mute_solo_r.Min(), reg_mute_solo_r.Max(), col_background, rounding);
        imgui.graphics->AddLine({reg_mute_r.Right(), reg_mute_r.y},
                                {reg_mute_r.Right(), reg_mute_r.Bottom()},
                                col_border);

        buttons::Toggle(g,
                        layer->processor.params[ToInt(LayerParamIndex::Mute)],
                        mute_r,
                        "M",
                        buttons::IconButton(imgui));
        buttons::Toggle(g,
                        layer->processor.params[ToInt(LayerParamIndex::Solo)],
                        solo_r,
                        "S",
                        buttons::IconButton(imgui));
    }

    // knobs
    {
        auto semitone_style = draggers::DefaultStyle(imgui);
        semitone_style.always_show_plus = true;
        draggers::Dragger(g,
                          layer->processor.params[ToInt(LayerParamIndex::TuneSemitone)],
                          c.knob1.control,
                          semitone_style);
        labels::Label(g,
                      layer->processor.params[ToInt(LayerParamIndex::TuneSemitone)],
                      c.knob1.label,
                      labels::ParameterCentred(imgui));

        KnobAndLabel(g,
                     layer->processor.params[ToInt(LayerParamIndex::TuneCents)],
                     c.knob2,
                     knobs::BidirectionalKnob(imgui));
        KnobAndLabel(g,
                     layer->processor.params[ToInt(LayerParamIndex::Pan)],
                     c.knob3,
                     knobs::BidirectionalKnob(imgui));
    }

    draw_divider(c.divider2);

    // current page
    switch (layer_gui->selected_page) {
        case PageType::Main: {
            // waveform
            {
                GUIDoSampleWaveform(g, layer, lay.GetRect(c.main.waveform));

                buttons::Toggle(g,
                                layer->processor.params[ToInt(LayerParamIndex::Reverse)],
                                c.main.reverse,
                                buttons::ParameterToggleButton(imgui));

                buttons::PopupWithItems(g,
                                        layer->processor.params[ToInt(LayerParamIndex::LoopMode)],
                                        c.main.loop_mode,
                                        buttons::ParameterPopupButton(imgui));
            }

            draw_divider(c.main.divider);

            // env
            {
                buttons::Toggle(g,
                                layer->processor.params[ToInt(LayerParamIndex::VolEnvOn)],
                                c.main.env_on,
                                buttons::LayerHeadingButton(imgui));
                bool const env_on = layer->processor.params[ToInt(LayerParamIndex::VolEnvOn)].ValueAsBool();
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
            bool const greyed_out = !layer->processor.params[ToInt(LayerParamIndex::FilterOn)].ValueAsBool();
            buttons::Toggle(g,
                            layer->processor.params[ToInt(LayerParamIndex::FilterOn)],
                            c.filter.filter_on,
                            buttons::LayerHeadingButton(imgui));

            buttons::PopupWithItems(g,
                                    layer->processor.params[ToInt(LayerParamIndex::FilterType)],
                                    c.filter.filter_type,
                                    buttons::ParameterPopupButton(imgui, greyed_out));

            KnobAndLabel(g,
                         layer->processor.params[ToInt(LayerParamIndex::FilterCutoff)],
                         c.filter.cutoff,
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->processor.params[ToInt(LayerParamIndex::FilterResonance)],
                         c.filter.reso,
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->processor.params[ToInt(LayerParamIndex::FilterEnvAmount)],
                         c.filter.env_amount,
                         knobs::BidirectionalKnob(imgui),
                         greyed_out);

            GUIDoEnvelope(
                g,
                layer,
                lay.GetRect(c.filter.envelope),
                greyed_out ||
                    (layer->processor.params[ToInt(LayerParamIndex::FilterEnvAmount)].LinearValue() == 0),
                {LayerParamIndex::FilterAttack,
                 LayerParamIndex::FilterDecay,
                 LayerParamIndex::FilterSustain,
                 LayerParamIndex::FilterRelease},
                GuiEnvelopeType::Filter);

            break;
        }
        case PageType::Eq: {
            bool const greyed_out = !layer->processor.params[ToInt(LayerParamIndex::EqOn)].ValueAsBool();
            buttons::Toggle(g,
                            layer->processor.params[ToInt(LayerParamIndex::EqOn)],
                            lay.GetRect(c.eq.on),
                            buttons::LayerHeadingButton(imgui));

            buttons::PopupWithItems(g,
                                    layer->processor.params[ToInt(LayerParamIndex::EqType1)],
                                    lay.GetRect(c.eq.type[0]),
                                    buttons::ParameterPopupButton(imgui, greyed_out));

            KnobAndLabel(g,
                         layer->processor.params[ToInt(LayerParamIndex::EqFreq1)],
                         c.eq.freq[0],
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->processor.params[ToInt(LayerParamIndex::EqResonance1)],
                         c.eq.reso[0],
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->processor.params[ToInt(LayerParamIndex::EqGain1)],
                         c.eq.gain[0],
                         knobs::BidirectionalKnob(imgui),
                         greyed_out);

            buttons::PopupWithItems(g,
                                    layer->processor.params[ToInt(LayerParamIndex::EqType2)],
                                    lay.GetRect(c.eq.type[1]),
                                    buttons::ParameterPopupButton(imgui, greyed_out));

            KnobAndLabel(g,
                         layer->processor.params[ToInt(LayerParamIndex::EqFreq2)],
                         c.eq.freq[1],
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->processor.params[ToInt(LayerParamIndex::EqResonance2)],
                         c.eq.reso[1],
                         knobs::DefaultKnob(imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->processor.params[ToInt(LayerParamIndex::EqGain2)],
                         c.eq.gain[1],
                         knobs::BidirectionalKnob(imgui),
                         greyed_out);

            break;
        }
        case PageType::Midi: {
            draggers::Dragger(g,
                              layer->processor.params[ToInt(LayerParamIndex::MidiTranspose)],
                              c.midi.transpose,
                              draggers::DefaultStyle(imgui));
            labels::Label(g,
                          layer->processor.params[ToInt(LayerParamIndex::MidiTranspose)],
                          c.midi.transpose_name,
                          labels::Parameter(imgui));
            {
                auto const label_id = imgui.GetID("transp");
                auto const label_r = lay.GetRect(c.midi.transpose_name);
                imgui.ButtonBehavior(imgui.GetRegisteredAndConvertedRect(label_r), label_id, {});
                Tooltip(g,
                        label_id,
                        label_r,
                        layer->processor.params[ToInt(LayerParamIndex::MidiTranspose)].info.tooltip);
                if (imgui.IsHot(label_id))
                    imgui.platform->gui_update_requirements.cursor_type = CursorType::Default;
            }

            buttons::Toggle(g,
                            layer->processor.params[ToInt(LayerParamIndex::Keytrack)],
                            c.midi.keytrack,
                            buttons::MidiButton(imgui));
            buttons::Toggle(g,
                            layer->processor.params[ToInt(LayerParamIndex::Monophonic)],
                            c.midi.mono,
                            buttons::MidiButton(imgui));
            buttons::Toggle(g,
                            layer->processor.params[ToInt(LayerParamIndex::CC64Retrigger)],
                            c.midi.retrig,
                            buttons::MidiButton(imgui));

            {
                static constexpr auto k_num_btns = ToInt(param_values::VelocityMappingMode::Count);
                static_assert(k_num_btns == 6, "");
                auto const btn_gap = MIDI_VeloButtonsSpacing;
                auto const whole_velo_r =
                    lay.GetRect(c.midi.velo_buttons).CutRight(btn_gap * 2).CutBottom(btn_gap);

                for (auto const btn_ind : Range(k_num_btns)) {
                    bool state = ToInt(layer->processor.GetVelocityMode()) == btn_ind;
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
                              layer->processor.params[ToInt(LayerParamIndex::VelocityMapping)],
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
                if (imgui.IsHot(label_id))
                    imgui.platform->gui_update_requirements.cursor_type = CursorType::Default;
            }

            break;
        }
        case PageType::Lfo: {
            buttons::Toggle(g,
                            layer->processor.params[ToInt(LayerParamIndex::LfoOn)],
                            c.lfo.on,
                            buttons::LayerHeadingButton(imgui));
            auto const greyed_out = !layer->processor.params[ToInt(LayerParamIndex::LfoOn)].ValueAsBool();

            buttons::PopupWithItems(g,
                                    layer->processor.params[ToInt(LayerParamIndex::LfoDestination)],
                                    c.lfo.target,
                                    buttons::ParameterPopupButton(imgui, greyed_out));
            labels::Label(g,
                          layer->processor.params[ToInt(LayerParamIndex::LfoDestination)],
                          c.lfo.target_name,
                          labels::Parameter(imgui));

            buttons::PopupWithItems(g,
                                    layer->processor.params[ToInt(LayerParamIndex::LfoRestart)],
                                    c.lfo.mode,
                                    buttons::ParameterPopupButton(imgui, greyed_out));
            labels::Label(g,
                          layer->processor.params[ToInt(LayerParamIndex::LfoRestart)],
                          c.lfo.mode_name,
                          labels::Parameter(imgui));

            buttons::PopupWithItems(g,
                                    layer->processor.params[ToInt(LayerParamIndex::LfoShape)],
                                    c.lfo.shape,
                                    buttons::ParameterPopupButton(imgui, greyed_out));
            labels::Label(g,
                          layer->processor.params[ToInt(LayerParamIndex::LfoShape)],
                          c.lfo.shape_name,
                          labels::Parameter(imgui));

            KnobAndLabel(g,
                         layer->processor.params[ToInt(LayerParamIndex::LfoAmount)],
                         c.lfo.amount,
                         knobs::BidirectionalKnob(imgui),
                         greyed_out);

            Parameter const* rate_param;
            if (layer->processor.params[ToInt(LayerParamIndex::LfoSyncSwitch)].ValueAsBool()) {
                rate_param = &layer->processor.params[ToInt(LayerParamIndex::LfoRateTempoSynced)];
                buttons::PopupWithItems(g,
                                        *rate_param,
                                        c.lfo.rate.control,
                                        buttons::ParameterPopupButton(imgui, greyed_out));
            } else {
                rate_param = &layer->processor.params[ToInt(LayerParamIndex::LfoRateHz)];
                knobs::Knob(g,
                            *rate_param,
                            c.lfo.rate.control,
                            knobs::DefaultKnob(imgui).GreyedOut(greyed_out));
            }

            auto const rate_name_r = lay.GetRect(c.lfo.rate.label);
            labels::Label(g, *rate_param, rate_name_r, labels::ParameterCentred(imgui, greyed_out));

            Rect const sync_r {rate_name_r.x + rate_name_r.w / 2 - LFO_SyncSwitchWidth / 2,
                               rate_name_r.Bottom() + LFO_SyncSwitchGapY,
                               LFO_SyncSwitchWidth,
                               LFO_SyncSwitchHeight};
            buttons::Toggle(g,
                            layer->processor.params[ToInt(LayerParamIndex::LfoSyncSwitch)],
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
        bool const has_dot = (page_type == PageType::Filter &&
                              layer->processor.params[ToInt(LayerParamIndex::FilterOn)].ValueAsBool()) ||
                             (page_type == PageType::Lfo &&
                              layer->processor.params[ToInt(LayerParamIndex::LfoOn)].ValueAsBool()) ||
                             (page_type == PageType::Eq &&
                              layer->processor.params[ToInt(LayerParamIndex::EqOn)].ValueAsBool());
        if (buttons::Toggle(g, id, tab_r, state, name, buttons::LayerTabButton(imgui, has_dot)))
            layer_gui->selected_page = page_type;
        Tooltip(g, id, tab_r, fmt::Format(g->scratch_arena, "Open {} tab", name));
    }

    // overlay
    auto const& layer_processor = plugin->processor.layer_processors[(usize)layer->index];
    if (layer_processor.is_silent.Load()) {
        auto pos = imgui.curr_window->unpadded_bounds.pos;
        imgui.graphics->AddRectFilled(pos, pos + imgui.Size(), GMC(LayerMutedOverlay));
    }
}

} // namespace layer_gui
