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

void PostReadBookkeeping(Library& lib) {
    for (auto [key, value] : lib.insts_by_name) {
        auto& inst = **value;

        inst.loop_overview.all_regions_require_looping = true;

        for (auto const i : ::Range(ToInt(Loop::Mode::Count))) {
            inst.loop_overview.all_loops_convertible_to_mode[i] = true;
            inst.loop_overview.has_loops_convertible_to_mode[i] = false;
        }

        usize num_loops_with_locked_loop_points = 0;
        Array<usize, ToInt(Loop::Mode::Count)> num_loops_per_mode {};
        usize num_loops = 0;

        for (auto& region : inst.regions) {
            if (auto const& l = region.file.loop) {
                ++num_loops;
                ++num_loops_per_mode[ToInt(l->mode)];

                if (l->lock_mode) {
                    // This loop mode is locked, therefore all other modes in the
                    // all_loops_convertible_to_mode array should be false.
                    for (auto const i : ::Range(ToInt(Loop::Mode::Count)))
                        if (i != ToInt(l->mode)) inst.loop_overview.all_loops_convertible_to_mode[i] = false;
                } else {
                    // This loop mode is unlocked, therefore all other modes in the
                    // has_loops_convertible_to_mode array should be true.
                    for (auto const i : ::Range(ToInt(Loop::Mode::Count)))
                        if (i != ToInt(l->mode)) inst.loop_overview.has_loops_convertible_to_mode[i] = true;
                }
                inst.loop_overview.has_loops_convertible_to_mode[ToInt(l->mode)] = true;

                if (l->lock_loop_points) ++num_loops_with_locked_loop_points;
            }

            if (!region.file.always_loop) inst.loop_overview.all_regions_require_looping = false;
        }

        inst.loop_overview.all_loops_mode = k_nullopt;
        for (auto const i : ::Range(ToInt(Loop::Mode::Count))) {
            if (num_loops_per_mode[i] == num_loops) {
                inst.loop_overview.all_loops_mode = Loop::Mode(i);
                break;
            }
        }

        if (num_loops) inst.loop_overview.has_loops = true;

        if (num_loops != inst.regions.size) inst.loop_overview.has_non_loops = true;

        inst.loop_overview.user_defined_loops_allowed = true;
        if (num_loops && num_loops_with_locked_loop_points == num_loops)
            // If all the loops are locked, then user-defined loops won't be allowed.
            inst.loop_overview.user_defined_loops_allowed = false;
    }
}

} // namespace detail

} // namespace sample_lib
