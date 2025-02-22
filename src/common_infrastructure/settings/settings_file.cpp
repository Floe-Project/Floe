// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_file.hpp"

#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "tests/framework.hpp"
#include "utils/json/json_reader.hpp"
#include "utils/logger/logger.hpp"

#include "common_errors.hpp"
#include "descriptors/param_descriptors.hpp"
#include "error_reporting.hpp"

namespace sts {

static bool IsKeyValid(String key) {
    if (key.size == 0) return false;
    if (key.size > k_max_key_size) return false;
    return true;
}

static bool IsKeyValid(Key const& key) {
    switch (key.tag) {
        case KeyType::GlobalString: return IsKeyValid(key.Get<String>());
        case KeyType::GlobalInt: return true;
        case KeyType::Sectioned: {
            auto const k = key.Get<SectionedKey>();
            if (!IsKeyValid(k.section)) return false;
            switch (k.key.tag) {
                case KeyValueType::String: return IsKeyValid(k.key.Get<String>());
                case KeyValueType::Int: return true;
            }
        }
    }
    PanicIfReached();
    return false;
}

u64 HashKey(Key const& key) {
    switch (key.tag) {
        case KeyType::GlobalString: return Hash(key.Get<String>());
        case KeyType::GlobalInt: {
            auto const v = key.Get<s64>();
            return Hash(Span {(u8 const*)&v, sizeof(v)});
        }
        case KeyType::Sectioned: {
            auto const k = key.Get<SectionedKey>();
            auto hash = HashInit();
            HashUpdate(hash, k.section);
            switch (k.key.tag) {
                case KeyValueType::String: HashUpdate(hash, k.key.Get<String>()); break;
                case KeyValueType::Int: HashUpdate(hash, k.key.Get<s64>()); break;
            }
            return hash;
        }
    }
    PanicIfReached();
    return 0;
}

ErrorCodeOr<void> CustomValueToString(Writer writer, Key const& key, fmt::FormatOptions options) {
    switch (key.tag) {
        case KeyType::GlobalString: return ValueToString(writer, key.Get<String>(), options);
        case KeyType::GlobalInt: return ValueToString(writer, key.Get<s64>(), options);
        case KeyType::Sectioned: {
            auto const k = key.Get<SectionedKey>();
            DynamicArrayBounded<char, k_max_key_size> buffer;
            String key_str {};
            switch (k.key.tag) {
                case KeyValueType::String: key_str = k.key.Get<String>(); break;
                case KeyValueType::Int:
                    static_assert(k_max_key_size >= 32);
                    auto const size = fmt::IntToString(k.key.Get<s64>(), buffer.data, {});
                    key_str = String {buffer.data, size};
                    break;
            }

            auto const size = 1 + k.section.size + 2 + key_str.size;
            TRY(PadToRequiredWidthIfNeeded(writer, options, size));
            TRY(writer.WriteChars("["));
            TRY(ValueToString(writer, k.section, options));
            TRY(writer.WriteChars("]."));
            TRY(ValueToString(writer, key_str, options));
            return k_success;
        }
    }
}

SettingsTable ParseSettingsFile(String file_data, ArenaAllocator& arena) {
    if (file_data.size == 0) return {};
    ASSERT(file_data.size < k_max_file_size);
    ASSERT(arena.ContainsPointer((u8 const*)file_data.data));

    DynamicHashTable<Key, Value*, HashKey> table {arena, 24};

    Optional<String> section {};

    for (auto line : SplitIterator {.whole = file_data, .token = '\n', .skip_consecutive = true}) {
        line = WhitespaceStrippedStart(line);
        if (line.size == 0 || line[0] == ';') continue;

        if (line[0] == '[') {
            auto maybe_section = WhitespaceStrippedEnd(line);
            if (Last(maybe_section) == ']') {
                maybe_section.RemovePrefix(1);
                maybe_section.RemoveSuffix(1);
                if (IsKeyValid(maybe_section)) {
                    section = maybe_section;
                    continue;
                }
            }
        }

        auto const equals = Find(line, '=');
        if (!equals) continue;

        auto key = WhitespaceStrippedEnd(line.SubSpan(0, *equals));
        if (!IsKeyValid(key)) {
            LogWarning(ModuleName::Settings, "invalid key {}", key.SubSpan(0, k_max_key_size));
            continue;
        }

        auto const value_str = line.SubSpan(*equals + 1);

        auto const stripped_value_str = WhitespaceStripped(value_str);

        usize num_chars_read = {};

        Value* value = nullptr;
        if (stripped_value_str.size == 0)
            value = nullptr; // empty values are allowed
        else if (IsEqualToCaseInsensitiveAscii(stripped_value_str, "true"_s))
            value = arena.New<Value>(true);
        else if (IsEqualToCaseInsensitiveAscii(stripped_value_str, "false"_s))
            value = arena.New<Value>(false);
        else if (auto const v = ParseInt(stripped_value_str, ParseIntBase::Decimal, &num_chars_read);
                 v && num_chars_read == stripped_value_str.size)
            value = arena.New<Value>(*v);
        else
            // We don't need to clone the string here because we've asserted it's already in the arena.
            value = arena.New<Value>(WhitespaceStrippedStart(value_str));

        auto const key_int = ParseInt(key, ParseIntBase::Decimal);

        Key full_key {key};
        if (section) {
            full_key = SectionedKey {*section, key};
            if (key_int) full_key = SectionedKey {*section, *key_int};
        } else {
            if (key_int) full_key = (s64)*key_int;
        }

        if (auto v = table.Find(full_key))
            SinglyLinkedListPrepend(*v, value);
        else
            table.Insert(full_key, value);
    }

    return table.ToOwnedTable();
}

SettingsTable ParseLegacySettingsFile(String file_data, ArenaAllocator& arena) {
    ASSERT(file_data.size < k_max_file_size);
    ASSERT(arena.ContainsPointer((u8 const*)file_data.data));

    struct Parser {
        bool HandleEvent(json::EventHandlerStack& handler_stack, json::Event const& event) {
            Optional<String> folder {};
            if (SetIfMatchingRef(event, "presets_folder", folder)) {
                // The old format only allowed for a single presets folder so we can just set it directly.
                if (path::IsAbsolute(*folder) && IsValidUtf8(*folder))
                    table.InsertGrowIfNeeded(arena, key::k_extra_presets_folder, arena.New<Value>(*folder));
                return true;
            }
            if (json::SetIfMatchingArray(
                    handler_stack,
                    event,
                    "libraries",
                    [this](json::EventHandlerStack& handler_stack, json::Event const& event) {
                        return HandleLibraries(handler_stack, event);
                    }))
                return true;
            if (json::SetIfMatchingObject(
                    handler_stack,
                    event,
                    "default_ccs",
                    [this](json::EventHandlerStack& handler_stack, json::Event const& event) {
                        return HandleLegacyCcToParamMapping(handler_stack, event);
                    }))
                return true;
            if (json::SetIfMatchingObject(
                    handler_stack,
                    event,
                    "gui_settings",
                    [this](json::EventHandlerStack& handler_stack, json::Event const& event) {
                        return HandleGUISettings(handler_stack, event);
                    }))
                return true;
            return false;
        }

        bool HandleLibraries(json::EventHandlerStack& handler_stack, json::Event const& event) {
            if (json::SetIfMatchingObject(
                    handler_stack,
                    event,
                    "",
                    [this](json::EventHandlerStack&, json::Event const& event) {
                        String library_path {};
                        if (json::SetIfMatchingRef(event, "path", library_path)) {
                            if (auto const dir = path::Directory(library_path);
                                dir && path::IsAbsolute(*dir)) {
                                auto existing = table.Find(key::k_extra_libraries_folder);
                                if (existing) {
                                    // skip if already exists
                                    for (auto v = *existing; v; v = v->next)
                                        if (auto const path = v->TryGet<String>();
                                            path && path::Equal(*path, *dir))
                                            return true;

                                    SinglyLinkedListPrepend(*existing, arena.New<Value>(*dir));
                                } else
                                    table.InsertGrowIfNeeded(arena,
                                                             key::k_extra_libraries_folder,
                                                             arena.New<Value>(*dir));
                            }
                            return true;
                        }
                        return false;
                    })) {
                return true;
            }
            return false;
        }

        bool HandleLegacyCcToParamMapping(json::EventHandlerStack& handler_stack, json::Event const& event) {
            if (event.type == json::EventType::ArrayStart) {
                if (auto o = ParseInt(event.key, ParseIntBase::Decimal); o.HasValue()) {
                    auto num = o.Value();
                    if (num > 0 && num <= 127) {
                        cc_num = (u8)num;

                        if (json::SetIfMatchingArray(
                                handler_stack,
                                event,
                                event.key,
                                [&](json::EventHandlerStack&, json::Event const& event) {
                                    if (event.type == json::EventType::String) {
                                        if (auto const result = ParamFromLegacyId(event.string);
                                            result && result->tag == ParamExistance::StillExists) {
                                            auto const key =
                                                SectionedKey {key::section::k_cc_to_param_id_map_section,
                                                              (s64)cc_num};
                                            auto const value =
                                                (s64)k_param_descriptors
                                                    [ToInt(result->GetFromTag<ParamExistance::StillExists>())]
                                                        .id;

                                            auto const existing = table.Find(key);
                                            if (existing) {
                                                for (auto v = *existing; v; v = v->next)
                                                    if (*v == value) return true;
                                                SinglyLinkedListPrepend(*existing, arena.New<Value>(value));
                                            } else {
                                                bool const inserted =
                                                    table.InsertGrowIfNeeded(arena,
                                                                             key,
                                                                             arena.New<Value>(value));
                                                ASSERT(inserted);
                                            }
                                        }
                                        return true;
                                    }
                                    return false;
                                })) {
                            return true;
                        }
                    }
                } else {
                    PanicIfReached();
                }
            }

            return false;
        }

        bool HandleGUISettings(json::EventHandlerStack&, json::Event const& event) {
            u16 gui_size_index {};
            if (SetIfMatching(event, "GUISize", gui_size_index)) {
                // We used to set the window size based on an index in an array.
                constexpr s64 k_window_width_presets[] = {580, 690, 800, 910, 1020, 1130, 1240};
                gui_size_index = Min(gui_size_index, (u16)(ArraySize(k_window_width_presets) - 1));
                table.InsertGrowIfNeeded(arena,
                                         key::k_window_width,
                                         arena.New<Value>(k_window_width_presets[gui_size_index]));
                return true;
            }

            s64 s64_value {};
            if (SetIfMatching(event, "KeyboardOctave", s64_value)) {
                table.InsertGrowIfNeeded(arena, key::k_gui_keyboard_octave, arena.New<Value>(s64_value));
                return true;
            }
            if (SetIfMatching(event, "PresetRandomMode", s64_value)) {
                table.InsertGrowIfNeeded(arena, key::k_presets_random_mode, arena.New<Value>(s64_value));
                return true;
            }

            bool bool_value {};
            if (SetIfMatching(event, "ShowKeyboard", bool_value)) {
                table.InsertGrowIfNeeded(arena, key::k_show_keyboard, arena.New<Value>(bool_value));
                return true;
            }
            if (SetIfMatching(event, "ShowTooltips", bool_value)) {
                table.InsertGrowIfNeeded(arena, key::k_show_tooltips, arena.New<Value>(bool_value));
                return true;
            }
            if (SetIfMatching(event, "HighContrast", bool_value)) {
                table.InsertGrowIfNeeded(arena, key::k_high_contrast_gui, arena.New<Value>(bool_value));
                return true;
            }
            return false;
        }

        SettingsTable& table;
        ArenaAllocator& arena;

        u8 cc_num {};
    };

    ArenaAllocatorWithInlineStorage<1000> scratch_arena {PageAllocator::Instance()};

    SettingsTable table {};

    Parser parser {
        .table = table,
        .arena = arena,
    };

    auto const o = json::Parse(file_data,
                               [&parser](json::EventHandlerStack& handler_stack, json::Event const& event) {
                                   return parser.HandleEvent(handler_stack, event);
                               },
                               scratch_arena,
                               {});
    if (o.HasError()) {
        // If the JSON is invalid we can't do anything about it, we'll just have to start with empty settings.
        return table;
    }

    return table;
}

ErrorCodeOr<ReadResult> ReadEntireSettingsFile(String path, ArenaAllocator& arena) {
    LogDebug(ModuleName::Settings, "Reading settings file: {}", path);

    auto file = TRY(OpenFile(path,
                             {
                                 .capability = FileMode::Capability::Read,
                                 .win32_share = FileMode::Share::ReadWrite | FileMode::Share::DeleteRename,
                                 .creation = FileMode::Creation::OpenExisting,
                             }));
    TRY(file.Lock({.type = FileLockOptions::Type::Shared}));
    DEFER { auto _ = file.Unlock(); };

    if (TRY(file.FileSize()) > k_max_file_size) return ErrorCode {CommonError::InvalidFileFormat};

    auto const file_last_modified = TRY(file.LastModifiedTimeNsSinceEpoch());
    auto const file_data = TRY(file.ReadWholeFile(arena));

    return ReadResult {
        .file_data = file_data,
        .file_last_modified = file_last_modified,
    };
}

static ErrorCodeOr<void> WriteKeyValLine(KeyValueUnion const& key, ValueUnion const& value, Writer writer) {
    switch (key.tag) {
        case KeyValueType::String: TRY(writer.WriteChars(key.Get<String>())); break;
        case KeyValueType::Int: TRY(writer.WriteChars(fmt::IntToString(key.Get<s64>()))); break;
    }
    TRY(writer.WriteChars(" = "));
    switch (value.tag) {
        case ValueType::String: {
            auto const s = value.Get<String>();
            TRY(writer.WriteChars(s));
            break;
        }
        case ValueType::Int: {
            auto const i = value.Get<s64>();
            TRY(writer.WriteChars(fmt::IntToString(i)));
            break;
        }
        case ValueType::Bool: {
            auto const b = value.Get<bool>();
            TRY(writer.WriteChars(b ? "true"_s : "false"));
            break;
        }
    }
    TRY(writer.WriteChars("\n"));

    return k_success;
}

static ErrorCodeOr<void>
WriteKeyValLineValueList(KeyValueUnion const& key, Value const* value_list, Writer writer) {
    for (auto value = value_list; value; value = value->next)
        TRY(WriteKeyValLine(key, *value, writer));
    return k_success;
}

#define ALLOW_UNSAFE_POINTER_TAGGING                                                                         \
    __attribute__((no_sanitize("pointer-overflow"))) __attribute__((no_sanitize("alignment")))

ErrorCodeOr<void> WriteSettingsTable(SettingsTable const& table, Writer writer) {
    // We use a 'pointer tagging' technique with value->next to track keys that we have written. This avoids
    // the need to allocate any tracking data.
    static_assert(alignof(Value*) != 1);
    auto const set_dirty = [](Value* value) ALLOW_UNSAFE_POINTER_TAGGING {
        value->next = (Value*)((uintptr)value->next + 1);
    };
    auto const clear_dirty = [](Value* value) ALLOW_UNSAFE_POINTER_TAGGING {
        value->next = (Value*)((uintptr)value->next - 1);
    };
    auto const is_dirty = [](Value* value) ALLOW_UNSAFE_POINTER_TAGGING {
        return !IsAligned(value->next, alignof(Value*));
    };

    // Write sectionless keys first
    for (auto const [key, value_list_ptr] : table) {
        KeyValueUnion key_value {""_s};
        switch (key.tag) {
            case KeyType::GlobalString: key_value = key.Get<String>(); break;
            case KeyType::GlobalInt: key_value = key.Get<s64>(); break;
            case KeyType::Sectioned: continue;
        }

        TRY(WriteKeyValLineValueList(key_value, *value_list_ptr, writer));
        set_dirty(*value_list_ptr);
    }

    // Write sectioned keys
    for (auto const [key_union, value_list_ptr] : table) {
        if (is_dirty(*value_list_ptr)) continue;

        String section {};

        switch (key_union.tag) {
            case KeyType::GlobalString:
            case KeyType::GlobalInt: PanicIfReached();
            case KeyType::Sectioned: section = key_union.Get<SectionedKey>().section; break;
        }

        TRY(writer.WriteChars("\n["));
        TRY(writer.WriteChars(section));
        TRY(writer.WriteChars("]\n"));

        // Write all keys in this section
        for (auto const [other_key_union, other_value_list_ptr] : table) {
            if (is_dirty(*other_value_list_ptr)) continue;

            switch (other_key_union.tag) {
                case KeyType::GlobalString:
                case KeyType::GlobalInt: PanicIfReached();
                case KeyType::Sectioned: break;
            }

            auto const [other_section, other_key] = other_key_union.Get<SectionedKey>();
            if (section != other_section) continue;

            TRY(WriteKeyValLineValueList(other_key, *other_value_list_ptr, writer));
            set_dirty(*other_value_list_ptr);
        }
    }

    // Un-dirty
    for (auto const [key, value_list_ptr] : table) {
        ASSERT(is_dirty(*value_list_ptr));
        clear_dirty(*value_list_ptr);
    }

    return k_success;
}

ErrorCodeOr<void>
WriteSettingsFile(SettingsTable const& table, String path, Optional<s128> set_last_modified) {
    auto file = TRY(OpenFile(path,
                             {
                                 .capability = FileMode::Capability::Write,
                                 .win32_share = FileMode::Share::ReadWrite,
                                 .creation = FileMode::Creation::CreateAlways,
                                 .everyone_read_write = true,
                             }));

    TRY(file.Lock({.type = FileLockOptions::Type::Exclusive}));
    DEFER { auto _ = file.Unlock(); };

    BufferedWriter<Kb(4)> buffered_writer {
        .unbuffered_writer = file.Writer(),
    };
    TRY(WriteSettingsTable(table, buffered_writer.Writer()));

    TRY(buffered_writer.Flush());
    TRY(file.Flush());
    if (set_last_modified) TRY(file.SetLastModifiedTimeNsSinceEpoch(*set_last_modified));

    return k_success;
}

Optional<s64> LookupInt(SettingsTable const& table, Key const& key) {
    if (auto const v = table.Find(key)) {
        if ((*v)->tag == ValueType::Int) return (*v)->Get<s64>();
    }
    return k_nullopt;
}

Optional<bool> LookupBool(SettingsTable const& table, Key const& key) {
    if (auto const v = table.Find(key)) {
        if ((*v)->tag == ValueType::Bool) return (*v)->Get<bool>();
    }
    return k_nullopt;
}

Optional<String> LookupString(SettingsTable const& table, Key const& key) {
    if (auto const v = table.Find(key)) {
        if ((*v)->tag == ValueType::String) return (*v)->Get<String>();
    }
    return k_nullopt;
}

Value const* LookupValues(SettingsTable const& table, Key const& key) {
    if (auto const v = table.Find(key)) return *v;
    return nullptr;
}

static void OnChange(Settings& settings, Key const& key, Value const* value) {
    settings.write_to_file_needed = true;
    if (settings.on_change) settings.on_change(key, value);
}

static ValueUnion CloneValueUnion(ValueUnion const& v, PathPool& pool, ArenaAllocator& arena) {
    ValueUnion result = v;
    if (v.tag == ValueType::String) result = pool.Clone(v.Get<String>(), arena);
    return result;
}

static void FreeValueUnion(ValueUnion const& v, PathPool& pool) {
    if (v.tag == ValueType::String) pool.Free(v.Get<String>());
}

static Key CloneKey(Key const& k, PathPool& pool, ArenaAllocator& arena) {
    switch (k.tag) {
        case KeyType::GlobalString: return pool.Clone(k.Get<String>(), arena);
        case KeyType::GlobalInt: return k.Get<s64>();
        case KeyType::Sectioned: {
            SectionedKey result = k.Get<SectionedKey>();
            result.section = pool.Clone(result.section, arena);
            switch (result.key.tag) {
                case KeyValueType::String: result.key = pool.Clone(result.key.Get<String>(), arena); break;
                case KeyValueType::Int: break;
            }
        }
    }
    PanicIfReached();
    return ""_s;
}

static void FreeKey(Key const& k, PathPool& pool) {
    switch (k.tag) {
        case KeyType::GlobalString: pool.Free(k.Get<String>()); break;
        case KeyType::GlobalInt: break;
        case KeyType::Sectioned: {
            auto const sectioned_key = k.Get<SectionedKey>();
            pool.Free(sectioned_key.section);
            switch (sectioned_key.key.tag) {
                case KeyValueType::String: pool.Free(sectioned_key.key.Get<String>()); break;
                case KeyValueType::Int: break;
            }
            break;
        }
    }
}

static Value* AllocateValue(Settings& settings, ValueUnion const& value) {
    if (settings.free_values) {
        auto result = settings.free_values;
        settings.free_values = settings.free_values->next;
        *result = {value};
        return result;
    }
    return settings.arena.New<Value>(value);
}

static void AddValueToFreeList(Settings& settings, Value* value) {
    value->next = settings.free_values;
    settings.free_values = value;
}

void SetValue(Settings& settings, Key const& key, ValueUnion const& value, SetValueOptions options) {
    ASSERT(IsKeyValid(key));
    Value* new_value {};

    auto const existing_values_ptr = settings.Find(key);
    if (existing_values_ptr) {
        auto const existing_values = *existing_values_ptr;
        if (existing_values->next == nullptr && *existing_values == value) return;

        // Free the content of all existing values
        for (auto node = existing_values; node;) {
            FreeValueUnion(*node, settings.path_pool);

            // We will reuse the first node, but for the others, we should add them to the free_values list.
            if (node == existing_values) {
                node = node->next;
            } else {
                auto next = node->next;
                AddValueToFreeList(settings, node);
                node = next;
            }
        }

        // We can reuse the first node.
        *existing_values = {CloneValueUnion(value, settings.path_pool, settings.arena)};
        new_value = existing_values;
    } else {
        new_value = AllocateValue(settings, CloneValueUnion(value, settings.path_pool, settings.arena));

        auto const inserted = settings.InsertGrowIfNeeded(
            settings.arena,
            options.clone_key_string ? CloneKey(key, settings.path_pool, settings.arena) : key,
            new_value);
        ASSERT(inserted);
    }

    if (!options.dont_track_changes) OnChange(settings, key, new_value);
}

bool AddValue(Settings& settings, Key const& key, ValueUnion const& value, SetValueOptions options) {
    ASSERT(IsKeyValid(key));
    Value* first_node {};

    auto existing_values_ptr = settings.Find(key);
    if (existing_values_ptr) {
        auto const existing_values = *existing_values_ptr;
        auto last = existing_values;
        for (; last->next; last = last->next)
            if (*last->next == value) return false;
        ASSERT(last->next == nullptr);

        last->next = AllocateValue(settings, CloneValueUnion(value, settings.path_pool, settings.arena));
        first_node = existing_values;
    } else {
        first_node = AllocateValue(settings, CloneValueUnion(value, settings.path_pool, settings.arena));
        auto const inserted = settings.InsertGrowIfNeeded(
            settings.arena,
            options.clone_key_string ? CloneKey(key, settings.path_pool, settings.arena) : key,
            first_node);
        ASSERT(inserted);
    }

    if (!options.dont_track_changes) OnChange(settings, key, first_node);
    return true;
}

bool RemoveValue(Settings& settings, Key const& key, ValueUnion const& value, RemoveValueOptions options) {
    ASSERT(IsKeyValid(key));
    auto const existing_values_ptr = settings.Find(key);
    if (!existing_values_ptr) return false;
    auto& existing_values = *existing_values_ptr;

    bool removed = false;
    bool single_value = existing_values->next == nullptr;
    SinglyLinkedListRemoveIf(
        existing_values,
        [&](Value const& node) { return node == value; },
        [&](Value* node) {
            FreeValueUnion(*node, settings.path_pool);
            AddValueToFreeList(settings, node);
            removed = true;
        });

    if (removed && !options.dont_track_changes) {
        if (single_value) {
            FreeKey(key, settings.path_pool);
            settings.Delete(key);
            OnChange(settings, key, nullptr);
        } else {
            OnChange(settings, key, existing_values);
        }
    }

    return removed;
}

void Remove(Settings& settings, Key const& key, RemoveValueOptions options) {
    ASSERT(IsKeyValid(key));
    auto existing = settings.Find(key);
    if (!existing) return;

    {
        for (auto node = *existing; node;) {
            FreeValueUnion(*node, settings.path_pool);
            auto next = node->next;
            node->next = settings.free_values;
            settings.free_values = node;
            node = next;
        }
        FreeKey(key, settings.path_pool);
        settings.Delete(key);
    }

    if (!options.dont_track_changes) OnChange(settings, key, nullptr);
}

void Init(Settings& settings, Span<String const> possible_paths) {
    ASSERT(settings.size == 0);

    for (auto const path : possible_paths) {
        auto const read_result = TRY_OR(ReadEntireSettingsFile(path, settings.arena), {
            if (error == FilesystemError::PathDoesNotExist || error == FilesystemError::AccessDenied)
                continue;
            LogWarning(ModuleName::Settings, "failed to read settings file: {}, {}", path, error);
            continue;
        });
        settings.last_known_file_modified_time = read_result.file_last_modified;

        if (path::Equal(path::Extension(path), ".json"_s))
            (SettingsTable&)settings = ParseLegacySettingsFile(read_result.file_data, settings.arena);
        else
            (SettingsTable&)settings = ParseSettingsFile(read_result.file_data, settings.arena);
    }

    auto watcher = CreateDirectoryWatcher(settings.watcher_arena);
    if (watcher.HasValue())
        settings.watcher.Emplace(watcher.ReleaseValue());
    else
        ReportError(sentry::Error::Level::Warning,
                    SourceLocationHash(),
                    "failed to create settings directory watcher: {}",
                    watcher.Error());
}

void Deinit(Settings& settings) {
    settings.on_change = {};
    if (settings.watcher.HasValue()) DestoryDirectoryWatcher(settings.watcher.Value());
}

void WriteIfNeeded(Settings& settings) {
    if (!settings.write_to_file_needed) return;

    settings.last_known_file_modified_time = NanosecondsSinceEpoch();
    TRY_OR(WriteSettingsFile(settings, SettingsFilepath(), settings.last_known_file_modified_time), {
        ReportError(sentry::Error::Level::Error,
                    SourceLocationHash(),
                    "failed to write settings file: {}",
                    error);
    });

    settings.write_to_file_needed = false;
}

void ReplaceSettings(Settings& settings, SettingsTable const& new_table, ReplaceSettingsOptions options) {
    if (options.remove_keys_not_in_new_table) {
        for (auto const [key, value] : settings) {
            if (!new_table.Find(key)) {
                Remove(settings, key, {.dont_track_changes = true});
                OnChange(settings, key, nullptr);
            }
        }
    }

    for (auto const [key, new_value_list_ptr] : new_table) {
        auto const new_value_list = *new_value_list_ptr;

        bool changed = false;

        if (auto existing_value_list_ptr = settings.Find(key)) {
            auto const existing_value_list = *existing_value_list_ptr;

            // Add new values that don't already exist.
            for (auto new_v = new_value_list; new_v; new_v = new_v->next)
                if (AddValue(settings, key, *new_v, {.clone_key_string = true, .dont_track_changes = true}))
                    changed = true;

            // Remove all old values that no longer exist.
            for (auto old_v = existing_value_list; old_v;) {
                auto const next = old_v->next;
                DEFER { old_v = next; };

                bool exists_in_new_values = false;
                for (auto new_v = new_value_list; new_v; new_v = new_v->next) {
                    if (*(ValueUnion const*)old_v == *(ValueUnion const*)new_v) {
                        exists_in_new_values = true;
                        break;
                    }
                }
                if (!exists_in_new_values) {
                    RemoveValue(settings, key, *old_v, {.dont_track_changes = true});
                    changed = true;
                }
            }
        } else {
            for (auto v = new_value_list; v; v = v->next)
                AddValue(settings, key, *v, {.clone_key_string = true, .dont_track_changes = true});
            changed = true;
        }

        if (changed) OnChange(settings, key, *settings.Find(key));
    }
}

void PollForExternalChanges(Settings& settings, PollForExternalChangesOptions options) {
    if (!settings.watcher) return;

    if (settings.write_to_file_needed) {
        // We ignore external changes if we have unsaved changes ourselves - our changes are probably more
        // recent.
        return;
    }

    if (!options.ignore_rate_limiting)
        if ((settings.last_watcher_poll_time + k_settings_file_watcher_poll_interval_seconds) >
            TimePoint::Now())
            return;
    DEFER { settings.last_watcher_poll_time = TimePoint::Now(); };

    auto const path = SettingsFilepath();
    auto const dir = *path::Directory(path);

    DirectoryToWatch const watch_dir {
        .path = dir,
        .recursive = false,
    };
    auto const changes = TRY_OR(PollDirectoryChanges(settings.watcher.Value(),
                                                     {
                                                         .dirs_to_watch = Array {watch_dir},
                                                         .result_arena = settings.watcher_scratch,
                                                         .scratch_arena = settings.watcher_scratch,
                                                     }),
                                {
                                    ReportError(sentry::Error::Level::Warning,
                                                SourceLocationHash(),
                                                "failed to poll for settings changes: {}",
                                                error);
                                });
    DEFER { settings.watcher_scratch.ResetCursorAndConsolidateRegions(); };

    for (auto const& change : changes) {
        for (auto const& subpath : change.subpath_changesets) {
            if (!path::Equal(subpath.subpath, path::Filename(path))) continue;

            // We ignore changes that are older or the same as our last known modification time.
            if (TRY_OR(LastModifiedTimeNsSinceEpoch(path), return) <= settings.last_known_file_modified_time)
                continue;

            // We need to apply the new settings to our existing settings. If we have a key that doesn't exist
            // in the new table, we keep our existing key-value, but for all keys that exist in the new table,
            // we update our values to exactly match the new table.
            auto const read_result = TRY_OR(ReadEntireSettingsFile(path, settings.watcher_scratch), return);
            auto const new_table = ParseSettingsFile(read_result.file_data, settings.watcher_scratch);

            ReplaceSettings(settings, new_table, {.remove_keys_not_in_new_table = false});

            // We just loaded fresh data from the file, so we can mark that we don't need to write to the
            // file.
            settings.last_known_file_modified_time = read_result.file_last_modified;
            settings.write_to_file_needed = false;
            return;
        }
    }
}

TEST_CASE(TestJsonParsing) {
    DynamicArray<char> json {R"foo({
    "presets_folder": "{ROOT}Presets",
    "use_old_param_mapping": false,
    "dismiss_all_notifications": false,
    "mirage_version": "2.1.0",
    "libraries": [
        {
            "name": "Slow",
            "path": "{ROOT}mdatas/slow.mdata"
        },
        {
            "name": "Wraith",
            "path": "{ROOT}mdatas/wraith.mdata"
        },
        {
            "name": "VMB",
            "path": "{ROOT}other_mdatas/vmb.mdata"
        }
    ],
    "default_ccs": {
        "1": [
            "MastVol"
        ]
    },
    "gui_settings": {
        "DismissedNotfications": [
            "Slow library released",
            "Arctic Strings library released"
        ],
        "GUISize": 3,
        "HideMarketing": true,
        "KeyboardOctave": 0,
        "NewsPrefsDecided": false,
        "PresetRandomMode": 0,
        "ShowKeyboard": true,
        "ShowTooltips": true,
        "HighContrast": false,
        "SortLibsAlpha": false
    }
})foo"_s,
                             tester.scratch_arena};
    auto const root_dir = IS_WINDOWS ? "C:/"_s : "/";
    dyn::Replace(json, "{ROOT}"_s, root_dir);

    auto const table = ParseLegacySettingsFile(json, tester.scratch_arena);

    {
        auto const v = LookupValues(table, key::k_extra_libraries_folder);
        REQUIRE(v);
        CHECK(v->next);

        auto const expected_dir_1 = fmt::Format(tester.scratch_arena, "{}mdatas"_s, root_dir);
        auto const expected_dir_2 = fmt::Format(tester.scratch_arena, "{}other_mdatas"_s, root_dir);

        usize num_dirs = 0;
        for (auto i = &*v; i; i = i->next) {
            ++num_dirs;
            ASSERT(num_dirs < 100);
            auto const path = i->Get<String>();
            if (path != expected_dir_1 && path != expected_dir_2) {
                TEST_FAILED("library path wrong: {}", path);
                break;
            }
        }

        CHECK_EQ(num_dirs, 2u);
    }

    CHECK(*LookupBool(table, key::k_show_keyboard));
    CHECK(*LookupBool(table, key::k_show_tooltips));
    CHECK(!*LookupBool(table, key::k_high_contrast_gui));
    CHECK_EQ(*LookupInt(table, key::k_gui_keyboard_octave), 0);
    CHECK_EQ(*LookupInt(table, key::k_window_width), 910);

    CHECK(*LookupString(table, key::k_extra_presets_folder) ==
          fmt::Format(tester.scratch_arena, "{}Presets"_s, root_dir));

    {
        auto const v = LookupValues(table,
                                    SectionedKey {
                                        .section = key::section::k_cc_to_param_id_map_section,
                                        .key = (s64)1,
                                    });
        REQUIRE(v);
        CHECK(!v->next);
        REQUIRE(v->tag == ValueType::Int);
        CHECK_EQ(v->Get<s64>(), k_param_descriptors[ToInt(ParamIndex::MasterVolume)].id);
    }

    return k_success;
}

TEST_CASE(TestSettings) {
    SUBCASE("read/write basics") {

#if IS_WINDOWS
#define ROOT "C:/"
#else
#define ROOT "/"
#endif

        struct KeyVal {
            Key key;
            ValueUnion value;
        };
        auto const keyvals = ArrayT<KeyVal>({
            {key::k_show_tooltips, true},
            {key::k_gui_keyboard_octave, (s64)0},
            {SectionedKey {.section = "section", .key = "key1"_s}, true},
            {SectionedKey {.section = "section", .key = "key2"_s}, false},
            {key::k_show_keyboard, true},
            {key::k_presets_random_mode, (s64)3},
            {key::k_window_width, (s64)1200},
            {SectionedKey {.section = key::section::k_cc_to_param_id_map_section, .key = (s64)10}, (s64)1},
            {SectionedKey {.section = key::section::k_cc_to_param_id_map_section, .key = (s64)10}, (s64)3},
            {SectionedKey {.section = key::section::k_cc_to_param_id_map_section, .key = (s64)10}, (s64)4},
            {"unknown_key"_s, "unknown value"_s},
            {key::k_extra_libraries_folder, ROOT "Libraries"_s},
            {key::k_extra_libraries_folder, ROOT "Floe Libraries"_s},
            {key::k_extra_presets_folder, ROOT "Projects/Test"_s},

        });

        DynamicArray<char> file_data {tester.scratch_arena};
        {
            Settings settings;
            for (auto const& kv : keyvals)
                AddValue(settings, kv.key, kv.value, {});
            TRY(WriteSettingsTable(settings, dyn::WriterFor(file_data)));
            tester.log.Debug("file_data: {}", file_data);
        }

        auto const settings = ParseSettingsFile(file_data, tester.scratch_arena);

        for (auto const& kv : keyvals) {
            CAPTURE(kv.key);
            DynamicArrayBounded<ValueUnion, 4> expected_values;
            for (auto const& other_kv : keyvals)
                if (kv.key == other_kv.key) dyn::Append(expected_values, other_kv.value);

            auto value_list = LookupValues(settings, kv.key);
            REQUIRE(value_list);

            usize num_values = 0;
            for (auto value = value_list; value; value = value->next) {
                ++num_values;
                bool is_in_expected = false;
                for (auto const& expected_value : expected_values) {
                    if (*value == expected_value) {
                        is_in_expected = true;
                        break;
                    }
                }
                if (!is_in_expected) TEST_FAILED("value not expected ({}): {}", kv.key, value->Get<String>());
            }

            CHECK_EQ(num_values, expected_values.size);
        }

        // Check for some specific values
        CHECK(*LookupBool(settings, SectionedKey {.section = "section", .key = "key1"_s}) == true);
        CHECK(*LookupBool(settings, SectionedKey {.section = "section", .key = "key2"_s}) == false);
    }

    SUBCASE("file read/write") {
        auto const filename = tests::TempFilename(tester);
        constexpr String k_file_data = "key1 = value1\nkey2 = value2\n"_s;
        TRY(WriteFile(filename, k_file_data));

        {
            auto const file_data = TRY(ReadEntireSettingsFile(filename, tester.scratch_arena));
            CHECK_EQ(file_data.file_data, k_file_data);

            auto const table = ParseSettingsFile(file_data.file_data, tester.scratch_arena);
            CHECK_EQ(table.size, 2u);
            CHECK_EQ(*LookupString(table, "key1"_s), "value1"_s);
            CHECK_EQ(*LookupString(table, "key2"_s), "value2"_s);

            TRY(WriteSettingsFile(table, filename, k_nullopt));
        }

        // Read again
        {
            auto const file_data = TRY(ReadEntireSettingsFile(filename, tester.scratch_arena));
            CHECK_EQ(file_data.file_data, k_file_data);
        }
    }

    SUBCASE("empty values are ok") {
        String const file_data = tester.scratch_arena.Clone("key = "_s);
        auto const table = ParseSettingsFile(file_data, tester.scratch_arena);
        CHECK_EQ(table.size, 1u);
        auto const v = LookupValues(table, "key"_s);
        CHECK(v == nullptr);
    }

    SUBCASE("values are recycled") {
        Settings settings;
        CHECK(settings.free_values == nullptr);

        for (auto _ : Range(10)) {
            AddValue(settings, "key"_s, "value"_s);
            RemoveValue(settings, "key"_s, "value"_s);
        }

        // There should be exactly one value in the free list.
        REQUIRE(settings.free_values);
        CHECK(settings.free_values->next == nullptr);
    }

    static constexpr String k_key = "key";
    static constexpr String k_alpha = "alpha"_s;
    static constexpr String k_beta = "beta"_s;
    static constexpr String k_gamma = "gamma"_s;

    SUBCASE("change listener") {
        Settings settings;

        // on_change is a fixed size function object that only has room for one pointer, so we combine our
        // necessary data into a single object.
        struct OnChangeContext {
            tests::Tester& tester;
            DynamicArrayBounded<String, 3> expected_values;
        } callback_ctx {tester, {}};

        settings.on_change = [&callback_ctx](Key key, Value const* value) {
            auto& tester = callback_ctx.tester;
            CHECK_EQ(key, k_key);
            usize num_values = 0;
            for (auto v = value; v; v = v->next) {
                CAPTURE(v->Get<String>());
                ++num_values;
                // Values are unordered, so we just check that all expected values are present.
                CHECK(Contains(callback_ctx.expected_values, v->Get<String>()));
            }
            CHECK_EQ(num_values, callback_ctx.expected_values.size);
        };

        auto const set_expected_callback = [&](Span<String const> expected_values) {
            callback_ctx.expected_values = expected_values;
        };

        SUBCASE("set") {
            set_expected_callback(Array {k_alpha});
            SetValue(settings, k_key, k_alpha);

            // replace with a new value
            set_expected_callback(Array {k_beta});
            SetValue(settings, k_key, k_beta);

            // add a second value
            set_expected_callback(Array {k_beta, k_gamma});
            AddValue(settings, k_key, k_gamma);
        }

        SUBCASE("simple add and remove") {
            set_expected_callback(Array {k_alpha});
            AddValue(settings, k_key, k_alpha);

            set_expected_callback({});
            RemoveValue(settings, k_key, k_alpha);
        }

        SUBCASE("add multiple") {
            set_expected_callback(Array {k_alpha});
            AddValue(settings, k_key, k_alpha);

            set_expected_callback(Array {k_alpha, k_beta});
            AddValue(settings, k_key, k_beta);

            SUBCASE("remove alpha first") {
                set_expected_callback(Array {k_beta});
                RemoveValue(settings, k_key, k_alpha);

                set_expected_callback({});
                RemoveValue(settings, k_key, k_beta);
            }

            SUBCASE("remove beta first") {
                set_expected_callback(Array {k_alpha});
                RemoveValue(settings, k_key, k_beta);

                set_expected_callback({});
                RemoveValue(settings, k_key, k_alpha);
            }

            SUBCASE("remove key") {
                set_expected_callback({});
                Remove(settings, k_key, {});
            }
        }
    }

    SUBCASE("replace entire settings") {
        Settings settings1;
        Settings settings2;

        SUBCASE("one value change") {
            AddValue(settings1, k_key, k_alpha);
            AddValue(settings2, k_key, k_beta);

            // We're expecting one on_change callback for the value change.
            settings1.on_change = [&tester](Key key, Value const* value) {
                CHECK_EQ(key, k_key);
                CHECK_EQ(value->Get<String>(), k_beta);
                CHECK(value->next == nullptr);
            };
            ReplaceSettings(settings1, settings2, {});

            CHECK_EQ(settings1.size, 1u);
            CHECK_EQ(*LookupString(settings1, k_key), k_beta);
        }

        SUBCASE("multiple values replaced") {
            AddValue(settings1, k_key, k_alpha);
            AddValue(settings1, k_key, k_beta);
            AddValue(settings1, k_key, k_gamma);

            AddValue(settings2, k_key, k_beta);
            AddValue(settings2, k_key, k_gamma);

            // We should receive a callback for the key containing 2 values.
            settings1.on_change = [&tester](Key key, Value const* value) {
                CHECK_EQ(key, k_key);
                usize num_values = 0;
                for (auto v = value; v; v = v->next) {
                    ++num_values;
                    CHECK(Contains(Array {k_beta, k_gamma}, v->Get<String>()));
                }
                CHECK_EQ(num_values, 2u);
            };
            ReplaceSettings(settings1, settings2, {});

            CHECK_EQ(settings1.size, 1u);
            CHECK_EQ(*LookupString(settings1, k_key), k_beta);
        }

        SUBCASE("new key added") {
            AddValue(settings2, k_key, k_alpha);

            settings1.on_change = [&tester](Key key, Value const* value) {
                CHECK_EQ(key, k_key);
                CHECK_EQ(value->Get<String>(), k_alpha);
                CHECK(value->next == nullptr);
            };
            ReplaceSettings(settings1, settings2, {});
        }

        SUBCASE("existing keys retain") {
            AddValue(settings1, k_key, k_alpha);

            AddValue(settings2, "other_key"_s, k_beta);

            ReplaceSettings(settings1, settings2, {.remove_keys_not_in_new_table = false});

            // The existing key should still be present, along with the new key.
            CHECK_EQ(settings1.size, 2u);
            CHECK_EQ(*LookupString(settings1, k_key), k_alpha);
            CHECK_EQ(*LookupString(settings1, "other_key"_s), k_beta);
        }

        SUBCASE("existing keys removed") {
            AddValue(settings1, k_key, k_alpha);

            AddValue(settings2, "other_key"_s, k_beta);

            ReplaceSettings(settings1, settings2, {.remove_keys_not_in_new_table = true});

            // The existing key should be removed, and the new key should be present.
            CHECK_EQ(settings1.size, 1u);
            CHECK_EQ(*LookupString(settings1, "other_key"_s), k_beta);
        }
    }

    SUBCASE("file watcher poll") {
        auto const filepath = SettingsFilepath();

        // The settings system only polls the official SettingsFilepath(), so we need to write to that file.
        // Let's first backup the existing file and restore it when we're done.
        auto const original_settings_file_data = ({
            Optional<String> d {};
            auto const o = ReadEntireFile(filepath, tester.scratch_arena);
            if (o.HasValue()) d = o.Value();
            d;
        });
        DEFER {
            if (original_settings_file_data) auto _ = WriteFile(filepath, *original_settings_file_data);
        };

        constexpr String k_file_data = "key1 = value1\nkey2 = value2\n"_s;
        TRY(WriteFile(filepath, k_file_data));

        Settings settings;

        Init(settings, Array {filepath});
        DEFER { Deinit(settings); };

        CHECK(!settings.write_to_file_needed);

        CHECK_EQ(settings.size, 2u);
        CHECK_EQ(*LookupString(settings, "key1"_s), "value1"_s);
        CHECK_EQ(*LookupString(settings, "key2"_s), "value2"_s);

        PollForExternalChanges(settings, {.ignore_rate_limiting = true});
        CHECK(!settings.write_to_file_needed);

        CHECK_EQ(settings.size, 2u);
        CHECK_EQ(*LookupString(settings, "key1"_s), "value1"_s);
        CHECK_EQ(*LookupString(settings, "key2"_s), "value2"_s);

        // Add a new key.
        {
            auto f = TRY(OpenFile(filepath,
                                  {
                                      .capability = FileMode::Capability::Append,
                                      .win32_share = FileMode::Share::Read,
                                      .creation = FileMode::Creation::OpenExisting,
                                  }));
            TRY(f.Write("key3 = value3\n"_s));
            TRY(f.Flush());
        }

        for (auto _ : Range(25)) {
            CHECK(!settings.write_to_file_needed);
            PollForExternalChanges(settings, {.ignore_rate_limiting = true});
            if (settings.size != 2) break;
            SleepThisThread(1); // wait for the file watcher to pick up the change
        }

        CHECK_EQ(settings.size, 3u);
        CHECK_EQ(*LookupString(settings, "key1"_s), "value1"_s);
        CHECK_EQ(*LookupString(settings, "key2"_s), "value2"_s);
        CHECK_EQ(*LookupString(settings, "key3"_s), "value3"_s);

        // Replace the value of key1.
        TRY(WriteFile(filepath, "key1 = value4\n"_s));

        for (auto _ : Range(25)) {
            PollForExternalChanges(settings, {.ignore_rate_limiting = true});
            CHECK(!settings.write_to_file_needed);
            if (LookupString(settings, "key1"_s) == "value4"_s) break;
            SleepThisThread(1); // wait for the file watcher to pick up the change
        }

        CHECK_EQ(settings.size, 3u);
        CHECK_EQ(*LookupString(settings, "key1"_s), "value4"_s);
        CHECK_EQ(*LookupString(settings, "key2"_s), "value2"_s);
        CHECK_EQ(*LookupString(settings, "key3"_s), "value3"_s);
    }

    return k_success;
}

} // namespace sts

TEST_REGISTRATION(RegisterSettingsFileTests) {
    REGISTER_TEST(sts::TestJsonParsing);
    REGISTER_TEST(sts::TestSettings);
}
