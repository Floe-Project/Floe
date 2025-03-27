// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_inst_picker.hpp"

#include "gui2_common_picker.hpp"

struct InstrumentCursor {
    bool operator==(InstrumentCursor const& o) const = default;
    usize lib_index;
    usize inst_index;
};

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
                                                    SearchDirection direction,
                                                    bool first,
                                                    bool picker_gui_is_open) {
    if (context.libraries.size == 0) return k_nullopt;

    if (cursor.lib_index >= context.libraries.size) cursor.lib_index = 0;

    if (!first) {
        switch (direction) {
            case SearchDirection::Forward: ++cursor.inst_index; break;
            case SearchDirection::Backward:
                static_assert(UnsignedInt<decltype(cursor.inst_index)>);
                --cursor.inst_index;
                break;
        }
    }

    for (usize lib_step = 0; lib_step < context.libraries.size + 1; (
             {
                 ++lib_step;
                 switch (direction) {
                     case SearchDirection::Forward:
                         cursor.lib_index = (cursor.lib_index + 1) % context.libraries.size;
                         cursor.inst_index = 0;
                         break;
                     case SearchDirection::Backward:
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
                         case SearchDirection::Forward: ++cursor.inst_index; break;
                         case SearchDirection::Backward: --cursor.inst_index; break;
                     }
                 })) {
            auto const& inst = *lib.sorted_instruments[cursor.inst_index];

            if (state.search.size && (!ContainsCaseInsensitiveAscii(inst.name, state.search) &&
                                      !ContainsCaseInsensitiveAscii(inst.folder.ValueOr({}), state.search)))
                continue;

            if ((!picker_gui_is_open || state.tab == InstPickerState::Tab::FloeLibaries) &&
                state.selected_tags_hashes.size) {
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

static void LoadInstrument(InstPickerContext const& context,
                           InstPickerState& state,
                           InstrumentCursor const& cursor,
                           bool scroll) {
    auto const& lib = *context.libraries[cursor.lib_index];
    auto const& inst = *lib.sorted_instruments[cursor.inst_index];
    LoadInstrument(context.engine,
                   context.layer.index,
                   sample_lib::InstrumentId {
                       .library = lib.Id(),
                       .inst_name = inst.name,
                   });
    if (scroll) state.scroll_to_show_selected = true;
}

void LoadAdjacentInstrument(InstPickerContext const& context,
                            InstPickerState& state,
                            SearchDirection direction,
                            bool picker_gui_is_open) {
    switch (context.layer.instrument_id.tag) {
        case InstrumentType::WaveformSynth: {
            auto waveform_index = ToInt(context.layer.instrument_id.Get<WaveformType>());
            switch (direction) {
                case SearchDirection::Forward:
                    if (waveform_index == ToInt(WaveformType::Count) - 1)
                        waveform_index = 0;
                    else
                        ++waveform_index;
                    break;
                case SearchDirection::Backward:
                    if (waveform_index == 0)
                        waveform_index = ToInt(WaveformType::Count) - 1;
                    else
                        --waveform_index;
                    break;
            }
            LoadInstrument(context.engine, context.layer.index, WaveformType(waveform_index));
            break;
        }
        case InstrumentType::None: {
            if (picker_gui_is_open && state.tab == InstPickerState::Tab::Waveforms) {
                LoadInstrument(context.engine, context.layer.index, WaveformType(0));
                break;
            }

            if (auto const cursor =
                    IterateInstrument(context, state, {0, 0}, direction, true, picker_gui_is_open)) {
                LoadInstrument(context, state, *cursor, true);
            }
            break;
        }
        case InstrumentType::Sampler: {
            auto const inst_id = context.layer.instrument_id.Get<sample_lib::InstrumentId>();

            if (auto const cursor = CurrentCursor(context, inst_id)) {
                if (auto const prev =
                        IterateInstrument(context, state, *cursor, direction, false, picker_gui_is_open)) {
                    LoadInstrument(context, state, *prev, true);
                }
            }
            break;
        }
    }
}

void LoadRandomInstrument(InstPickerContext const& context, InstPickerState& state, bool picker_gui_is_open) {
    if (picker_gui_is_open && state.tab == InstPickerState::Tab::Waveforms) {
        LoadInstrument(
            context.engine,
            context.layer.index,
            WaveformType(
                RandomIntInRange<u32>(context.engine.random_seed, 0, ToInt(WaveformType::Count) - 1)));
        return;
    }

    auto const first = IterateInstrument(context,
                                         state,
                                         {.lib_index = 0, .inst_index = 0},
                                         SearchDirection::Forward,
                                         true,
                                         picker_gui_is_open);
    if (!first) return;

    auto cursor = *first;

    usize num_instruments = 0;
    while (true) {
        if (auto const next = IterateInstrument(context,
                                                state,
                                                cursor,
                                                SearchDirection::Forward,
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
        cursor =
            *IterateInstrument(context, state, cursor, SearchDirection::Forward, false, picker_gui_is_open);

    LoadInstrument(context, state, cursor, true);
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
                                         SearchDirection::Forward,
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
                                                                         box_system.arena,
                                                                         true);
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

        if (auto next = IterateInstrument(context, state, cursor, SearchDirection::Forward, false, true)) {
            cursor = *next;
            if (cursor == *first) break;
        } else {
            break;
        }
    }
}

void DoInstPickerPopup(GuiBoxSystem& box_system,
                       imgui::Id popup_id,
                       Rect absolute_button_rect,
                       InstPickerContext& context,
                       InstPickerState& state) {
    DynamicSet<String> all_tags {box_system.arena};
    for (auto const& l_ptr : context.libraries) {
        auto const& lib = *l_ptr;
        for (auto const& ir : lib.sorted_irs)
            for (auto const& tag : ir->tags)
                all_tags.Insert(tag);
    }

    // IMPORTANT: we create the options struct inside the call so that lambdas and values from
    // statement-expressions live long enough.
    DoPickerPopup(
        box_system,
        popup_id,
        absolute_button_rect,
        PickerPopupOptions {
            .title = fmt::Format(box_system.arena, "Layer {} Instrument", context.layer.index + 1),
            .height = box_system.imgui.PixelsToVw(box_system.imgui.frame_input.window_size.height * 0.9f),
            .rhs_width = 200,
            .filters_col_width = 200,
            .item_type_name = "instrument",
            .items_section_heading = "Instruments",
            .tab_config = ({
                DynamicArrayBounded<ModalTabConfig, 3> tab_config {};
                dyn::Append(tab_config,
                            {
                                .text = context.has_mirage_libraries ? "Floe Instruments"_s : "Instruments",
                            });
                if (context.has_mirage_libraries) dyn::Append(tab_config, {.text = "Mirage Instruments"});
                dyn::Append(tab_config, {.text = "Waveforms"});
                tab_config;
            }),
            .current_tab_index = &ToIntRef(state.tab),
            .rhs_top_button = ({
                Optional<PickerPopupOptions::Button> unload_button {};
                if (context.layer.instrument_id.tag != InstrumentType::None) {
                    unload_button = PickerPopupOptions::Button {
                        .text = fmt::Format(box_system.arena, "Unload {}", context.layer.InstName()),
                        .tooltip = "Unload the current instrument.",
                        .on_fired =
                            [&]() {
                                LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
                                box_system.imgui.CloseCurrentPopup();
                            },
                    };
                }
                unload_button;
            }),
            .rhs_do_items = [&](GuiBoxSystem& box_system) { InstPickerItems(box_system, context, state); },
            .search = state.tab != InstPickerState::Tab::Waveforms ? &state.search : nullptr,
            .on_load_previous =
                [&]() { LoadAdjacentInstrument(context, state, SearchDirection::Backward, true); },
            .on_load_next = [&]() { LoadAdjacentInstrument(context, state, SearchDirection::Forward, true); },
            .on_load_random = [&]() { LoadRandomInstrument(context, state, true); },
            .on_scroll_to_show_selected = [&]() { state.scroll_to_show_selected = true; },
            .libraries = context.libraries,
            .library_filters = ({
                Optional<LibraryFilters> f {};
                if (state.tab != InstPickerState::Tab::Waveforms) {
                    f = LibraryFilters {
                        .selected_library_hashes = state.tab == InstPickerState::Tab::FloeLibaries
                                                       ? state.selected_library_hashes
                                                       : state.selected_mirage_library_hashes,
                        .library_images = context.library_images,
                        .sample_library_server = context.sample_library_server,
                        .skip_library =
                            [&](sample_lib::Library const& lib) {
                                if (lib.sorted_instruments.size == 0) return true;
                                if (state.tab == InstPickerState::Tab::Waveforms) return true;
                                return lib.file_format_specifics.tag != state.FileFormatForCurrentTab();
                            },
                    };
                }
                f;
            }),
            .tags_filters = ({
                Optional<TagsFilters> f {};
                if (state.tab == InstPickerState::Tab::FloeLibaries) {
                    f = TagsFilters {
                        .selected_tags_hashes = state.selected_tags_hashes,
                        .tags = {all_tags.table},
                    };
                }
                f;
            }),
            .on_clear_all_filters = [&]() { state.ClearAllFilters(); },
            .status_bar_height = 50,
            .status = [&]() -> Optional<String> {
                Optional<String> status {};

                if (auto const i = context.hovering_inst) {
                    DynamicArray<char> buf {box_system.arena};
                    fmt::Append(buf, "{} from {} by {}.", i->name, i->library.name, i->library.author);

                    if (i->description) fmt::Append(buf, " {}", i->description);

                    fmt::Append(buf, "\nTags: ");
                    if (i->tags.size == 0)
                        fmt::Append(buf, "None");
                    else {
                        for (auto const t : i->tags)
                            fmt::Append(buf, "{}, ", t);
                        dyn::Pop(buf, 2);
                    }

                    status = buf.ToOwnedSpan();
                } else if (auto const w = context.waveform_type_hovering) {
                    status = fmt::Format(
                        box_system.arena,
                        "{} waveform. A simple waveform useful for layering with sample instruments.",
                        k_waveform_type_names[ToInt(*w)]);
                }

                return status;
            },
        });
}
