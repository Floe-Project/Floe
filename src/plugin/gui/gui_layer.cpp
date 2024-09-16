// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_layer.hpp"

#include <IconsFontAwesome5.h>

#include "engine/engine.hpp"
#include "gui.hpp"
#include "gui_button_widgets.hpp"
#include "gui_dragger_widgets.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_envelope.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_label_widgets.hpp"
#include "gui_modal_windows.hpp"
#include "gui_peak_meter_widget.hpp"
#include "gui_waveform.hpp"
#include "gui_widget_compounds.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"
#include "processor/layer_processor.hpp"

namespace layer_gui {

static void LayerInstrumentMenuItems(Gui* g, LayerProcessor* layer) {
    auto const scratch_cursor = g->scratch_arena.TotalUsed();
    DEFER { g->scratch_arena.TryShrinkTotalUsed(scratch_cursor); };

    auto libs = sample_lib_server::AllLibrariesRetained(g->shared_engine_systems.sample_library_server,
                                                        g->scratch_arena);
    DEFER { sample_lib_server::ReleaseAll(libs); };

    StartFloeMenu(g);
    DEFER { EndFloeMenu(g); };

    // TODO(1.0): this is not production-ready code. We need a new powerful database-like browser GUI
    int current = 0;
    DynamicArray<String> insts {g->scratch_arena};
    DynamicArray<sample_lib::InstrumentId> inst_ids {g->scratch_arena};
    dyn::Append(insts, "None");
    dyn::Append(inst_ids, {});

    for (auto const i : Range(ToInt(WaveformType::Count))) {
        dyn::Append(insts, k_waveform_type_names[i]);
        dyn::Append(inst_ids, {});
        if (auto current_id = layer->instrument_id.TryGet<WaveformType>()) {
            if (*current_id == (WaveformType)i) current = (int)i + 1;
        }
    }

    for (auto l : libs) {
        for (auto [key, inst_ptr] : l->insts_by_name) {
            auto const lib_id = l->Id();
            auto const inst_name = key;
            if (auto current_id = layer->instrument_id.TryGet<sample_lib::InstrumentId>()) {
                if (current_id->library == l->Id() && current_id->inst_name == inst_name)
                    current = (int)insts.size;
            }
            dyn::Append(insts, fmt::Format(g->scratch_arena, "{}: {}", lib_id, inst_name));
            dyn::Append(inst_ids,
                        sample_lib::InstrumentId {
                            .library = lib_id,
                            .inst_name = inst_name,
                        });
        }
    }

    if (DoMultipleMenuItems(g, insts, current)) {
        if (current == 0)
            LoadInstrument(g->engine, layer->index, InstrumentId {InstrumentType::None});
        else if (current >= 1 && current <= (int)WaveformType::Count)
            LoadInstrument(g->engine, layer->index, InstrumentId {(WaveformType)(current - 1)});
        else
            LoadInstrument(g->engine, layer->index, InstrumentId {inst_ids[(usize)current]});
    }
}

static void DoInstSelectorGUI(Gui* g, Rect r, u32 layer) {
    auto& engine = g->engine;
    auto& imgui = g->imgui;
    imgui.PushID("inst selector");
    DEFER { imgui.PopID(); };
    auto imgui_id = imgui.GetID((u64)layer);

    auto layer_obj = &engine.Layer(layer);
    auto const inst_name = layer_obj->InstName();

    Optional<graphics::TextureHandle> icon_tex {};
    if (layer_obj->instrument_id.tag == InstrumentType::Sampler) {
        auto sample_inst_id = layer_obj->instrument_id.Get<sample_lib::InstrumentId>();
        auto imgs = LibraryImagesFromLibraryId(g, sample_inst_id.library);
        if (imgs && imgs->icon) icon_tex = imgui.frame_input.graphics_ctx->GetTextureFromImage(*imgs->icon);
    }

    if (buttons::Popup(g,
                       imgui_id,
                       imgui_id + 1,
                       r,
                       inst_name,
                       buttons::InstSelectorPopupButton(imgui, icon_tex))) {
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

    auto container = layout::CreateItem(lay,
                                        {
                                            .size = {(f32)width, (f32)height},
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::JustifyContent::Start,
                                        });

    // selector
    {
        auto const layer_selector_box_height = LiveSize(imgui, LayerSelectorBoxHeight);
        auto const layer_selector_button_w = LiveSize(imgui, LayerSelectorButtonW);
        auto const layer_selector_box_buttons_margin_r = LiveSize(imgui, LayerSelectorBoxButtonsMarginR);

        c.selector_box = layout::CreateItem(lay,
                                            {
                                                .parent = container,
                                                .size = {1, layer_selector_box_height},
                                                .margins = {.l = LiveSize(imgui, LayerSelectorBoxMarginL),
                                                            .r = LiveSize(imgui, LayerSelectorBoxMarginR),
                                                            .t = LiveSize(imgui, LayerSelectorBoxMarginT),
                                                            .b = LiveSize(imgui, LayerSelectorBoxMarginB)},
                                                .anchor = layout::Anchor::LeftAndRight,
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::JustifyContent::Start,
                                            });

        c.selector_menu = layout::CreateItem(lay,
                                             {
                                                 .parent = c.selector_box,
                                                 .size = {1, 1},
                                                 .anchor = layout::Anchor::All,
                                             });
        c.selector_l = layout::CreateItem(lay,
                                          {
                                              .parent = c.selector_box,
                                              .size = {layer_selector_button_w, 1},
                                              .anchor = layout::Anchor::TopAndBottom,
                                          });
        c.selector_r = layout::CreateItem(lay,
                                          {
                                              .parent = c.selector_box,
                                              .size = {layer_selector_button_w, 1},
                                              .anchor = layout::Anchor::TopAndBottom,
                                          });
        c.selector_randomise = layout::CreateItem(lay,
                                                  {
                                                      .parent = c.selector_box,
                                                      .size = {layer_selector_button_w, 1},
                                                      .margins = {.r = layer_selector_box_buttons_margin_r},
                                                      .anchor = layout::Anchor::TopAndBottom,
                                                  });
    }

    if (layer->instrument.tag == InstrumentType::None) return;

    // mixer container 1
    {
        auto subcontainer_1 = layout::CreateItem(lay,
                                                 {
                                                     .parent = container,
                                                     .size = {1, 0},
                                                     .margins {
                                                         .l = LiveSize(imgui, LayerMixerContainer1MarginL),
                                                         .r = LiveSize(imgui, LayerMixerContainer1MarginR),
                                                         .t = LiveSize(imgui, LayerMixerContainer1MarginT),
                                                         .b = LiveSize(imgui, LayerMixerContainer1MarginB),
                                                     },
                                                     .anchor = layout::Anchor::LeftAndRight,
                                                     .contents_direction = layout::Direction::Row,
                                                     .contents_align = layout::JustifyContent::Middle,
                                                 });

        c.volume = layout::CreateItem(lay,
                                      {
                                          .parent = subcontainer_1,
                                          .size = LiveSize(imgui, LayerVolumeKnobSize),
                                          .margins = {.r = LiveSize(imgui, LayerVolumeKnobMarginR)},
                                      });

        c.mute_solo = layout::CreateItem(
            lay,
            {
                .parent = subcontainer_1,
                .size = {LiveSize(imgui, LayerMuteSoloWidth), LiveSize(imgui, LayerMuteSoloHeight)},
                .margins {
                    .l = LiveSize(imgui, LayerMuteSoloMarginL),
                    .r = LiveSize(imgui, LayerMuteSoloMarginR),
                    .t = LiveSize(imgui, LayerMuteSoloMarginT),
                    .b = LiveSize(imgui, LayerMuteSoloMarginB),
                },
            });
    }

    // mixer container 2
    {
        auto subcontainer_2 = layout::CreateItem(lay,
                                                 {
                                                     .parent = container,
                                                     .size = {0, 0},
                                                     .contents_direction = layout::Direction::Row,
                                                     .contents_align = layout::JustifyContent::Middle,
                                                 });
        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob1,
                                 layer->params[ToInt(LayerParamIndex::TuneSemitone)],
                                 LayerPitchMarginLR);
        auto const layer_pitch_width = LiveSize(imgui, LayerPitchWidth);
        auto const layer_pitch_height = LiveSize(imgui, LayerPitchHeight);
        auto const layer_pitch_margin_t = LiveSize(imgui, LayerPitchMarginT);
        auto const layer_pitch_margin_b = LiveSize(imgui, LayerPitchMarginB);
        layout::SetSize(lay, c.knob1.control, f32x2 {layer_pitch_width, layer_pitch_height});
        layout::SetMargins(lay, c.knob1.control, {.t = layer_pitch_margin_t, .b = layer_pitch_margin_b});

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
    c.divider = layout::CreateItem(lay,
                                   {
                                       .parent = container,
                                       .size = {1, 1},
                                       .margins = {.tb = layer_mixer_divider_vert_margins},
                                       .anchor = layout::Anchor::LeftAndRight,
                                   });

    // tabs
    {
        auto const layer_params_group_tabs_h = LiveSize(imgui, LayerParamsGroupTabsH);
        auto const layer_params_group_box_gap_x = LiveSize(imgui, LayerParamsGroupBoxGapX);
        auto const layer_params_group_tabs_gap = LiveSize(imgui, LayerParamsGroupTabsGap);
        auto tab_lay = layout::CreateItem(lay,
                                          {
                                              .parent = container,
                                              .size = {1, layer_params_group_tabs_h},
                                              .margins = {.lr = layer_params_group_box_gap_x},
                                              .anchor = layout::Anchor::LeftAndRight,
                                              .contents_direction = layout::Direction::Row,
                                              .contents_align = layout::JustifyContent::Middle,
                                          });
        for (auto const i : Range(k_num_pages)) {
            auto const page_type = (PageType)i;
            auto size = draw::GetTextWidth(imgui.graphics->context->CurrentFont(), GetPageTitle(page_type));
            if (page_type == PageType::Filter || page_type == PageType::Lfo || page_type == PageType::Eq)
                size += LiveSize(imgui, LayerParamsGroupTabsIconW2);
            c.tabs[i] = layout::CreateItem(lay,
                                           {
                                               .parent = tab_lay,
                                               .size = {size + layer_params_group_tabs_gap, 1},
                                               .anchor = layout::Anchor::TopAndBottom,
                                           });
        }
    }

    // divider2
    {
        c.divider2 = layout::CreateItem(lay,
                                        {
                                            .parent = container,
                                            .size = {1, 1},
                                            .margins = {.tb = layer_mixer_divider_vert_margins},
                                            .anchor = layout::Anchor::LeftAndRight,
                                        });
    }

    {
        auto const page_heading_margin_l = LiveSize(imgui, Page_HeadingMarginL);
        auto const page_heading_margin_t = LiveSize(imgui, Page_HeadingMarginT);
        auto const page_heading_margin_b = LiveSize(imgui, Page_HeadingMarginB);
        auto const heading_margins = layout::Margins {
            .l = page_heading_margin_l,
            .r = 0,
            .t = page_heading_margin_t,
            .b = page_heading_margin_b,
        };

        auto page_container = layout::CreateItem(lay,
                                                 {
                                                     .parent = container,
                                                     .size = {1, 1},
                                                     .anchor = layout::Anchor::All,
                                                     .contents_direction = layout::Direction::Column,
                                                     .contents_align = layout::JustifyContent::Start,
                                                 });

        auto const main_envelope_h = LiveSize(imgui, Main_EnvelopeH);

        switch (layer_gui->selected_page) {
            case PageType::Main: {
                auto const main_waveform_h = LiveSize(imgui, Main_WaveformH);
                auto const main_waveform_margin_lr = LiveSize(imgui, Main_WaveformMarginLR);
                auto const main_waveform_margin_tb = LiveSize(imgui, Main_WaveformMarginTB);
                c.main.waveform = layout::CreateItem(lay,
                                                     {
                                                         .parent = page_container,
                                                         .size = {1, main_waveform_h},
                                                         .margins =
                                                             {
                                                                 .lr = main_waveform_margin_lr,
                                                                 .tb = main_waveform_margin_tb,
                                                             },
                                                         .anchor = layout::Anchor::LeftAndRight,
                                                     });

                auto const main_item_margin_lr = LiveSize(imgui, Main_ItemMarginLR);
                auto const main_item_height = LiveSize(imgui, Main_ItemHeight);
                auto const main_item_gap_y = LiveSize(imgui, Main_ItemGapY);
                auto btn_container = layout::CreateItem(lay,
                                                        {
                                                            .parent = page_container,
                                                            .size = {1, 0},
                                                            .margins = {.lr = main_item_margin_lr},
                                                            .anchor = layout::Anchor::LeftAndRight,
                                                            .contents_direction = layout::Direction::Row,

                                                        });
                c.main.reverse = layout::CreateItem(lay,
                                                    {
                                                        .parent = btn_container,
                                                        .size = {1, main_item_height},
                                                        .margins = {.tb = main_item_gap_y},
                                                        .anchor = layout::Anchor::LeftAndRight,
                                                    });
                c.main.loop_mode = layout::CreateItem(lay,
                                                      {
                                                          .parent = btn_container,
                                                          .size = {1, param_popup_button_height},
                                                          .margins = {.tb = main_item_gap_y},
                                                          .anchor = layout::Anchor::LeftAndRight,
                                                      });

                auto const main_divider_margin_t = LiveSize(imgui, Main_DividerMarginT);
                auto const main_divider_margin_b = LiveSize(imgui, Main_DividerMarginB);
                c.main.divider = layout::CreateItem(
                    lay,
                    {
                        .parent = page_container,
                        .size = {1, 1},
                        .margins = {.t = main_divider_margin_t, .b = main_divider_margin_b},
                        .anchor = layout::Anchor::LeftAndRight,
                    });

                c.main.env_on = layout::CreateItem(lay,
                                                   {
                                                       .parent = page_container,
                                                       .size = {1, page_heading_height},
                                                       .margins = ({
                                                           auto m = heading_margins;
                                                           m.b = 0;
                                                           m;
                                                       }),
                                                       .anchor = layout::Anchor::LeftAndRight,
                                                   });

                auto const main_envelope_margin_lr = LiveSize(imgui, Main_EnvelopeMarginLR);
                auto const main_envelope_margin_tb = LiveSize(imgui, Main_EnvelopeMarginTB);
                c.main.envelope = layout::CreateItem(lay,
                                                     {
                                                         .parent = page_container,
                                                         .size = {1, main_envelope_h},
                                                         .margins {
                                                             .lr = main_envelope_margin_lr,
                                                             .tb = main_envelope_margin_tb,
                                                         },
                                                         .anchor = layout::Anchor::LeftAndRight,
                                                     });
                break;
            }
            case PageType::Filter: {
                auto const filter_gap_y_before_knobs = LiveSize(imgui, Filter_GapYBeforeKnobs);

                auto filter_heading_container =
                    layout::CreateItem(lay,
                                       {
                                           .parent = page_container,
                                           .size = {1, 0},
                                           .margins {.b = filter_gap_y_before_knobs},
                                           .anchor = layout::Anchor::LeftAndRight,
                                           .contents_direction = layout::Direction::Row,
                                       });
                c.filter.filter_on =
                    layout::CreateItem(lay,
                                       {
                                           .parent = filter_heading_container,
                                           .size = {1, page_heading_height},
                                           .margins = heading_margins,
                                           .anchor = layout::Anchor::LeftAndRight | layout::Anchor::Top,
                                       });
                c.filter.filter_type = layout::CreateItem(lay,
                                                          {
                                                              .parent = filter_heading_container,
                                                              .size = {1, param_popup_button_height},
                                                              .margins = {.l = page_heading_margin_l},
                                                              .anchor = layout::Anchor::LeftAndRight,
                                                          });

                auto filter_knobs_container =
                    layout::CreateItem(lay,
                                       {
                                           .parent = page_container,
                                           .size = {1, 0},
                                           .anchor = layout::Anchor::LeftAndRight,
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::JustifyContent::Middle,
                                       });
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

                c.filter.envelope = layout::CreateItem(lay,
                                                       {
                                                           .parent = page_container,
                                                           .size = {1, main_envelope_h},
                                                           .margins {
                                                               .lr = LiveSize(imgui, Filter_EnvelopeMarginLR),
                                                               .tb = LiveSize(imgui, Filter_EnvelopeMarginTB),
                                                           },
                                                           .anchor = layout::Anchor::LeftAndRight,
                                                       });
                break;
            }
            case PageType::Eq: {
                c.eq.on = layout::CreateItem(lay,
                                             {
                                                 .parent = page_container,
                                                 .size = {1, page_heading_height},
                                                 .margins = heading_margins,
                                                 .anchor = layout::Anchor::LeftAndRight,
                                             });

                auto const eq_band_gap_y = LiveSize(imgui, EQ_BandGapY);
                {
                    c.eq.type[0] = layout::CreateItem(lay,
                                                      {
                                                          .parent = page_container,
                                                          .size = {1, param_popup_button_height},
                                                          .margins {
                                                              .lr = page_heading_margin_l,
                                                              .tb = eq_band_gap_y,
                                                          },
                                                          .anchor = layout::Anchor::LeftAndRight,
                                                      });

                    auto knob_container =
                        layout::CreateItem(lay,
                                           {
                                               .parent = page_container,
                                               .size = {1, 0},
                                               .anchor = layout::Anchor::LeftAndRight,
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::JustifyContent::Middle,
                                           });
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
                    layout::SetMargins(lay, knob_container, {.b = eq_band_gap_y});
                }

                {
                    c.eq.type[1] = layout::CreateItem(lay,
                                                      {
                                                          .parent = page_container,
                                                          .size = {1, param_popup_button_height},
                                                          .margins {
                                                              .lr = page_heading_margin_l,
                                                              .tb = eq_band_gap_y,
                                                          },
                                                          .anchor = layout::Anchor::LeftAndRight,
                                                      });
                    auto knob_container =
                        layout::CreateItem(lay,
                                           {
                                               .parent = page_container,
                                               .size = {1, 0},
                                               .anchor = layout::Anchor::LeftAndRight,
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::JustifyContent::Middle,
                                           });
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

                auto layout_item = [&](layout::Id& control, layout::Id& name, f32 height) {
                    auto parent = layout::CreateItem(lay,
                                                     {
                                                         .parent = page_container,
                                                         .size = {1, 0},
                                                         .anchor = layout::Anchor::LeftAndRight,
                                                         .contents_direction = layout::Direction::Row,

                                                     });
                    control = layout::CreateItem(lay,
                                                 {
                                                     .parent = parent,
                                                     .size = {midi_item_width, height},
                                                     .margins {
                                                         .lr = midi_item_margin_lr,
                                                         .tb = midi_item_gap_y,
                                                     },
                                                 });
                    name = layout::CreateItem(lay,
                                              {
                                                  .parent = parent,
                                                  .size = {1, height},
                                                  .anchor = layout::Anchor::LeftAndRight,
                                              });
                };

                layout_item(c.midi.transpose, c.midi.transpose_name, midi_item_height);

                auto const button_options = layout::ItemOptions {
                    .parent = page_container,
                    .size = {1, midi_item_height},
                    .margins {
                        .lr = midi_item_margin_lr,
                        .tb = midi_item_gap_y,
                    },
                    .anchor = layout::Anchor::LeftAndRight,
                };
                c.midi.keytrack = layout::CreateItem(lay, button_options);
                c.midi.mono = layout::CreateItem(lay, button_options);
                c.midi.retrig = layout::CreateItem(lay, button_options);
                layout_item(c.midi.velo_buttons, c.midi.velo_name, LiveSize(imgui, MIDI_VeloButtonsHeight));
                break;
            }
            case PageType::Lfo: {
                c.lfo.on = layout::CreateItem(lay,
                                              {
                                                  .parent = page_container,
                                                  .size = {1, page_heading_height},
                                                  .margins = heading_margins,
                                                  .anchor = layout::Anchor::LeftAndRight,
                                              });

                auto const lfo_item_width = LiveSize(imgui, LFO_ItemWidth);
                auto const lfo_item_margin_l = LiveSize(imgui, LFO_ItemMarginL);
                auto const lfo_item_gap_y = LiveSize(imgui, LFO_ItemGapY);
                auto const lfo_item_margin_r = LiveSize(imgui, LFO_ItemMarginR);
                auto const lfo_gap_y_before_knobs = LiveSize(imgui, LFO_GapYBeforeKnobs);
                auto layout_item = [&](layout::Id& control, layout::Id& name) {
                    auto parent = layout::CreateItem(lay,
                                                     {
                                                         .parent = page_container,
                                                         .size = {1, 0},
                                                         .anchor = layout::Anchor::LeftAndRight,
                                                         .contents_direction = layout::Direction::Row,
                                                     });
                    control = layout::CreateItem(lay,
                                                 {
                                                     .parent = parent,
                                                     .size = {lfo_item_width, param_popup_button_height},
                                                     .margins {
                                                         .l = lfo_item_margin_l,
                                                         .r = lfo_item_margin_r,
                                                         .tb = lfo_item_gap_y,
                                                     },
                                                 });
                    name = layout::CreateItem(lay,
                                              {
                                                  .parent = parent,
                                                  .size = {1, param_popup_button_height},
                                                  .anchor = layout::Anchor::LeftAndRight,
                                              });
                };

                layout_item(c.lfo.target, c.lfo.target_name);
                layout_item(c.lfo.shape, c.lfo.shape_name);
                layout_item(c.lfo.mode, c.lfo.mode_name);

                auto knob_container = layout::CreateItem(lay,
                                                         {
                                                             .parent = page_container,
                                                             .size = {1, 0},
                                                             .margins = {.t = lfo_gap_y_before_knobs},
                                                             .anchor = layout::Anchor::LeftAndRight,
                                                             .contents_direction = layout::Direction::Row,
                                                             .contents_align = layout::JustifyContent::Middle,
                                                         });

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
          Engine* engine,
          Rect r,
          LayerProcessor* layer,
          LayerLayoutTempIDs& c,
          LayerLayout* layer_gui) {
    using enum UiSizeId;

    auto& lay = g->layout;
    auto& imgui = g->imgui;

    auto settings = FloeWindowSettings(imgui, [&](IMGUI_DRAW_WINDOW_BG_ARGS) {});
    settings.flags |= imgui::WindowFlags_NoScrollbarY;
    imgui.BeginWindow(settings, imgui.GetID(layer), r);
    DEFER { imgui.EndWindow(); };

    auto const draw_divider = [&](layout::Id id) {
        auto line_r = layout::GetRect(lay, id);
        imgui.RegisterAndConvertRect(&line_r);
        imgui.graphics->AddLine({line_r.x, line_r.Bottom()},
                                {line_r.Right(), line_r.Bottom()},
                                LiveCol(imgui, UiColMap::LayerDividerLine));
    };

    // Inst selector
    {
        auto selector_left_id = imgui.GetID("SelcL");
        auto selector_right_id = imgui.GetID("SelcR");
        auto selector_menu_r = layout::GetRect(lay, c.selector_menu);
        auto selector_left_r = layout::GetRect(lay, c.selector_l);
        auto selector_right_r = layout::GetRect(lay, c.selector_r);

        bool const should_highlight = false;
        // TODO(1.0): how are we going to handle the new dynamics knob changes
#if 0
        if (auto inst = layer->instrument.GetNullable<AssetReference<LoadedInstrument>>();
            inst && (*inst) &&
            (g->dynamics_slider_is_held ||
             CcControllerMovedParamRecently(g->engine.processor, ParamIndex::MasterDynamics)) &&
            ((*inst)->instrument.flags & FloeLibrary::Instrument::HasDynamicLayers)) {
            should_highlight = true;
        }
#endif

        auto const registered_selector_box_r =
            imgui.GetRegisteredAndConvertedRect(layout::GetRect(lay, c.selector_box));
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
                g->engine.sample_lib_server_async_channel.instrument_loading_percents[(usize)layer->index]
                    .Load(LoadMemoryOrder::Relaxed);
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
            CycleInstrument(*engine, layer->index, CycleDirection::Backward);
        if (buttons::Button(g,
                            selector_right_id,
                            selector_right_r,
                            ICON_FA_CARET_RIGHT,
                            buttons::IconButton(imgui))) {
            CycleInstrument(*engine, layer->index, CycleDirection::Forward);
        }
        {
            auto rand_id = imgui.GetID("Rand");
            auto rand_r = layout::GetRect(lay, c.selector_randomise);
            if (buttons::Button(g,
                                rand_id,
                                rand_r,
                                ICON_FA_RANDOM,
                                buttons::IconButton(imgui).WithRandomiseIconScaling())) {
                LoadRandomInstrument(*engine, layer->index, false);
            }
            Tooltip(g, rand_id, rand_r, "Load a random instrument"_s);
        }

        Tooltip(g, selector_left_id, selector_left_r, "Load the previous instrument"_s);
        Tooltip(g, selector_right_id, selector_right_r, "Load the next instrument"_s);
    }

    if (layer->instrument.tag == InstrumentType::None) return;

    // divider
    draw_divider(c.divider);

    auto const volume_knob_r = layout::GetRect(lay, c.volume);
    // level meter
    {
        auto const layer_peak_meter_width = LiveSize(imgui, LayerPeakMeterWidth);
        auto const layer_peak_meter_height = LiveSize(imgui, LayerPeakMeterHeight);
        auto const layer_peak_meter_bottom_gap = LiveSize(imgui, LayerPeakMeterBottomGap);

        Rect const peak_meter_r {.xywh {
            volume_knob_r.Centre().x - layer_peak_meter_width / 2,
            volume_knob_r.y + (volume_knob_r.h - (layer_peak_meter_height + layer_peak_meter_bottom_gap)),
            layer_peak_meter_width,
            layer_peak_meter_height - layer_peak_meter_bottom_gap}};
        auto const& processor = engine->processor.layer_processors[(usize)layer->index];
        peak_meters::PeakMeter(g, peak_meter_r, processor.peak_meter, false);
    }

    // volume
    {
        auto const volume_name_h = layout::GetRect(lay, c.knob1.label).h;
        auto const volume_name_y_gap = LiveSize(imgui, LayerVolumeNameGapY);
        Rect const volume_name_r {.xywh {volume_knob_r.x,
                                         volume_knob_r.Bottom() - volume_name_h + volume_name_y_gap,
                                         volume_knob_r.w,
                                         volume_name_h}};

        KnobAndLabel(g,
                     layer->params[ToInt(LayerParamIndex::Volume)],
                     volume_knob_r,
                     volume_name_r,
                     knobs::DefaultKnob(imgui));
    }

    // mute and solo
    {
        auto mute_solo_r = layout::GetRect(lay, c.mute_solo);
        Rect const mute_r = {.xywh {mute_solo_r.x, mute_solo_r.y, mute_solo_r.w / 2, mute_solo_r.h}};
        Rect const solo_r = {
            .xywh {mute_solo_r.x + mute_solo_r.w / 2, mute_solo_r.y, mute_solo_r.w / 2, mute_solo_r.h}};

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
                GUIDoSampleWaveform(g, layer, layout::GetRect(lay, c.main.waveform));

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
                              layout::GetRect(lay, c.main.envelope),
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
                          layout::GetRect(lay, c.filter.envelope),
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
                            layout::GetRect(lay, c.eq.on),
                            buttons::LayerHeadingButton(imgui));

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::EqType1)],
                                    layout::GetRect(lay, c.eq.type[0]),
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
                                    layout::GetRect(lay, c.eq.type[1]),
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
                auto const label_r = layout::GetRect(lay, c.midi.transpose_name);
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
                    layout::GetRect(lay, c.midi.velo_buttons).CutRight(btn_gap * 2).CutBottom(btn_gap);

                for (auto const btn_ind : Range(k_num_btns)) {
                    bool state = ToInt(layer->GetVelocityMode()) == btn_ind;
                    auto imgui_id = imgui.GetID(layer_gui::k_velo_btn_tooltips[(usize)btn_ind]);

                    Rect btn_r {.xywh {whole_velo_r.x + (whole_velo_r.w / 3) * (btn_ind % 3),
                                       whole_velo_r.y + (whole_velo_r.h / 2) * (f32)(int)(btn_ind / 3),
                                       whole_velo_r.w / 3,
                                       whole_velo_r.h / 2}};

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
                        SetParameterValue(g->engine.processor, velo_param_id, (f32)btn_ind, {});
                    }

                    Tooltip(g, imgui_id, btn_r, layer_gui::k_velo_btn_tooltips[(usize)btn_ind]);
                }

                labels::Label(g,
                              layer->params[ToInt(LayerParamIndex::VelocityMapping)],
                              c.midi.velo_name,
                              labels::Parameter(imgui));

                auto const label_id = imgui.GetID("velobtn");
                auto const label_r = layout::GetRect(lay, c.midi.velo_name);
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

            auto const rate_name_r = layout::GetRect(lay, c.lfo.rate.label);
            labels::Label(g, *rate_param, rate_name_r, labels::ParameterCentred(imgui, greyed_out));

            auto const lfo_sync_switch_width = LiveSize(imgui, LFO_SyncSwitchWidth);
            auto const lfo_sync_switch_height = LiveSize(imgui, LFO_SyncSwitchHeight);
            auto const lfo_sync_switch_gap_y = LiveSize(imgui, LFO_SyncSwitchGapY);

            Rect const sync_r {.xywh {rate_name_r.x + rate_name_r.w / 2 - lfo_sync_switch_width / 2,
                                      rate_name_r.Bottom() + lfo_sync_switch_gap_y,
                                      lfo_sync_switch_width,
                                      lfo_sync_switch_height}};
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
        auto const tab_r = layout::GetRect(lay, c.tabs[i]);
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
    auto const& layer_processor = engine->processor.layer_processors[(usize)layer->index];
    if (layer_processor.is_silent.Load(LoadMemoryOrder::Relaxed)) {
        auto pos = imgui.curr_window->unpadded_bounds.pos;
        imgui.graphics->AddRectFilled(pos, pos + imgui.Size(), LiveCol(imgui, UiColMap::LayerMutedOverlay));
    }
}

} // namespace layer_gui
