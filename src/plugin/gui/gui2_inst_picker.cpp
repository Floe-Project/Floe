// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_inst_picker.hpp"

#include "gui2_common_picker.hpp"

static Optional<InstrumentCursor> CurrentCursor(InstPickerContext const& context,
                                                sample_lib::InstrumentId const& inst_id) {
    for (auto const [lib_index, l] : Enumerate(context.libraries)) {
        if (l->Id() != inst_id.library) continue;
        for (auto const [inst_index, i] : Enumerate(l->sorted_instruments))
            if (i->name == inst_id.inst_name) return InstrumentCursor {lib_index, inst_index};
    }

    return k_nullopt;
}

static Optional<InstrumentCursor> IterateInstrument(InstPickerContext const& context,
                                                    InstPickerState const& state,
                                                    InstrumentCursor cursor,
                                                    IterateInstrumentDirection direction,
                                                    bool first,
                                                    bool picker_gui_is_open) {
    if (cursor.lib_index >= context.libraries.size) cursor.lib_index = 0;

    if (!first) {
        switch (direction) {
            case IterateInstrumentDirection::Forward: ++cursor.inst_index; break;
            case IterateInstrumentDirection::Backward:
                static_assert(UnsignedInt<decltype(cursor.inst_index)>);
                --cursor.inst_index;
                break;
        }
    }

    for (usize lib_step = 0; lib_step < context.libraries.size + 1; (
             {
                 ++lib_step;
                 switch (direction) {
                     case IterateInstrumentDirection::Forward:
                         cursor.lib_index = (cursor.lib_index + 1) % context.libraries.size;
                         cursor.inst_index = 0;
                         break;
                     case IterateInstrumentDirection::Backward:
                         static_assert(UnsignedInt<decltype(cursor.lib_index)>);
                         --cursor.lib_index;
                         if (cursor.lib_index >= context.libraries.size) // check wraparound
                             cursor.lib_index = context.libraries.size - 1;
                         cursor.inst_index = context.libraries[cursor.lib_index]->sorted_instruments.size - 1;
                         break;
                 }
             })) {
        auto const& lib = *context.libraries[cursor.lib_index];

        if (lib.sorted_instruments.size == 0) continue;
        if (picker_gui_is_open && lib.file_format_specifics.tag != state.FileFormatForCurrentTab()) continue;

        if (state.tab == InstPickerState::Tab::FloeLibaries && state.selected_library_hashes.size &&
            !Contains(state.selected_library_hashes, lib.Id().Hash())) {
            continue;
        }

        if (state.tab == InstPickerState::Tab::MirageLibraries && state.selected_mirage_library_hashes.size &&
            !Contains(state.selected_mirage_library_hashes, lib.Id().Hash())) {
            continue;
        }

        for (; cursor.inst_index < lib.sorted_instruments.size; (
                 {
                     switch (direction) {
                         case IterateInstrumentDirection::Forward: ++cursor.inst_index; break;
                         case IterateInstrumentDirection::Backward: --cursor.inst_index; break;
                     }
                 })) {
            auto const& inst = *lib.sorted_instruments[cursor.inst_index];

            if (state.search.size && (!ContainsCaseInsensitiveAscii(inst.name, state.search) &&
                                      !ContainsCaseInsensitiveAscii(inst.folder.ValueOr({}), state.search)))
                continue;

            if (state.selected_tags_hashes.size) {
                bool found = false;
                for (auto const tag : inst.tags) {
                    auto const tag_hash = Hash(tag);
                    if (Contains(state.selected_tags_hashes, tag_hash)) {
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
            }

            return cursor;
        }
    }

    return k_nullopt;
}

void LoadAdjacentInstrument(InstPickerContext const& context,
                            InstPickerState& state,
                            IterateInstrumentDirection direction,
                            bool picker_gui_is_open) {
    switch (context.layer.instrument_id.tag) {
        case InstrumentType::WaveformSynth: {
            auto const waveform_type = context.layer.instrument_id.Get<WaveformType>();
            auto prev = ToInt(waveform_type) - 1;
            if (prev < 0) prev = ToInt(WaveformType::Count) - 1;
            LoadInstrument(context.engine, context.layer.index, WaveformType(prev));
            break;
        }
        case InstrumentType::None: {
            if (auto const cursor =
                    IterateInstrument(context, state, {0, 0}, direction, true, picker_gui_is_open)) {
                auto const& lib = *context.libraries[cursor->lib_index];
                auto const& inst = *lib.sorted_instruments[cursor->inst_index];
                LoadInstrument(context.engine,
                               context.layer.index,
                               sample_lib::InstrumentId {
                                   .library = lib.Id(),
                                   .inst_name = inst.name,
                               });
                state.scroll_to_show_selected = true;
            }
            break;
        }
        case InstrumentType::Sampler: {
            auto const inst_id = context.layer.instrument_id.Get<sample_lib::InstrumentId>();

            if (auto const cursor = CurrentCursor(context, inst_id)) {
                if (auto const prev =
                        IterateInstrument(context, state, *cursor, direction, false, picker_gui_is_open)) {
                    auto const& lib = *context.libraries[prev->lib_index];
                    auto const& inst = *lib.sorted_instruments[prev->inst_index];
                    LoadInstrument(context.engine,
                                   context.layer.index,
                                   sample_lib::InstrumentId {
                                       .library = lib.Id(),
                                       .inst_name = inst.name,
                                   });
                    state.scroll_to_show_selected = true;
                }
            }
            break;
        }
    }
}

void LoadRandomInstrument(InstPickerContext const& context, InstPickerState& state, bool picker_gui_is_open) {
    auto const first = IterateInstrument(context,
                                         state,
                                         {.lib_index = 0, .inst_index = 0},
                                         IterateInstrumentDirection::Forward,
                                         true,
                                         picker_gui_is_open);
    if (!first) return;

    auto cursor = *first;

    usize num_instruments = 0;
    while (true) {
        if (auto const next = IterateInstrument(context,
                                                state,
                                                cursor,
                                                IterateInstrumentDirection::Forward,
                                                false,
                                                picker_gui_is_open)) {
            cursor = *next;
            if (cursor == *first) break;
            ++num_instruments;
        } else {
            break;
        }
    }

    auto const random_pos = RandomIntInRange<usize>(context.engine.random_seed, 0, num_instruments - 1);

    cursor = *first;
    for (usize i = 0; i < random_pos; ++i)
        cursor = *IterateInstrument(context,
                                    state,
                                    cursor,
                                    IterateInstrumentDirection::Forward,
                                    false,
                                    picker_gui_is_open);

    auto const& lib = *context.libraries[cursor.lib_index];
    auto const& inst = *lib.sorted_instruments[cursor.inst_index];
    LoadInstrument(context.engine,
                   context.layer.index,
                   sample_lib::InstrumentId {
                       .library = lib.Id(),
                       .inst_name = inst.name,
                   });
    state.scroll_to_show_selected = true;
}

static void InstPickerStatusBar(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState&) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    if (auto const l = context.hovering_lib) {
        DynamicArray<char> buf {box_system.arena};
        fmt::Append(buf, "{} by {}.", l->name, l->author);
        if (l->description) fmt::Append(buf, " {}", l->description);
        DoBox(box_system,
              {
                  .parent = root,
                  .text = buf,
                  .wrap_width = k_wrap_to_parent,
                  .font = FontType::Body,
                  .size_from_text = true,
              });
    }

    if (auto const i = context.hovering_inst) {
        DynamicArray<char> buf {box_system.arena};
        fmt::Append(buf, "{} from {} by {}.", i->name, i->library.name, i->library.author);

        if (i->description) fmt::Append(buf, " {}", i->description);

        fmt::Append(buf, "\nTags: ");
        if (i->tags.size == 0)
            fmt::Append(buf, "None");
        else
            for (auto const t : i->tags)
                fmt::Append(buf, "{} ", t);

        DoBox(box_system,
              {
                  .parent = root,
                  .text = buf,
                  .wrap_width = k_wrap_to_parent,
                  .font = FontType::Body,
                  .size_from_text = true,
              });
    }

    if (auto const w = context.waveform_type_hovering) {
        DoBox(box_system,
              {
                  .parent = root,
                  .text = fmt::Format(
                      box_system.arena,
                      "{} waveform. A simple waveform useful for layering with sample instruments.",
                      k_waveform_type_names[ToInt(*w)]),
                  .wrap_width = k_wrap_to_parent,
                  .font = FontType::Body,
                  .size_from_text = true,
              });
    }
}

static void InstPickerWaveformItems(GuiBoxSystem& box_system,
                                    InstPickerContext& context,
                                    InstPickerState&,
                                    Box const root) {
    auto const container = DoBox(box_system,
                                 {
                                     .parent = root,
                                     .layout =
                                         {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_direction = layout::Direction::Column,
                                         },
                                 });

    for (auto const waveform_type : EnumIterator<WaveformType>()) {
        auto const is_current = waveform_type == context.layer.instrument_id.TryGetOpt<WaveformType>();

        auto const item = DoPickerItem(box_system,
                                       {
                                           .parent = container,
                                           .text = k_waveform_type_names[ToInt(waveform_type)],
                                           .is_current = is_current,
                                           .icon = k_nullopt,
                                       });

        if (item.is_hot) context.waveform_type_hovering = waveform_type;
        if (item.button_fired) {
            if (is_current)
                LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
            else {
                LoadInstrument(context.engine, context.layer.index, waveform_type);
                box_system.imgui.CloseCurrentPopup();
            }
        }
    }
}

static void InstPickerItems(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
    auto const root = DoPickerItemsRoot(box_system);

    if (state.tab == InstPickerState::Tab::Waveforms) {
        InstPickerWaveformItems(box_system, context, state, root);
        return;
    }

    Optional<Optional<String>> previous_folder {};
    Box folder_box {};

    auto const first = IterateInstrument(context,
                                         state,
                                         {.lib_index = 0, .inst_index = 0},
                                         IterateInstrumentDirection::Forward,
                                         true,
                                         true);
    if (!first) return;

    sample_lib::Library const* previous_library {};
    Optional<graphics::TextureHandle> lib_icon_tex {};
    auto cursor = *first;
    while (true) {
        auto const& lib = *context.libraries[cursor.lib_index];
        auto const& inst = *lib.sorted_instruments[cursor.inst_index];
        auto const& folder = inst.folder;

        if (folder != previous_folder) {
            previous_folder = folder;
            folder_box = DoPickerItemsSectionContainer(box_system,
                                                       {
                                                           .parent = root,
                                                           .heading = folder,
                                                           .heading_is_folder = true,
                                                       });
        }

        auto const inst_id = sample_lib::InstrumentId {lib.Id(), inst.name};
        auto const is_current = context.layer.instrument_id == inst_id;

        auto const item = DoPickerItem(
            box_system,
            {
                .parent = folder_box,
                .text = inst.name,
                .is_current = is_current,
                .icon = ({
                    if (&lib != previous_library) {
                        lib_icon_tex = k_nullopt;
                        previous_library = &lib;
                        if (auto const imgs = LibraryImagesFromLibraryId(context.library_images,
                                                                         box_system.imgui,
                                                                         lib.Id(),
                                                                         context.sample_library_server,
                                                                         box_system.arena);
                            imgs && imgs->icon) {
                            lib_icon_tex =
                                box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(imgs->icon);
                        }
                    }
                    lib_icon_tex;
                }),
            });

        if (is_current && box_system.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender &&
            Exchange(state.scroll_to_show_selected, false)) {
            box_system.imgui.ScrollWindowToShowRectangle(layout::GetRect(box_system.layout, item.layout_id));
        }

        if (item.is_hot) context.hovering_inst = &inst;
        if (item.button_fired) {
            if (is_current) {
                LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
            } else {
                LoadInstrument(context.engine,
                               context.layer.index,
                               sample_lib::InstrumentId {
                                   .library = lib.Id(),
                                   .inst_name = inst.name,
                               });
                box_system.imgui.CloseCurrentPopup();
            }
        }

        if (auto next =
                IterateInstrument(context, state, cursor, IterateInstrumentDirection::Forward, false, true)) {
            cursor = *next;
            if (cursor == *first) break;
        } else {
            break;
        }
    }
}

static void InstPickerFilters(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
    if (state.tab == InstPickerState::Tab::Waveforms) return;

    auto const root = DoPickerItemsRoot(box_system);

    {
        auto const section = DoPickerItemsSectionContainer(box_system,
                                                           {
                                                               .parent = root,
                                                               .heading = "LIBRARIES"_s,
                                                               .multiline_contents = true,
                                                           });

        for (auto const& l_ptr : context.libraries) {
            auto const& lib = *l_ptr;

            if (lib.insts_by_name.size == 0) continue;
            if (lib.file_format_specifics.tag != state.FileFormatForCurrentTab()) continue;

            auto const lib_id_hash = lib.Id().Hash();
            auto& hashes = state.tab == InstPickerState::Tab::FloeLibaries
                               ? state.selected_library_hashes
                               : state.selected_mirage_library_hashes;
            auto const is_selected = Contains(hashes, lib_id_hash);

            auto const button = DoFilterButton(
                box_system,
                section,
                is_selected,
                LibraryImagesFromLibraryId(context.library_images,
                                           box_system.imgui,
                                           lib.Id(),
                                           context.sample_library_server,
                                           box_system.arena)
                    .AndThen([&](LibraryImages const& imgs) {
                        return box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(imgs.icon);
                    }),
                lib.name);
            if (button.is_hot) context.hovering_lib = &lib;
            if (button.button_fired) {
                if (is_selected)
                    dyn::RemoveValue(hashes, lib_id_hash);
                else
                    dyn::Append(hashes, lib_id_hash);
            }
        }
    }

    if (state.tab == InstPickerState::Tab::FloeLibaries) {
        if (!context.all_tags) {
            DynamicSet<String> all_tags {box_system.arena};
            for (auto const& l_ptr : context.libraries) {
                auto const& lib = *l_ptr;
                for (auto const& inst : lib.sorted_instruments)
                    for (auto const& tag : inst->tags)
                        all_tags.Insert(tag);
            }
            context.all_tags = all_tags.ToOwnedSet();
        }

        auto const section = DoPickerItemsSectionContainer(box_system,
                                                           {
                                                               .parent = root,
                                                               .heading = "TAGS",
                                                               .multiline_contents = true,
                                                           });
        for (auto const element : context.all_tags->Elements()) {
            if (!element.active) continue;

            auto const tag = element.key;
            auto const tag_hash = element.hash;

            auto const is_selected = Contains(state.selected_tags_hashes, tag_hash);
            if (DoFilterButton(box_system, section, is_selected, {}, tag).button_fired) {
                if (is_selected)
                    dyn::RemoveValue(state.selected_tags_hashes, tag_hash);
                else
                    dyn::Append(state.selected_tags_hashes, tag_hash);
            }
        }
    }
}

static void InstPickerPopup(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
    auto const root = DoBox(
        box_system,
        {
            .layout {
                .size = {layout::k_hug_contents,
                         box_system.imgui.PixelsToVw(box_system.imgui.frame_input.window_size.height * 0.9f)},
                .contents_direction = layout::Direction::Column,
                .contents_align = layout::Alignment::Start,
            },
        });

    DoBox(box_system,
          {
              .parent = root,
              .text = fmt::Format(box_system.arena, "Layer {} Instrument", context.layer.index + 1),
              .font = FontType::Heading2,
              .size_from_text = true,
              .layout {
                  .margins = {.lrtb = k_picker_spacing},
              },
          });

    {
        DynamicArrayBounded<ModalTabConfig, 3> tab_config {};
        dyn::Append(tab_config,
                    {
                        .text = context.has_mirage_libraries ? "Floe Instruments"_s : "Instruments",
                    });
        if (context.has_mirage_libraries) dyn::Append(tab_config, {.text = "Mirage Instruments"});
        dyn::Append(tab_config, {.text = "Waveforms"});

        DoModalTabBar(box_system,
                      {
                          .parent = root,
                          .tabs = tab_config,
                          .current_tab_index = ToIntRef(state.tab),
                      });
    }

    constexpr auto k_instrument_list_width = 200.0f;
    constexpr auto k_filter_list_width = 200.0f;

    {
        auto const headings_row = DoBox(box_system,
                                        {
                                            .parent = root,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::Alignment::Start,
                                            },
                                        });

        {
            auto const instruments_top =
                DoBox(box_system,
                      {
                          .parent = headings_row,
                          .layout {
                              .size = {k_instrument_list_width, layout::k_hug_contents},
                              .contents_padding = {.lr = k_picker_spacing, .tb = k_picker_spacing / 2},
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            DoBox(box_system,
                  {
                      .parent = instruments_top,
                      .text = "Instruments",
                      .font = FontType::Heading2,
                      .layout {
                          .size = {layout::k_fill_parent, style::k_font_heading2_size},
                      },
                  });

            if (IconButton(box_system,
                           instruments_top,
                           ICON_FA_CARET_LEFT,
                           "Load previous instrument",
                           style::k_font_heading2_size,
                           style::k_font_heading2_size)
                    .button_fired) {
                dyn::Append(box_system.state->deferred_actions, [&]() {
                    LoadAdjacentInstrument(context, state, IterateInstrumentDirection::Backward, true);
                });
            }

            if (IconButton(box_system,
                           instruments_top,
                           ICON_FA_CARET_RIGHT,
                           "Load next instrument",
                           style::k_font_heading2_size,
                           style::k_font_heading2_size)
                    .button_fired) {
                dyn::Append(box_system.state->deferred_actions, [&]() {
                    LoadAdjacentInstrument(context, state, IterateInstrumentDirection::Forward, true);
                });
            }

            if (IconButton(box_system,
                           instruments_top,
                           ICON_FA_RANDOM,
                           "Load random instrument",
                           style::k_font_heading2_size * 0.8f,
                           style::k_font_heading2_size)
                    .button_fired) {
                dyn::Append(box_system.state->deferred_actions,
                            [&]() { LoadRandomInstrument(context, state, true); });
            }

            if (context.layer.instrument_id.tag != InstrumentType::None) {
                if (IconButton(box_system,
                               instruments_top,
                               ICON_FA_LOCATION_ARROW,
                               "Scroll to current instrument",
                               style::k_font_heading2_size * 0.7f,
                               style::k_font_heading2_size)
                        .button_fired) {
                    dyn::Append(box_system.state->deferred_actions,
                                [&]() { state.scroll_to_show_selected = true; });
                }
            }
        }

        DoModalDivider(box_system, headings_row, DividerType::Vertical);

        {
            auto const filters_top =
                DoBox(box_system,
                      {
                          .parent = headings_row,
                          .layout {
                              .size = {k_filter_list_width, layout::k_hug_contents},
                              .contents_padding = {.lr = k_picker_spacing, .tb = k_picker_spacing / 2},
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            DoBox(box_system,
                  {
                      .parent = filters_top,
                      .text = "Filters",
                      .font = FontType::Heading2,
                      .layout {
                          .size = {layout::k_fill_parent, style::k_font_heading2_size},
                      },
                  });

            if (state.HasFilters()) {
                if (IconButton(box_system,
                               filters_top,
                               ICON_FA_TIMES,
                               "Clear all filters.",
                               style::k_font_heading2_size * 0.9f,
                               style::k_font_heading2_size)
                        .button_fired) {
                    dyn::Append(box_system.state->deferred_actions, [&]() { state.ClearAllFilters(); });
                }
            }
        }
    }

    DoModalDivider(box_system, root, DividerType::Horizontal);

    auto const main_section = DoBox(box_system,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_hug_contents, layout::k_fill_parent},
                                            .contents_direction = layout::Direction::Row,
                                            .contents_align = layout::Alignment::Start,
                                        },
                                    });

    {
        auto const lhs = DoBox(box_system,
                               {
                                   .parent = main_section,
                                   .layout {
                                       .size = {200, layout::k_fill_parent},
                                       .contents_padding = {.lr = k_picker_spacing, .t = k_picker_spacing},
                                       .contents_gap = k_picker_spacing,
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                   },
                               });

        {
            if (context.layer.instrument_id.tag != InstrumentType::None) {
                if (TextButton(box_system,
                               lhs,
                               fmt::Format(box_system.arena, "Unload {}", context.layer.InstName()),
                               "Unload the current instrument.",
                               true)) {
                    LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
                    box_system.imgui.CloseCurrentPopup();
                }
            }

            {
                auto const search_box =
                    DoBox(box_system,
                          {
                              .parent = lhs,
                              .background_fill = style::Colour::Background2,
                              .round_background_corners = 0b1111,
                              .layout {
                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                  .contents_padding = {.lr = k_picker_spacing / 2},
                                  .contents_direction = layout::Direction::Row,
                                  .contents_align = layout::Alignment::Start,
                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                              },
                          });

                DoBox(box_system,
                      {
                          .parent = search_box,
                          .text = ICON_FA_SEARCH,
                          .font_size = k_picker_item_height * 0.9f,
                          .font = FontType::Icons,
                          .text_fill = style::Colour::Subtext0,
                          .size_from_text = true,
                      });

                auto const initial_size = state.search.size;

                if (auto const text_input =
                        DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = state.search,
                                  .text_input_box = TextInputBox::SingleLine,
                                  .text_input_cursor = style::Colour::Text,
                                  .text_input_selection = style::Colour::Highlight,
                                  .layout {
                                      .size = {layout::k_fill_parent, k_picker_item_height},
                                  },
                              });
                    text_input.text_input_result && text_input.text_input_result->buffer_changed) {
                    dyn::Assign(state.search, text_input.text_input_result->text);
                }

                if (initial_size) {
                    if (DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = ICON_FA_TIMES,
                                  .font_size = k_picker_item_height * 0.9f,
                                  .font = FontType::Icons,
                                  .text_fill = style::Colour::Subtext0,
                                  .size_from_text = true,
                                  .background_fill_auto_hot_active_overlay = true,
                                  .activate_on_click_button = MouseButton::Left,
                                  .activation_click_event = ActivationClickEvent::Up,
                              })
                            .button_fired) {
                        dyn::Clear(state.search);
                    }
                }
            }
        }

        AddPanel(box_system,
                 {
                     .run = [&context, &state](
                                GuiBoxSystem& box_system) { InstPickerItems(box_system, context, state); },
                     .data =
                         Subpanel {
                             .id = DoBox(box_system,
                                         {
                                             .parent = lhs,
                                             .layout {
                                                 .size = layout::k_fill_parent,
                                             },
                                         })
                                       .layout_id,
                             .imgui_id = (imgui::Id)SourceLocationHash(),
                             .debug_name = "items",
                         },
                 });
    }

    DoModalDivider(box_system, main_section, DividerType::Vertical);

    {
        auto const rhs = DoBox(box_system,
                               {
                                   .parent = main_section,
                                   .layout {
                                       .size = {200, layout::k_fill_parent},
                                       .contents_padding = {.lr = k_picker_spacing, .t = k_picker_spacing},
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                   },
                               });

        AddPanel(box_system,
                 {
                     .run = [&context, &state](
                                GuiBoxSystem& box_system) { InstPickerFilters(box_system, context, state); },
                     .data =
                         Subpanel {
                             .id = DoBox(box_system,
                                         {
                                             .parent = rhs,
                                             .layout {
                                                 .size = layout::k_fill_parent,
                                             },
                                         })
                                       .layout_id,
                             .imgui_id = (imgui::Id)SourceLocationHash(),
                             .debug_name = "filters",
                         },
                 });
    }

    DoModalDivider(box_system, root, DividerType::Horizontal);

    AddPanel(box_system,
             {
                 .run = [&context, &state](
                            GuiBoxSystem& box_system) { InstPickerStatusBar(box_system, context, state); },
                 .data =
                     Subpanel {
                         .id = DoBox(box_system,
                                     {
                                         .parent = root,
                                         .layout {
                                             .size = {layout::k_fill_parent, 50},
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Start,
                                         },
                                     })
                                   .layout_id,
                         .imgui_id = (imgui::Id)SourceLocationHash(),
                         .debug_name = "status bar",
                     },
             });
}

void DoInstPickerPopup(GuiBoxSystem& box_system,
                       imgui::Id popup_id,
                       Rect absolute_button_rect,
                       InstPickerContext& context,
                       InstPickerState& state) {
    RunPanel(box_system,
             Panel {
                 .run = [&context,
                         &state](GuiBoxSystem& box_system) { InstPickerPopup(box_system, context, state); },
                 .data =
                     PopupPanel {
                         .creator_absolute_rect = absolute_button_rect,
                         .popup_imgui_id = popup_id,
                     },
             });
}
