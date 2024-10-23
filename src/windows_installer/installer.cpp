// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "installer.hpp"

#include <stb_image.h>
#include <windows.h>

//
#include "os/undef_windows_macros.h"
//

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "os/misc_windows.hpp"
#include "os/threading.hpp"
#include "utils/logger/logger.hpp"

#include "gui.hpp"

constexpr auto k_log_module = "installer"_log_module;

enum class Pages : u32 {
    Configuration,
    Installing,
    Summary,
    Count,
};

struct PageInfo {
    String title;
    String label;
};

constexpr auto k_page_infos = Array {
    PageInfo {
        .title = "Configuration",
        .label =
            "Welcome to the installer for Floe"
            ".\n\nPlease close your DAW before clicking install. Plugins are installed to standard locations so that any DAW can find them.",
    },
    PageInfo {
        .title = "Installing",
        .label = "Installing plugins...",
    },
    PageInfo {
        .title = "Summary",
        .label = "",
    },
};

static_assert(k_page_infos.size == ToInt(Pages::Count));

struct Component {
    ComponentInfo info {};
    String install_dir {};
    Span<u8 const> data {};
};

struct InstallError {
    DynamicArrayBounded<char, 500> message;
    ErrorCode error;
};

using InstallResult = VoidOrError<InstallError>;

using InstallationResults = Array<InstallResult, ToInt(ComponentTypes::Count)>;

struct Application {
    Array<Component, ToInt(ComponentTypes::Count)> components = {};
    u32 page_title;
    u32 page_label;
    u32 next_button;
    u32 back_button;
    u32 cancel_button;
    u32 installing_bar;
    Array<u32, ToInt(Pages::Count)> pages;
    Array<u32, ToInt(Pages::Count)> page_sidebar_labels;
    u32 plugin_checkboxes;
    u32 summary_textbox;
    Pages current_page = Pages::Configuration;
    Array<bool, ToInt(ComponentTypes::Count)> components_selected;
    Thread installing_thread;
    Atomic<bool> installing_completed {false};
    MutexProtected<InstallationResults> installation_results = {};
    ArenaAllocator arena {PageAllocator::Instance()};
};

ErrorCodeOr<Span<u8 const>> GetResource(int resource_id) {
    auto const res = FindResourceW(GetModuleHandleW(nullptr),
                                   MAKEINTRESOURCEW(resource_id),
                                   MAKEINTRESOURCEW(RAW_DATA_TYPE));
    if (!res) return Win32ErrorCode(GetLastError());
    auto const res_handle = LoadResource(nullptr, res);
    if (!res_handle) return Win32ErrorCode(GetLastError());
    auto const res_data = (u8 const*)LockResource(res_handle);
    auto const res_size = SizeofResource(nullptr, res);

    return Span<u8 const> {res_data, (size_t)res_size};
}

static InstallResult TryInstall(Component const& comp) {
    // IMPROVE: error messages could be more helpful here for the end-user. Explain what they could try to fix
    // the issue.
    InstallError error {};
    if (auto const o = CreateDirectory(comp.install_dir,
                                       {
                                           .create_intermediate_directories = true,
                                           .fail_if_exists = false,
                                       });
        o.HasError()) {
        fmt::Assign(error.message, "Failed to create directory: {}", o.Error());
        error.error = o.Error();
        return error;
    }

    ArenaAllocatorWithInlineStorage<1000> arena {Malloc::Instance()};
    if (auto const o = WriteFile(path::Join(arena, Array {comp.install_dir, comp.info.filename}), comp.data);
        o.HasError()) {
        fmt::Assign(error.message, "Failed to write file: {}", o.Error());
        error.error = o.Error();
        return error;
    }

    return k_success;
}

static void BackgroundInstallingThread(Application& app) {
    ArenaAllocatorWithInlineStorage<1000> arena {Malloc::Instance()};
    for (auto const i : Range(ToInt(ComponentTypes::Count))) {
        if (!app.components_selected[i]) continue;
        auto const& comp = app.components[i];
        g_log.Debug(k_log_module, "Installing {} to {}", comp.info.name, comp.install_dir);
        InstallResult outcome = k_success;
        if (comp.data.size != 0) outcome = TryInstall(comp);
        app.installation_results.Use([i, outcome](InstallationResults& r) { r[i] = outcome; });
    }
    SleepThisThread(1500); // Intentional delay because it feels good
    app.installing_completed.Store(true, StoreMemoryOrder::Release);
}

static void SwitchPage(Application& app, GuiFramework& framework, Pages page) {
    String next_button_text = "Next >";
    Optional<String> label_text_override {};

    switch (page) {
        case Pages::Configuration: {
            EditWidget(framework, app.back_button, {.enabled = false});
            EditWidget(framework, app.next_button, {.enabled = true});
            next_button_text = "Install";
            break;
        }
        case Pages::Installing: {
            EditWidget(framework, app.back_button, {.enabled = false});
            EditWidget(framework, app.next_button, {.enabled = false});
            break;
        }
        case Pages::Summary: {
            EditWidget(framework, app.back_button, {.enabled = false});
            EditWidget(framework, app.next_button, {.enabled = true});

            auto const installation_results =
                app.installation_results.Use([](InstallationResults const& r) { return r; });

            bool failure = false;
            for (auto const& result : installation_results)
                if (result.HasError()) failure = true;

            if (failure)
                label_text_override = "Installation failed"_s;
            else
                label_text_override = "Done. Installation succeeded."_s;

            DynamicArray<char> summary_text {PageAllocator::Instance()};
            for (auto const comp_index : Range(ToInt(ComponentTypes::Count))) {
                if (!app.components_selected[comp_index]) continue;
                auto const result = installation_results[comp_index];
                if (!result.HasError())
                    fmt::Append(summary_text,
                                "Installed {} ({}) to {}"_s,
                                app.components[comp_index].info.name,
                                app.components[comp_index].info.filename,
                                app.components[comp_index].install_dir);
                else
                    fmt::Append(summary_text,
                                "Failed to install {} ({}) to {}: {}, {}"_s,
                                app.components[comp_index].info.name,
                                app.components[comp_index].info.filename,
                                app.components[comp_index].install_dir,
                                result.Error().message,
                                result.Error().error);
                dyn::AppendSpan(summary_text, "\n\n");
            }

            if (!failure) dyn::AppendSpan(summary_text, "\nOpen your DAW and load the Floe plugin."_s);

            EditWidget(framework, app.summary_textbox, {.text = summary_text});

            next_button_text = "Finish";
            break;
        }
        case Pages::Count: PanicIfReached(); break;
    }

    for (auto const page_index : Range(ToInt(Pages::Count)))
        EditWidget(framework, app.pages[page_index], {.visible = page_index == ToInt(page)});

    EditWidget(framework, app.page_title, {.text = k_page_infos[ToInt(page)].title});
    if (label_text_override)
        EditWidget(framework, app.page_label, {.text = *label_text_override});
    else
        EditWidget(framework, app.page_label, {.text = k_page_infos[ToInt(page)].label});
    EditWidget(framework, app.next_button, {.text = next_button_text});

    for (auto const i : Range(ToInt(Pages::Count))) {
        LabelStyle style = LabelStyle::DullColour;
        if (i == ToInt(page))
            style = LabelStyle::Bold;
        else if (i < ToInt(page))
            style = LabelStyle::Regular;
        EditWidget(framework, app.page_sidebar_labels[i], {.label_style = style});
    }

    app.current_page = page;
    RecalculateLayout(framework);

    switch (page) {
        case Pages::Configuration: break;
        case Pages::Installing: {
            if (app.installing_thread.Joinable()) app.installing_thread.Join();
            app.installing_completed.Store(false, StoreMemoryOrder::Release);
            app.installing_thread.Start([&app]() { BackgroundInstallingThread(app); }, "install");
            break;
        }
        case Pages::Summary: break;
        case Pages::Count: PanicIfReached(); break;
    }
}

Application* CreateApplication(GuiFramework& framework, u32 root_layout_id) {
    auto* app = new Application();

    for (auto& selected : app->components_selected)
        selected = true;

    for (auto const i : Range(k_plugin_infos.size)) {
        auto const& info = k_plugin_infos[i];
        auto data = GetResource(info.resource_id);
        if (data.HasError()) {
            if constexpr (PRODUCTION_BUILD) {
                ErrorDialog(framework, "Bug: missing data resource");
                ExitProgram(framework);
            } else {
                g_log.Debug(k_log_module, "Failed to load component data: {}", data.Error());
                data = Span<u8 const> {};
            }
        }

        app->components[i] = {
            .info = info,
            .install_dir = ({
                String p {};
                if (info.install_dir) p = KnownDirectory(app->arena, *info.install_dir, {.create = true});
                p;
            }),
            .data = data.Value(),
        };
    }

    constexpr u16 k_margin = 10;

    {
        auto const found_sidebar_image = GetResource(SIDEBAR_IMAGE_RC_ID);
        if (found_sidebar_image.HasValue()) {
            auto const& bin_data = found_sidebar_image.Value();

            int width;
            int height;
            int channels;
            auto rgba_data = stbi_load_from_memory((stbi_uc const*)bin_data.data,
                                                   (int)bin_data.size,
                                                   &width,
                                                   &height,
                                                   &channels,
                                                   4);
            if (rgba_data == nullptr) Panic("Failed to load image");
            DEFER { stbi_image_free(rgba_data); };
            CreateWidget(framework,
                         root_layout_id,
                         {
                             .margins = {16, 16, 8, 8},
                             .type = WidgetOptions::Image {.rgba_data = rgba_data,
                                                           .size = {CheckedCast<u16>(width),
                                                                    CheckedCast<u16>(height)}},
                         });
        }
    }

    CreateWidget(framework,
                 root_layout_id,
                 {
                     .expand_x = true,
                     .type = WidgetOptions::Divider {.orientation = Orientation::Horizontal},
                 });

    auto const main = CreateStackLayoutWidget(framework,
                                              root_layout_id,
                                              {
                                                  .expand_x = true,
                                                  .expand_y = true,
                                                  .type =
                                                      WidgetOptions::Container {
                                                          .orientation = Orientation::Horizontal,
                                                          .alignment = Alignment::Start,
                                                      },
                                              });

    {
        auto const lhs = CreateStackLayoutWidget(framework,
                                                 main,
                                                 {
                                                     .expand_y = true,
                                                     .debug_name = "LHS",
                                                     .type = WidgetOptions::Container {},
                                                 });

        for (auto const i : Range(k_page_infos.size)) {
            app->page_sidebar_labels[i] =
                CreateWidget(framework,
                             lhs,
                             {
                                 .margins = {k_margin,
                                             k_margin,
                                             (i == 0) ? (u16)8 : (u16)2,
                                             (i == (k_page_infos.size - 1)) ? (u16)8 : (u16)2},
                                 .expand_x = true,
                                 .text = k_page_infos[i].title,
                                 .type = WidgetOptions::Label {.style = LabelStyle::DullColour},
                             });
        }
    }

    CreateWidget(framework,
                 main,
                 {
                     .type = WidgetOptions::Divider {.orientation = Orientation::Vertical},
                 });

    {
        auto const rhs = CreateStackLayoutWidget(framework,
                                                 main,
                                                 {
                                                     .expand_x = true,
                                                     .expand_y = true,
                                                     .debug_name = "RHS",
                                                     .type = WidgetOptions::Container {},
                                                 });
        auto const rhs_inner =
            CreateStackLayoutWidget(framework,
                                    rhs,
                                    {
                                        .margins = {k_margin, k_margin, k_margin, k_margin},
                                        .expand_x = true,
                                        .expand_y = true,
                                        .debug_name = "RHS Inner",
                                        .type =
                                            WidgetOptions::Container {
                                                .spacing = 7,
                                            },
                                    });
        {

            app->page_title = CreateWidget(framework,
                                           rhs_inner,
                                           {
                                               .expand_x = true,
                                               .text = "title",
                                               .type = WidgetOptions::Label {.style = LabelStyle::Heading},
                                           });
            app->page_label = CreateWidget(framework,
                                           rhs_inner,
                                           {
                                               .margins = {0, 0, 2, 8},
                                               .expand_x = true,
                                               .text = "label",
                                               .type = WidgetOptions::Label {.style = LabelStyle::Regular},
                                           });

            auto const page_options = WidgetOptions {
                .expand_x = true,
                .expand_y = true,
                .type =
                    WidgetOptions::Container {
                        .spacing = 5,
                    },
            };

            {
                app->pages[ToInt(Pages::Configuration)] =
                    CreateStackLayoutWidget(framework, rhs_inner, page_options);

                app->plugin_checkboxes =
                    CreateWidget(framework,
                                 app->pages[ToInt(Pages::Configuration)],
                                 {
                                     .expand_x = true,
                                     .expand_y = true,
                                     .type =
                                         WidgetOptions::CheckboxTable {
                                             .columns =
                                                 Array {
                                                     WidgetOptions::CheckboxTable::Column {
                                                         .label = "Component"_s,
                                                         .default_width = 240,
                                                     },
                                                     WidgetOptions::CheckboxTable::Column {
                                                         .label = "Size",
                                                         .default_width = 160,
                                                     },
                                                 },
                                         },
                                 });

                for (auto const& comp : app->components) {
                    FixedSizeAllocator<32> allocator {&Malloc::Instance()};
                    EditWidget(framework,
                               app->plugin_checkboxes,
                               {
                                   .add_checkbox_table_item =
                                       EditWidgetOptions::CheckboxTableItem {
                                           .state = true,
                                           .items = Array {comp.info.name,
                                                           fmt::Format(allocator,
                                                                       "{.2} MB",
                                                                       (f64)comp.data.size / Mb(1))},
                                       },
                               });
                }
            }
            {
                app->pages[ToInt(Pages::Installing)] =
                    CreateStackLayoutWidget(framework, rhs_inner, page_options);

                app->installing_bar = CreateWidget(framework,
                                                   app->pages[ToInt(Pages::Installing)],
                                                   {
                                                       .expand_x = true,
                                                       .type = WidgetType::ProgressBar,
                                                   });
            }

            {
                app->pages[ToInt(Pages::Summary)] =
                    CreateStackLayoutWidget(framework, rhs_inner, page_options);
                app->summary_textbox = CreateWidget(framework,
                                                    app->pages[ToInt(Pages::Summary)],
                                                    {
                                                        .expand_x = true,
                                                        .expand_y = true,
                                                        .type = WidgetType::ReadOnlyTextbox,
                                                    });
            }
        }

        CreateWidget(framework,
                     rhs,
                     {
                         .type = WidgetOptions::Divider {.orientation = Orientation::Horizontal},
                     });

        auto nav_layout = CreateStackLayoutWidget(framework,
                                                  rhs,
                                                  {
                                                      .margins = {k_margin, k_margin, k_margin, k_margin},
                                                      .expand_x = true,
                                                      .expand_y = false,
                                                      .debug_name = "NavContainer",
                                                      .type =
                                                          WidgetOptions ::Container {
                                                              .spacing = 5,
                                                              .orientation = Orientation::Horizontal,
                                                              .alignment = Alignment::End,
                                                          },
                                                  });
        app->back_button =
            CreateWidget(framework,
                         nav_layout,
                         {.text = "< Back", .type = WidgetOptions::Button {.is_default = false}});
        app->next_button =
            CreateWidget(framework,
                         nav_layout,
                         {.text = "Next >", .type = WidgetOptions::Button {.is_default = true}});
        app->cancel_button =
            CreateWidget(framework,
                         nav_layout,
                         {.text = "Cancel", .type = WidgetOptions::Button {.is_default = false}});
    }

    SwitchPage(*app, framework, Pages::Configuration);

    return app;
}

void DestroyApplication(Application& app, GuiFramework&) {
    if (app.installing_thread.Joinable()) app.installing_thread.Join();
    delete &app;
}

void OnTimer(Application& app, GuiFramework& framework) {
    if (app.current_page == Pages::Installing) {
        if (app.installing_completed.Load(LoadMemoryOrder::Relaxed))
            SwitchPage(app, framework, Pages::Summary);
        else
            EditWidget(framework, app.installing_bar, {.progress_bar_pulse = true});
    }
}

void HandleUserInteraction(Application& app, GuiFramework& framework, UserInteraction const& info) {
    switch (info.type) {
        case UserInteraction::Type::ButtonPressed: {
            if (info.widget_id == app.next_button) {
                auto const next_page = (int)app.current_page + 1;
                if (next_page == (int)Pages::Count)
                    ExitProgram(framework);
                else
                    SwitchPage(app, framework, (Pages)next_page);
            } else if (info.widget_id == app.back_button) {
                auto const prev_page = (int)(app.current_page) - 1;
                ASSERT(prev_page >= 0);
                SwitchPage(app, framework, (Pages)prev_page);
            } else if (info.widget_id == app.cancel_button) {
                ExitProgram(framework);
            }
            break;
        }
        case UserInteraction::Type::CheckboxTableItemToggled: {
            if (info.widget_id == app.plugin_checkboxes) {
                g_log.Debug(k_log_module, "Checkbox {} toggled to {}", info.button_index, info.button_state);
                app.components_selected[info.button_index] = info.button_state;
            }
            if (Find(app.components_selected, true))
                EditWidget(framework, app.next_button, {.enabled = true});
            else
                EditWidget(framework, app.next_button, {.enabled = false});
            break;
        }
        case UserInteraction::Type::RadioButtonSelected:
        case UserInteraction::Type::TextInputChanged:
        case UserInteraction::Type::TextInputEnterPressed: break;
    }
}
