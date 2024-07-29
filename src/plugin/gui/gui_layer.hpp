// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui_widget_compounds.hpp"
#include "layout.hpp"
#include "plugin_instance.hpp"

struct Gui;
struct PluginInstance;

namespace layer_gui {

static constexpr Array<String, 6> k_velo_btn_tooltips = {
    "No special volume-velocity relationship",
    "Loudest at high velocity, quietest at low velocity",
    "Loudest at low velocity, quietest at high velocity",
    "Loudest at high velocity, quietest at middle velocity and below",
    "Loudest at middle velocity, quietest at both high and low velocities",
    "Loudest at bottom velocity, quietest at middle velocity and above",
};

enum class PageType {
    Main,
    Filter,
    Lfo,
    Eq,
    Midi,
    Count,
};

constexpr usize k_num_pages = ToInt(PageType::Count);

struct LayerLayoutTempIDs {
    LayID selector_box;
    LayID selector_menu;
    LayID selector_l, selector_r;
    LayID selector_randomise;

    LayID volume;
    LayID mute_solo;

    LayIDPair knob1, knob2, knob3;

    LayID divider;
    LayID tabs[k_num_pages];
    LayID divider2;

    union {
        struct {
            LayID waveform;
            LayID loop_mode;
            LayID reverse;

            LayID divider;

            LayID env_on;
            LayID envelope;
        } main;

        struct {
            LayID filter_on;
            LayID filter_type;
            LayIDPair cutoff;
            LayIDPair reso;
            LayIDPair env_amount;
            LayID envelope;
        } filter;

        struct {
            LayID on;
            LayID type[k_num_layer_eq_bands];
            LayIDPair freq[k_num_layer_eq_bands];
            LayIDPair reso[k_num_layer_eq_bands];
            LayIDPair gain[k_num_layer_eq_bands];
        } eq;

        struct {
            LayID transpose;
            LayID transpose_name;
            LayID keytrack;
            LayID mono;
            LayID retrig;
            LayID velo_buttons;
            LayID velo_name;
        } midi;

        struct {
            LayID on;
            LayIDPair amount;
            LayIDPair rate;
            LayID target;
            LayID target_name;
            LayID shape;
            LayID shape_name;
            LayID mode;
            LayID mode_name;
        } lfo;
    };
};

struct LayerLayout {
    PageType selected_page;
};

void Layout(Gui* g,
            LayerProcessor* layer,
            LayerLayoutTempIDs& ids,
            LayerLayout* layer_gui,
            f32 width,
            f32 height);

void Draw(Gui* g,
          PluginInstance* a,
          Rect r,
          LayerProcessor* layer,
          LayerLayoutTempIDs& ids,
          LayerLayout* layer_gui);

} // namespace layer_gui
