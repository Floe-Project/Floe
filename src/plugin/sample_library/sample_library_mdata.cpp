// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foundation/foundation.hpp"
#include "tests/framework.hpp"
#include "utils/json/json_reader.hpp"

#include "audio_file.hpp"
#include "common/common_errors.hpp"
#include "common/constants.hpp"
#include "sample_library.hpp"

namespace sample_lib {

static Range ConvertVelocityToStartEndRange(s8 low_velo, s8 high_velo) {
    auto const lo = Max((s8)1, low_velo) - 1;
    auto const hi = high_velo - 1;

    constexpr auto k_existing_steps = 126.0;
    constexpr auto k_new_steps = 99.0;

    auto const start = RoundPositiveFloat((lo / k_existing_steps) * k_new_steps);
    auto const end = RoundPositiveFloat(Min((((hi + 1) / k_existing_steps) * k_new_steps), k_new_steps + 1));

    return {(u8)start, (u8)end};
}

TEST_CASE(TestConvertVelocityRange) {
    auto check = [&](s8 low_velo, s8 high_velo, Range expected_out) {
        auto const out = ConvertVelocityToStartEndRange(low_velo, high_velo);
        REQUIRE_EQ(out.start, expected_out.start);
        REQUIRE_EQ(out.end, expected_out.end);
    };

    check(1, 127, {0, 100});
    check(64, 127, {50, 100});
    check(1, 10, {0, 8});
    check(11, 20, {8, 16});
    check(21, 30, {16, 24});
    check(31, 40, {24, 31});
    check(41, 50, {31, 39});
    check(51, 60, {39, 47});
    check(61, 70, {47, 55});
    check(71, 80, {55, 63});
    check(81, 90, {63, 71});
    check(91, 100, {71, 79});
    check(101, 110, {79, 86});
    check(111, 120, {86, 94});
    check(121, 127, {94, 100});
    return k_success;
}

static String GetString(Library const& library, mdata::StringInPool s) {
    return mdata::StringFromStringPool(library.file_format_specifics.Get<MdataSpecifics>().string_pool.data,
                                       s);
}

static ErrorCodeOr<Reader> CreateMdataFileReader(Library const& library, String library_file_path) {
    auto const mdata_info = library.file_format_specifics.Get<MdataSpecifics>();
    auto f = mdata_info.files_by_path.Find(library_file_path);
    if (!f) return ErrorCode {FilesystemError::PathDoesNotExist};
    auto& file = **f;

    auto const read_pos = mdata_info.file_data_pool_offset + file.offset_in_file_data_pool;
    ASSERT(file.size_bytes > 0);

    if (mdata_info.file_data.size) {
        ASSERT(mdata_info.file_data.size);
        return Reader::FromMemory(mdata_info.file_data.SubSpan((usize)read_pos, (usize)file.size_bytes));
    } else {
        return Reader::FromFileSection(library.path, read_pos, file.size_bytes);
    }
}

static ErrorCodeOr<Library*>
ReadMdataFile(ArenaAllocator& arena, ArenaAllocator& scratch_arena, Reader& reader) {
    static_assert(k_endianness == Endianness::Little);
    reader.pos = 0;

    auto library_ptr = arena.NewUninitialised<Library>();
    PLACEMENT_NEW(library_ptr)
    Library {
        .create_file_reader = CreateMdataFileReader,
        .file_format_specifics = MdataSpecifics {},
    };
    auto& library = *library_ptr;
    auto& mdata_info = library.file_format_specifics.Get<MdataSpecifics>();

    {
        mdata::MasterHeader header;
        TRY(reader.Read(&header, sizeof(mdata::MasterHeader)));
        if (header.id_magic != mdata::HeaderIdMasterMagic) return ErrorCode(CommonError::FileFormatIsInvalid);
        library.name = arena.Clone(header.Name());
        library.minor_version = header.version;
    }

    {
        mdata::ChunkHeader info_header;
        TRY(reader.Read(&info_header, sizeof(mdata::ChunkHeader)));
        if (info_header.size_bytes_of_following_data == 0) return ErrorCode(CommonError::FileFormatIsInvalid);
        if (info_header.id != mdata::HeaderIdInfoJson) return ErrorCode(CommonError::FileFormatIsInvalid);

        DynamicArray<char> json_string {scratch_arena};
        dyn::Resize(json_string, (usize)info_header.size_bytes_of_following_data);
        TRY(reader.Read(json_string.data, (usize)info_header.size_bytes_of_following_data));

        using namespace json;

        auto parsed = Parse(json_string,
                            [&](EventHandlerStack&, Event const& event) {
                                if (SetIfMatching(event, "description", library.tagline, arena)) return true;

                                String url {};
                                if (SetIfMatching(event, "url", url, arena)) {
                                    library.url = url;
                                    return true;
                                }

                                if constexpr (false) {
                                    String s;
                                    if (SetIfMatching(event, "default_inst_relative_folder", s, arena))
                                        return true;
                                }

                                if constexpr (false) {
                                    String file_extension {};
                                    if (SetIfMatching(event, "file_extension", file_extension, arena))
                                        return true;
                                }

                                if constexpr (false) {
                                    u32 major;
                                    u32 minor;
                                    u32 patch;
                                    if (SetIfMatching(event, "required_floe_version_major", major))
                                        return true;
                                    if (SetIfMatching(event, "required_floe_version_minor", minor))
                                        return true;
                                    if (SetIfMatching(event, "required_floe_version_patch", patch))
                                        return true;
                                }

                                return false;
                            },
                            scratch_arena,
                            {});
        ASSERT(parsed.Succeeded());
    }

    Span<mdata::ExtendedInstrumentInfo> ex_inst_infos {};
    Span<mdata::InstrumentInfo> inst_infos {};
    Span<mdata::SamplerRegionInfo> sampler_region_infos {};

    while (reader.pos < reader.size) {
        mdata::ChunkHeader header;
        TRY(reader.Read(&header, sizeof(mdata::ChunkHeader)));
        if (header.size_bytes_of_following_data <= 0) continue;
        auto const size_bytes_of_following_data = (u64)header.size_bytes_of_following_data;

        switch (header.id) {
            case mdata::HeaderIdInfoJson: {
                PanicIfReached(); // should be handled above
                break;
            }

            case mdata::HeaderIdStringPool: {
                mdata_info.string_pool =
                    arena.AllocateExactSizeUninitialised<char>(size_bytes_of_following_data);
                TRY(reader.Read((char*)mdata_info.string_pool.data, size_bytes_of_following_data));
                break;
            }

            case mdata::HeaderIdFileDataPool: {
                mdata_info.file_data_pool_offset = reader.pos;
                break;
            }

            case mdata::HeaderIdInstrumentInfoArray: {
                ASSERT(mdata_info.string_pool.size != 0); // string pool must be first
                auto num_insts = size_bytes_of_following_data / sizeof(mdata::InstrumentInfo);
                inst_infos = scratch_arena.AllocateExactSizeUninitialised<mdata::InstrumentInfo>(num_insts);
                TRY(reader.Read(inst_infos.data, size_bytes_of_following_data));
                break;
            }

            case mdata::HeaderIdExtendedInstrumentInfoArray: {
                auto num_insts = size_bytes_of_following_data / sizeof(mdata::ExtendedInstrumentInfo);
                ex_inst_infos =
                    scratch_arena.AllocateExactSizeUninitialised<mdata::ExtendedInstrumentInfo>(num_insts);
                TRY(reader.Read(ex_inst_infos.data, size_bytes_of_following_data));
                break;
            }

            case mdata::HeaderIdSamplerRegionInfoArray: {
                ASSERT(mdata_info.string_pool.size != 0); // string pool must be first
                auto num_samples = size_bytes_of_following_data / sizeof(mdata::SamplerRegionInfo);

                sampler_region_infos =
                    scratch_arena.AllocateExactSizeUninitialised<mdata::SamplerRegionInfo>(num_samples);
                TRY(reader.Read(sampler_region_infos.data, size_bytes_of_following_data));
                break;
            }

            case mdata::HeaderIdFileInfoArray: {
                auto num_files = size_bytes_of_following_data / sizeof(mdata::FileInfo);

                mdata_info.file_infos = arena.AllocateExactSizeUninitialised<mdata::FileInfo>(num_files);
                TRY(reader.Read(mdata_info.file_infos.data, size_bytes_of_following_data));

                for (auto& f : mdata_info.file_infos) {
                    auto const path = GetString(library, f.virtual_filepath);
                    if (f.file_type == mdata::FileTypeRawAudioSamples) {
                        // Confusingly, the file extension of raw audio samples was still wav, we amend that
                        // here. There could be various forms of raw samples, but in reality only 1 type was
                        // used.
                        auto ext = path::Extension(path);
                        ASSERT(ext == ".wav"_s);
                        ASSERT(f.channels == 2);
                        ASSERT(f.audio_format == mdata::AudioFileTypeRaw16Pcm);
                        ASSERT(RoundPositiveFloat(f.sample_rate) == 44100);
                        static_assert(".wav"_s.size == k_raw_16_bit_stereo_44100_format_ext.size);
                        CopyStringIntoBufferWithNullTerm(MutableString {(char*)ext.data, ext.size},
                                                         k_raw_16_bit_stereo_44100_format_ext);
                    }
                }

                for (auto const& f : mdata_info.file_infos) {
                    if (f.folder_type != mdata::FolderTypeFiles) continue;
                    auto const name = GetString(library, f.name);
                    if (name == "icon.png" || name == "icon.jpg")
                        library.icon_image_path = GetString(library, f.virtual_filepath);
                    if (name == "background.png" || name == "background.jpg")
                        library.background_image_path = GetString(library, f.virtual_filepath);
                }

                mdata_info.files_by_path =
                    HashTable<String, mdata::FileInfo const*>::Create(arena, mdata_info.file_infos.size);
                for (auto& f : mdata_info.file_infos) {
                    if (f.file_type == mdata::FileTypeSpecialAudioData) continue;
                    auto const path = GetString(library, f.virtual_filepath);
                    bool const inserted = mdata_info.files_by_path.InsertGrowIfNeeded(arena, path, &f);
                    ASSERT(inserted);
                }

                for (auto const& f : mdata_info.file_infos) {
                    if (f.folder_type != mdata::FolderTypeIRs) continue;
                    auto const name = GetString(library, f.name_no_ext);
                    auto const path = GetString(library, f.virtual_filepath);
                    ASSERT(name.size <= k_max_ir_name_size);

                    auto ir = arena.NewUninitialised<ImpulseResponse>();
                    PLACEMENT_NEW(ir)
                    ImpulseResponse {
                        .library = library,
                        .name = name,
                        .path = path,
                    };
                    library.irs_by_name.InsertGrowIfNeeded(arena, name, ir);
                }
                break;
            }

            case mdata::HeaderIdDirectoryEntryArray:
            case mdata::HeaderIdDirectoryEntryTreeRoots:
            default: {
                reader.pos += size_bytes_of_following_data;
                break;
            }
        }
    }

    library.insts_by_name = HashTable<String, Instrument*>::Create(arena, inst_infos.size);
    for (auto& i : inst_infos) {
        auto const path = GetString(library, i.virtual_filepath);

        if (mdata::SpecialAudioDataFromInstPath(path) != mdata::SpecialAudioDataTypeNone) continue;

        auto name = path::Filename(path);
        if (auto f = library.insts_by_name.Find(name)) {
            // The MDATA format didn't require instrument names to be unique, but we now do. Most instrument
            // names were unique anyways in the available MDATA libraries. However, the few conflicts that
            // existed must be handled when we read old presets. Therefore, be careful changing this renaming
            // algorithm, it will effect the conflict-resolution code used when parsing old presets.
            int num = 2;
            DynamicArray<char> buf {arena};
            do {
                fmt::Assign(buf, "{} {}", name, num++);
            } while (library.insts_by_name.Find(buf));
            name = buf.ToOwnedSpan();
        } else {
            name = arena.Clone(name);
        }

        auto folders = path::Directory(path).ValueOr({});
        folders = TrimStartIfMatches(folders, "sampler"_s);
        while (EndsWith(folders, '/'))
            folders.RemoveSuffix(1);
        folders = arena.Clone(folders);

        auto inst = arena.NewUninitialised<Instrument>();
        PLACEMENT_NEW(inst)
        Instrument {
            .library = library,
            .name = name,
            .folders = folders.size ? Optional<String>(folders) : nullopt,
        };

        bool velocity_layers_are_feathered =
            false; // velocity layer feathering used to be instrument wide rather than per-region
        auto trigger_event = TriggerEvent::NoteOn;
        auto groups_are_xfade_layers = false;
        for (auto const& i_ex : ex_inst_infos) {
            if (i_ex.inst_index == i.index) {
                if (i_ex.flags & mdata::InstExtendedFlagsGroupsAreXfadeLayers) groups_are_xfade_layers = true;
                if (i_ex.flags & mdata::InstExtendedFlagsFeatherVelocityLayers)
                    velocity_layers_are_feathered = true;
                if (i_ex.flags & mdata::InstExtendedFlagsTriggerOnRelease)
                    trigger_event = TriggerEvent::NoteOff;
            }
        }

        u32 max_rr_pos = 0;

        inst->regions = arena.AllocateExactSizeUninitialised<Region>(CheckedCast<usize>(i.total_num_regions));
        usize regions_span_index = 0;
        for (auto [group_index, group_info] : Enumerate(i.Groups())) {
            ASSERT(group_index == (usize)group_info.index);

            for (auto [region_index, region_info] : Enumerate<mdata::Index>(sampler_region_infos)) {
                if (region_info.inst_info_index != i.index) continue;
                if (region_info.group_index != group_info.index) continue;

                auto const file_info = mdata_info.file_infos[CheckedCast<usize>(region_info.file_info_index)];
                ASSERT(region_info.loop_end <= (s32)file_info.num_frames);

                auto const file_path = GetString(library, file_info.virtual_filepath);
                if (i.sampler_region_index_for_gui_waveform == region_index)
                    inst->audio_file_path_for_waveform = file_path;

                if (group_info.round_robin_or_xfade_index > (s32)max_rr_pos)
                    max_rr_pos = CheckedCast<u32>(group_info.round_robin_or_xfade_index);

                inst->regions[regions_span_index++] = Region {
                    .file =
                        {
                            .path = file_path,
                            .root_key = CheckedCast<u8>(region_info.root_note),
                            .loop = ({
                                Optional<Loop> l {};
                                constexpr bool k_ping_pong = false; // MDATA didn't allow ping pong loops
                                if (region_info.looping_mode == mdata::SampleLoopingModeAlwaysLoopAnyRegion ||
                                    region_info.looping_mode == mdata::SampleLoopingModeAlwaysLoopSetRegion) {
                                    l = Loop {.start_frame = region_info.loop_start,
                                              .end_frame = region_info.loop_end,
                                              .crossfade_frames =
                                                  CheckedCast<u32>(region_info.loop_crossfade),
                                              .ping_pong = k_ping_pong};
                                } else if (region_info.looping_mode ==
                                           mdata::SampleLoopingModeAlwaysLoopWholeRegion)
                                    l = Loop {0, file_info.num_frames, 0, k_ping_pong};
                                l;
                            }),
                        },
                    .trigger =
                        {
                            .event = trigger_event,
                            .key_range = {CheckedCast<u8>(region_info.low_note),
                                          CheckedCast<u8>((int)region_info.high_note + 1)},
                            .velocity_range =
                                ConvertVelocityToStartEndRange(region_info.low_velo, region_info.high_velo),
                            .round_robin_index = ({
                                Optional<u32> rr {};
                                if (!groups_are_xfade_layers &&
                                    group_info.round_robin_or_xfade_index != mdata::k_no_round_robin_or_xfade)
                                    rr = CheckedCast<u32>(group_info.round_robin_or_xfade_index);
                                rr;
                            }),
                        },
                    .options =
                        {
                            .timbre_crossfade_region = ({
                                Optional<Range> r {};
                                if (groups_are_xfade_layers) {
                                    switch (group_info.round_robin_or_xfade_index) {
                                        case 0: r = Range {0, 90}; break;
                                        case 1: r = Range {10, 100}; break;
                                        default: PanicIfReached();
                                    }
                                }
                                r;
                            }),
                            .feather_overlapping_velocity_regions = velocity_layers_are_feathered,
                        },
                };
            }
        }
        ASSERT((mdata::Index)inst->regions.size == i.total_num_regions);

        inst->max_rr_pos = max_rr_pos;

        ASSERT(name.size <= k_max_instrument_name_size);
        auto const inserted = library.insts_by_name.InsertWithoutGrowing(name, inst);
        ASSERT(inserted);
    }

    // In the MDATA format when velocity-feathering was enabled for an instrument, adjacent velocity layers
    // were automatically made to overlap. We recreate that old behaviour here taking into account that now
    // velocity feathering is a per-region setting.
    for (auto [key, inst_ptr_ptr] : library.insts_by_name) {
        auto inst = *inst_ptr_ptr;

        // with MDATA, the velocity feathering feature was instrument-wide rather then per-region so we can
        // just check the first
        if (!inst->regions.size || !inst->regions[0].options.feather_overlapping_velocity_regions) continue;

        Sort(inst->regions, [](Region const& a, Region const& b) {
            return a.trigger.velocity_range.start < b.trigger.velocity_range.start;
        });

        for (auto const rr_group : ::Range(inst->max_rr_pos + 1)) {
            DynamicArray<Region*> group {scratch_arena};
            for (auto& region : inst->regions)
                if (!region.trigger.round_robin_index || region.trigger.round_robin_index.Value() == rr_group)
                    dyn::Append(group, &region);

            DynamicArray<DynamicArray<Region*>> key_range_bins {scratch_arena};
            for (auto& region : group) {
                bool put_in_bin = false;
                for (auto& bin : key_range_bins) {
                    if (region->trigger.key_range == bin[0]->trigger.key_range) {
                        dyn::Append(bin, region);
                        put_in_bin = true;
                        break;
                    }
                }
                if (!put_in_bin) {
                    DynamicArray<Region*> bin {scratch_arena};
                    dyn::Append(bin, region);
                    dyn::Emplace(key_range_bins, Move(bin));
                }
            }

            constexpr f32 k_overlap_percent = 0.35f;
            for (auto& regions : key_range_bins) {
                if (regions.size == 1) continue;

                // I don't know why this is the case, but some in-development MDATAs have this region range,
                // let's just skip it for now because library development will transition to the Lua format
                // anyways.
                if (regions[0]->trigger.key_range == Range {1, 2}) continue;

                DynamicArray<Range> new_ranges {scratch_arena};

                for (auto const i : ::Range(regions.size)) {
                    auto& region = regions[i];

                    Range new_range {region->trigger.velocity_range.start,
                                     region->trigger.velocity_range.end};

                    if (i != 0) {
                        auto const& prev_region = regions[i - 1];
                        if (prev_region->trigger.velocity_range.end == region->trigger.velocity_range.start) {
                            auto const delta =
                                (u8)(prev_region->trigger.velocity_range.Size() * k_overlap_percent);
                            ASSERT(new_range.start > delta);
                            new_range.start -= delta;
                        }
                    }

                    if (i != (regions.size - 1)) {
                        auto const& next_region = regions[i + 1];
                        if (next_region->trigger.velocity_range.start == region->trigger.velocity_range.end) {
                            auto const delta =
                                (s8)(next_region->trigger.velocity_range.Size() * k_overlap_percent);
                            ASSERT(new_range.end < 100);
                            new_range.end += delta;
                        }
                    }

                    dyn::Append(new_ranges, new_range);
                }

                auto regions_it = regions.begin();
                auto new_ranges_it = new_ranges.begin();
                for (; regions_it != regions.end() && new_ranges_it != new_ranges.end();
                     ++regions_it, ++new_ranges_it)
                    (*regions_it)->trigger.velocity_range = *new_ranges_it;

                for (auto const vel : ::Range((u8)100)) {
                    int num = 0;
                    for (auto region : regions)
                        if (region->trigger.velocity_range.Contains(vel)) ++num;
                    ASSERT(num <= 2);
                }
            }
        }
    }

    return library_ptr;
}

ErrorCodeOr<u64> MdataHash(Reader& reader) {
    reader.pos = 0;
    mdata::MasterHeader header;
    TRY(reader.Read(&header, sizeof(mdata::MasterHeader)));
    if (header.id_magic != mdata::HeaderIdMasterMagic) return ErrorCode(CommonError::FileFormatIsInvalid);
    return Hash(header.Name());
}

LibraryPtrOrError
ReadMdata(Reader& reader, String filepath, ArenaAllocator& result_arena, ArenaAllocator& scratch_arena) {
    auto const scratch_cursor = scratch_arena.TotalUsed();
    DEFER { scratch_arena.TryShrinkTotalUsed(scratch_cursor); };

    auto library = ({
        auto o = ReadMdataFile(result_arena, scratch_arena, reader);
        if (o.HasError()) return Error {o.Error(), {}};
        o.Value();
    });

    library->path = String(filepath.Clone(result_arena));
    if (reader.memory)
        library->file_format_specifics.Get<MdataSpecifics>().file_data = {reader.memory, reader.size};

    return library;
}

} // namespace sample_lib

TEST_REGISTRATION(RegisterLibraryMdataTests) { REGISTER_TEST(sample_lib::TestConvertVelocityRange); }
