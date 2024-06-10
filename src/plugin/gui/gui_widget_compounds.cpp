// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_widget_compounds.hpp"

#include "gui.hpp"
#include "gui_editor_ui_style.hpp"
#include "gui_label_widgets.hpp"
#include "gui_widget_helpers.hpp"
#include "param_info.hpp"

LayID LayoutParameterComponent(Gui* g,
                               LayID parent,
                               LayID& param_layid,
                               LayID& name,
                               LayoutType type,
                               Optional<ParamIndex> index_for_menu_items,
                               bool is_convo_ir,
                               Optional<UiSizeId> size_index_for_gapx,
                               bool set_gapx_independent_of_size,
                               bool set_bottom_gap_independent_of_size) {
    auto& imgui = g->imgui;
    auto& lay = g->layout;

#define GUI_SIZE(cat, n, v, u) [[maybe_unused]] const auto cat##n = editor::GetSize(imgui, UiSizeId::cat##n);
#include SIZES_DEF_FILENAME
#undef GUI_SIZE

    auto width = type == LayoutType::Layer ? ParamComponentLargeWidth
                                           : (type == LayoutType::Effect ? ParamComponentSmallWidth
                                                                         : ParamComponentExtraSmallWidth);
    auto const starting_width = width;
    auto height = width - ParamComponentHeightOffset;
    auto const starting_height = height;
    auto gap_x = size_index_for_gapx ? editor::GetSize(imgui, *size_index_for_gapx) : ParamComponentMarginLR;
    auto gap_bottom = ParamComponentMarginB;
    auto gap_top = ParamComponentMarginT;

    if (index_for_menu_items) {
        auto const menu_items = ParameterMenuItems(*index_for_menu_items);
        auto strings_width = MaxStringLength(g, menu_items) + MenuButtonTextMarginL * 2;
        width = (LayScalar)strings_width;
        height = (LayScalar)ParamPopupButtonHeight;
    } else if (is_convo_ir) {
        height = (LayScalar)ParamPopupButtonHeight;
        width = FXConvoIRWidth;
    }

    if (set_gapx_independent_of_size && width != starting_width)
        gap_x -= (LayScalar)Max(0.0f, (width - starting_width) / 2);

    if (set_bottom_gap_independent_of_size && height != starting_height) {
        auto const delta = (LayScalar)Max(0.0f, starting_height - height);
        gap_bottom += (LayScalar)(delta / 2);
        gap_top += (LayScalar)(delta / 2);
    }

    auto container = lay.CreateParentItem(parent, 0, 0, 0, LAY_COLUMN | LAY_START);
    lay.SetMargins(container, gap_x, gap_top, gap_x, gap_bottom);

    param_layid = lay.CreateChildItem(container, width, (LayScalar)height, 0);
    lay.SetBottomMargin(param_layid, ParamComponentLabelGapY);
    name = lay.CreateChildItem(container, width, (LayScalar)(imgui.graphics->context->CurrentFontSize()), 0);

    return container;
}

LayID LayoutParameterComponent(Gui* g,
                               LayID parent,
                               LayID& param_layid,
                               LayID& name,
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
        param.info.value_type == ParamValueType::Menu ? Optional<ParamIndex> {param.info.index} : nullopt,
        false,
        size_index_for_gapx,
        set_gapx_independent_of_size,
        set_bottom_gap_independent_of_size);

    if (param.info.value_type == ParamValueType::Int) {
        auto const dragger_width = editor::GetSize(g->imgui, UiSizeId::FXDraggerWidth);
        auto const dragger_height = editor::GetSize(g->imgui, UiSizeId::FXDraggerHeight);
        auto const dragger_margin_t = editor::GetSize(g->imgui, UiSizeId::FXDraggerMarginT);
        auto const dragger_margin_b = editor::GetSize(g->imgui, UiSizeId::FXDraggerMarginB);

        lay_set_size_xy(&g->layout.ctx, param_layid, dragger_width, dragger_height);
        g->layout.SetTopMargin(param_layid, dragger_margin_t);
        g->layout.SetBottomMargin(param_layid, dragger_margin_b);
    }

    return result;
}

LayID LayoutParameterComponent(Gui* g,
                               LayID parent,
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
    labels::Label(g, param, label_r, labels::ParameterCentred(greyed_out));
    return changed;
}

bool KnobAndLabel(Gui* g, Parameter const& param, LayIDPair ids, knobs::Style const& style, bool greyed_out) {
    return KnobAndLabel(g,
                        param,
                        g->layout.GetRect(ids.control),
                        g->layout.GetRect(ids.label),
                        style,
                        greyed_out);
}
