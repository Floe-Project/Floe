// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"

// Settings are stored in the INI file format.
//
// This is for anything we want to persist between sessions, e.g. window size, extra library folders, etc.
//
// In general, we take the approach that the settings system doesn't know anything about the data it's
// storing. Intstead, each part of the code that uses the settings should know their own keys and validate the
// values they get from the settings. However, for backwards compatibility this system does know about keys in
// the legacy file format so that it can remap them.
//
// We want settings to be both forwards and backwards compatible because sometimes multiple versions of Floe
// can be installed at the same type (for example, when using multiple plugin folders, DAWs can sometimes load
// the plugin from either version). This isn't a common senario but it's one that can sometimes occur. We want
// both old and new versions of Floe to be able to read and write the settings file without losing any data.
//
// INI is not a strict format, these are our rules:
// - 'key = value\n' syntax. Spaces or tabs around the = are ignored.
// - Key and value must be on the same line.
// - There's no escaping of special characters.
// - The same key can appear multiple times with different values, in which case the same key has multiple
//   values (an array). These values are unordered. Duplicate values for the same key are ignored.
// - Keys must be a-z, 0-9, and _. Same for section names.
// - Sections are in square brackets: [section_name].
// - Comments are lines starting with a semicolon.
//
// Settings are kept in a hash table. The key is a string/int or a section + key string/int pair. The value is
// a linked list. You can loop over the table to get all the key-value pairs.

namespace sts {

constexpr usize k_max_file_size = Kb(32);
constexpr usize k_max_key_part_size = 50; // also for sections
constexpr usize k_max_key_size = k_max_key_part_size * 2 + 1; // keys can have 2 parts separated by a dot
constexpr f64 k_settings_file_watcher_poll_interval_seconds = 1;

enum class ValueType : u8 {
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

enum class KeyValueType : u8 {
    String,
    Int,
};

using KeyValueUnion =
    TaggedUnion<KeyValueType, TypeAndTag<String, KeyValueType::String>, TypeAndTag<s64, KeyValueType::Int>>;

struct SectionedKey {
    bool operator==(SectionedKey const& other) const = default;
    String section;
    KeyValueUnion key;
};

// NOTE: GlobalString and GlobalInt could be combined and stored using KeyValueUnion, but it makes the API a
// little less ergonomic because we'd then have to explictly specify the KeyValueUnion type when creating a
// key, rather than just String or s64.
enum class KeyType : u32 { GlobalString, GlobalInt, Sectioned };

// Our HashTable implementation currently requires keys to be default constructible.
constexpr auto TaggedUnionDefaultValue(sts::KeyType) { return ""_s; }

using Key = TaggedUnion<KeyType,
                        TypeAndTag<String, KeyType::GlobalString>,
                        TypeAndTag<s64, KeyType::GlobalInt>,
                        TypeAndTag<SectionedKey, KeyType::Sectioned>>;

ErrorCodeOr<void> CustomValueToString(Writer writer, Key key, fmt::FormatOptions options);
u64 HashKey(Key key);

using SettingsTable = HashTable<Key, Value*, HashKey>;

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

// NOTE: you shouldn't trust the values you get from any of the Lookup functions because they could be
// manually modified in the file, or they could be from a different version of Floe that had different uses of
// the same key. Therefore, you should always validate the values before using them.

// These functions assume that the key has a single value (or no value). If you want to lookup multiple
// values, then use LookupValues.
Optional<s64> LookupInt(SettingsTable const& table, Key key);
Optional<bool> LookupBool(SettingsTable const& table, Key key);
Optional<String> LookupString(SettingsTable const& table, Key key);

// Can return null. Value is an intrusive linked list. Iterate through it using 'next'.
// The order of values is always undefined. There's guaranteed to not be duplicate values for a key.
Value const* LookupValues(SettingsTable const& table, Key key);

template <dyn::DynArray DynArrayType>
PUBLIC void ValuesToArray(Value const* value_list, DynArrayType& array) {
    for (auto value = value_list; value; value = value->next)
        if (auto const v = value->TryGet<typename DynArrayType::ValueType>())
            dyn::AppendIfNotAlreadyThere(array, *v);
}

template <dyn::DynArray DynArrayType>
PUBLIC void LookupValues(SettingsTable const& table, Key key, DynArrayType& array) {
    dyn::Clear(array);
    if (auto v = LookupValues(table, key)) ValuesToArray(v, array);
}

template <typename Type, usize k_size>
PUBLIC DynamicArrayBounded<Type, k_size> LookupValues(SettingsTable const& table, Key key) {
    DynamicArrayBounded<Type, k_size> result;
    LookupValues(table, key, result);
    return result;
}

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

    // null for 'value' means the key was removed. Also remember Value is a linked list if you are expecting
    // multiple values.
    TrivialFixedSizeFunction<8, void(Key key, Value const* value_list)> on_change {};

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
void SetValue(Settings& settings, Key key, ValueUnion value, SetValueOptions options = {});

// Same as SetValue in all ways, except instead of replacing all/eny values, this logic is used: if the key
// already has value(s), each value will be compared to the new value; if the new value matches any of the
// existing values, nothing will be added.
bool AddValue(Settings& settings, Key key, ValueUnion value, SetValueOptions options = {});

struct RemoveValueOptions {
    bool dont_track_changes {};
};

// The value will be compared to all values for the given key, if it matches it will be removed.
// If the last value is removed, the key will be removed.
bool RemoveValue(Settings& settings, Key key, ValueUnion value, RemoveValueOptions options = {});

// Remove key and all values associated with it.
void Remove(Settings& settings, Key key, RemoveValueOptions options = {});

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

namespace key {
// We have code that needs to remap legacy settings keys to new keys, so we need to store this here. Usually
// though, settings keys should be private to the module that needs them.
namespace section {
constexpr String k_cc_to_param_id_map_section = "cc_to_param_id_map"_s;
}
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

} // namespace sts
