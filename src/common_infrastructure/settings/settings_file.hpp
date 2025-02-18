// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"

// Settings are stored in a simple INI-like file format. The same key can appear on multiple lines, in which
// case the same key has multiple values.
//
// Settings are for anything we want to persist between sessions, e.g. window size, extra library folders,
// etc.
//
// We want settings to be both forwards and backwards compatible because sometimes multiple versions of Floe
// can be installed at the same type (for example, when using multiple plugin folders, DAWs can sometimes load
// the plugin from either version). This isn't a common senario but it's one that can sometimes occur. We want
// both old and new versions of Floe to be able to read and write the settings file without losing any data.
// Loosing settings can be frustrating for users.

namespace sts {

constexpr usize k_max_file_size = Kb(32);
constexpr usize k_max_key_size = 64;
constexpr f64 k_settings_file_watcher_poll_interval_seconds = 1;

enum class ValueType : u32 {
    String,
    Int,
    Bool,
};

using ValueUnion = TaggedUnion<ValueType,
                               TypeAndTag<String, ValueType::String>,
                               TypeAndTag<s64, ValueType::Int>,
                               TypeAndTag<bool, ValueType::Bool>>;

struct Value : ValueUnion {
    bool operator==(ValueUnion const& v) const { return (ValueUnion const&)*this == v; }

    Value* next {};
};

using SettingsTable = HashTable<String, Value*>;

SettingsTable ParseSettingsFile(String file_data, ArenaAllocator& arena);
SettingsTable ParseLegacySettingsFile(String file_data, ArenaAllocator& arena);

struct ReadResult {
    String file_data;
    s128 file_last_modified {};
};
ErrorCodeOr<ReadResult> ReadEntireSettingsFile(String path, ArenaAllocator& arena);

ErrorCodeOr<void> WriteSettingsTable(SettingsTable const& table, Writer writer);
ErrorCodeOr<void>
WriteSettingsFile(SettingsTable const& table, String path, Optional<s128> set_last_modified);

Optional<s64> LookupInt(SettingsTable const& table, String key);
Optional<bool> LookupBool(SettingsTable const& table, String key);
Optional<String> LookupString(SettingsTable const& table, String key);

// Can return null. ValueNode is an intrusive linked list. Iterate through it using 'next'.
// The order of values is always undefined.
Value const* LookupValue(SettingsTable const& table, String key);

template <dyn::DynArray DynArrayType>
PUBLIC void ValuesToArray(Value const* value_list, DynArrayType& array) {
    for (auto value = value_list; value; value = value->next)
        if (auto const v = value->TryGet<typename DynArrayType::ValueType>())
            dyn::AppendIfNotAlreadyThere(array, *v);
}

template <dyn::DynArray DynArrayType>
PUBLIC void LookupValues(SettingsTable const& table, String key, DynArrayType& array) {
    dyn::Clear(array);
    if (auto v = LookupValue(table, key)) ValuesToArray(v, array);
}

template <typename Type, usize k_size>
PUBLIC DynamicArrayBounded<Type, k_size> LookupValues(SettingsTable const& table, String key) {
    DynamicArrayBounded<Type, k_size> result;
    LookupValues(table, key, result);
    return result;
}

// SettingsTable DeepCloneSettingsTable(SettingsTable const& table, ArenaAllocator& arena);

namespace key {
// We have code that needs to remap legacy settings keys to new keys, so we need to store this here. Usually
// though, settings keys should be private to the module that needs them.
constexpr String k_cc_to_param_id_map = "cc_to_param_id_map"_s;
constexpr String k_extra_libraries_folder = "extra_libraries_folder"_s;
constexpr String k_extra_presets_folder = "extra_presets_folder"_s;
constexpr String k_libraries_install_location = "libraries_install_location"_s;
constexpr String k_presets_install_location = "presets_install_location"_s;
constexpr String k_gui_keyboard_octave = "gui_keyboard_octave"_s;
constexpr String k_high_contrast_gui = "high_contrast_gui"_s;
constexpr String k_presets_random_mode = "presets_random_mode"_s;
constexpr String k_show_keyboard = "show_keyboard"_s;
constexpr String k_show_tooltips = "show_tooltips"_s;
constexpr String k_window_width = "window_width"_s;
} // namespace key

// =================================================================================================
// Higher-level API
// =================================================================================================
//
// This is a 'managed' instance of the settings table. It is designed to be a long-lived object that can be
// edited over time.

struct Settings : SettingsTable {
    ArenaAllocator arena {PageAllocator::Instance()};
    PathPool path_pool {};
    Value* free_values {};

    // We use track the last modified time so we can detect if the file has been changed externally or by our
    // own write operation.
    s128 last_known_file_modified_time {};
    bool write_to_file_needed {};

    // null for 'value' means the key was removed. Also remember ValueNode is a linked list if you are
    // expecting multiple values.
    TrivialFixedSizeFunction<8, void(String key, Value const* value)> on_change {};

    // Watcher
    ArenaAllocator watcher_scratch {PageAllocator::Instance()};
    ArenaAllocator watcher_arena {PageAllocator::Instance()};
    Optional<DirectoryWatcher> watcher;
    TimePoint last_watcher_poll_time {};
};

struct SetValueOptions {
    bool clone_key_string {};
    bool dont_track_changes {};
};

// The value will be allocated in the arena/path_pool (the string is cloned).
// Sets the value of key to the single value 'value'. If the key already has a value, it will be replaced.
void SetValue(Settings& settings, String key, ValueUnion value, SetValueOptions options = {});

// Same as SetValue in all ways, except instead of replacing all/eny values, this logic is used: if the key
// already has value(s), each value will be compared to the new value; if the new value matches any of the
// existing values, nothing will be added.
bool AddValue(Settings& settings, String key, ValueUnion value, SetValueOptions options = {});

struct RemoveValueOptions {
    bool dont_track_changes {};
};

// The value will be compared to all values for the given key, if it matches it will be removed.
// If the last value is removed, the key will be removed.
bool RemoveValue(Settings& settings, String key, ValueUnion value, RemoveValueOptions options = {});

// Remove key and all values associated with it.
void Remove(Settings& settings, String key, RemoveValueOptions options = {});

struct ReplaceSettingsOptions {
    // Whether keys in the exsiting settings should be removed if they don't exist in the new table. Keys that
    // do exist in the new table always entirely replace all existing values.
    bool remove_keys_not_in_new_table {};
};

// Entirely replaces the settings table with a new one. Emits minimal on_change notifications for all
// keys/value that have changed.
void ReplaceSettings(Settings& settings, SettingsTable const& new_table, ReplaceSettingsOptions options = {});

// Inits the object. Reads and parses the file. possible_paths should be sorted in order of
// preference. The first is the most preferred.
void Init(Settings& settings, Span<String const> possible_paths);
void Deinit(Settings& settings);

void WriteIfNeeded(Settings& settings);

void PollForExternalChanges(Settings& settings);

} // namespace sts
