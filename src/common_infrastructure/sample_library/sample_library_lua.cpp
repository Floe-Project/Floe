// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <xxhash.h>

#include "foundation/container/hash_table.hpp"
#include "foundation/foundation.hpp"
#include "foundation/utils/format.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "tests/framework.hpp"

#include "common_infrastructure/constants.hpp"

#include "sample_library.hpp"

namespace sample_lib {

u64 Hash(LibraryPath const& path) { return HashFnv1a(path.str); }

ErrorCodeCategory const lua_error_category {
    .category_id = "LUA",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((LuaErrorCode)code.code) {
            case LuaErrorCode::Memory: str = "Lua script uses too much memory"; break;
            case LuaErrorCode::Syntax: str = "Lua syntax error"; break;
            case LuaErrorCode::Runtime: str = "Lua runtime error"; break;
            case LuaErrorCode::Timeout: str = "Lua script took too long"; break;
            case LuaErrorCode::Unexpected: str = "something unexpected happened"; break;
        }
        return writer.WriteChars(str);
    },
};

static String LuaString(lua_State* lua, int stack_index) {
    usize size {};
    auto ptr = lua_tolstring(lua, stack_index, &size);
    return {ptr, size};
}

static String LuaTypeName(lua_State* lua, int type) { return FromNullTerminated(lua_typename(lua, type)); }

static MutableString LuaValueToString(lua_State* lua, int stack_index, ArenaAllocator& arena) {
    auto const type = lua_type(lua, stack_index);
    DynamicArray<char> result {arena};
    switch (type) {
        case LUA_TNUMBER: {
            fmt::Append(result, "{g}: ", lua_tonumber(lua, stack_index));
            break;
        }
        case LUA_TBOOLEAN: {
            fmt::Append(result, "{}: ", (bool)lua_toboolean(lua, stack_index));
            break;
        }
        case LUA_TSTRING: {
            fmt::Append(result, "\"{}\": ", LuaString(lua, stack_index));
            break;
        }
        case LUA_TTABLE: break;
        case LUA_TFUNCTION: break;
        case LUA_TUSERDATA: break;
        case LUA_TTHREAD: break;
        case LUA_TLIGHTUSERDATA: break;
    }
    fmt::Append(result, "a {}", LuaTypeName(lua, type));
    return result.ToOwnedSpan();
}

struct LuaState {
    lua_State* lua;
    ArenaAllocator& result_arena;
    ArenaAllocator& lua_arena;
    usize initial_lua_arena_size;
    Options const& options;
    TimePoint const start_time;
    String filepath;
    DynamicHashTable<LibraryPath, FileAttribution, Hash> files_requiring_attribution {result_arena};
};

#define SET_FIELD_VALUE_ARGS                                                                                 \
    [[maybe_unused]] LuaState &ctx, [[maybe_unused]] void *obj, [[maybe_unused]] const struct FieldInfo &info
#define FIELD_OBJ (*(Type*)obj)

enum class InterpretedTypes : u32 {
    Library,
    Instrument,
    ImpulseResponse,
    Region,
    BuiltinLoop,
    RegionLoop,
    RegionAudioProps,
    RegionTimbreLayering,
    TriggerCriteria,
    FileAttribution,
    Count,
};

struct FieldInfo {
    struct Range {
        constexpr bool Active() const { return min != max; }
        f64 min;
        f64 max;
    };

    ErrorCodeOr<void> AppendDescription(Writer writer, bool verbose) const {
        TRY(writer.WriteChars(description_sentence));

        if (range.Active())
            TRY(fmt::FormatToWriter(writer, " On a range from {.0} to {.0}.", range.min, range.max));

        if (enum_options.size) {
            auto const multiline = verbose && enum_descriptions.size;
            TRY(fmt::FormatToWriter(writer, " Must be one of: "));
            if (multiline) TRY(writer.WriteChar('\n'));
            for (auto const enum_index : ::Range(enum_options.size)) {
                auto& option = enum_options[enum_index];
                if (option == nullptr) break;
                if (enum_options.size != 1 && (&option != Begin(enum_options))) {
                    if (!multiline) {
                        if (&option == (End(enum_options) - 2))
                            TRY(writer.WriteChars(" or "));
                        else
                            TRY(writer.WriteChars(", "));
                    }
                }
                TRY(fmt::FormatToWriter(writer, "\"{}\"", option));
                if (multiline) {
                    ASSERT_EQ(enum_options.size, enum_descriptions.size + 1);
                    TRY(fmt::FormatToWriter(writer, " => {}", enum_descriptions[enum_index]));
                    if (&option != (End(enum_options) - 2)) TRY(writer.WriteChar('\n'));
                }
            }
            if (!multiline) TRY(writer.WriteChar('.'));
        }

        if (verbose) {
            if (required)
                TRY(fmt::FormatToWriter(writer, " [required]"));
            else
                TRY(fmt::FormatToWriter(writer, "\n[optional, default: {}]", default_value));
        }

        return k_success;
    }

    String name;
    String description_sentence;
    String example;
    String default_value;
    int lua_type;
    Optional<InterpretedTypes> subtype {};
    bool required;
    bool is_array;
    Range range {};
    Span<char const* const> enum_options {};
    Span<char const* const> enum_descriptions {};
    void (*set)(SET_FIELD_VALUE_ARGS);
};

using ErrorString = MutableString;

template <typename T>
struct TableFields;

template <typename Type>
concept InterpretableType = requires {
    Enum<typename TableFields<Type>::Field>;
    // there's other requirements too but this will do for now
};

enum class UserdataTypes : u32 { Library, Instrument, SoundSource, Ir, Count };
static constexpr char const* k_userdata_type_names[] = {
    "library",
    "instrument",
    "sound_source",
    "ir",
};
static_assert(ArraySize(k_userdata_type_names) == ToInt(UserdataTypes::Count));
static auto TypeName(auto e) { return k_userdata_type_names[ToInt(e)]; }

template <typename Type>
struct LightUserDataWrapper {
    UserdataTypes type;
    Type obj;
};

template <typename Type>
static Type* LuaUserdataOrNull(lua_State* lua, int stack_index, UserdataTypes t) {
    if (!lua_islightuserdata(lua, stack_index)) return nullptr;
    auto d = (LightUserDataWrapper<Type>*)lua_touserdata(lua, stack_index);
    if (d->type != t) return nullptr;
    return &d->obj;
}

template <typename Type>
static Type* LuaCheckUserdata(lua_State* lua, int stack_index, UserdataTypes t) {
    auto ud = LuaUserdataOrNull<Type>(lua, stack_index, t);
    if (ud == nullptr) {
        auto const msg = fmt::FormatInline<64>("'{}' expected\0", TypeName(t));
        luaL_argcheck(lua, false, stack_index, msg.data);
    }
    return ud;
}

static Error ErrorAndNotify(LuaState& ctx,
                            ErrorCode error,
                            FunctionRef<void(DynamicArray<char>& message)> append_message) {
    DynamicArray<char> buf {ctx.result_arena};
    if (append_message) {
        append_message(buf);
        dyn::Append(buf, '\n');
    }
    dyn::AppendSpan(buf, ctx.filepath);
    auto const error_message = buf.ToOwnedSpan();
    return {error, error_message};
}

template <InterpretableType Type>
void InterpretTable(LuaState& ctx, int stack_index, Type& type);

static void IterateTableAtTop(LuaState& ctx, auto&& table_pair_callback) {
    if (lua_checkstack(ctx.lua, 3) == false) luaL_error(ctx.lua, "out of memory");

    auto const table_index = lua_gettop(ctx.lua);
    lua_pushnil(ctx.lua); // first key
    while (lua_next(ctx.lua, table_index) != 0) {
        DEFER {
            // removes 'value'; keeps 'key' for next iteration
            lua_pop(ctx.lua, 1);
        };

        // 'key' (at index -2) and 'value' (at index -1)
        table_pair_callback();
    }
}

static MutableString StringFromTop(LuaState& ctx) {
    return ctx.result_arena.Clone(FromNullTerminated(luaL_checkstring(ctx.lua, -1)));
}

static LibraryPath PathFromTop(LuaState& ctx) {
    auto const path = FromNullTerminated(luaL_checkstring(ctx.lua, -1));
    // we wan't Floe libraries to be portable and therefore they shouldn't reference files outside the library
    if (path::IsAbsolute(path) || StartsWithSpan(path, ".."_s))
        luaL_error(ctx.lua, "Path '{}' must be a relaive path to within the folder of floe.lua", path);
    return {ctx.result_arena.Clone(path)};
}

template <typename Type>
static Type NumberFromTop(LuaState& ctx, FieldInfo field_info) {
    f64 const val = Integral<Type> ? (f64)luaL_checkinteger(ctx.lua, -1) : luaL_checknumber(ctx.lua, -1);
    if (field_info.range.Active()) {
        if (val < field_info.range.min || val > field_info.range.max) {
            luaL_error(ctx.lua,
                       "%d is not within the range %d to %d",
                       (int)val,
                       (int)field_info.range.min,
                       (int)field_info.range.max);
        }
    }
    return (Type)val;
}

static DynamicArrayBounded<long long, 4> ListOfInts(LuaState& ctx, usize num_expected, FieldInfo field_info) {
    DynamicArrayBounded<long long, 4> result;
    for (auto const i : ::Range(num_expected)) {
        bool success = false;
        lua_geti(ctx.lua, -1, (int)i + 1);
        if (lua_isinteger(ctx.lua, -1)) {
            dyn::Append(result, lua_tointeger(ctx.lua, -1));
            success = true;
        }
        lua_pop(ctx.lua, 1);
        if (!success)
            luaL_error(ctx.lua,
                       "wrong values for '%s' (expecting an array of %d numbers)",
                       field_info.name.data,
                       (int)num_expected);
    }
    return result;
}

static Span<String> SetArrayOfStrings(LuaState& ctx, FieldInfo field_info, bool case_insentive) {
    DynamicArray<String> list {ctx.result_arena};
    {
        lua_len(ctx.lua, -1);
        list.Reserve(CheckedCast<usize>(lua_tointeger(ctx.lua, -1)));
        lua_pop(ctx.lua, 1);
    }

    IterateTableAtTop(ctx, [&]() {
        if (auto const type = lua_type(ctx.lua, -2); type != LUA_TNUMBER) {
            auto const err_message = fmt::Format(ctx.lua_arena,
                                                 "{}: expecting a list; keys should be numbers, not {}",
                                                 field_info.name,
                                                 LuaValueToString(ctx.lua, -2, ctx.lua_arena));
            lua_pushlstring(ctx.lua, err_message.data, err_message.size);
            lua_error(ctx.lua);
        }

        if (auto const type = lua_type(ctx.lua, -1); type != LUA_TSTRING) {
            auto const err_message = fmt::Format(ctx.lua_arena,
                                                 "{}: expecting a list of strings, not {}"_s,
                                                 field_info.name,
                                                 LuaTypeName(ctx.lua, type));
            lua_pushlstring(ctx.lua, err_message.data, err_message.size);
            lua_error(ctx.lua);
        }

        auto str = StringFromTop(ctx);
        if (case_insentive)
            for (auto& c : str)
                c = ToLowercaseAscii(c);
        dyn::Append(list, str);
    });

    return list.ToOwnedSpan();
}

template <>
struct TableFields<Region::AudioProperties> {
    using Type = Region::AudioProperties;

    enum class Field : u32 {
        GainDb,
        Count,
    };
    static constexpr FieldInfo FieldInfo(Field f) {
        switch (f) {
            case Field::GainDb:
                return {
                    .name = "gain_db",
                    .description_sentence = "Apply a gain to the audio data in decibels.",
                    .example = "-3",
                    .default_value = "0",
                    .lua_type = LUA_TNUMBER,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.gain_db = (f32)luaL_checknumber(ctx.lua, -1); },
                };
            case Field::Count: break;
        }
        return {};
    }
};

template <>
struct TableFields<Region::TimbreLayering> {
    using Type = Region::TimbreLayering;

    enum class Field : u32 {
        LayerRange,
        Count,
    };
    static constexpr FieldInfo FieldInfo(Field f) {
        switch (f) {
            case Field::LayerRange:
                return {
                    .name = "layer_range",
                    .description_sentence =
                        "The start and end point, from 0 to 100, of the Timbre knob on Floe's GUI that this region should be heard. You should overlap this range with other timbre layer ranges. Floe will create an even crossfade of all overlapping sounds. The start number is inclusive, end is exclusive. This region's velocity_range should be 0-100.",
                    .example = "{ 0, 50 }",
                    .default_value = "no timbre layering",
                    .lua_type = LUA_TTABLE,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            auto& region = FIELD_OBJ;
                            region.layer_range = Range {};
                            auto const vals = ListOfInts(ctx, 2, info);
                            if (vals[0] < 0 || vals[1] > 100 || vals[1] < 1 || vals[1] > 101)
                                luaL_error(
                                    ctx.lua,
                                    "'%s' should be in the range [0, 99] the first number and [1, 100] for the second",
                                    info.name.data);
                            region.layer_range->start = (u8)vals[0];
                            region.layer_range->end = (u8)vals[1];
                        },
                };
                break;
            case Field::Count: break;
        }
        return {};
    }
};

template <>
struct TableFields<Region::TriggerCriteria> {
    using Type = Region::TriggerCriteria;

    enum class Field : u32 {
        Event,
        KeyRange,
        VelocityRange,
        RoundRobinIndex,
        FeatherOverlappingVelocityLayers,
        AutoMapKeyRangeGroup,
        Count,
    };

    static constexpr char const* k_trigger_event_names[] = {"note-on", "note-off", nullptr};
    static_assert(ArraySize(k_trigger_event_names) == ToInt(TriggerEvent::Count) + 1);

    static constexpr FieldInfo FieldInfo(Field f) {
        switch (f) {
            case Field::Event:
                return {
                    .name = "trigger_event",
                    .description_sentence = "What event triggers this region.",
                    .example = FromNullTerminated(k_trigger_event_names[0]),
                    .default_value = FromNullTerminated(k_trigger_event_names[0]),
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .enum_options = k_trigger_event_names,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.trigger_event =
                                (TriggerEvent)luaL_checkoption(ctx.lua, -1, nullptr, k_trigger_event_names);
                        },
                };
            case Field::KeyRange:
                return {
                    .name = "key_range",
                    .description_sentence =
                        "The pitch range of the keyboard that this region is mapped to. These should be MIDI note numbers, from 0 to 128. The start number is inclusive, the end is exclusive.",
                    .example = "{ 60, 64 }",
                    .default_value = "{ 60, 64 }",
                    .lua_type = LUA_TTABLE,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            auto const vals = ListOfInts(ctx, 2, info);
                            if (vals[0] < 0 || vals[1] > 128 || vals[1] < 1 || vals[1] > 129)
                                luaL_error(
                                    ctx.lua,
                                    "'%s' should be in the range [0, 127] the first number and [1, 128] for the second",
                                    info.name.data);

                            FIELD_OBJ.key_range = {
                                .start = (u8)vals[0],
                                .end = (u8)vals[1],
                            };
                        },
                };
            case Field::VelocityRange:
                return {
                    .name = "velocity_range",
                    .description_sentence =
                        "The velocity range of the keyboard that this region is mapped to. This should be an array of 2 numbers ranging from 0 to 100. The start number is inclusive, the end is exclusive.",
                    .example = "{ 0, 100 }",
                    .default_value = "{ 0, 100 }",
                    .lua_type = LUA_TTABLE,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            // IMPROVE: support floats
                            auto const vals = ListOfInts(ctx, 2, info);
                            if (vals[0] < 0 || vals[1] > 100 || vals[1] < 1 || vals[1] > 101)
                                luaL_error(
                                    ctx.lua,
                                    "'%s' should be in the range [0, 99] the first number and [1, 100] for the second",
                                    info.name.data);
                            FIELD_OBJ.velocity_range = {
                                .start = (u8)vals[0],
                                .end = (u8)vals[1],
                            };
                        },
                };
            case Field::RoundRobinIndex:
                return {
                    .name = "round_robin_index",
                    .description_sentence =
                        "Trigger this region only on this round-robin index. For example, if this index is 0 and there are 2 other groups with round-robin indices of 1 and 2, then this region will trigger on every third press of a key only.",
                    .example = "0",
                    .default_value = "no round-robin",
                    .lua_type = LUA_TNUMBER,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            auto const val = luaL_checkinteger(ctx.lua, -1);
                            if (val < 0)
                                luaL_error(ctx.lua, "'%s' should be a positive integer", info.name.data);
                            FIELD_OBJ.round_robin_index = (u32)val;
                        },
                };
            case Field::FeatherOverlappingVelocityLayers:
                return {
                    .name = "feather_overlapping_velocity_layers",
                    .description_sentence =
                        "If another region in this instrument is triggered at the same time as this one and is overlapping this, and also has this option enabled, then both regions will play crossfaded in a proportional amount for the overlapping area, creating a smooth transition between velocity layers. Only works if there's exactly 2 overlapping layers.",
                    .example = "false",
                    .default_value = "false",
                    .lua_type = LUA_TBOOLEAN,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.feather_overlapping_velocity_layers = lua_toboolean(ctx.lua, -1);
                        },
                };
            case Field::AutoMapKeyRangeGroup:
                return {
                    .name = "auto_map_key_range_group",
                    .description_sentence =
                        "For every region that has this same string, automatically set the start and end values for each region's key range based on its root key. Only works if all region's velocity range are the same.",
                    .example = "group1",
                    .default_value = "no auto-map",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.auto_map_key_range_group = StringFromTop(ctx); },
                };
            case Field::Count: break;
        }
        return {};
    }
};

template <>
struct TableFields<BuiltinLoop> {
    using Type = BuiltinLoop;

    enum class Field : u32 {
        Start,
        End,
        Crossfade,
        Mode,
        LockLoopPoints,
        LockMode,
        Count,
    };

    static constexpr char const* k_loop_mode_names[] = {"standard", "ping-pong", nullptr};
    static_assert(ArraySize(k_loop_mode_names) == ToInt(LoopMode::Count) + 1);

    static constexpr FieldInfo FieldInfo(Field f) {
        switch (f) {
            case Field::Start:
                return {
                    .name = "start_frame",
                    .description_sentence =
                        "The start of the loop in frames. Inclusive. It can be negative meaning index the file from the end rather than the start. For example, -1 == number_frames_in_file, -2 == (number_frames_in_file - 1), etc.",
                    .example = "24",
                    .lua_type = LUA_TNUMBER,
                    .required = true,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.start_frame = NumberFromTop<u32>(ctx, info); },
                };
            case Field::End:
                return {
                    .name = "end_frame",
                    .description_sentence =
                        "The end of the loop in frames. Exclusive. It can be negative meaning index the file from the end rather than the start. For example, -1 == number_frames_in_file, -2 == (number_frames_in_file - 1), etc.",
                    .example = "6600",
                    .lua_type = LUA_TNUMBER,
                    .required = true,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.end_frame = NumberFromTop<u32>(ctx, info); },
                };
            case Field::Crossfade:
                return {
                    .name = "crossfade",
                    .description_sentence = "The number of frames to crossfade.",
                    .example = "100",
                    .lua_type = LUA_TNUMBER,
                    .required = true,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.crossfade_frames = NumberFromTop<u32>(ctx, info);
                        },
                };
            case Field::Mode:
                return {
                    .name = "mode",
                    .description_sentence = "The mode of the loop.",
                    .example = FromNullTerminated(k_loop_mode_names[ToInt(LoopMode::Standard)]),
                    .default_value = FromNullTerminated(k_loop_mode_names[ToInt(LoopMode::Standard)]),
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .enum_options = k_loop_mode_names,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.mode =
                                (LoopMode)luaL_checkoption(ctx.lua, -1, nullptr, k_loop_mode_names);
                        },
                };
            case Field::LockLoopPoints:
                return {
                    .name = "lock_loop_points",
                    .description_sentence =
                        "If true, the start, end and crossfade values cannot be overriden by a custom loop from Floe's GUI.",
                    .example = "false",
                    .lua_type = LUA_TBOOLEAN,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.lock_loop_points = lua_toboolean(ctx.lua, -1) != 0;
                        },
                };
            case Field::LockMode:
                return {
                    .name = "lock_mode",
                    .description_sentence =
                        "If true, the loop mode value cannot be overriden by a custom mode from Floe's GUI.",
                    .example = "false",
                    .lua_type = LUA_TBOOLEAN,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.lock_mode = lua_toboolean(ctx.lua, -1) != 0; },
                };
            case Field::Count: break;
        }
        return {};
    }
};

template <>
struct TableFields<Region::Loop> {
    using Type = Region::Loop;

    enum class Field : u32 {
        BuiltinLoop,
        NeverLoop,
        AlwaysLoop,
        Count,
    };

    static constexpr FieldInfo FieldInfo(Field f) {
        switch (f) {
            case Field::BuiltinLoop:
                return {
                    .name = "builtin_loop",
                    .description_sentence = "Define a built-in loop.",
                    .default_value = "no built-in loop",
                    .lua_type = LUA_TTABLE,
                    .subtype = InterpretedTypes::BuiltinLoop,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            BuiltinLoop loop;
                            InterpretTable(ctx, -1, loop);
                            FIELD_OBJ.builtin_loop = loop;
                        },
                };
            case Field::NeverLoop:
                return {
                    .name = "never_loop",
                    .description_sentence =
                        "If true, this region will never loop even if there is a user-defined loop. Set all regions of an instrument to this to entirely disable looping for the instrument.",
                    .example = "false",
                    .default_value = "false",
                    .lua_type = LUA_TBOOLEAN,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.never_loop = lua_toboolean(ctx.lua, -1) != 0;
                            // Mutually exclusive with always_loop
                            if (FIELD_OBJ.never_loop && FIELD_OBJ.always_loop)
                                luaL_error(ctx.lua, "never_loop and always_loop are mutually exclusive");
                        },
                };
            case Field::AlwaysLoop:
                return {
                    .name = "always_loop",
                    .description_sentence =
                        "If true, this region will always loop - either using the built in loop, a user defined loop, or a default built-in loop.",
                    .example = "false",
                    .default_value = "false",
                    .lua_type = LUA_TBOOLEAN,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.always_loop = lua_toboolean(ctx.lua, -1) != 0;
                            // Mutually exclusive with never_loop
                            if (FIELD_OBJ.never_loop && FIELD_OBJ.always_loop)
                                luaL_error(ctx.lua, "never_loop and always_loop are mutually exclusive");
                        },
                };
            case Field::Count: break;
        }
        return {};
    }
};

template <>
struct TableFields<Region> {
    using Type = Region;

    static constexpr char const* k_trigger_event_names[] = {"note-on", "note-off", nullptr};
    static_assert(ArraySize(k_trigger_event_names) == ToInt(TriggerEvent::Count) + 1);

    enum class Field : u32 {
        Path,
        RootKey,
        TriggerCriteria,
        Loop,
        TimbreLayering,
        AudioProperties,
        Count,
    };

    static constexpr FieldInfo FieldInfo(Field f) {
        switch (f) {
            case Field::Path:
                return {
                    .name = "path",
                    .description_sentence = "A path to an audio file, relative to this current lua file.",
                    .example = "Samples/One-shots/Resonating String.flac",
                    .lua_type = LUA_TSTRING,
                    .required = true,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.path = PathFromTop(ctx); },
                };
            case Field::RootKey:
                return {
                    .name = "root_key",
                    .description_sentence =
                        "The pitch of the audio file as a number from 0 to 127 (a MIDI note number).",
                    .example = "60",
                    .lua_type = LUA_TNUMBER,
                    .required = true,
                    .range = {0, 127},
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.root_key = NumberFromTop<u8>(ctx, info); },
                };

            case Field::TriggerCriteria:
                return {
                    .name = "trigger_criteria",
                    .description_sentence = "How this region should be triggered.",
                    .default_value = "defaults",
                    .lua_type = LUA_TTABLE,
                    .subtype = InterpretedTypes::TriggerCriteria,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { InterpretTable(ctx, -1, FIELD_OBJ.trigger); },
                };
            case Field::Loop:
                return {
                    .name = "loop",
                    .description_sentence = "Loop configuration.",
                    .default_value = "defaults",
                    .lua_type = LUA_TTABLE,
                    .subtype = InterpretedTypes::RegionLoop,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { InterpretTable(ctx, -1, FIELD_OBJ.loop); },
                };
            case Field::TimbreLayering:
                return {
                    .name = "timbre_layering",
                    .description_sentence = "Timbre layering configuration.",
                    .default_value = "no timbre layering",
                    .lua_type = LUA_TTABLE,
                    .subtype = InterpretedTypes::RegionTimbreLayering,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { InterpretTable(ctx, -1, FIELD_OBJ.timbre_layering); },
                };
            case Field::AudioProperties:
                return {
                    .name = "audio_properties",
                    .description_sentence = "Audio properties.",
                    .default_value = "defaults",
                    .lua_type = LUA_TTABLE,
                    .subtype = InterpretedTypes::RegionAudioProps,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { InterpretTable(ctx, -1, FIELD_OBJ.audio_props); },
                };
            case Field::Count: break;
        }
        return {};
    }
};

template <>
struct TableFields<FileAttribution> {
    using Type = FileAttribution;

    enum class Field : u32 {
        Title,
        LicenseName,
        LicenseUrl,
        AttributionText,
        AttributionUrl,
        Count,
    };

    static constexpr FieldInfo FieldInfo(Field f) {
        switch (f) {
            case Field::Title:
                return {
                    .name = "title",
                    .description_sentence = "The title of the work.",
                    .example = "Bell Strike",
                    .lua_type = LUA_TSTRING,
                    .required = true,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.title = StringFromTop(ctx); },
                };
            case Field::LicenseName:
                return {
                    .name = "license_name",
                    .description_sentence = "Name of the license.",
                    .example = "CC-BY-4.0",
                    .lua_type = LUA_TSTRING,
                    .required = true,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.license_name = StringFromTop(ctx); },
                };
            case Field::LicenseUrl:
                return {
                    .name = "license_url",
                    .description_sentence = "URL to the license.",
                    .example = "https://creativecommons.org/licenses/by/4.0/",
                    .lua_type = LUA_TSTRING,
                    .required = true,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.license_url = StringFromTop(ctx); },
                };
            case Field::AttributionText:
                return {
                    .name = "attributed_to",
                    .description_sentence =
                        "The name/identification of the persons or entities to attribute the work to.",
                    .example = "John Doe",
                    .lua_type = LUA_TSTRING,
                    .required = true,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.attributed_to = StringFromTop(ctx); },
                };
            case Field::AttributionUrl:
                return {
                    .name = "attribution_url",
                    .description_sentence = "URL to the original work if possible.",
                    .example = "https://example.com",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.attribution_url = StringFromTop(ctx); },
                };
            case Field::Count: break;
        }
        return {};
    }
};

template <>
struct TableFields<ImpulseResponse> {
    using Type = ImpulseResponse;

    enum class Field : u32 {
        Name,
        Path,
        Folder,
        Tags,
        Description,
        Count,
    };

    static constexpr FieldInfo FieldInfo(Field f) {
        switch (f) {
            case Field::Name:
                return {
                    .name = "name",
                    .description_sentence = "The name of the IR. Must be unique.",
                    .example = "Cathedral",
                    .lua_type = LUA_TSTRING,
                    .required = true,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.name = StringFromTop(ctx);
                            if (FIELD_OBJ.name.size > k_max_ir_name_size)
                                luaL_error(ctx.lua,
                                           "IR name must be less than %d characters long.",
                                           (int)k_max_ir_name_size);
                        },
                };
            case Field::Path:
                return {
                    .name = "path",
                    .description_sentence =
                        "File path to the impulse response file, relative to this script.",
                    .example = "irs/cathedral.flac",
                    .lua_type = LUA_TSTRING,
                    .required = true,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.path = PathFromTop(ctx); },
                };
            case Field::Folder:
                return {
                    .name = "folder",
                    .description_sentence =
                        "Specify a folder to group IRs under a common heading. It may contain slashes to represent a hierarchy. See https://floe.audio/develop/tags-and-folders.html for more information.",
                    .example = "Cathedrals",
                    .default_value = "no folders",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.folder = StringFromTop(ctx); },
                };
            case Field::Tags:
                return {
                    .name = "tags",
                    .description_sentence =
                        "An array of strings to denote properties of the IR. See https://floe.audio/develop/tags-and-folders.html for more information.",
                    .example = "{ \"acoustic\", \"cathedral\" }",
                    .default_value = "no tags",
                    .lua_type = LUA_TTABLE,
                    .required = false,
                    .is_array = true,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.tags = SetArrayOfStrings(ctx, info, true); },
                };
            case Field::Description:
                return {
                    .name = "description",
                    .description_sentence =
                        "A description of the IR. Start with a capital letter an end with a period.",
                    .example = "Sine sweep in St. Paul's Cathedral.",
                    .default_value = "no description",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.description = StringFromTop(ctx); },
                };
            case Field::Count: break;
        }
        return {};
    }
};

template <>
struct TableFields<Instrument> {
    using Type = Instrument;

    enum class Field : u32 {
        Name,
        Folder,
        Description,
        Tags,
        WaveformFilepath,
        Count,
    };

    static constexpr FieldInfo FieldInfo(Field f) {
        switch (f) {
            case Field::Name:
                return {
                    .name = "name",
                    .description_sentence = "The name of the instrument. Must be unique.",
                    .example = "Metal Fence Strike",
                    .lua_type = LUA_TSTRING,
                    .required = true,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.name = StringFromTop(ctx);
                            if (FIELD_OBJ.name.size > k_max_instrument_name_size)
                                luaL_error(ctx.lua,
                                           "Instrument name must be less than %d characters long.",
                                           (int)k_max_instrument_name_size);
                        },
                };
            case Field::Folder:
                return {
                    .name = "folder",
                    .description_sentence =
                        "Specify a folder to group instruments under a common heading. It may contain slashes to represent a hierarchy. See https://floe.audio/develop/tags-and-folders.html for more information.",
                    .example = "Fences/Steel",
                    .default_value = "no folders",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.folder = StringFromTop(ctx); },
                };
            case Field::Description:
                return {
                    .name = "description",
                    .description_sentence =
                        "A description of the instrument. Start with a capital letter an end with a period.",
                    .example = "Tonal pluck metallic pluck made from striking a steel fence.",
                    .default_value = "no description",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.description = StringFromTop(ctx); },
                };
            case Field::Tags:
                return {
                    .name = "tags",
                    .description_sentence =
                        "An array of strings to denote properties of the instrument. See https://floe.audio/develop/tags-and-folders.html for more information.",
                    .example =
                        "{ \"found sounds\", \"tonal percussion\", \"metal\", \"keys\", \"cold\", \"ambient\", \"IDM\", \"cinematic\" }",
                    .default_value = "no tags",
                    .lua_type = LUA_TTABLE,
                    .required = false,
                    .is_array = true,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.tags = SetArrayOfStrings(ctx, info, true); },
                };
            case Field::WaveformFilepath:
                return {
                    .name = "waveform_audio_path",
                    .description_sentence =
                        "Path to an audio file relative to this script that should be used as the waveform on Floe's GUI.",
                    .example = "Samples/file1.flac",
                    .default_value = "first region path",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.audio_file_path_for_waveform = PathFromTop(ctx);
                        },
                };
            case Field::Count: break;
        }
        return {};
    }
};

template <>
struct TableFields<Library> {
    using Type = Library;

    enum class Field : u32 {
        Name,
        Tagline,
        LibraryUrl,
        Description,
        Author,
        AuthorUrl,
        MinorVersion,
        BackgroundImagePath,
        IconImagePath,
        Count,
    };

    static constexpr FieldInfo FieldInfo(Field f) {
        switch (f) {
            case Field::Name:
                return {
                    .name = "name",
                    .description_sentence =
                        "The name of the library. Keep it short and use tagline for more details.",
                    .example = "Iron Vibrations",
                    .lua_type = LUA_TSTRING,
                    .required = true,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.name = StringFromTop(ctx);
                            if (FIELD_OBJ.name.size > k_max_library_name_size)
                                luaL_error(ctx.lua,
                                           "Library name must be less than %d characters long.",
                                           (int)k_max_library_name_size);
                        },
                };
            case Field::Tagline:
                return {
                    .name = "tagline",
                    .description_sentence = "A few words to describe the library.",
                    .example = "Organic sounds from resonating metal objects",
                    .lua_type = LUA_TSTRING,
                    .required = true,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.tagline = StringFromTop(ctx); },
                };
            case Field::LibraryUrl:
                return {
                    .name = "library_url",
                    .description_sentence = "The URL for this Floe library.",
                    .example = "https://example.com/iron-vibrations",
                    .default_value = "no url",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.library_url = StringFromTop(ctx); },
                };
            case Field::Description:
                return {
                    .name = "description",
                    .description_sentence =
                        "A description of the library. You can be verbose and use newlines (\\n).",
                    .example =
                        "A collection of resonating metal objects sampled using a handheld stereo recorder.",
                    .default_value = "no description",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.description = StringFromTop(ctx); },
                };
            case Field::Author:
                return {
                    .name = "author",
                    .description_sentence =
                        "Who created this library. Keep it short, use the description for more details.",
                    .example = "Found-sound Labs",
                    .lua_type = LUA_TSTRING,
                    .required = true,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) {
                            FIELD_OBJ.author = StringFromTop(ctx);
                            if (FIELD_OBJ.author.size > k_max_library_author_size)
                                luaL_error(ctx.lua,
                                           "Library author must be less than %d characters long.",
                                           (int)k_max_library_author_size);
                        },
                };
            case Field::AuthorUrl:
                return {
                    .name = "author_url",
                    .description_sentence = "URL relating to the author or their work.",
                    .example = "https://example.com",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.author_url = StringFromTop(ctx); },
                };
            case Field::MinorVersion:
                return {
                    .name = "minor_version",
                    .description_sentence =
                        "The minor version of this library - backwards-compatible changes are allowed on a library; this field represents that. Non-backwards-compatibile changes are not allowed: you'd need to create a new library such as: \"Strings 2\".",
                    .example = "1",
                    .default_value = "1",
                    .lua_type = LUA_TNUMBER,
                    .required = false,
                    .set =
                        [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.minor_version = NumberFromTop<u32>(ctx, info); },
                };
            case Field::BackgroundImagePath:
                return {
                    .name = "background_image_path",
                    .description_sentence =
                        "Path relative to this script for the background image. It should be a jpg or png.",
                    .example = "Images/background.jpg",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.background_image_path = PathFromTop(ctx); },
                };
            case Field::IconImagePath:
                return {
                    .name = "icon_image_path",
                    .description_sentence =
                        "Path relative to this script for the icon image. It should be a square jpg or png.",
                    .example = "Images/icon.png",
                    .lua_type = LUA_TSTRING,
                    .required = false,
                    .set = [](SET_FIELD_VALUE_ARGS) { FIELD_OBJ.icon_image_path = PathFromTop(ctx); },
                };
            case Field::Count: break;
        }
        return {};
    }
};

template <typename Type>
static constexpr auto FieldInfos() {
    using Interpreter = TableFields<Type>;
    using Field = TableFields<Type>::Field;
    constexpr auto k_infos = []() {
        Array<FieldInfo, ToInt(Field::Count)> result;
        for (auto const i : ::Range(ToInt(Field::Count)))
            result[i] = Interpreter::FieldInfo((Field)i);
        return result;
    }();
    return k_infos;
}

template <typename Type>
static Span<FieldInfo const> FieldInfosSpan() {
    static constexpr auto k_fields = FieldInfos<Type>();
    return k_fields;
}

template <InterpretableType Type>
void InterpretTable(LuaState& ctx, int stack_index, Type& result) {
    if (stack_index == -1) stack_index = lua_gettop(ctx.lua);
    for (auto [index, f] : Enumerate(FieldInfos<Type>())) {
        auto const type = lua_getfield(ctx.lua, stack_index, f.name.data);
        if (!f.required && type == LUA_TNIL) {
            lua_pop(ctx.lua, 1);
            continue;
        }

        if (type != f.lua_type)
            luaL_error(ctx.lua,
                       "bad argument '%s' (%s expected, got %s)",
                       f.name.data,
                       lua_typename(ctx.lua, f.lua_type),
                       lua_typename(ctx.lua, type));

        f.set(ctx, &result, f);

        lua_pop(ctx.lua, 1);
    }
}

// We only add a few standard libraries at the moment because some libraries aren't useful for creating sample
// library configurations and give too much power to the lua (os.execute, etc.).
static constexpr luaL_Reg k_lua_standard_libs[] = {
    {LUA_GNAME, luaopen_base},
    {LUA_TABLIBNAME, luaopen_table},
    {LUA_STRLIBNAME, luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_UTF8LIBNAME, luaopen_utf8},
};

static ErrorCodeOr<Reader> CreateLuaFileReader(Library const& library, LibraryPath path) {
    PathArena arena {Malloc::Instance()};
    auto const dir = ({
        auto d = path::Directory(library.path);
        if (!d) return ErrorCode {FilesystemError::PathDoesNotExist};
        ASSERT(path::IsAbsolute(*d));
        *d;
    });
    return Reader::FromFile(path::Join(arena, Array {dir, path.str}));
}

static int NewLibrary(lua_State* lua) {
    auto& ctx = **(LuaState**)lua_getextraspace(lua);

    lua_settop(lua, 1);
    luaL_checktype(lua, 1, LUA_TTABLE);

    auto ptr = ctx.result_arena.NewUninitialised<LightUserDataWrapper<Library>>();
    ptr->type = UserdataTypes::Library;
    PLACEMENT_NEW(&ptr->obj)
    Library {
        .path = ctx.result_arena.Clone(ctx.filepath),
        .create_file_reader = CreateLuaFileReader,
        .file_format_specifics = LuaSpecifics {},
    };
    lua_pushlightuserdata(ctx.lua, ptr);
    InterpretTable<Library>(ctx, 1, ptr->obj);

    return 1;
}

static int NewInstrument(lua_State* lua) {
    auto& ctx = **(LuaState**)lua_getextraspace(lua);

    lua_settop(lua, 2);
    auto library = LuaCheckUserdata<Library>(lua, 1, UserdataTypes::Library);
    luaL_checktype(lua, 2, LUA_TTABLE);

    auto ptr = ctx.result_arena.NewUninitialised<LightUserDataWrapper<Instrument>>();
    ptr->type = UserdataTypes::Instrument;
    PLACEMENT_NEW(&ptr->obj)
    Instrument {
        .library = *library,
    };
    lua_pushlightuserdata(ctx.lua, ptr);
    auto& inst = ptr->obj;
    InterpretTable<Instrument>(ctx, 2, inst);

    if (!library->insts_by_name.InsertGrowIfNeeded(ctx.result_arena, inst.name, &inst)) {
        DynamicArrayBounded<char, k_max_instrument_name_size + 1> name {inst.name};
        luaL_error(lua, "Instrument names must be unique: %s is found twice", dyn::NullTerminated(name));
    }

    return 1;
}

static int SetAttributionRequirement(lua_State* lua) {
    auto& ctx = **(LuaState**)lua_getextraspace(lua);

    // function takes 2 args, first is the path to a file, second is a table of config
    luaL_checktype(lua, 1, LUA_TSTRING);
    luaL_checktype(lua, 2, LUA_TTABLE);

    LibraryPath path = {ctx.result_arena.Clone(LuaString(lua, 1))};
    FileAttribution info {};
    InterpretTable<FileAttribution>(ctx, 2, info);
    ctx.files_requiring_attribution.Insert(path, info);

    return 0;
}

static int AddIr(lua_State* lua) {
    auto& ctx = **(LuaState**)lua_getextraspace(lua);

    lua_settop(lua, 2);
    auto library = LuaCheckUserdata<Library>(lua, 1, UserdataTypes::Library);
    luaL_checktype(lua, 2, LUA_TTABLE);

    auto ptr = ctx.result_arena.NewUninitialised<LightUserDataWrapper<ImpulseResponse>>();
    ptr->type = UserdataTypes::Ir;
    PLACEMENT_NEW(&ptr->obj)
    ImpulseResponse {
        .library = *library,
    };
    lua_pushlightuserdata(ctx.lua, ptr);
    auto& ir = ptr->obj;
    InterpretTable<ImpulseResponse>(ctx, 2, ir);

    if (!library->irs_by_name.InsertGrowIfNeeded(ctx.result_arena, ir.name, &ir)) {
        DynamicArrayBounded<char, k_max_ir_name_size + 1> name {ir.name};
        luaL_error(lua, "IR names must be unique: %s is found twice", dyn::NullTerminated(name));
    }

    return 0;
}

static int AddRegion(lua_State* lua) {
    auto& ctx = **(LuaState**)lua_getextraspace(lua);

    lua_settop(lua, 2);
    auto instrument = LuaCheckUserdata<Instrument>(lua, 1, UserdataTypes::Instrument);
    luaL_checktype(lua, 2, LUA_TTABLE);

    auto dyn_array = DynamicArray<Region>::FromOwnedSpan(instrument->regions,
                                                         instrument->regions_allocated_capacity,
                                                         ctx.result_arena);
    dyn::Resize(dyn_array, dyn_array.size + 1);
    instrument->regions_allocated_capacity = dyn_array.Capacity();
    auto [span, cap] = dyn_array.ToOwnedSpanUnchangedCapacity();
    instrument->regions = span;
    instrument->regions_allocated_capacity = cap;
    auto& region = Last(instrument->regions);

    InterpretTable(ctx, 2, region);

    if (instrument->audio_file_path_for_waveform.str.size == 0)
        instrument->audio_file_path_for_waveform = region.path;

    if (region.trigger.round_robin_index)
        instrument->max_rr_pos = Max(instrument->max_rr_pos, *region.trigger.round_robin_index);

    if (region.trigger.feather_overlapping_velocity_layers) {
        usize num_overlaps = 0;
        for (auto const& r : instrument->regions) {
            if (&r == &region) continue;
            if (r.trigger.feather_overlapping_velocity_layers &&
                region.trigger.trigger_event == r.trigger.trigger_event &&
                region.trigger.round_robin_index == r.trigger.round_robin_index &&
                region.trigger.key_range.Overlaps(r.trigger.key_range) &&
                region.trigger.velocity_range.Overlaps(r.trigger.velocity_range)) {
                num_overlaps++;
            }
        }

        // IMPROVE: we could possibly support more than 1 but we'd need to implement a different kind of
        // feathering algorithm.
        if (num_overlaps > 1) luaL_error(ctx.lua, "Only 2 feathered velocity regions can overlap.");
    }

    if (region.timbre_layering.layer_range) {
        usize num_overlaps = 0;
        for (auto const& r : instrument->regions) {
            if (&r == &region) continue;
            if (r.timbre_layering.layer_range && region.trigger.trigger_event == r.trigger.trigger_event &&
                region.trigger.round_robin_index == r.trigger.round_robin_index &&
                region.trigger.key_range.Overlaps(r.trigger.key_range) &&
                region.trigger.velocity_range.Overlaps(r.trigger.velocity_range) &&
                region.timbre_layering.layer_range->Overlaps(*r.timbre_layering.layer_range)) {
                num_overlaps++;
            }
        }

        // IMPROVE: we could possibly support more than 1 but we'd need to implement a different kind of
        // algorithm.
        if (num_overlaps > 1) luaL_error(ctx.lua, "Only 2 timbre layer regions can overlap.");
    }

    return 0;
}

static VoidOrError<Error> TryRunLuaCode(LuaState& ctx, int r) {
    if (r == LUA_OK) return k_success;
    switch (r) {
        case LUA_ERRRUN: {
            if (ctx.start_time.SecondsFromNow() > ctx.options.max_seconds_allowed) {
                return ErrorAndNotify(ctx, LuaErrorCode::Timeout, [&](DynamicArray<char>& message) {
                    fmt::Append(message,
                                "the lua script must complete within {} seconds",
                                ctx.options.max_seconds_allowed);
                });
            }

            return ErrorAndNotify(ctx, LuaErrorCode::Runtime, [&](DynamicArray<char>& message) {
                if (lua_isstring(ctx.lua, -1)) {
                    DynamicArray<char> lua_error {LuaString(ctx.lua, -1), ctx.lua_arena};
                    // Because we are running from a string rather than file (we read the file into memory),
                    // the chunkname is in a confusing format. We replace it will the actual filename.
                    auto const filename = path::Filename(ctx.filepath);
                    auto const chunk_msg = fmt::Format(ctx.lua_arena, "[string \"{}\"]", filename);
                    dyn::Replace(lua_error, chunk_msg, filename);

                    fmt::Append(message, "\n{}", lua_error);
                } else
                    dyn::AppendSpan(message, "\nUnknown error");
            });
        }
        case LUA_ERRMEM: return Error {LuaErrorCode::Memory, {}};
        case LUA_ERRERR:
            LogError(ModuleName::SampleLibrary, "error while running the Lua error handler function");
            return Error {LuaErrorCode::Unexpected, {}};
    }
    return Error {LuaErrorCode::Unexpected, {}};
}

static const struct luaL_Reg k_floe_lib[] = {
    {"new_library", NewLibrary},
    {"new_instrument", NewInstrument},
    {"add_region", AddRegion},
    {"add_ir", AddIr},
    {"set_attribution_requirement", SetAttributionRequirement},
    {nullptr, nullptr},
};

constexpr char const* k_floe_lua_helpers = R"aaa(
floe.extend_table = function(base_table, t)
    if not t then
        t = {}
    end

    for key, value in pairs(base_table) do
        if type(value) == "table" then
            -- Recursively handle sub-tables
            t[key] = floe.extend_table(value, t[key])
        else
            -- If key doesn't exist in t, copy from base_table
            if t[key] == nil then
                t[key] = value
            end
        end
    end

    return t
end
)aaa";

constexpr String k_example_extend_table_usage = R"aaa(
local group1 = {
    trigger_criteria = {
        trigger_event = "note-on",
        velocity_range = { 0, 100 },
        auto_map_key_range_group = "group1",
        feather_overlapping_velocity_regions = false,
    },
}

floe.add_region(instrument, floe.extend_table(group1, {
    path = "One-shots/Resonating String 2.flac",
    root_key = 65,
}))

floe.add_region(instrument, floe.extend_table(group1, {
    path = "One-shots/Resonating String 3.flac",
    root_key = 68,
}))
)aaa";

static VoidOrError<Error> OpenFloeLuaLibrary(LuaState& ctx) {
    luaL_newlib(ctx.lua, k_floe_lib); // puts functions into an table on the top of the stack
    lua_setglobal(ctx.lua, "floe"); // pops top stack value and assigns it to global name

    TRY(TryRunLuaCode(ctx, luaL_dostring(ctx.lua, k_floe_lua_helpers)));

    return k_success;
}

static int ErrorHandler(lua_State* lua) {
    char const* message = nullptr;
    if (lua_isstring(lua, -1)) message = lua_tostring(lua, -1);
    luaL_traceback(lua, lua, message, 1);
    return 1;
}

ErrorCodeOr<u64> LuaHash(Reader& reader) {
    reader.pos = 0;
    ArenaAllocator scratch_arena {PageAllocator::Instance()};
    auto data = TRY(reader.ReadOrFetchAll(scratch_arena));
    return XXH3_64bits(data.data, data.size);
}

LibraryPtrOrError ReadLua(Reader& reader,
                          String lua_filepath,
                          ArenaAllocator& result_arena,
                          ArenaAllocator& scratch_arena,
                          Options options) {
    ASSERT(path::IsAbsolute(lua_filepath));
    LuaState ctx {
        .result_arena = result_arena,
        .lua_arena = scratch_arena,
        .initial_lua_arena_size = scratch_arena.TotalUsed(),
        .options = options,
        .start_time = TimePoint::Now(),
        .filepath = lua_filepath,
    };

    static constexpr void* (*k_arena_alloc_fuction)(void*, void*, size_t, size_t) =
        [](void* user_data, void* ptr, size_t original_size, size_t new_size) -> void* {
        auto& ctx = *(LuaState*)user_data;
        if (new_size == 0) {
            if (ptr) {
                ASSERT(original_size != 0);
                ctx.lua_arena.Free({(u8*)ptr, original_size});
            }
            return nullptr;
        }

        if ((ctx.lua_arena.TotalUsed() - ctx.initial_lua_arena_size) > ctx.options.max_memory_allowed)
            return nullptr;

        if (ptr == nullptr) {
            // NOTE: When ptr is NULL, original_size encodes the kind of object that Lua is allocating.
            // original_size is any of LUA_TSTRING, LUA_TTABLE, LUA_TFUNCTION, LUA_TUSERDATA, or LUA_TTHREAD
            // when (and only when) Lua is creating a new object of that type. When original_size is some
            // other value, Lua is allocating memory for something else.
            auto result = ctx.lua_arena.Allocate({
                .size = new_size,
                .alignment = k_max_alignment,
                .allow_oversized_result = false,
            });
            return result.data;
        }

        return ctx.lua_arena.Reallocate<u8>(new_size, {(u8*)ptr, original_size}, original_size, false).data;
    };

    // We don't need a lua_close() because we use lua in a very short-lived environment and we do our memory
    // allocation into an arena. The docs say that lua_close will: "close all active to-be-closed variables in
    // the main thread, release all objects in the given Lua state (calling the corresponding
    // garbage-collection metamethods, if any), and frees all dynamic memory used by this state."
    ctx.lua = lua_newstate(k_arena_alloc_fuction, &ctx);
    if (!ctx.lua) {
        return ErrorAndNotify(ctx, LuaErrorCode::Memory, [](DynamicArray<char>& message) {
            dyn::AppendSpan(message, "Sorry, there's a bug. Please report this.");
        });
    }

    struct OutOfMemory {};
    lua_atpanic(ctx.lua, [](lua_State*) {
        throw OutOfMemory(); // IMPORTANT: the lua library must be compiled in C++ mode
        return 0;
    });

    try {
        static_assert(LUA_EXTRASPACE >= sizeof(void*));
        *(LuaState**)lua_getextraspace(ctx.lua) = &ctx;

        lua_sethook(
            ctx.lua,
            [](lua_State* lua, lua_Debug*) {
                auto const& ctx = **(LuaState**)lua_getextraspace(lua);
                ASSERT(ctx.start_time < TimePoint::Now());
                if (ctx.start_time.SecondsFromNow() > ctx.options.max_seconds_allowed)
                    luaL_error(lua, "timeout");
            },
            LUA_MASKCOUNT,
            50);

        for (auto& lib : k_lua_standard_libs) {
            luaL_requiref(ctx.lua, lib.name, lib.func, 1);
            lua_pop(ctx.lua, 1);
        }
        TRY(OpenFloeLuaLibrary(ctx));

        // Set up the traceback function as the error handler
        lua_pushcfunction(ctx.lua, ErrorHandler);
        int const traceback_index = lua_gettop(ctx.lua);

        DynamicArray<char> chunkname {path::Filename(lua_filepath), ctx.lua_arena};
        auto const lua_source_code = ({
            auto o = reader.ReadOrFetchAll(ctx.lua_arena);
            if (o.HasError()) return Error {o.Error(), {}};
            o.Value();
        });
        if (auto const r = luaL_loadbuffer(ctx.lua,
                                           (char const*)lua_source_code.data,
                                           lua_source_code.size,
                                           dyn::NullTerminated(chunkname));
            r != LUA_OK) {
            switch (r) {
                case LUA_ERRSYNTAX: {
                    return ErrorAndNotify(ctx, LuaErrorCode::Syntax, [&](DynamicArray<char>& message) {
                        // The top of the stack will contain the error message
                        if (lua_isstring(ctx.lua, -1))
                            fmt::Append(message, "{}", LuaString(ctx.lua, -1));
                        else
                            dyn::AppendSpan(message, "unknown error");
                    });
                }
                case LUA_ERRMEM: return Error {LuaErrorCode::Memory, {}};
            }

            LogError(ModuleName::SampleLibrary, "unknown error from lua_load: {}", r);
            return Error {LuaErrorCode::Unexpected, {}};
        }

        if (!lua_isfunction(ctx.lua, -1)) {
            LogError(ModuleName::SampleLibrary, "we're expecting the Lua file to be a function");
            return Error {LuaErrorCode::Unexpected, {}};
        }

        TRY(TryRunLuaCode(ctx, lua_pcall(ctx.lua, 0, LUA_MULTRET, traceback_index)));

        Library* library {};
        if (lua_gettop(ctx.lua)) library = LuaUserdataOrNull<Library>(ctx.lua, -1, UserdataTypes::Library);
        if (!library) {
            return ErrorAndNotify(ctx, LuaErrorCode::Runtime, [&](DynamicArray<char>& message) {
                dyn::AppendSpan(message, "lua script didn't return a library");
            });
        }

        for (auto [key, inst_ptr] : library->insts_by_name) {
            auto& inst = *inst_ptr;
            struct RegionRef {
                Region* data;
                RegionRef* next;
            };
            DynamicHashTable<String, RegionRef*> auto_map_groups {ctx.lua_arena};

            for (auto& region : inst->regions) {
                if (!region.trigger.auto_map_key_range_group) continue;

                auto new_ref = ctx.lua_arena.New<RegionRef>();
                new_ref->data = &region;
                if (auto e = auto_map_groups.Find(*region.trigger.auto_map_key_range_group)) {
                    auto& ref = *e;
                    new_ref->next = ref;
                    ref = new_ref;
                } else {
                    new_ref->next = nullptr;
                    auto_map_groups.Insert(*region.trigger.auto_map_key_range_group, new_ref);
                }
            }

            for (auto item : auto_map_groups) {
                SinglyLinkedListSort(
                    *item.value_ptr,
                    SinglyLinkedListLast(*item.value_ptr),
                    [](Region const* a, Region const* b) { return a->root_key < b->root_key; });

                auto const map_sample = [](Region& region, u8 prev_end_before, u8 next_root) {
                    region.trigger.key_range.start = prev_end_before;
                    auto const this_root = region.root_key;
                    region.trigger.key_range.end = this_root + (next_root - this_root) / 2 + 1;
                    if (next_root == 128) region.trigger.key_range.end = 128;
                };

                RegionRef* previous {};
                for (auto ref = *item.value_ptr; ref != nullptr; ref = ref->next) {
                    map_sample(*ref->data,
                               previous ? previous->data->trigger.key_range.end : 0,
                               ref->next ? ref->next->data->root_key : 128);
                    previous = ref;
                }
            };
        }

        for (auto [key, inst_ptr] : library->insts_by_name) {
            auto const& inst = *inst_ptr;
            if (inst->regions.size == 0) {
                return ErrorAndNotify(ctx, LuaErrorCode::Runtime, [&](DynamicArray<char>& message) {
                    fmt::Append(message, "Instrument {} has no regions", inst->name);
                });
            }
        }

        library->num_regions = 0;
        for (auto [key, inst_ptr] : library->insts_by_name) {
            auto const& inst = *inst_ptr;
            library->num_regions += inst->regions.size;
        }

        {
            auto audio_paths = Set<String>::Create(scratch_arena, library->num_regions);
            for (auto [key, inst_ptr] : library->insts_by_name) {
                auto const& inst = *inst_ptr;
                for (auto& region : inst->regions)
                    audio_paths.InsertWithoutGrowing(region.path.str);
            }
            library->num_instrument_samples = (u32)audio_paths.size;
        }

        library->files_requiring_attribution = ctx.files_requiring_attribution.ToOwnedTable();

        detail::PostReadBookkeeping(*library, ctx.result_arena);

        return library;
    } catch (OutOfMemory const& e) {
        return Error {LuaErrorCode::Memory, {}};
    }
}

static LibraryPtrOrError ReadLua(String lua_code,
                                 String lua_filepath,
                                 ArenaAllocator& result_arena,
                                 ArenaAllocator& scratch_arena,
                                 Options options = {}) {
    auto reader = Reader::FromMemory(lua_code);
    return ReadLua(reader, lua_filepath, result_arena, scratch_arena, options);
}

static ErrorCodeOr<void>
WordWrap(String string, Writer writer, u32 width, Optional<String> line_prefix = {}) {
    if (!width) return k_success;

    usize col = 0;
    if (line_prefix) {
        col = line_prefix->size;
        TRY(writer.WriteChars(*line_prefix));
    }

    for (usize i = 0; i < string.size;) {
        auto next_white_space =
            FindIf(string, [](char c) { return IsWhitespace(c); }, i).ValueOr(string.size);

        auto const word = string.SubSpan(i, next_white_space - i);
        if ((col + word.size) > width) {
            if (col != 0) {
                TRY(writer.WriteChar('\n'));
                if (line_prefix) TRY(writer.WriteChars(*line_prefix));
            }
            col = line_prefix.ValueOr({}).size;
        }
        TRY(writer.WriteChars(word));
        i += word.size;
        col += word.size;
        while (i < string.size && IsWhitespace(string[i])) {
            if (string[i] == '\n') {
                if (col != 0) {
                    TRY(writer.WriteChar('\n'));
                    if (line_prefix) TRY(writer.WriteChars(*line_prefix));
                }
                col = line_prefix.ValueOr({}).size;
            } else {
                TRY(writer.WriteChar(string[i]));
                ++col;
            }
            ++i;
        }
    }
    TRY(writer.WriteChar('\n'));
    return k_success;
}

struct LuaCodePrinter {
    enum PrintModeFlags : u32 {
        PrintModeFlagsDocumentedExample = 1,
        PrintModeFlagsPlaceholderFieldValue = 2,
        PrintModeFlagsPlaceholderFieldKey = 4,
    };

    struct FieldIndex {
        InterpretedTypes type;
        usize index;
    };

    struct PrintMode {
        u32 mode_flags;
        FieldIndex placeholder_field_index;
    };

    LuaCodePrinter() {
        for (auto const i : ::Range(ToInt(InterpretedTypes::Count))) {
            switch ((InterpretedTypes)i) {
                case InterpretedTypes::Library: struct_fields[i] = FieldInfosSpan<Library>(); break;
                case InterpretedTypes::Instrument: struct_fields[i] = FieldInfosSpan<Instrument>(); break;
                case InterpretedTypes::ImpulseResponse:
                    struct_fields[i] = FieldInfosSpan<ImpulseResponse>();
                    break;
                case InterpretedTypes::BuiltinLoop: struct_fields[i] = FieldInfosSpan<BuiltinLoop>(); break;
                case InterpretedTypes::RegionLoop: struct_fields[i] = FieldInfosSpan<Region::Loop>(); break;
                case InterpretedTypes::RegionAudioProps:
                    struct_fields[i] = FieldInfosSpan<Region::AudioProperties>();
                    break;
                case InterpretedTypes::RegionTimbreLayering:
                    struct_fields[i] = FieldInfosSpan<Region::TimbreLayering>();
                    break;
                case InterpretedTypes::TriggerCriteria:
                    struct_fields[i] = FieldInfosSpan<Region::TriggerCriteria>();
                    break;
                case InterpretedTypes::Region: struct_fields[i] = FieldInfosSpan<Region>(); break;
                case InterpretedTypes::FileAttribution:
                    struct_fields[i] = FieldInfosSpan<FileAttribution>();
                    break;
                case InterpretedTypes::Count: break;
            }
        }
    }

    static ErrorCodeOr<void> PrintIndent(Writer writer, u32 indent) {
        String const spaces = "                                                    ";
        TRY(writer.WriteChars(spaces.SubSpan(0, indent * k_indent_spaces)));
        return k_success;
    }

    static ErrorCodeOr<void> PrintWordwrappedComment(Writer writer, String str, u32 indent) {
        DynamicArrayBounded<char, 100> line_prefix {"-- "};
        dyn::InsertRepeated(line_prefix, 0, indent * k_indent_spaces, ' ');
        TRY(WordWrap(str, writer, k_word_wrap_width, line_prefix));
        return k_success;
    }

    ErrorCodeOr<void> PrintField(Writer writer, FieldIndex field, String prefix, PrintMode mode, u32 indent) {
        auto const& f = struct_fields[ToInt(field.type)][field.index];

        u32 const mode_flags = ({
            u32 flags = mode.mode_flags;
            if (!(mode.placeholder_field_index.type == field.type &&
                  mode.placeholder_field_index.index == field.index)) {
                // if the given field doesn't match the placeholder then unset the placeholder bits
                flags &= ~PrintModeFlagsPlaceholderFieldKey;
                flags &= ~PrintModeFlagsPlaceholderFieldValue;
            }
            flags;
        });

        if (mode_flags & PrintModeFlagsDocumentedExample) {
            DynamicArrayBounded<char, 4000> comment_buffer;
            auto comment_writer = dyn::WriterFor(comment_buffer);
            TRY(f.AppendDescription(comment_writer, true));
            TRY(PrintWordwrappedComment(writer, comment_buffer, indent));
        }

        TRY(PrintIndent(writer, indent));

        if (!(mode_flags & PrintModeFlagsPlaceholderFieldKey &&
              mode_flags & PrintModeFlagsPlaceholderFieldValue)) {
            if (!(mode_flags & PrintModeFlagsPlaceholderFieldKey)) {
                TRY(writer.WriteChars(prefix));
                TRY(writer.WriteChars(f.name));
            } else {
                TRY(writer.WriteChars(k_placeholder));
            }

            TRY(writer.WriteChars(" = "));

            if (!(mode_flags & PrintModeFlagsPlaceholderFieldValue))
                if (f.lua_type == LUA_TSTRING)
                    TRY(fmt::FormatToWriter(writer, "\"{}\"", f.example));
                else
                    TRY(writer.WriteChars(f.example));
            else
                TRY(writer.WriteChars(k_placeholder));
        } else {
            TRY(writer.WriteChars(k_placeholder));
        }

        bool const ends_with_placeholder = mode_flags & PrintModeFlagsPlaceholderFieldValue;
        if (ends_with_placeholder || f.lua_type != LUA_TTABLE || f.example.size) {
            if (indent != 0) TRY(writer.WriteChar(','));
            TRY(writer.WriteChars("\n"));
        }
        return k_success;
    }

    ErrorCodeOr<void> PrintStruct(Writer writer, InterpretedTypes type, PrintMode mode, u32 indent) {
        auto const fields = struct_fields[ToInt(type)];
        for (auto [index, f] : Enumerate(fields)) {
            TRY(PrintField(writer, {type, index}, "", mode, indent));

            if (f.subtype.HasValue()) {
                TRY(writer.WriteChars("{\n"));

                if (f.is_array) {
                    ++indent;
                    TRY(PrintIndent(writer, indent));
                    TRY(writer.WriteChars("{\n"));
                }

                ++indent;
                TRY(PrintStruct(writer, f.subtype.Value(), mode, indent));
                --indent;

                if (f.is_array) {
                    TRY(PrintIndent(writer, indent));
                    TRY(writer.WriteChars("},\n"));
                    --indent;
                }

                TRY(PrintIndent(writer, indent));
                if (type == InterpretedTypes::Library)
                    TRY(writer.WriteChars("}\n"));
                else
                    TRY(writer.WriteChars("},\n"));
            }

            if (index != fields.size - 1 && (mode.mode_flags & PrintModeFlagsDocumentedExample))
                TRY(writer.WriteChar('\n'));
        }
        return k_success;
    }

    ErrorCodeOr<void> PrintWholeLua(Writer writer, PrintMode mode) {
        auto begin_function = [&](String name) -> ErrorCodeOr<void> {
            if (mode.mode_flags & PrintModeFlagsDocumentedExample)
                TRY(fmt::FormatToWriter(writer, "-- SECTION: {}\n", name));
            return k_success;
        };
        auto end_function = [&](String name) -> ErrorCodeOr<void> {
            if (mode.mode_flags & PrintModeFlagsDocumentedExample)
                TRY(fmt::FormatToWriter(writer, "-- SECTION_END: {}\n", name));
            TRY(writer.WriteChars("\n"));
            return k_success;
        };

        TRY(begin_function("new_library"));
        TRY(writer.WriteChars("local library = floe.new_library({\n"));
        TRY(PrintStruct(writer, InterpretedTypes::Library, mode, 1));
        TRY(writer.WriteChars("})\n"));
        TRY(end_function("new_library"));

        TRY(begin_function("new_instrument"));
        TRY(writer.WriteChars("local instrument = floe.new_instrument(library, {\n"));
        TRY(PrintStruct(writer, InterpretedTypes::Instrument, mode, 1));
        TRY(writer.WriteChars("})\n"));
        TRY(end_function("new_instrument"));

        TRY(begin_function("add_region"));
        TRY(writer.WriteChars("floe.add_region(instrument, {\n"));
        TRY(PrintStruct(writer, InterpretedTypes::Region, mode, 1));
        TRY(writer.WriteChars("})\n"));
        TRY(end_function("add_region"));

        TRY(begin_function("set_attribution_requirement"));
        TRY(writer.WriteChars("floe.set_attribution_requirement(\"Samples/bell.flac\", {\n"));
        TRY(PrintStruct(writer, InterpretedTypes::FileAttribution, mode, 1));
        TRY(writer.WriteChars("})\n"));
        TRY(end_function("set_attribution_requirement"));

        if (mode.mode_flags & LuaCodePrinter::PrintModeFlagsDocumentedExample) {
            TRY(begin_function("extend_table"));
            TRY(writer.WriteChars(k_example_extend_table_usage));
            TRY(end_function("extend_table"));
        }

        TRY(begin_function("add_ir"));
        TRY(writer.WriteChars("floe.add_ir(library, {\n"));
        TRY(PrintStruct(writer, InterpretedTypes::ImpulseResponse, mode, 1));
        TRY(writer.WriteChars("})\n"));
        TRY(end_function("add_ir"));

        TRY(writer.WriteChars("return library\n"));

        return k_success;
    }

    static constexpr String k_placeholder = "<PLACEHOLDER>";
    static constexpr u32 k_indent_spaces = 4;
    static constexpr u32 k_word_wrap_width = 82;
    Array<Span<FieldInfo const>, ToInt(InterpretedTypes::Count)> struct_fields;
};

ErrorCodeOr<void> WriteDocumentedLuaExample(Writer writer, bool include_comments) {
    LuaCodePrinter printer;
    TRY(printer.PrintWholeLua(
        writer,
        {
            .mode_flags = include_comments ? LuaCodePrinter::PrintModeFlagsDocumentedExample : 0,
        }));
    return k_success;
}

bool CheckAllReferencedFilesExist(Library const& lib, Writer error_writer) {
    bool success = true;
    auto check_file = [&](LibraryPath path) {
        auto outcome = lib.create_file_reader(lib, path);
        if (outcome.HasError()) {
            auto _ =
                fmt::FormatToWriter(error_writer, "Error: file in Lua \"{}\": {}.\n", path, outcome.Error());
            success = false;
        }
    };

    if (lib.background_image_path) check_file(*lib.background_image_path);
    if (lib.icon_image_path) check_file(*lib.icon_image_path);

    for (auto [key, inst_ptr] : lib.insts_by_name) {
        auto inst = *inst_ptr;
        for (auto& region : inst->regions)
            check_file(region.path);
    }

    for (auto [key, ir_ptr] : lib.irs_by_name) {
        auto ir = *ir_ptr;
        check_file(ir->path);
    }

    return success;
}

TEST_CASE(TestWordWrap) {
    DynamicArray<char> buffer {tester.scratch_arena};
    TRY(WordWrap(
        "This is a very long sentence that will be split into multiple lines, with any luck at least.",
        dyn::WriterFor(buffer),
        30));
    tester.log.Debug("{}", buffer);
    return k_success;
}

TEST_CASE(TestDocumentedExampleIsValid) {
    auto& scratch_arena = tester.scratch_arena;
    ArenaAllocator result_arena {PageAllocator::Instance()};
    DynamicArray<char> buf {scratch_arena};

    LuaCodePrinter printer;
    TRY(printer.PrintWholeLua(dyn::WriterFor(buf),
                              {.mode_flags = LuaCodePrinter::PrintModeFlagsDocumentedExample}));
    tester.log.Debug("{}", buf);
    auto o = ReadLua(buf, FAKE_ABSOLUTE_PATH_PREFIX "doc.lua", result_arena, scratch_arena);
    if (auto err = o.TryGet<Error>()) tester.log.Error("Error: {}, {}", err->code, err->message);
    CHECK(o.Is<Library*>());

    return k_success;
}

TEST_CASE(TestIncorrectParameters) {
    auto& arena = tester.scratch_arena;
    LuaCodePrinter printer;

    auto check_error = [&](String lua) {
        ArenaAllocator result_arena {PageAllocator::Instance()};
        auto o = ReadLua(lua, FAKE_ABSOLUTE_PATH_PREFIX "test.lua", result_arena, arena);
        CHECK(o.Is<Error>());
        if (o.Is<Error>()) {
            auto const err = o.Get<Error>();
            tester.log.Debug("Success: this error was expected: {}, {}", err.code, err.message);
            CHECK(o.Get<Error>().code == LuaErrorCode::Runtime);
        } else
            tester.log.Error("Error: not expecting this code to succeed: {}", lua);
    };

    SUBCASE("all arguments are functions") {
        for (auto const type : ::Range(ToInt(InterpretedTypes::Count))) {
            for (auto const field_index : ::Range(printer.struct_fields[type].size)) {
                auto arena_pos = arena.TotalUsed();
                DEFER { arena.TryShrinkTotalUsed(arena_pos); };

                DynamicArray<char> buf {arena};
                TRY(printer.PrintWholeLua(
                    dyn::WriterFor(buf),
                    {
                        .mode_flags = LuaCodePrinter::PrintModeFlagsPlaceholderFieldValue,
                        .placeholder_field_index = {(InterpretedTypes)type, field_index},
                    }));
                auto const lua = fmt::FormatStringReplace(arena,
                                                          buf,
                                                          ArrayT<fmt::StringReplacement>({
                                                              {"<PLACEHOLDER>", "function() end"},
                                                          }));
                check_error(lua);
            }
        }
    }

    SUBCASE("out of range") {
        for (auto field : ArrayT<LuaCodePrinter::FieldIndex>({
                 {InterpretedTypes::TriggerCriteria,
                  ToInt(TableFields<Region::TriggerCriteria>::Field::KeyRange)},
                 {InterpretedTypes::TriggerCriteria,
                  ToInt(TableFields<Region::TriggerCriteria>::Field::VelocityRange)},
                 {InterpretedTypes::RegionTimbreLayering,
                  ToInt(TableFields<Region::TimbreLayering>::Field::LayerRange)},
             })) {
            DynamicArray<char> buf {arena};
            TRY(printer.PrintWholeLua(dyn::WriterFor(buf),
                                      {
                                          .mode_flags = LuaCodePrinter::PrintModeFlagsPlaceholderFieldValue,
                                          .placeholder_field_index = field,
                                      }));
            auto const lua = fmt::FormatStringReplace(arena,
                                                      buf,
                                                      ArrayT<fmt::StringReplacement>({
                                                          {"<PLACEHOLDER>", "{9000, -1000}"},
                                                      }));
            check_error(lua);
        }
    }

    return k_success;
}

TEST_CASE(TestAutoMapKeyRange) {
    auto& arena = tester.scratch_arena;
    ArenaAllocator result_arena {PageAllocator::Instance()};

    auto create_lua = [&](Span<int> root_notes) {
        String const lua_pattern = R"aaa(
        local library = floe.new_library({
            name = "Lib",
            tagline = "tagline",
            author = "Sam",
            background_image_path = "",
            icon_image_path = "",
        })
        local instrument = floe.new_instrument(library, {
            name = "Inst1",
        })
        local group = {
            trigger_criteria = { auto_map_key_range_group = "group1" },
        }
        <REGION_DEFS>
        return library)aaa";

        String const region_def_pattern = R"aaa(
        floe.add_region(instrument, floe.extend_table(group, {
            path = "f",
            root_key = <ROOT_KEY>,
        })))aaa";

        DynamicArray<char> region_defs {arena};
        for (auto root : root_notes) {
            dyn::AppendSpan(region_defs,
                            fmt::FormatStringReplace(arena,
                                                     region_def_pattern,
                                                     ArrayT<fmt::StringReplacement>({
                                                         {"<ROOT_KEY>", fmt::IntToString(root)},
                                                     })));
        }

        return fmt::FormatStringReplace(arena,
                                        lua_pattern,
                                        ArrayT<fmt::StringReplacement>({
                                            {"<REGION_DEFS>", region_defs},
                                        }));
    };

    SUBCASE("2 files") {
        auto r =
            ReadLua(create_lua(Array {10, 30}), FAKE_ABSOLUTE_PATH_PREFIX "test.lua", result_arena, arena);
        if (auto err = r.TryGet<Error>()) tester.log.Error("Error: {}, {}", err->code, err->message);
        REQUIRE(!r.Is<Error>());

        auto library = r.Get<Library*>();
        REQUIRE(library->insts_by_name.size);
        auto inst = *(*library->insts_by_name.begin()).value_ptr;
        REQUIRE(inst->regions.size == 2);

        CHECK_EQ(inst->regions[0].root_key, 10);
        CHECK_EQ(inst->regions[0].trigger.key_range.start, 0);
        CHECK_EQ(inst->regions[0].trigger.key_range.end, 21);

        CHECK_EQ(inst->regions[1].root_key, 30);
        CHECK_EQ(inst->regions[1].trigger.key_range.start, 21);
        CHECK_EQ(inst->regions[1].trigger.key_range.end, 128);
    }

    SUBCASE("1 file") {
        auto r = ReadLua(create_lua(Array {60}), FAKE_ABSOLUTE_PATH_PREFIX "test.lua", result_arena, arena);
        if (auto err = r.TryGet<Error>()) tester.log.Error("Error: {}, {}", err->code, err->message);
        REQUIRE(!r.Is<Error>());

        auto library = r.Get<Library*>();
        REQUIRE(library->insts_by_name.size);
        auto inst = *(*library->insts_by_name.begin()).value_ptr;
        REQUIRE(inst->regions.size == 1);

        CHECK_EQ(inst->regions[0].trigger.key_range.start, 0);
        CHECK_EQ(inst->regions[0].trigger.key_range.end, 128);
    }

    return k_success;
}

TEST_CASE(TestBasicFile) {
    auto& arena = tester.scratch_arena;
    ArenaAllocator result_arena {PageAllocator::Instance()};
    auto r = ReadLua(R"aaa(
    local library = floe.new_library({
        name = "Lib",
        tagline = "tagline",
        author = "Sam",
        background_image_path = "images/background.jpg",
        icon_image_path = "image/icon.png",
    })
    local instrument = floe.new_instrument(library, {
        name = "Inst1",
        tags = {"tag1"},
        folder = "Folders/Sub",
    })
    local instrument2 = floe.new_instrument(library, {
        name = "Inst2",
        tags = {"tag1", "tag2"},
    })
    local proto = {
        trigger_criteria = { auto_map_key_range_group = "group1" },
    }
    floe.add_region(instrument, floe.extend_table(proto, {
        path = "foo/file.flac",   -- path relative to this file
        root_key = 10,            -- MIDI note number
        loop = { 
            builtin_loop = {
                start_frame = 3000, 
                end_frame = 9000, 
                crossfade = 2, 
                mode = 'standard',
            },
        },
    }))
    floe.add_region(instrument2, floe.extend_table(proto, {
        path = "foo/file.flac",
        root_key = 10,
    }))
    floe.add_ir(library, {
        name = "IR1",
        path = "bar/bar.flac",
    })
    return library
    )aaa",
                     FAKE_ABSOLUTE_PATH_PREFIX "test.lua",
                     result_arena,
                     arena);
    if (auto err = r.TryGet<Error>()) tester.log.Error("Error: {}, {}", err->code, err->message);
    REQUIRE(!r.Is<Error>());

    auto& lib = *r.Get<Library*>();
    CHECK_EQ(lib.name, "Lib"_s);
    CHECK_EQ(lib.tagline, "tagline"_s);
    CHECK_EQ(lib.author, "Sam"_s);
    CHECK_EQ(lib.minor_version, 1u);

    REQUIRE(lib.insts_by_name.size);

    {
        auto inst2_ptr = lib.insts_by_name.Find("Inst2");
        REQUIRE(inst2_ptr);
        auto inst2 = *inst2_ptr;
        CHECK_EQ(inst2->name, "Inst2"_s);
        REQUIRE(inst2->tags.size == 2);
        CHECK_EQ(inst2->tags[0], "tag1"_s);
        CHECK_EQ(inst2->tags[1], "tag2"_s);
    }

    {
        auto inst1_ptr = lib.insts_by_name.Find("Inst1");
        REQUIRE(inst1_ptr);
        auto inst1 = *inst1_ptr;
        CHECK_EQ(inst1->name, "Inst1"_s);
        CHECK_EQ(inst1->folder, "Folders/Sub"_s);
        REQUIRE(inst1->tags.size == 1);
        CHECK_EQ(inst1->tags[0], "tag1"_s);

        CHECK_EQ(inst1->audio_file_path_for_waveform, "foo/file.flac"_s);

        REQUIRE(inst1->regions.size == 1);
        auto const& region = inst1->regions[0];
        CHECK_EQ(region.trigger.auto_map_key_range_group, "group1"_s);
        auto& file = region;
        CHECK_EQ(file.path, "foo/file.flac"_s);
        CHECK_EQ(file.root_key, 10);
        REQUIRE(file.loop.builtin_loop);
        auto loop = file.loop.builtin_loop.Value();
        CHECK_EQ(loop.start_frame, 3000u);
        CHECK_EQ(loop.end_frame, 9000u);
        CHECK_EQ(loop.crossfade_frames, 2u);
    }

    {
        auto ir = lib.irs_by_name.Find("IR1");
        REQUIRE(ir);
        CHECK_EQ((*ir)->name, "IR1"_s);
        CHECK_EQ((*ir)->path, "bar/bar.flac"_s);
    }

    return k_success;
}

TEST_CASE(TestErrorHandling) {
    auto& scratch_arena = tester.scratch_arena;
    auto const lua_filepath = FAKE_ABSOLUTE_PATH_PREFIX "test.lua"_s;

    auto check = [&](ErrorCodeOr<void> expected, String lua_code, Options options = {}) {
        ArenaAllocator result_arena {PageAllocator::Instance()};
        auto const outcome = ReadLua(lua_code, lua_filepath, result_arena, scratch_arena, options);
        if (auto err = outcome.TryGetFromTag<ResultType::Error>()) {
            if (expected.Succeeded()) {
                tester.log.Error(
                    "Error: we expected the lua code to succeed interpretation but it failed. Lua code:\n{}\nError:\n{d}, {}",
                    lua_code,
                    err->code,
                    err->message);
            } else {
                tester.log.Debug("Success: failure expected: {}", err->code);
            }

            REQUIRE(expected.HasError());
            CHECK_EQ(err->code, expected.Error());
        } else {
            if (expected.HasError()) {
                tester.log.Error(
                    "Error: we expected the lua code to fail interpretation but it succeeded. Lua code:\n{}",
                    lua_code);
            }
            REQUIRE(expected.Succeeded());
        }
    };

    SUBCASE("empty") {
        check(ErrorCode {LuaErrorCode::Syntax}, "{}");
        check(ErrorCode {LuaErrorCode::Runtime}, "return {}");
    }

    SUBCASE("wrong return type") {
        constexpr String k_lua = R"aaa(
        local file = floe.new_instrument({
            name = "",
            tagline = "",
        })
        return file 
        )aaa";
        check(ErrorCode {LuaErrorCode::Runtime}, k_lua);
    }

    SUBCASE("fails when requirements are low") {
        DynamicArray<char> buf {scratch_arena};
        dyn::AppendSpan(buf, "local tab = {}\n");
        for (auto _ : ::Range(3))
            for (char c = 'a'; c <= 'z'; ++c)
                fmt::Append(buf, "tab[\"{}\"] = 1\n", c);
        dyn::AppendSpan(buf, "return tab\n");

        SUBCASE("fail with small memory") {
            for (auto size : Array {0uz, 500, Kb(1), Kb(2), Kb(4), Kb(8)}) {
                CAPTURE(size);
                check(ErrorCode {LuaErrorCode::Memory}, buf, {.max_memory_allowed = size});
            }
        }

        SUBCASE("success with large memory") {
            for (auto size : Array {Kb(800), Mb(5)}) {
                CAPTURE(size);
                check(ErrorCode {LuaErrorCode::Runtime}, buf, {.max_memory_allowed = size});
            }
        }

        SUBCASE("time") {
            f64 seconds_allowed = 0;
            SUBCASE("zero") { seconds_allowed = 0; }
            SUBCASE("really small") { seconds_allowed = 0.00001; }
            check(ErrorCode {LuaErrorCode::Timeout}, buf, {.max_seconds_allowed = seconds_allowed});
        }
    }

    SUBCASE("infinite loop") {
        String const lua = R"aaa(while 1 == 1 do end)aaa";
        check(ErrorCode {LuaErrorCode::Timeout}, lua, {.max_seconds_allowed = 0.005});
    }

    SUBCASE("can use standard libs") {
        SUBCASE("string") {
            String const lua = R"aaa(
        s = "hello world"
        i, j = string.find(s, "hello")
        return s)aaa";
            check(ErrorCode {LuaErrorCode::Runtime}, lua);
        }
        SUBCASE("assert") {
            String const lua = "assert(1 == 0) return {}";
            check(ErrorCode {LuaErrorCode::Runtime}, lua);
        }
    }

    return k_success;
}

} // namespace sample_lib

TEST_REGISTRATION(RegisterLibraryLuaTests) {
    REGISTER_TEST(sample_lib::TestDocumentedExampleIsValid);
    REGISTER_TEST(sample_lib::TestWordWrap);
    REGISTER_TEST(sample_lib::TestBasicFile);
    REGISTER_TEST(sample_lib::TestIncorrectParameters);
    REGISTER_TEST(sample_lib::TestErrorHandling);
    REGISTER_TEST(sample_lib::TestAutoMapKeyRange);
}
