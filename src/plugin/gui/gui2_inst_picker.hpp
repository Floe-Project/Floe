// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/engine.hpp"
#include "gui/gui2_common_modal_panel.hpp"
#include "gui/gui2_inst_picker_state.hpp"
#include "gui/gui_library_images.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "processor/layer_processor.hpp"

// Ephemeral
struct InstPickerContext {
    void Init(ArenaAllocator& arena) {
        libraries = sample_lib_server::AllLibrariesRetained(sample_library_server, arena);
        Sort(libraries, [](auto const& a, auto const& b) { return a->name < b->name; });

        for (auto const& l : libraries) {
            if (l->file_format_specifics.tag == sample_lib::FileFormat::Mdata) {
                has_mirage_libraries = true;
                break;
            }
        }
    }
    void Deinit() { sample_lib_server::ReleaseAll(libraries); }

    LayerProcessor& layer;
    sample_lib_server::Server& sample_library_server;
    LibraryImagesArray& library_images;
    Engine& engine;

    Span<sample_lib_server::RefCounted<sample_lib::Library>> libraries;
    sample_lib::Instrument const* hovering_inst {};
    sample_lib::Library const* hovering_lib {};
    Optional<WaveformType> waveform_type_hovering {};
    bool has_mirage_libraries {};
};

struct InstrumentCursor {
    bool operator==(InstrumentCursor const& o) const = default;
    usize lib_index;
    usize inst_index;
};

enum class IterateInstrumentDirection { Forward, Backward };

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

PUBLIC void LoadAdjacentInstrument(InstPickerContext const& context,
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

PUBLIC void
LoadRandomInstrument(InstPickerContext const& context, InstPickerState& state, bool picker_gui_is_open) {
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

constexpr auto k_inst_picker_item_height = 20.0f;
constexpr auto k_inst_picker_spacing = 8.0f;

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
        auto const item =
            DoBox(box_system,
                  {
                      .parent = container,
                      .text = k_waveform_type_names[ToInt(waveform_type)],
                      .font = FontType::Body,
                      .background_fill = is_current ? style::Colour::Highlight : style::Colour::None,
                      .background_fill_auto_hot_active_overlay = true,
                      .activate_on_click_button = MouseButton::Left,
                      .activation_click_event = ActivationClickEvent::Up,
                      .layout =
                          {
                              .size = {layout::k_fill_parent, k_inst_picker_item_height},
                          },
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
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lr = k_inst_picker_spacing},
                                    .contents_gap = k_inst_picker_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

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
            folder_box = DoBox(box_system,
                               {
                                   .parent = root,
                                   .layout =
                                       {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_direction = layout::Direction::Column,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                       },
                               });

            previous_folder = folder;

            if (folder) {
                DynamicArrayBounded<char, 200> buf {*folder};
                for (auto& c : buf)
                    c = ToUppercaseAscii(c);
                dyn::Replace(buf, "/"_s, ": "_s);

                DoBox(box_system,
                      {.parent = folder_box,
                       .text = buf,
                       .font = FontType::Heading3,
                       .size_from_text = true,
                       .layout = {
                           .margins = {.b = k_inst_picker_spacing / 2},
                       }});
            }
        }

        auto const inst_id = sample_lib::InstrumentId {lib.Id(), inst.name};
        auto const is_current = context.layer.instrument_id == inst_id;

        auto const item =
            DoBox(box_system,
                  {
                      .parent = folder_box,
                      .background_fill = is_current ? style::Colour::Highlight : style::Colour::None,
                      .background_fill_auto_hot_active_overlay = true,
                      .round_background_corners = 0b1111,
                      .activate_on_click_button = MouseButton::Left,
                      .activation_click_event = ActivationClickEvent::Up,
                      .layout =
                          {
                              .size = {layout::k_fill_parent, k_inst_picker_item_height},
                              .contents_direction = layout::Direction::Row,
                          },
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

        if (&lib != previous_library) {
            lib_icon_tex = k_nullopt;
            previous_library = &lib;
            if (auto const imgs = LibraryImagesFromLibraryId(context.library_images,
                                                             box_system.imgui,
                                                             lib.Id(),
                                                             context.sample_library_server,
                                                             box_system.arena);
                imgs && imgs->icon) {
                lib_icon_tex = box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(imgs->icon);
            }
        }

        if (lib_icon_tex) {
            DoBox(box_system,
                  {
                      .parent = item,
                      .background_tex = *lib_icon_tex,
                      .layout {
                          .size = {k_inst_picker_item_height, k_inst_picker_item_height},
                          .margins = {.r = k_inst_picker_spacing / 2},
                      },
                  });
        }

        DoBox(box_system,
              {
                  .parent = item,
                  .text = inst.name,
                  .font = FontType::Body,
                  .layout =
                      {
                          .size = layout::k_fill_parent,
                      },
              });

        if (auto next =
                IterateInstrument(context, state, cursor, IterateInstrumentDirection::Forward, false, true)) {
            cursor = *next;
            if (cursor == *first) break;
        } else {
            break;
        }
    }
}

static Box FilterButton(GuiBoxSystem& box_system,
                        Box const& parent,
                        bool is_selected,
                        Optional<graphics::TextureHandle> icon,
                        String text) {
    auto const button =
        DoBox(box_system,
              {
                  .parent = parent,
                  .background_fill = is_selected ? style::Colour::Highlight : style::Colour::None,
                  .background_fill_active = style::Colour::Highlight,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .activate_on_click_button = MouseButton::Left,
                  .activation_click_event = ActivationClickEvent::Up,
                  .layout {
                      .size = {layout::k_hug_contents, k_inst_picker_item_height},
                      .contents_padding = {.r = k_inst_picker_spacing / 2},
                      .contents_gap = {k_inst_picker_spacing / 2, 0},
                  },
              });

    if (icon) {
        DoBox(box_system,
              {
                  .parent = button,
                  .background_tex = icon,
                  .layout {
                      .size = {k_inst_picker_item_height, k_inst_picker_item_height},
                      .margins = {.r = 3},
                  },
              });
    }

    DoBox(box_system,
          {
              .parent = button,
              .text = text,
              .font = FontType::Body,
              .text_fill = style::Colour::Text,
              .text_fill_hot = style::Colour::Text,
              .text_fill_active = style::Colour::Text,
              .size_from_text = true,
              .parent_dictates_hot_and_active = true,
              .layout =
                  {
                      .margins = {.l = icon ? 0 : k_inst_picker_spacing / 2},
                  },
          });

    return button;
}

static Box FilterButtonSection(GuiBoxSystem& box_system, Box const& parent, String text) {
    auto const section = DoBox(box_system,
                               {
                                   .parent = parent,
                                   .layout =
                                       {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_direction = layout::Direction::Column,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                       },
                               });

    DoBox(box_system,
          {
              .parent = section,
              .text = text,
              .font = FontType::Heading3,
              .size_from_text = true,
              .layout =
                  {
                      .margins = {.b = k_inst_picker_spacing / 2},
                  },
          });
    return DoBox(box_system,
                 {
                     .parent = section,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_direction = layout::Direction::Row,
                         .contents_multiline = true,
                         .contents_align = layout::Alignment::Start,
                     },
                 });
}

static void InstPickerFilters(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
    if (state.tab == InstPickerState::Tab::Waveforms) return;

    auto const root =
        DoBox(box_system,
              {
                  .layout {
                      .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                      .contents_padding = {.lr = k_inst_picker_spacing, .t = k_inst_picker_spacing},
                      .contents_gap = k_inst_picker_spacing,
                      .contents_direction = layout::Direction::Column,
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                  },
              });

    {
        auto const section = FilterButtonSection(box_system, root, "LIBRARIES");

        for (auto const& l_ptr : context.libraries) {
            auto const& lib = *l_ptr;

            if (lib.insts_by_name.size == 0) continue;
            if (lib.file_format_specifics.tag != state.FileFormatForCurrentTab()) continue;

            auto const lib_id_hash = lib.Id().Hash();
            auto& hashes = state.tab == InstPickerState::Tab::FloeLibaries
                               ? state.selected_library_hashes
                               : state.selected_mirage_library_hashes;
            auto const is_selected = Contains(hashes, lib_id_hash);

            auto const button = FilterButton(
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
        auto const section = FilterButtonSection(box_system, root, "TAGS");
        for (auto const& l_ptr : context.libraries) {
            auto const& lib = *l_ptr;

            if (lib.insts_by_name.size == 0) continue;

            for (auto const& inst : lib.sorted_instruments) {
                for (auto const& tag : inst->tags) {
                    auto const tag_hash = Hash(tag);
                    auto const is_selected = Contains(state.selected_tags_hashes, tag_hash);
                    if (FilterButton(box_system, section, is_selected, {}, tag).button_fired) {
                        if (is_selected)
                            dyn::RemoveValue(state.selected_tags_hashes, tag_hash);
                        else
                            dyn::Append(state.selected_tags_hashes, tag_hash);
                    }
                }
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

    // Heading
    {
        auto const heading_box =
            DoBox(box_system,
                  {
                      .parent = root,
                      .layout {
                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                          .contents_padding = {.lr = k_inst_picker_spacing, .tb = k_inst_picker_spacing / 2},
                          .contents_direction = layout::Direction::Row,
                          .contents_align = layout::Alignment::Justify,
                      },
                  });
        DoBox(box_system,
              {
                  .parent = heading_box,
                  .text = fmt::Format(box_system.arena, "Layer {} Instrument", context.layer.index + 1),
                  .font = FontType::Heading1,
                  .size_from_text = true,
              });
    }

    {
        auto const tab_row = DoBox(box_system,
                                   {
                                       .parent = root,
                                       .background_fill = style::Colour::Background2,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_padding = {.lr = 3, .t = 3},
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                       },
                                   });

        for (auto const tab : EnumIterator<InstPickerState::Tab>()) {
            if (tab == InstPickerState::Tab::MirageLibraries && !context.has_mirage_libraries) continue;

            auto const tab_button = DoBox(
                box_system,
                {
                    .parent = tab_row,
                    .background_fill = tab == state.tab ? style::Colour::Background0 : style::Colour::None,
                    .background_fill_auto_hot_active_overlay = true,
                    .round_background_corners = 0b1100,
                    .activate_on_click_button = MouseButton::Left,
                    .activation_click_event =
                        tab != state.tab ? ActivationClickEvent::Up : ActivationClickEvent::None,
                    .layout =
                        {
                            .size = layout::k_hug_contents,
                        },
                });

            DoBox(box_system,
                  {
                      .parent = tab_button,
                      .text = ({
                          String s {};
                          switch (InstPickerState::Tab(tab)) {
                              case InstPickerState::Tab::FloeLibaries:
                                  s = context.has_mirage_libraries ? "Floe Instruments"_s : "Instruments";
                                  break;
                              case InstPickerState::Tab::MirageLibraries: s = "Mirage Instruments"; break;
                              case InstPickerState::Tab::Waveforms: s = "Waveforms"; break;
                              case InstPickerState::Tab::Count: PanicIfReached(); break;
                          }
                          s;
                      }),
                      .text_fill = tab == state.tab ? style::Colour::Text : style::Colour::Subtext0,
                      .size_from_text = true,
                      .layout =
                          {
                              .margins = {.lr = k_inst_picker_spacing, .tb = k_inst_picker_spacing / 2},
                          },
                  });

            if (tab_button.button_fired) state.tab = tab;
        }
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
            auto const instruments_top = DoBox(
                box_system,
                {
                    .parent = headings_row,
                    .layout {
                        .size = {k_instrument_list_width, layout::k_hug_contents},
                        .contents_padding = {.lr = k_inst_picker_spacing, .tb = k_inst_picker_spacing / 2},
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
                           "Previous instrument.",
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
                           "Next instrument.",
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
                           "Random instrument.",
                           style::k_font_heading2_size * 0.8f,
                           style::k_font_heading2_size)
                    .button_fired) {
                dyn::Append(box_system.state->deferred_actions,
                            [&]() { LoadRandomInstrument(context, state, true); });
            }
        }

        // divider
        DoBox(box_system,
              {
                  .parent = headings_row,
                  .background_fill = style::Colour::Surface2,
                  .layout {
                      .size = {box_system.imgui.PixelsToVw(1), layout::k_fill_parent},
                  },
              });

        {
            auto const filters_top = DoBox(
                box_system,
                {
                    .parent = headings_row,
                    .layout {
                        .size = {k_filter_list_width, layout::k_hug_contents},
                        .contents_padding = {.lr = k_inst_picker_spacing, .tb = k_inst_picker_spacing / 2},
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

    // divider
    DoBox(box_system,
          {
              .parent = root,
              .background_fill = style::Colour::Surface2,
              .layout {
                  .size = {layout::k_fill_parent, box_system.imgui.PixelsToVw(1)},
              },
          });

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
                                       .contents_padding = {.t = k_inst_picker_spacing},
                                       .contents_gap = k_inst_picker_spacing,
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                   },
                               });

        {
            if (context.layer.instrument_id.tag != InstrumentType::None) {
                if (TextButton(box_system,
                               DoBox(box_system,
                                     {
                                         .parent = lhs,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_padding = {.lr = k_inst_picker_spacing},
                                         },
                                     }),
                               fmt::Format(box_system.arena, "Unload {}", context.layer.InstName()),
                               "Unload the current instrument.",
                               true)) {
                    LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
                    box_system.imgui.CloseCurrentPopup();
                }
            }

            auto const search_box = DoBox(box_system,
                                          {
                                              .parent = lhs,
                                              .background_fill = style::Colour::Background2,
                                              .round_background_corners = 0b1111,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .margins = {.lr = k_inst_picker_spacing},
                                                  .contents_padding = {.lr = k_inst_picker_spacing / 2},
                                                  .contents_direction = layout::Direction::Row,
                                                  .contents_align = layout::Alignment::Start,
                                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                              },
                                          });
            // Icon
            DoBox(box_system,
                  {
                      .parent = search_box,
                      .text = ICON_FA_SEARCH,
                      .font_size = k_inst_picker_item_height * 0.9f,
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
                                  .size = {layout::k_fill_parent, k_inst_picker_item_height},
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
                              .font_size = k_inst_picker_item_height * 0.9f,
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

    // Vertical divider
    DoBox(box_system,
          {
              .parent = main_section,
              .background_fill = style::Colour::Surface2,
              .layout {
                  .size = {box_system.imgui.PixelsToVw(1), layout::k_fill_parent},
              },
          });

    {
        auto const rhs = DoBox(box_system,
                               {
                                   .parent = main_section,
                                   .layout {
                                       .size = {200, layout::k_fill_parent},
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

    // Horizontal divider
    DoBox(box_system,
          {
              .parent = root,
              .background_fill = style::Colour::Surface2,
              .layout {
                  .size = {layout::k_fill_parent, box_system.imgui.PixelsToVw(1)},
              },
          });

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

PUBLIC void DoInstPickerPopup(GuiBoxSystem& box_system,
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
