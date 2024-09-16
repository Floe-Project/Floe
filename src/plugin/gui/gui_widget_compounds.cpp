// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_widget_compounds.hpp"

#include "descriptors/param_descriptors.hpp"
#include "gui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_label_widgets.hpp"
#include "gui_widget_helpers.hpp"

layout::Id LayoutParameterComponent(Gui* g,
                                    layout::Id parent,
                                    layout::Id& param_layid,
                                    layout::Id& name,
                                    LayoutType type,
                                    Optional<ParamIndex> index_for_menu_items,
                                    bool is_convo_ir,
                                    Optional<UiSizeId> size_index_for_gapx,
                                    bool set_gapx_independent_of_size,
                                    bool set_bottom_gap_independent_of_size) {
    auto& imgui = g->imgui;
    auto& lay = g->layout;

    auto const param_component_large_width = LiveSize(imgui, UiSizeId::ParamComponentLargeWidth);
    auto const param_component_small_width = LiveSize(imgui, UiSizeId::ParamComponentSmallWidth);
    auto const param_component_extra_small_width = LiveSize(imgui, UiSizeId::ParamComponentExtraSmallWidth);
    auto const param_component_height_offset = LiveSize(imgui, UiSizeId::ParamComponentHeightOffset);
    auto const param_component_margin_lr = LiveSize(imgui, UiSizeId::ParamComponentMarginLR);
    auto const param_component_margin_t = LiveSize(imgui, UiSizeId::ParamComponentMarginT);
    auto const param_component_margin_b = LiveSize(imgui, UiSizeId::ParamComponentMarginB);
    auto const param_component_label_gap_y = LiveSize(imgui, UiSizeId::ParamComponentLabelGapY);
    auto const param_popup_button_height = LiveSize(imgui, UiSizeId::ParamPopupButtonHeight);
    auto const menu_button_text_margin_l = LiveSize(imgui, UiSizeId::MenuButtonTextMarginL);
    auto const fx_convo_ir_width = LiveSize(imgui, UiSizeId::FXConvoIRWidth);

    auto width = type == LayoutType::Layer ? param_component_large_width
                                           : (type == LayoutType::Effect ? param_component_small_width
                                                                         : param_component_extra_small_width);
    auto const starting_width = width;
    auto height = width - param_component_height_offset;
    auto const starting_height = height;
    auto gap_x = size_index_for_gapx ? LiveSize(imgui, *size_index_for_gapx) : param_component_margin_lr;
    auto gap_bottom = param_component_margin_b;
    auto gap_top = param_component_margin_t;

    if (index_for_menu_items) {
        auto const menu_items = ParameterMenuItems(*index_for_menu_items);
        auto strings_width = MaxStringLength(g, menu_items) + menu_button_text_margin_l * 2;
        width = (f32)strings_width;
        height = (f32)param_popup_button_height;
    } else if (is_convo_ir) {
        height = (f32)param_popup_button_height;
        width = fx_convo_ir_width;
    }

    if (set_gapx_independent_of_size && width != starting_width)
        gap_x -= (f32)Max(0.0f, (width - starting_width) / 2);

    if (set_bottom_gap_independent_of_size && height != starting_height) {
        auto const delta = (f32)Max(0.0f, starting_height - height);
        gap_bottom += (f32)(delta / 2);
        gap_top += (f32)(delta / 2);
    }

    auto container = layout::CreateItem(lay,
                                        {
                                            .parent = parent,
                                            .size = {0, 0},
                                            .margins {.lr = gap_x, .t = gap_top, .b = gap_bottom},
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::JustifyContent::Start,

                                        });

    param_layid = layout::CreateItem(lay,
                                     {
                                         .parent = container,
                                         .size = {width, (f32)height},
                                         .margins = {.b = param_component_label_gap_y},
                                     });
    name = layout::CreateItem(lay,
                              {
                                  .parent = container,
                                  .size = {width, (f32)(imgui.graphics->context->CurrentFontSize())},
                              });

    return container;
}

layout::Id LayoutParameterComponent(Gui* g,
                                    layout::Id parent,
                                    layout::Id& param_layid,
                                    layout::Id& name,
                                    Parameter const& param,
                                    Optional<UiSizeId> size_index_for_gapx,
                                    bool set_gapx_independent_of_size,
                                    bool set_bottom_gap_independent_of_size) {
    auto result = LayoutParameterComponent(
        g,
        parent,
        param_layid,
        name,
        param.info.IsLayerParam() ? LayoutType::Layer
                                  : (param.info.IsEffectParam() ? LayoutType::Effect : LayoutType::Generic),
        param.info.value_type == ParamValueType::Menu ? Optional<ParamIndex> {param.info.index} : k_nullopt,
        false,
        size_index_for_gapx,
        set_gapx_independent_of_size,
        set_bottom_gap_independent_of_size);

    if (param.info.value_type == ParamValueType::Int) {
        auto const dragger_width = LiveSize(g->imgui, UiSizeId::FXDraggerWidth);
        auto const dragger_height = LiveSize(g->imgui, UiSizeId::FXDraggerHeight);
        auto const dragger_margin_t = LiveSize(g->imgui, UiSizeId::FXDraggerMarginT);
        auto const dragger_margin_b = LiveSize(g->imgui, UiSizeId::FXDraggerMarginB);

        layout::SetSize(g->layout, param_layid, f32x2 {dragger_width, dragger_height});
        auto margins = layout::GetMargins(g->layout, param_layid);
        margins.t = dragger_margin_t;
        margins.b = dragger_margin_b;
        layout::SetMargins(g->layout, param_layid, margins);
    }

    return result;
}

layout::Id LayoutParameterComponent(Gui* g,
                                    layout::Id parent,
                                    LayIDPair& ids,
                                    Parameter const& param,
                                    Optional<UiSizeId> size_index_for_gapx,
                                    bool set_gapx_independent_of_size,
                                    bool set_bottom_gap_independent_of_size) {
    return LayoutParameterComponent(g,
                                    parent,
                                    ids.control,
                                    ids.label,
                                    param,
                                    size_index_for_gapx,
                                    set_gapx_independent_of_size,
                                    set_bottom_gap_independent_of_size);
}

bool KnobAndLabel(Gui* g,
                  Parameter const& param,
                  Rect knob_r,
                  Rect label_r,
                  knobs::Style const& style,
                  bool greyed_out) {
    knobs::Style knob_style = style;
    knob_style.GreyedOut(greyed_out);
    if (param.info.display_format == ParamDisplayFormat::VolumeAmp)
        knob_style.overload_position = param.info.LineariseValue(1, true);
    bool const changed = knobs::Knob(g, param, knob_r, knob_style);
    labels::Label(g, param, label_r, labels::ParameterCentred(g->imgui, greyed_out));
    return changed;
}

bool KnobAndLabel(Gui* g, Parameter const& param, LayIDPair ids, knobs::Style const& style, bool greyed_out) {
    return KnobAndLabel(g,
                        param,
                        layout::GetRect(g->layout, ids.control),
                        layout::GetRect(g->layout, ids.label),
                        style,
                        greyed_out);
}
