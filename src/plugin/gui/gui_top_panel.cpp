// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome5.h>

#include "gui_button_widgets.hpp"
#include "gui_menu.hpp"
#include "gui_peak_meter_widget.hpp"
#include "gui_standalone_popups.hpp"
#include "gui_widget_compounds.hpp"
#include "gui_widget_helpers.hpp"
#include "plugin_instance.hpp"
#include "presets_folder.hpp"
#include "state/state_snapshot.hpp"

static void PresetsWindowButton(Gui* g, PluginInstance* a, Rect r) {
    auto& imgui = g->imgui;

    auto button_id = imgui.GetID("PresetMenu");

    DynamicArrayInline<char, 100> preset_text {a->last_snapshot.metadata.Name()};
    if (StateChangedSinceLastSnapshot(*a)) dyn::AppendSpan(preset_text, " (modified)"_s);

    if (buttons::Button(g, button_id, r, preset_text, buttons::PresetsPopupButton(g->imgui))) {
        if (!g->preset_browser_data.show_preset_panel) g->preset_browser_data.ShowPresetBrowser();
    }

    Tooltip(g, button_id, r, "Open presets window"_s);
}

static void DoSettingsMenuItems(Gui* g) {
    String const longest_string_in_menu = "Randomise All Parameters";
    PopupMenuItems top_menu(g, {&longest_string_in_menu, 1});

    if (top_menu.DoButton("Reset All Parameters")) SetAllParametersToDefaultValues(g->plugin.processor);
    if (top_menu.DoButton("Randomise All Parameters")) RandomiseAllParameterValues(g->plugin.processor);
    top_menu.Divider();

    if (top_menu.DoSubMenuButton("Information", g->imgui.GetID("about"))) {
        String const longest_string_in_submenu = "Licences";
        PopupMenuItems submenu(g, {&longest_string_in_submenu, 1});
        if (submenu.DoButton("About")) {
            g->imgui.ClosePopupToLevel(0);
            g->imgui.OpenPopup(GetStandaloneID(StandaloneWindows::About));
        }
        if (submenu.DoButton("Metrics")) {
            g->imgui.ClosePopupToLevel(0);
            g->imgui.OpenPopup(GetStandaloneID(StandaloneWindows::Metrics));
        }
        if (submenu.DoButton("Licences")) {
            g->imgui.ClosePopupToLevel(0);
            g->imgui.OpenPopup(GetStandaloneID(StandaloneWindows::Licences));
        }
        g->imgui.EndWindow();
    }
}

void TopPanel(Gui* g) {
    auto& imgui = g->imgui;
    auto& lay = g->layout;
    auto& plugin = g->plugin;

    bool const has_insts_with_dynamics = true; // TODO(1.0), get the value properly?
    auto title_font = g->mada_big;

    auto const title_width = LiveSize(imgui, UiSizeId::Top2TitleWidth);
    auto const title_margin_l = LiveSize(imgui, UiSizeId::Top2TitleMarginL);
    auto const preset_box_margin_l = LiveSize(imgui, UiSizeId::Top2PresetBoxMarginL);
    auto const preset_box_margin_r = LiveSize(imgui, UiSizeId::Top2PresetBoxMarginR);
    auto const preset_box_pad_r = LiveSize(imgui, UiSizeId::Top2PresetBoxPadR);
    auto const preset_box_w = LiveSize(imgui, UiSizeId::Top2PresetBoxW);
    auto const preset_box_icon_width = LiveSize(imgui, UiSizeId::Top2PresetBoxIconWidth);
    auto const icon_width = LiveSize(imgui, UiSizeId::Top2IconWidth);
    auto const icon_height = LiveSize(imgui, UiSizeId::Top2IconHeight);
    auto const knobs_margin_l = LiveSize(imgui, UiSizeId::Top2KnobsMarginL);
    auto const knobs_margin_r = LiveSize(imgui, UiSizeId::Top2KnobsMarginR);
    auto const peak_meter_w = LiveSize(imgui, UiSizeId::Top2PeakMeterW);
    auto const peak_meter_h = LiveSize(imgui, UiSizeId::Top2PeakMeterH);

    auto root = lay.CreateRootItem((LayScalar)imgui.Width(), (LayScalar)imgui.Height(), LAY_ROW | LAY_START);

    auto left_container = lay.CreateParentItem(root, 0, 1, LAY_VFILL, LAY_ROW | LAY_START);

    auto title = lay.CreateChildItem(left_container,
                                     title_width,
                                     (LayScalar)title_font->font_size_no_scale,
                                     LAY_VCENTER);
    lay.SetLeftMargin(title, title_margin_l);

    auto right_container = lay.CreateParentItem(root, 1, 1, LAY_FILL, LAY_ROW | LAY_END);

    auto preset_box = lay.CreateParentItem(right_container, 0, 0, LAY_VCENTER, LAY_ROW | LAY_START);
    lay.SetRightMargin(preset_box, preset_box_margin_r);
    lay.SetLeftMargin(preset_box, preset_box_margin_l);

    auto preset_menu = lay.CreateChildItem(preset_box, preset_box_w, icon_height, 0);
    auto preset_left = lay.CreateChildItem(preset_box, preset_box_icon_width, icon_height, 0);
    auto preset_right = lay.CreateChildItem(preset_box, preset_box_icon_width, icon_height, 0);
    auto preset_random = lay.CreateChildItem(preset_box, preset_box_icon_width, icon_height, 0);
    auto preset_random_menu = lay.CreateChildItem(preset_box, preset_box_icon_width, icon_height, 0);
    auto preset_save = lay.CreateChildItem(preset_box, preset_box_icon_width, icon_height, 0);
    lay.SetRightMargin(preset_save, preset_box_pad_r);

    auto box = lay.CreateChildItem(right_container, icon_width, icon_height, LAY_VCENTER);
    auto cog = lay.CreateChildItem(right_container, icon_width, icon_height, LAY_VCENTER);
    auto menu = lay.CreateChildItem(right_container, icon_width, icon_height, LAY_VCENTER);

    auto knob_container = lay.CreateParentItem(right_container, 0, 0, LAY_VCENTER, LAY_ROW);
    lay.SetMargins(knob_container, knobs_margin_l, 0, knobs_margin_r, 0);
    LayIDPair dyn;
    LayoutParameterComponent(g,
                             knob_container,
                             dyn,
                             plugin.processor.params[ToInt(ParamIndex::MasterDynamics)],
                             UiSizeId::Top2KnobsGapX);
    LayIDPair velo;
    LayoutParameterComponent(g,
                             knob_container,
                             velo,
                             plugin.processor.params[ToInt(ParamIndex::MasterVelocity)],
                             UiSizeId::Top2KnobsGapX);
    LayIDPair vol;
    LayoutParameterComponent(g,
                             knob_container,
                             vol,
                             plugin.processor.params[ToInt(ParamIndex::MasterVolume)],
                             UiSizeId::Top2KnobsGapX);

    auto level = lay.CreateChildItem(right_container, peak_meter_w, peak_meter_h, LAY_VCENTER);

    lay.PerformLayout();

    auto preset_rand_r = lay.GetRect(preset_random);
    auto preset_rand_menu_r = lay.GetRect(preset_random_menu);
    auto preset_menu_r = lay.GetRect(preset_menu);
    auto preset_left_r = lay.GetRect(preset_left);
    auto preset_right_r = lay.GetRect(preset_right);
    auto preset_save_r = lay.GetRect(preset_save);
    auto level_r = lay.GetRect(level);

    //
    //
    //
    {
        auto back_r = imgui.GetRegisteredAndConvertedRect(lay.GetRect(preset_box));
        auto const rounding = LiveSize(imgui, UiSizeId::CornerRounding);
        imgui.graphics->AddRectFilled(back_r.Min(),
                                      back_r.Max(),
                                      LiveCol(imgui, UiColMap::TopPanelPresetsBack),
                                      rounding);
    }

    {
        if (title_font) imgui.graphics->context->PushFont(title_font);

        String const name = PRODUCT_NAME;

        auto title_r = lay.GetRect(title).Up(Round(-title_font->descent));

        labels::Label(g, title_r, name, labels::Title(imgui, LiveCol(imgui, UiColMap::TopPanelTitleText)));
        if (title_font) imgui.graphics->context->PopFont();
    }

    imgui.graphics->context->PushFont(g->mada);

    Optional<PresetSelectionCriteria> preset_load_criteria {};

    //
    auto large_icon_button_style = buttons::TopPanelIconButton(imgui).WithLargeIcon();
    {
        auto btn_id = imgui.GetID("L");
        if (buttons::Button(g, btn_id, preset_left_r, ICON_FA_CARET_LEFT, large_icon_button_style))
            preset_load_criteria = DirectoryListing::AdjacentDirection::Previous;
        Tooltip(g, btn_id, preset_left_r, "Load previous preset"_s);
    }
    {
        auto btn_id = imgui.GetID("R");
        if (buttons::Button(g, btn_id, preset_right_r, ICON_FA_CARET_RIGHT, large_icon_button_style))
            preset_load_criteria = DirectoryListing::AdjacentDirection::Next;
        Tooltip(g, btn_id, preset_left_r, "Load next preset"_s);
    }
    if (g->icons) g->frame_input.graphics_ctx->PushFont(g->icons);

    auto& settings = g->settings.settings.gui;
    {
        auto btn_id = imgui.GetID("rand_pre");
        if (buttons::Button(g,
                            btn_id,
                            preset_rand_r,
                            ICON_FA_RANDOM,
                            large_icon_button_style.WithIconScaling(0.8f))) {
            switch ((PresetRandomiseMode)settings.presets_random_mode) {
                case PresetRandomiseMode::All:
                    preset_load_criteria = PresetRandomiseCriteria {PresetRandomiseMode::All};
                    break;
                case PresetRandomiseMode::BrowserFilters:
                    preset_load_criteria = PresetRandomiseCriteria {plugin.preset_browser_filters};
                    break;
                case PresetRandomiseMode::Folder:
                    preset_load_criteria = PresetRandomiseCriteria {PresetRandomiseMode::Folder};
                    break;
                case PresetRandomiseMode::Library:
                    // TODO(1.0):
                    break;
            }
        }

        String preset_mode_description = {};
        switch ((PresetRandomiseMode)settings.presets_random_mode) {
            case PresetRandomiseMode::All: preset_mode_description = "Load any random preset"; break;
            case PresetRandomiseMode::BrowserFilters:
                preset_mode_description =
                    "Load a preset based on the filters set in the preset browser (same as the button "
                    "adjacent to the search bar on the browser panel)";
                break;
            case PresetRandomiseMode::Folder:
                preset_mode_description =
                    "Load a random preset from the same folder as the currently loaded preset";
                break;
            case PresetRandomiseMode::Library:
                preset_mode_description =
                    "Load a random preset from the same library as the currently loaded library";
                break;
        }

        constexpr String k_second_sentence =
            "You can change the randomisation mode by clicking the down-arrow icon to the right";

        Tooltip(g,
                btn_id,
                preset_rand_r,
                fmt::Format(g->scratch_arena, "{}. {}", preset_mode_description, k_second_sentence));
    }

    if (preset_load_criteria) {
        LoadPresetFromListing(plugin,
                              *preset_load_criteria,
                              FetchOrRescanPresetsFolder(
                                  plugin.shared_data.preset_listing,
                                  RescanMode::RescanAsyncIfNeeded,
                                  plugin.shared_data.settings.settings.filesystem.extra_presets_scan_folders,
                                  &plugin.shared_data.thread_pool));
        g->preset_browser_data.scroll_to_show_current_preset = true;
    }

    {
        auto const btn_id = imgui.GetID("save");
        auto const pop_id = imgui.GetID("save_pop");
        if (buttons::Popup(g, btn_id, pop_id, preset_save_r, ICON_FA_SAVE, large_icon_button_style)) {
            auto save_over_text = fmt::Format(g->scratch_arena,
                                              "Save (Overwrite \"{}\")",
                                              g->plugin.last_snapshot.metadata.Name());
            auto const existing_path = g->plugin.last_snapshot.metadata.Path();
            auto ptr = existing_path ? String(save_over_text) : "Save Preset As"_s;

            PopupMenuItems items(g, {&ptr, 1});
            if (existing_path) {
                if (items.DoButton(save_over_text)) SaveCurrentStateToFile(plugin, *existing_path);
            }

            if (items.DoButton("Save Preset As")) g->OpenDialog(DialogType::SavePreset);

            imgui.EndWindow();
        }
        Tooltip(g, btn_id, preset_save_r, "Save the current state as a preset"_s);
    }

    if (g->icons) g->frame_input.graphics_ctx->PopFont();
    {
        auto btn_id = imgui.GetID("rand_pre_menu");
        auto pop_id = imgui.GetID("rand_pre_menu_pop");
        if (buttons::Popup(g,
                           btn_id,
                           pop_id,
                           preset_rand_menu_r,
                           ICON_FA_CARET_DOWN,
                           large_icon_button_style)) {
            Array<String, 4> options = {
                "Button Mode: Random Any Preset",
                "Button Mode: Random Same Folder Preset",
                "Button Mode: Random Same Library Preset",
                "Button Mode: Random Preset From Browser Filters",
            };

            PopupMenuItems items(g, options.Items());
            auto mode = settings.presets_random_mode;
            if (items.DoMultipleMenuItems(mode)) {
                g->settings.tracking.changed = true;
                settings.presets_random_mode = mode;
            }

            imgui.EndWindow();
        }
        Tooltip(g, btn_id, preset_rand_menu_r, "Select the mode of the random-preset button"_s);
    }

    {
        auto btn_id = imgui.GetID("install");
        auto btn_r = lay.GetRect(box);
        if (buttons::Button(g, btn_id, btn_r, ICON_FA_BOX_OPEN, large_icon_button_style))
            OpenStandalone(imgui, StandaloneWindows::InstallWizard);
        Tooltip(g, btn_id, btn_r, "Install libraries"_s);
    }

    {
        auto btn_id = imgui.GetID("sets");
        auto btn_r = lay.GetRect(cog);
        if (buttons::Button(g, btn_id, btn_r, ICON_FA_COG, large_icon_button_style))
            OpenStandalone(imgui, StandaloneWindows::Settings);
        Tooltip(g, btn_id, btn_r, "Open settings window"_s);
    }

    {
        auto additonal_menu_r = lay.GetRect(menu);
        auto additional_menu_id = imgui.GetID("Menu");
        auto popup_id = imgui.GetID("MenuPopup");
        if (buttons::Popup(g,
                           additional_menu_id,
                           popup_id,
                           additonal_menu_r,
                           ICON_FA_ELLIPSIS_V,
                           large_icon_button_style)) {
            DoSettingsMenuItems(g);
            imgui.EndWindow();
        }
        Tooltip(g, additional_menu_id, additonal_menu_r, "Additional functions and information"_s);
    }

    imgui.graphics->context->PopFont();
    PresetsWindowButton(g, &plugin, preset_menu_r);

    //

    peak_meters::PeakMeter(g, level_r, plugin.processor.peak_meter, true);

    KnobAndLabel(g, plugin.processor.params[ToInt(ParamIndex::MasterVolume)], vol, knobs::DefaultKnob(imgui));
    KnobAndLabel(g,
                 plugin.processor.params[ToInt(ParamIndex::MasterVelocity)],
                 velo,
                 knobs::DefaultKnob(imgui));

    {
        g->dynamics_slider_is_held = false;
        auto const id = imgui.GetID((u64)plugin.processor.params[ToInt(ParamIndex::MasterDynamics)].info.id);
        if (has_insts_with_dynamics) {
            knobs::Knob(g,
                        id,
                        plugin.processor.params[ToInt(ParamIndex::MasterDynamics)],
                        dyn.control,
                        knobs::DefaultKnob(imgui));
            g->dynamics_slider_is_held = imgui.IsActive(id);
            if (imgui.WasJustActivated(id))
                g->imgui.frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
        } else {
            auto knob_r = lay.GetRect(dyn.control);
            knobs::FakeKnob(g, knob_r);

            imgui.RegisterAndConvertRect(&knob_r);
            imgui.ButtonBehavior(knob_r, id, {});
            Tooltip(
                g,
                id,
                knob_r,
                "Dynamics: no currently loaded instruments have dynamics information; this knob is inactive"_s);
            if (imgui.IsHot(id)) imgui.frame_output.cursor_type = CursorType::Default;
        }
        labels::Label(g,
                      plugin.processor.params[ToInt(ParamIndex::MasterDynamics)],
                      dyn.label,
                      labels::ParameterCentred(imgui, !has_insts_with_dynamics));
    }

    lay.Reset();
}
