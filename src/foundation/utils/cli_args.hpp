// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/hash_table.hpp"
#include "foundation/utils/format.hpp"
#include "foundation/utils/string.hpp"
#include "foundation/utils/writer.hpp"

struct CommandLineArgDefinition {
    u32 id; // normally an enum, used for lookup
    String key;
    String description;
    bool required;
    bool needs_value; // else it's just a flag
};

struct CommandLineArg {
    Optional<String> OptValue() const { return was_provided ? Optional<String> {value} : nullopt; }
    CommandLineArgDefinition const& info;
    String value; // empty if no value given
    bool was_provided;
};

// args straight from main()
struct ArgsCstr {
    int size;
    char const* const* args; // remember the first arg is the program name
};

PUBLIC ErrorCodeOr<void>
PrintUsage(Writer writer, String exe_name, Span<CommandLineArgDefinition const> args) {
    TRY(fmt::FormatToWriter(writer, "Usage: {} [ARGS]\n\n", exe_name));

    auto print_arg = [&](CommandLineArgDefinition const& arg) -> ErrorCodeOr<void> {
        TRY(fmt::FormatToWriter(writer, "  --{}{}", arg.key, arg.needs_value ? "=<value>" : ""_s));
        if (arg.description.size) TRY(fmt::FormatToWriter(writer, "  {}", arg.description));
        TRY(writer.WriteChar('\n'));
        return k_success;
    };

    if (FindIf(args, [](auto const& arg) { return arg.required; })) {
        TRY(fmt::FormatToWriter(writer, "Required arguments:\n"));
        for (auto const& arg : args) {
            if (!arg.required) continue;
            TRY(print_arg(arg));
        }
    }

    if (FindIf(args, [](auto const& arg) { return !arg.required; })) {
        TRY(fmt::FormatToWriter(writer, "Optional arguments:\n"));
        for (auto const& arg : args) {
            if (arg.required) continue;
            TRY(print_arg(arg));
        }
    }

    TRY(writer.WriteChar('\n'));

    return k_success;
}

PUBLIC Span<String> ArgsToStringsSpan(ArenaAllocator& arena, ArgsCstr args, bool include_program_name) {
    ASSERT(args.size > 0);
    auto const argv_start_index = (usize)(include_program_name ? 0 : 1);
    auto const result_size = (usize)args.size - argv_start_index;
    if (!result_size) return {};
    auto result = arena.AllocateExactSizeUninitialised<String>(result_size);
    for (auto const result_index : Range(result_size))
        result[result_index] = FromNullTerminated(args.args[result_index + argv_start_index]);
    return result;
}

// quite basic. only supports -a, -a=value, -a value --arg, --arg=value, --arg value
PUBLIC HashTable<String, String> ArgsToKeyValueTable(ArenaAllocator& arena, Span<String const> args) {
    DynamicHashTable<String, String> result {arena};
    enum class ArgType { Short, Long, None };
    auto const check_arg = [](String arg) {
        if (arg[0] == '-' && IsAlphanum(arg[1])) return ArgType::Short;
        if (arg.size > 2) {
            if (arg[0] == '-' && arg[1] == '-') return ArgType::Long;
            if (arg[0] == '-' && IsAlphanum(arg[1]) && arg[2] == '=') return ArgType::Short;
        }
        return ArgType::None;
    };
    auto const prefix_size = [](ArgType type) -> usize {
        switch (type) {
            case ArgType::Short: return 1;
            case ArgType::Long: return 2;
            case ArgType::None: return 0;
        }
        return 0;
    };
    struct KeyVal {
        String key;
        String value;
    };
    auto const try_get_combined_key_val = [](String arg) {
        if (auto const opt_index = Find(arg, '='))
            return KeyVal {arg.SubSpan(0, *opt_index), arg.SubSpan(*opt_index + 1)};
        return KeyVal {arg, ""_s};
    };

    for (usize i = 0; i < args.size;) {
        if (auto const type = check_arg(args[i]); type != ArgType::None) {
            auto const arg = args[i].SubSpan(prefix_size(type));
            auto const [key, value] = try_get_combined_key_val(arg);

            bool added_next = false;
            if (i != args.size - 1) {
                auto const& next = args[i + 1];
                if (auto const next_type = check_arg(next); next_type == ArgType::None) {
                    result.Insert(arg, next);
                    i += 2;
                    added_next = true;
                }
            }

            if (!added_next) {
                result.Insert(key, value);
                i += 1;
            }
        } else {
            // positional arguments aren't supported
            i += 1;
        }
    }

    return result.ToOwnedTable();
}

PUBLIC HashTable<String, String> ArgsToKeyValueTable(ArenaAllocator& arena, ArgsCstr args) {
    return ArgsToKeyValueTable(arena, ArgsToStringsSpan(arena, args, false));
}

enum class CliError {
    InvalidArguments,
    HelpRequested,
};
PUBLIC ErrorCodeCategory const& CliErrorCodeType() {
    static constexpr ErrorCodeCategory const k_cat {
        .category_id = "CLI",
        .message =
            [](Writer const& writer, ErrorCode e) {
                return writer.WriteChars(({
                    String s {};
                    switch ((CliError)e.code) {
                        case CliError::InvalidArguments: s = "Invalid CLI arguments"; break;
                        case CliError::HelpRequested: s = "Help requested"; break;
                    }
                    s;
                }));
            },
    };
    return k_cat;
}
PUBLIC ErrorCodeCategory const& ErrorCategoryForEnum(CliError) { return CliErrorCodeType(); }

struct ParseCommandLineArgsOptions {
    bool handle_help_option = true;
    bool print_usage_on_error = true;
};

// always returns a span the same size as the arg_defs, if an arg wasn't set it will have was_provided = false
PUBLIC ErrorCodeOr<Span<CommandLineArg>> ParseCommandLineArgs(Writer writer,
                                                              ArenaAllocator& arena,
                                                              String program_name,
                                                              Span<String const> args,
                                                              Span<CommandLineArgDefinition const> arg_defs,
                                                              ParseCommandLineArgsOptions options = {}) {
    auto error = [&](CliError e) -> ErrorCode {
        if (options.print_usage_on_error) TRY(PrintUsage(writer, program_name, arg_defs));
        return ErrorCode {e};
    };

    auto result = arena.AllocateExactSizeUninitialised<CommandLineArg>(arg_defs.size);
    for (auto const arg_index : Range(arg_defs.size)) {
        PLACEMENT_NEW(&result[arg_index])
        CommandLineArg {
            .info = arg_defs[arg_index],
            .value = {},
            .was_provided = false,
        };
    }

    for (auto [key, value] : ArgsToKeyValueTable(arena, args)) {
        if (options.handle_help_option && key == "help") {
            TRY(PrintUsage(writer, program_name, arg_defs));
            return ErrorCode {CliError::HelpRequested};
        }

        auto const arg_index = FindIf(arg_defs, [&](auto const& arg) { return arg.key == key; });
        if (!arg_index) {
            TRY(fmt::FormatToWriter(writer, "Unknown option: {}\n", key));
            return error(CliError::InvalidArguments);
        }

        auto const& arg = arg_defs[*arg_index];

        if (arg.needs_value && !value->size) {
            TRY(fmt::FormatToWriter(writer, "Option --{} requires a value\n", key));
            return error(CliError::InvalidArguments);
        }

        result[*arg_index].value = *value;
        result[*arg_index].was_provided = true;
    }

    for (auto const arg_index : Range(arg_defs.size))
        if (arg_defs[arg_index].required && !result[arg_index].was_provided) {
            TRY(fmt::FormatToWriter(writer, "Required arg --{} not provided\n", arg_defs[arg_index].key));
            return error(CliError::InvalidArguments);
        }

    return result;
}

PUBLIC ErrorCodeOr<Span<CommandLineArg>> ParseCommandLineArgs(Writer writer,
                                                              ArenaAllocator& arena,
                                                              ArgsCstr args,
                                                              Span<CommandLineArgDefinition const> arg_defs,
                                                              ParseCommandLineArgsOptions options = {}) {
    return ParseCommandLineArgs(writer,
                                arena,
                                FromNullTerminated(args.args[0]),
                                ArgsToStringsSpan(arena, args, false),
                                arg_defs,
                                options);
}

// Compile-time helper that ensures command line arg definitions exactly match an enum. Allowing for easy
// lookup.
template <EnumWithCount EnumType, usize N>
consteval Array<CommandLineArgDefinition, N> MakeCommandLineArgDefs(CommandLineArgDefinition (&&a)[N]) {
    auto args = ArrayT(a);

    if (args.size != ToInt(EnumType::Count))
        throw "MakeCommandLineArgDefs: size of array doesn't match enum count";

    for (auto const [arg_index, arg] : Enumerate(args)) {
        if (arg.id != (u32)arg_index) throw "MakeCommandLineArgDefs: id is out of order with enum value";
        if (!arg.key.size) throw "MakeCommandLineArgDefs: key is empty";

        for (auto const& other_arg : args) {
            if (&arg == &other_arg) continue;
            if (arg.key == other_arg.key) throw "MakeCommandLineArgDefs: duplicate key";
        }
    }

    return args;
}

// NOTE: not necessary if you created args with MakeCommandLineArgDefs - you can just use array indexing
PUBLIC constexpr CommandLineArg const* LookupArg(Span<CommandLineArg const> args, auto id) {
    auto index = FindIf(args, [&](auto const& arg) { return arg.info.id == (u32)id; });
    if (index) return &args[*index];
    return nullptr;
}
