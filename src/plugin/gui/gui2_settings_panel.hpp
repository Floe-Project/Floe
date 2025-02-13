// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "os/misc.hpp"

#include "engine/package_installation.hpp"
#include "gui2_common_modal_panel.hpp"
#include "gui2_settings_panel_state.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "sample_lib_server/sample_library_server.hpp"
#include "settings/settings.hpp"
#include "settings/settings_filesystem.hpp"
#include "settings/settings_gui.hpp"

static void SettingsLhsTextWidget(GuiBoxSystem& box_system, Box parent, String text) {
    DoBox(
        box_system,
        {
            .parent = parent,
            .text = text,
            .font = FontType::Body,
            .layout {
                .size = {style::k_settings_lhs_width,
                         box_system.imgui.PixelsToPoints(box_system.fonts[ToInt(FontType::Body)]->font_size)},
            },
        });
}

static void SettingsRhsText(GuiBoxSystem& box_system, Box parent, String text) {
    DoBox(box_system,
          {
              .parent = parent,
              .text = text,
              .font = FontType::Body,
              .text_fill = style::Colour::Subtext0,
              .size_from_text = true,
          });
}

static Box SettingsMenuButton(GuiBoxSystem& box_system, Box parent, String text, String tooltip) {
    auto const button =
        DoBox(box_system,
              {
                  .parent = parent,
                  .background_fill = style::Colour::Background2,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .activate_on_click_button = MouseButton::Left,
                  .activation_click_event = ActivationClickEvent::Up,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding = {.lr = style::k_button_padding_x, .tb = style::k_button_padding_y},
                      .contents_align = layout::Alignment::Justify,
                  },
                  .tooltip = tooltip,
              });

    DoBox(box_system,
          {
              .parent = button,
              .text = text,
              .font = FontType::Body,
              .size_from_text = true,
          });

    DoBox(box_system,
          {
              .parent = button,
              .text = ICON_FA_CARET_DOWN,
              .font = FontType::Icons,
              .size_from_text = true,
          });

    return button;
}

static Box SettingsRow(GuiBoxSystem& box_system, Box parent) {
    return DoBox(box_system,
                 {
                     .parent = parent,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_direction = layout::Direction::Row,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                     },
                 });
}

static Box SettingsRhsColumn(GuiBoxSystem& box_system, Box parent, f32 gap) {
    return DoBox(box_system,
                 {
                     .parent = parent,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_gap = gap,
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                     },
                 });
}

struct FolderSelectorResult {
    bool delete_pressed;
    bool open_pressed;
};

static FolderSelectorResult
SettingsFolderSelector(GuiBoxSystem& box_system, Box parent, String path, String subtext, bool deletable) {
    auto const container = DoBox(box_system,
                                 {
                                     .parent = parent,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = style::k_settings_small_gap,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                     },
                                 });

    auto const path_container =
        DoBox(box_system,
              {
                  .parent = container,
                  .background_fill = style::Colour::Background1,
                  .round_background_corners = 0b1111,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding = {.lr = style::k_button_padding_x, .tb = style::k_button_padding_y},
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Justify,
                  },
              });
    DoBox(box_system,
          {
              .parent = path_container,
              .text = path,
              .font = FontType::Body,
              .size_from_text = true,
          });
    auto const icon_button_container = DoBox(box_system,
                                             {
                                                 .parent = path_container,
                                                 .layout {
                                                     .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                     .contents_gap = style::k_settings_small_gap,
                                                     .contents_direction = layout::Direction::Row,
                                                 },
                                             });

    FolderSelectorResult result {};
    if (deletable) {
        result.delete_pressed = DoBox(box_system,
                                      {
                                          .parent = icon_button_container,
                                          .text = ICON_FA_TRASH_ALT,
                                          .font = FontType::Icons,
                                          .text_fill = style::Colour::Subtext0,
                                          .text_fill_hot = style::Colour::Subtext0,
                                          .text_fill_active = style::Colour::Subtext0,
                                          .size_from_text = true,
                                          .background_fill_auto_hot_active_overlay = true,
                                          .round_background_corners = 0b1111,
                                          .activate_on_click_button = MouseButton::Left,
                                          .activation_click_event = ActivationClickEvent::Up,
                                          .extra_margin_for_mouse_events = 2,
                                          .tooltip = "Stop scanning this folder"_s,
                                      })
                                    .button_fired;
    }
    result.open_pressed =
        DoBox(box_system,
              {
                  .parent = icon_button_container,
                  .text = ICON_FA_EXTERNAL_LINK_ALT,
                  .font = FontType::Icons,
                  .text_fill = style::Colour::Subtext0,
                  .text_fill_hot = style::Colour::Subtext0,
                  .text_fill_active = style::Colour::Subtext0,
                  .size_from_text = true,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .activate_on_click_button = MouseButton::Left,
                  .activation_click_event = ActivationClickEvent::Up,
                  .extra_margin_for_mouse_events = 2,
                  .tooltip = fmt::FormatInline<64>("Open folder in {}"_s, GetFileBrowserAppName()),
              })
            .button_fired;

    if (subtext.size) SettingsRhsText(box_system, container, subtext);

    return result;
}

struct SettingsPanelContext {
    SettingsFile& settings;
    sample_lib_server::Server& sample_lib_server;
    package::InstallJobs& package_install_jobs;
    ThreadPool& thread_pool;
};

static void SetFolderSubtext(DynamicArrayBounded<char, 200>& out,
                             String dir,
                             bool is_default,
                             ScanFolderType type,
                             sample_lib_server::Server& server) {
    dyn::Clear(out);
    switch (type) {
        case ScanFolderType::Libraries: {
            u32 num_libs = 0;
            for (auto& l_node : server.libraries) {
                if (auto l = l_node.TryScoped()) {
                    if (path::IsWithinDirectory(l->lib->path, dir)) ++num_libs;
                }
            }

            if (is_default) dyn::AppendSpan(out, "Default. ");
            dyn::AppendSpan(out, "Contains ");
            if (num_libs < 1000 && out.size + 4 < out.Capacity())
                out.size += fmt::IntToString(num_libs, out.data + out.size);
            else if (num_libs)
                dyn::AppendSpan(out, "many");
            else
                dyn::AppendSpan(out, "no"_s);
            fmt::Append(out, " sample librar{}", num_libs == 1 ? "y" : "ies");
            break;
        }
        case ScanFolderType::Presets: {
            if (is_default) dyn::AppendSpan(out, "Default.");
            // IMPROVE: show number of presets in folder
            break;
        }
        case ScanFolderType::Count: break;
    }
}

static bool AddExtraScanFolderDialog(GuiBoxSystem& box_system,
                                     SettingsPanelContext& context,
                                     ScanFolderType type,
                                     bool set_as_install_location) {
    Optional<String> default_folder {};
    if (auto const extra_paths = context.settings.settings.filesystem.extra_scan_folders[ToInt(type)];
        extra_paths.size)
        default_folder = extra_paths[0];

    if (auto const o = FilesystemDialog({
            .type = DialogArguments::Type::SelectFolder,
            .allocator = box_system.arena,
            .title = fmt::Format(box_system.arena, "Select {} Folder", ({
                                     String s {};
                                     switch (type) {
                                         case ScanFolderType::Libraries: s = "Libraries"; break;
                                         case ScanFolderType::Presets: s = "Presets"; break;
                                         case ScanFolderType::Count: PanicIfReached();
                                     }
                                     s;
                                 })),
            .default_path = default_folder,
            .filters = {},
            .parent_window = box_system.imgui.frame_input.native_window,
        });
        o.HasValue()) {
        if (auto const paths = o.Value(); paths.size) {
            filesystem_settings::AddScanFolder(context.settings, type, paths[0]);
            if (set_as_install_location)
                filesystem_settings::SetInstallLocation(context.settings, type, paths[0]);
            return true;
        }
    } else {
        LogError(ModuleName::Gui, "Failed to create dialog: {}", o.Error());
    }
    return false;
}

static void FolderSettingsPanel(GuiBoxSystem& box_system, SettingsPanelContext& context) {
    sample_lib_server::RequestScanningOfUnscannedFolders(context.sample_lib_server);

    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToPoints(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = style::k_spacing},
                                    .contents_gap = style::k_settings_large_gap,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    for (auto const scan_folder_type : Range(ToInt(ScanFolderType::Count))) {
        auto const row = SettingsRow(box_system, root);
        SettingsLhsTextWidget(box_system, row, ({
                                  String s;
                                  switch ((ScanFolderType)scan_folder_type) {
                                      case ScanFolderType::Libraries: s = "Sample library folders"; break;
                                      case ScanFolderType::Presets: s = "Preset folders"; break;
                                      case ScanFolderType::Count: PanicIfReached();
                                  }
                                  s;
                              }));

        auto const rhs_column = SettingsRhsColumn(box_system, row, style::k_settings_medium_gap);

        DynamicArrayBounded<char, 200> subtext_buffer {};

        {
            auto const dir = context.settings.paths.always_scanned_folder[scan_folder_type];
            SetFolderSubtext(subtext_buffer,
                             dir,
                             true,
                             (ScanFolderType)scan_folder_type,
                             context.sample_lib_server);
            if (auto const o = SettingsFolderSelector(box_system, rhs_column, dir, subtext_buffer, false);
                o.open_pressed)
                OpenFolderInFileBrowser(dir);
        }

        Optional<String> to_remove {};
        for (auto const dir : context.settings.settings.filesystem.extra_scan_folders[scan_folder_type]) {
            SetFolderSubtext(subtext_buffer,
                             dir,
                             false,
                             (ScanFolderType)scan_folder_type,
                             context.sample_lib_server);
            if (auto const o = SettingsFolderSelector(box_system, rhs_column, dir, subtext_buffer, true);
                o.open_pressed || o.delete_pressed) {
                if (o.open_pressed) OpenFolderInFileBrowser(dir);
                if (o.delete_pressed) to_remove = dir;
            }
        }
        if (to_remove)
            filesystem_settings::RemoveScanFolder(context.settings,
                                                  (ScanFolderType)scan_folder_type,
                                                  *to_remove);

        auto const contents_name = ({
            String s;
            switch ((ScanFolderType)scan_folder_type) {
                case ScanFolderType::Libraries: s = "sample libraries"; break;
                case ScanFolderType::Presets: s = "presets"; break;
                case ScanFolderType::Count: PanicIfReached();
            }
            s;
        });
        if (context.settings.settings.filesystem.extra_scan_folders[scan_folder_type].size !=
                k_max_extra_scan_folders &&
            TextButton(box_system,
                       rhs_column,
                       "Add folder",
                       fmt::FormatInline<100>("Add a folder to scan for {}", contents_name))) {
            AddExtraScanFolderDialog(box_system, context, (ScanFolderType)scan_folder_type, false);
        }
    }
}

static void InstallLocationMenu(GuiBoxSystem& box_system,
                                SettingsPanelContext& context,
                                ScanFolderType scan_folder_type) {
    sample_lib_server::RequestScanningOfUnscannedFolders(context.sample_lib_server);

    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    DynamicArrayBounded<char, 200> subtext_buffer {};

    auto const menu_item = [&](String path, String subtext) {
        auto const item = DoBox(box_system,
                                {
                                    .parent = root,
                                    .background_fill_auto_hot_active_overlay = true,
                                    .activate_on_click_button = MouseButton::Left,
                                    .activation_click_event = ActivationClickEvent::Up,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_direction = layout::Direction::Row,
                                    },
                                });

        if (item.button_fired) {
            filesystem_settings::SetInstallLocation(context.settings, scan_folder_type, path);
            box_system.imgui.CloseTopPopupOnly();
        }

        auto const current_install_location =
            context.settings.settings.filesystem.install_location[ToInt(scan_folder_type)];

        DoBox(box_system,
              {
                  .parent = item,
                  .text = path == current_install_location ? String(ICON_FA_CHECK) : "",
                  .font = FontType::Icons,
                  .text_fill = style::Colour::Subtext0,
                  .layout {
                      .size = style::k_settings_icon_button_size,
                      .margins {.l = style::k_menu_item_padding_x},
                  },

              });

        auto const text_container = DoBox(box_system,
                                          {
                                              .parent = item,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .contents_padding = {.lr = style::k_menu_item_padding_x,
                                                                       .tb = style::k_menu_item_padding_y},
                                                  .contents_direction = layout::Direction::Column,
                                                  .contents_align = layout::Alignment::Start,
                                                  .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                              },
                                          });
        DoBox(box_system,
              {
                  .parent = text_container,
                  .text = path,
                  .font = FontType::Body,
                  .size_from_text = true,
              });
        DoBox(box_system,
              {
                  .parent = text_container,
                  .text = subtext,
                  .text_fill = style::Colour::Subtext0,
                  .size_from_text = true,
              });
    };

    {
        auto const dir = context.settings.paths.always_scanned_folder[ToInt(scan_folder_type)];
        SetFolderSubtext(subtext_buffer, dir, true, scan_folder_type, context.sample_lib_server);
        menu_item(dir, subtext_buffer);
    }

    for (auto const dir : context.settings.settings.filesystem.extra_scan_folders[ToInt(scan_folder_type)]) {
        SetFolderSubtext(subtext_buffer, dir, false, scan_folder_type, context.sample_lib_server);
        menu_item(dir, subtext_buffer);
    }

    DoBox(box_system,
          {
              .parent = root,
              .background_fill = style::Colour::Overlay0,
              .layout {
                  .size = {layout::k_fill_parent, box_system.imgui.PixelsToPoints(1)},
                  .margins {.tb = style::k_menu_item_padding_y},
              },
          });

    auto const add_button = DoBox(box_system,
                                  {
                                      .parent = root,
                                      .background_fill_auto_hot_active_overlay = true,
                                      .activate_on_click_button = MouseButton::Left,
                                      .activation_click_event = ActivationClickEvent::Up,
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                                          .contents_padding = {.l = style::k_menu_item_padding_x * 2 +
                                                                    style::k_settings_icon_button_size,
                                                               .r = style::k_menu_item_padding_x,
                                                               .tb = style::k_menu_item_padding_y},
                                          .contents_direction = layout::Direction::Row,
                                          .contents_align = layout::Alignment::Start,
                                      },
                                      .tooltip = "Select a new folder"_s,
                                  });
    DoBox(box_system,
          {
              .parent = add_button,
              .text = "Add folder",
              .size_from_text = true,
          });

    if (add_button.button_fired)
        if (AddExtraScanFolderDialog(box_system, context, scan_folder_type, true))
            box_system.imgui.CloseTopPopupOnly();
}

static void PackagesSettingsPanel(GuiBoxSystem& box_system, SettingsPanelContext& context) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToPoints(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = style::k_spacing},
                                    .contents_gap = style::k_settings_medium_gap,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    for (auto const scan_folder_type : Range(ToInt(ScanFolderType::Count))) {
        auto const row = SettingsRow(box_system, root);
        SettingsLhsTextWidget(box_system, row, ({
                                  String s;
                                  switch ((ScanFolderType)scan_folder_type) {
                                      case ScanFolderType::Libraries:
                                          s = "Sample library install folder";
                                          break;
                                      case ScanFolderType::Presets: s = "Preset install folder"; break;
                                      case ScanFolderType::Count: PanicIfReached();
                                  }
                                  s;
                              }));

        auto const popup_id = box_system.imgui.GetID(scan_folder_type);

        String menu_text = context.settings.settings.filesystem.install_location[scan_folder_type];
        if (auto const default_dir = context.settings.paths.always_scanned_folder[scan_folder_type];
            menu_text == default_dir) {
            menu_text = "Default";
        }

        auto const btn = SettingsMenuButton(box_system, row, menu_text, "Select install location");
        if (btn.button_fired) box_system.imgui.OpenPopup(popup_id, btn.imgui_id);

        AddPanel(box_system,
                 Panel {
                     .run =
                         [scan_folder_type, &context](GuiBoxSystem& box_system) {
                             InstallLocationMenu(box_system, context, (ScanFolderType)scan_folder_type);
                         },
                     .data =
                         PopupPanel {
                             .creator_layout_id = btn.layout_id,
                             .popup_imgui_id = popup_id,
                         },
                 });
    }

    {
        auto const row = SettingsRow(box_system, root);
        SettingsLhsTextWidget(box_system, row, "Install");
        auto const rhs = SettingsRhsColumn(box_system, row, style::k_settings_small_gap);
        SettingsRhsText(box_system, rhs, "Install libraries and presets from a '.floe.zip' file");
        if (!context.package_install_jobs.Full() &&
            TextButton(box_system,
                       rhs,
                       "Install package",
                       "Install libraries and presets from a '.floe.zip' file")) {
            if (auto const o = FilesystemDialog({
                    .type = DialogArguments::Type::OpenFile,
                    .allocator = box_system.arena,
                    .title = "Select 1 or more Floe Package",
                    .default_path =
                        KnownDirectory(box_system.arena, KnownDirectoryType::Downloads, {.create = false}),
                    .filters = ArrayT<DialogArguments::FileFilter>({
                        {
                            .description = "Floe Package"_s,
                            .wildcard_filter = "*.floe.zip"_s,
                        },
                    }),
                    .allow_multiple_selection = true,
                    .parent_window = box_system.imgui.frame_input.native_window,
                });
                o.HasValue()) {
                for (auto const path : o.Value()) {
                    package::AddJob(context.package_install_jobs,
                                    path,
                                    context.settings,
                                    context.thread_pool,
                                    box_system.arena,
                                    context.sample_lib_server);
                }
            } else {
                LogError(ModuleName::Gui, "Failed to create dialog: {}", o.Error());
            }
        }
    }
}

static void GeneralSettingsPanel(GuiBoxSystem& box_system, SettingsPanelContext& context) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToPoints(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = style::k_spacing},
                                    .contents_gap = style::k_settings_medium_gap,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    {
        auto const row = SettingsRow(box_system, root);

        SettingsLhsTextWidget(box_system, row, "GUI size");

        auto const button_container = DoBox(box_system,
                                            {
                                                .parent = row,
                                                .background_fill = style::Colour::Background2,
                                                .round_background_corners = 0b1111,
                                                .layout {
                                                    .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                    .contents_direction = layout::Direction::Row,
                                                    .contents_align = layout::Alignment::Start,
                                                },
                                            });

        Optional<int> width_change {};

        if (DoBox(box_system,
                  {
                      .parent = button_container,
                      .text = ICON_FA_CARET_DOWN,
                      .font = FontType::Icons,
                      .text_align_x = TextAlignX::Centre,
                      .text_align_y = TextAlignY::Centre,
                      .background_fill_auto_hot_active_overlay = true,
                      .round_background_corners = 0b1001,
                      .activate_on_click_button = MouseButton::Left,
                      .activation_click_event = ActivationClickEvent::Up,
                      .layout {
                          .size = style::k_settings_icon_button_size,
                      },
                      .tooltip = "Decrease GUI size"_s,
                  })
                .button_fired) {
            width_change = -1;
        }
        DoBox(box_system,
              {
                  .parent = button_container,
                  .background_fill = style::Colour::Surface2,
                  .layout {
                      .size = {box_system.imgui.PixelsToPoints(1), layout::k_fill_parent},
                  },
              });
        if (DoBox(box_system,
                  {
                      .parent = button_container,
                      .text = ICON_FA_CARET_UP,
                      .font = FontType::Icons,
                      .text_align_x = TextAlignX::Centre,
                      .text_align_y = TextAlignY::Centre,
                      .background_fill_auto_hot_active_overlay = true,
                      .round_background_corners = 0b0110,
                      .activate_on_click_button = MouseButton::Left,
                      .activation_click_event = ActivationClickEvent::Up,
                      .layout {
                          .size = style::k_settings_icon_button_size,
                      },
                      .tooltip = "Increase GUI size"_s,
                  })
                .button_fired) {
            width_change = 1;
        }

        if (width_change) {
            auto const new_width = (int)context.settings.settings.gui.window_width + *width_change * 110;
            if (new_width > 0 && new_width < UINT16_MAX)
                gui_settings::SetWindowSize(context.settings.settings.gui,
                                            context.settings.tracking,
                                            (u16)new_width);
        }
    }

    {
        auto const style_row = SettingsRow(box_system, root);

        SettingsLhsTextWidget(box_system, style_row, "Style");
        auto const options_rhs_column = SettingsRhsColumn(box_system, style_row, style::k_settings_small_gap);

        if (CheckboxButton(box_system,
                           options_rhs_column,
                           "Show tooltips",
                           context.settings.settings.gui.show_tooltips)) {
            context.settings.settings.gui.show_tooltips = !context.settings.settings.gui.show_tooltips;
            context.settings.tracking.changed = true;
        }

        {
            bool state = context.settings.settings.gui.show_keyboard;
            if (CheckboxButton(box_system, options_rhs_column, "Show keyboard", state))
                gui_settings::SetShowKeyboard(context.settings.settings.gui,
                                              context.settings.tracking,
                                              !state);
        }

        if (CheckboxButton(box_system,
                           options_rhs_column,
                           "High contrast GUI",
                           context.settings.settings.gui.high_contrast_gui)) {
            context.settings.settings.gui.high_contrast_gui =
                !context.settings.settings.gui.high_contrast_gui;
            context.settings.tracking.changed = true;
        }

        if (CheckboxButton(box_system,
                           options_rhs_column,
                           "Show instance name",
                           context.settings.settings.gui.show_instance_name)) {
            context.settings.settings.gui.show_instance_name =
                !context.settings.settings.gui.show_instance_name;
            context.settings.tracking.changed = true;
        }
    }

    {
        auto const misc_row = SettingsRow(box_system, root);

        SettingsLhsTextWidget(box_system, misc_row, "General");
        auto const options_rhs_column = SettingsRhsColumn(box_system, misc_row, style::k_settings_small_gap);

        if (CheckboxButton(
                box_system,
                options_rhs_column,
                "Disable anonymous error reports",
                context.settings.settings.online_reporting_disabled,
                "If an error occurs, Floe sends anonymous data about the error, your system, and Floe's state to a server. Additionally, Floe sends anonymous data points about when a session starts and ends for determining software health.")) {
            gui_settings::SetDisableOnlineReporting(context.settings,
                                                    context.settings.settings.online_reporting_disabled);
        }

        if (auto const v = IntField(box_system,
                                    options_rhs_column,
                                    "Autosave interval (seconds)",
                                    30.0f,
                                    context.settings.settings.autosave_interval_seconds,
                                    1,
                                    3600)) {
            context.settings.settings.autosave_interval_seconds = CheckedCast<u16>(*v);
            context.settings.tracking.changed = true;
        }

        if (auto const v = IntField(box_system,
                                    options_rhs_column,
                                    "Max autosaves per instance",
                                    30.0f,
                                    context.settings.settings.max_autosaves_per_instance,
                                    1,
                                    100)) {
            context.settings.settings.max_autosaves_per_instance = CheckedCast<u16>(*v);
            context.settings.tracking.changed = true;
        }

        if (auto const v = IntField(box_system,
                                    options_rhs_column,
                                    "Autosave delete after days",
                                    30.0f,
                                    context.settings.settings.autosave_delete_after_days,
                                    1,
                                    365)) {
            context.settings.settings.autosave_delete_after_days = CheckedCast<u16>(*v);
            context.settings.tracking.changed = true;
        }
    }
}

static void
SettingsPanel(GuiBoxSystem& box_system, SettingsPanelContext& context, SettingsPanelState& state) {
    constexpr auto k_tab_config = []() {
        Array<ModalTabConfig, ToInt(SettingsPanelState::Tab::Count)> tabs {};
        for (auto const tab : EnumIterator<SettingsPanelState::Tab>()) {
            auto const index = ToInt(tab);
            switch (tab) {
                case SettingsPanelState::Tab::General:
                    tabs[index] = {.icon = ICON_FA_SLIDERS_H, .text = "General"};
                    break;
                case SettingsPanelState::Tab::Folders:
                    tabs[index] = {.icon = ICON_FA_FOLDER_OPEN, .text = "Folders"};
                    break;
                case SettingsPanelState::Tab::Packages:
                    tabs[index] = {.icon = ICON_FA_BOX_OPEN, .text = "Packages"};
                    break;
                case SettingsPanelState::Tab::Count: PanicIfReached();
            }
        }
        return tabs;
    }();

    auto const root = DoModal(box_system,
                              {
                                  .title = "Settings"_s,
                                  .on_close = [&state] { state.open = false; },
                                  .tabs = k_tab_config,
                                  .current_tab_index = ToIntRef(state.tab),
                              });

    using TabPanelFunction = void (*)(GuiBoxSystem&, SettingsPanelContext&);
    AddPanel(box_system,
             Panel {
                 .run = ({
                     TabPanelFunction f {};
                     switch (state.tab) {
                         case SettingsPanelState::Tab::General: f = GeneralSettingsPanel; break;
                         case SettingsPanelState::Tab::Folders: f = FolderSettingsPanel; break;
                         case SettingsPanelState::Tab::Packages: f = PackagesSettingsPanel; break;
                         case SettingsPanelState::Tab::Count: PanicIfReached();
                     }
                     [f, &context](GuiBoxSystem& box_system) { f(box_system, context); };
                 }),
                 .data =
                     Subpanel {
                         .id = DoBox(box_system,
                                     {
                                         .parent = root,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_fill_parent},
                                         },
                                     })
                                   .layout_id,
                         .imgui_id = box_system.imgui.GetID((u64)state.tab + 999999),
                     },
             });
}

PUBLIC void
DoSettingsPanel(GuiBoxSystem& box_system, SettingsPanelContext& context, SettingsPanelState& state) {
    if (state.open) {
        RunPanel(box_system,
                 Panel {
                     .run = [&context, &state](GuiBoxSystem& b) { SettingsPanel(b, context, state); },
                     .data =
                         ModalPanel {
                             .r = CentredRect(
                                 {.pos = 0, .size = box_system.imgui.frame_input.window_size.ToFloat2()},
                                 f32x2 {box_system.imgui.PointsToPixels(style::k_settings_dialog_width),
                                        box_system.imgui.PointsToPixels(style::k_settings_dialog_height)}),
                             .imgui_id = box_system.imgui.GetID("new settings"),
                             .on_close = [&state]() { state.open = false; },
                             .close_on_click_outside = true,
                             .darken_background = true,
                             .disable_other_interaction = true,
                         },
                 });
    }
}
