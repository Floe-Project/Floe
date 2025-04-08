// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sample_library.hpp"

ErrorCodeOr<void>
CustomValueToString(Writer writer, sample_lib::LibraryIdRef id, fmt::FormatOptions options) {
    auto const sep = " - "_s;
    TRY(PadToRequiredWidthIfNeeded(writer, options, id.author.size + sep.size + id.name.size));
    TRY(writer.WriteChars(id.author));
    TRY(writer.WriteChars(sep));
    return writer.WriteChars(id.name);
}

namespace sample_lib {

u64 Hash(LibraryIdRef const& id) { return id.Hash(); }

ErrorCodeOr<u64> Hash(Reader& reader, FileFormat format) {
    switch (format) {
        case FileFormat::Mdata: return MdataHash(reader);
        case FileFormat::Lua: return LuaHash(reader);
    }
    PanicIfReached();
    return {};
}

bool FilenameIsFloeLuaFile(String filename) {
    return IsEqualToCaseInsensitiveAscii(filename, "floe.lua") ||
           EndsWithCaseInsensitiveAscii(filename, ".floe.lua"_s);
}

bool FilenameIsMdataFile(String filename) { return EndsWithCaseInsensitiveAscii(filename, ".mdata"_s); }

Optional<FileFormat> DetermineFileFormat(String path) {
    auto const filename = path::Filename(path);
    if (FilenameIsFloeLuaFile(filename)) return FileFormat::Lua;
    if (FilenameIsMdataFile(filename)) return FileFormat::Mdata;
    return k_nullopt;
}

LibraryPtrOrError Read(Reader& reader,
                       FileFormat format,
                       String filepath,
                       ArenaAllocator& result_arena,
                       ArenaAllocator& scatch_arena,
                       Options options) {
    switch (format) {
        case FileFormat::Mdata: return ReadMdata(reader, filepath, result_arena, scatch_arena);
        case FileFormat::Lua: return ReadLua(reader, filepath, result_arena, scatch_arena, options);
    }
    PanicIfReached();
}

namespace detail {

void PostReadBookkeeping(Library& lib, Allocator& arena) {

    // Sort items into their folders, and by name within each folder.
    auto const sort_function = [](auto* a, auto* b) {
        auto const& a_folder = a->folder;
        auto const& b_folder = b->folder;
        if (!a_folder && b_folder) return true; // no-folder items first
        if (a_folder && !b_folder) return false; // no-folder items first
        if (!a_folder && !b_folder) return a->name < b->name; // no-folder items by name
        // They both have folders
        if (a_folder != b_folder) return *a_folder < *b_folder;
        return a->name < b->name;
    };

    {
        lib.sorted_instruments = arena.AllocateExactSizeUninitialised<Instrument*>(lib.insts_by_name.size);
        usize index = 0;
        for (auto [key, value] : lib.insts_by_name) {
            auto& inst = **value;
            lib.sorted_instruments[index++] = &inst;
        }

        Sort(lib.sorted_instruments, sort_function);
    }

    {
        lib.sorted_irs = arena.AllocateExactSizeUninitialised<ImpulseResponse*>(lib.irs_by_name.size);
        usize index = 0;
        for (auto [key, value] : lib.irs_by_name) {
            auto& ir = **value;
            lib.sorted_irs[index++] = &ir;
        }

        Sort(lib.sorted_irs, sort_function);
    }

    for (auto [key, value] : lib.insts_by_name) {
        auto& inst = **value;

        inst.loop_overview.all_regions_require_looping = true;

        for (auto const i : ::Range(ToInt(LoopMode::Count)))
            inst.loop_overview.all_loops_convertible_to_mode[i] = true;

        Array<usize, ToInt(LoopMode::Count)> num_loops_per_mode {};
        Array<usize, ToInt(LoopMode::Count)> num_loops_per_mode_with_locked_points {};

        bool all_regions_never_loop = true;

        for (auto& region : inst.regions) {
            if (auto const& l = region.loop.builtin_loop) {
                ++num_loops_per_mode[ToInt(l->mode)];

                if (l->lock_mode) {
                    // This loop mode is locked, therefore all other modes in the
                    // all_loops_convertible_to_mode array should be false.
                    for (auto const i : ::Range(ToInt(LoopMode::Count)))
                        if (i != ToInt(l->mode)) inst.loop_overview.all_loops_convertible_to_mode[i] = false;
                }

                if (l->lock_loop_points) ++num_loops_per_mode_with_locked_points[ToInt(l->mode)];
            }

            if (region.loop.loop_requirement != LoopRequirement::AlwaysLoop)
                inst.loop_overview.all_regions_require_looping = false;
            if (region.loop.loop_requirement != LoopRequirement::NeverLoop) all_regions_never_loop = false;

            if (region.timbre_layering.layer_range) inst.uses_timbre_layering = true;
        }

        auto const num_loops = Sum(num_loops_per_mode);

        if (num_loops) inst.loop_overview.has_loops = true;
        if (num_loops != inst.regions.size) inst.loop_overview.has_non_loops = true;

        inst.loop_overview.all_loops_mode = k_nullopt;
        for (auto const i : ::Range(ToInt(LoopMode::Count))) {
            if (num_loops_per_mode[i] == num_loops) {
                inst.loop_overview.all_loops_mode = LoopMode(i);
                break;
            }
        }

        {
            inst.loop_overview.user_defined_loops_allowed = true;

            // If all regions have loops, and they all have locked loop points, then user-defined loops are
            // not allowed.
            if (num_loops && Sum(num_loops_per_mode_with_locked_points) == num_loops)
                inst.loop_overview.user_defined_loops_allowed = false;

            // If all regions never loop, then user-defined loops are not allowed.
            if (all_regions_never_loop) inst.loop_overview.user_defined_loops_allowed = false;
        }
    }
}

} // namespace detail

} // namespace sample_lib
