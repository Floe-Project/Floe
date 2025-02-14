// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autosave.hpp"

#include "tests/framework.hpp"

#include "state/state_coding.hpp"

constexpr f64 k_autosave_interval_seconds = 60;
constexpr s128 k_autosave_max_age_seconds = 60 * 60 * 24 * 7; // 1 week
constexpr usize k_max_autosaves_per_instance = 8;

static void AutosaveFilenamePrefix(DynamicArrayBounded<char, 32>& out, AutosaveState const& state) {
    fmt::Assign(out, "{} autosave ", state.instance_id);
}

static ErrorCodeOr<void> CleanupExcessInstanceAutosaves(AutosaveState const& state,
                                                        FloePaths const& paths,
                                                        ArenaAllocator& scratch_arena) {
    DynamicArrayBounded<char, 32> wildcard;
    AutosaveFilenamePrefix(wildcard, state);
    dyn::Append(wildcard, '*');
    dyn::AppendSpan(wildcard, FLOE_PRESET_FILE_EXTENSION);

    auto const entries = TRY(FindEntriesInFolder(scratch_arena,
                                                 paths.autosave_path,
                                                 {
                                                     .options {
                                                         .wildcard = wildcard,
                                                     },
                                                     .recursive = false,
                                                     .only_file_type = FileType::File,
                                                 }));

    if (entries.size <= k_max_autosaves_per_instance) return k_success;

    struct EntryWithTime {
        dir_iterator::Entry const* entry;
        s128 last_modified_time;
    };
    auto entries_with_times = scratch_arena.AllocateExactSizeUninitialised<EntryWithTime>(entries.size);
    for (auto [i, entry] : Enumerate(entries)) {
        auto const path = path::Join(scratch_arena, Array {paths.autosave_path, entry.subpath});
        DEFER { scratch_arena.Free(path.ToByteSpan()); };
        PLACEMENT_NEW(&entries_with_times[i])
        EntryWithTime {.entry = &entry, .last_modified_time = TRY(LastModifiedTimeNsSinceEpoch(path))};
    }

    Sort(entries_with_times, [](EntryWithTime const& a, EntryWithTime const& b) {
        return a.last_modified_time < b.last_modified_time;
    });

    auto const excess_count = entries.size - k_max_autosaves_per_instance;
    for (auto i : Range(excess_count)) {
        auto const path =
            path::Join(scratch_arena, Array {paths.autosave_path, entries_with_times[i].entry->subpath});
        DEFER { scratch_arena.Free(path.ToByteSpan()); };
        auto _ = Delete(path, {.type = DeleteOptions::Type::File, .fail_if_not_exists = false});
    }

    return k_success;
}

static ErrorCodeOr<void>
Autosave(AutosaveState& state, StateSnapshot const& snapshot, FloePaths const& paths) {
    state.last_save_time = TimePoint::Now();

    PathArena arena {PageAllocator::Instance()};

    DynamicArrayBounded<char, 32> prefix;
    AutosaveFilenamePrefix(prefix, state);

    DynamicArrayBounded<char, 32> unique_name;
    {
        auto seed = RandomSeed();
        auto const date = LocalTimeNow();
        fmt::Assign(unique_name,
                    "{02}{02} {02}s {} {} {} {} ({})",
                    date.hour,
                    date.minute,
                    date.second,
                    date.DayName(),
                    date.day_of_month,
                    date.MonthName(),
                    date.year,
                    RandomIntInRange<u32>(seed, 1000, 9999));
    }

    auto const filename = fmt::Join(arena, Array {String {prefix}, unique_name, FLOE_PRESET_FILE_EXTENSION});
    auto const path = path::Join(arena, Array {paths.autosave_path, filename});
    TRY(SavePresetFile(path, snapshot));

    return k_success;
}

static ErrorCodeOr<void> CleanupOldAutosavesIfNeeded(FloePaths const& paths, ArenaAllocator& scratch_arena) {
    constexpr String k_wildcard = "*autosave*" FLOE_PRESET_FILE_EXTENSION;
    auto const entries = TRY(FindEntriesInFolder(scratch_arena,
                                                 paths.autosave_path,
                                                 {
                                                     .options {
                                                         .wildcard = k_wildcard,
                                                     },
                                                     .recursive = false,
                                                     .only_file_type = FileType::File,
                                                 }));

    for (auto const& entry : entries) {
        auto const file_time =
            TRY_OR(LastModifiedTimeNsSinceEpoch(
                       path::Join(scratch_arena, Array {paths.autosave_path, entry.subpath})),
                   continue);

        auto const now = NanosecondsSinceEpoch();

        auto const delta_ns = now - file_time;
        auto const delta_secs = delta_ns / 1'000'000'000;
        if (delta_secs < (s128)k_autosave_max_age_seconds) continue;

        auto const path = path::Join(scratch_arena, Array {paths.autosave_path, entry.subpath});
        DEFER { scratch_arena.Free(path.ToByteSpan()); };

        auto _ = Delete(path, {.type = DeleteOptions::Type::File, .fail_if_not_exists = false});
    }

    return k_success;
}

void InitAutosaveState(AutosaveState& state, u64& random_seed, StateSnapshot const& initial_state) {
    constexpr auto k_instance_words = Array {
        "wave"_s, "pond",  "beam", "drift", "breeze", "flow",  "spark",  "glow",  "river",  "cloud",
        "stream", "rain",  "sun",  "moon",  "star",   "wind",  "storm",  "frost", "flame",  "mist",
        "ocean",  "peak",  "dawn", "dusk",  "leaf",   "stone", "spring", "sand",  "brook",  "lake",
        "cliff",  "pine",  "snow", "bird",  "reed",   "fog",   "bay",    "bloom", "branch", "creek",
        "cave",   "delta", "dew",  "elm",   "fern",   "grove", "glen",   "hill",  "isle",   "marsh",
        "meadow", "nest",  "opal", "path",  "reef",   "ridge", "sage",   "shell", "shore",  "slope",
        "swift",  "tide",  "vale", "vine",  "wood",   "ash",   "comet",  "dust",  "flash",  "haze",
        "light",  "nova",  "orb",  "plume", "ray",    "shade", "torch",  "void",  "wisp",   "zinc",
    };

    auto const word_index = RandomIntInRange<u32>(random_seed, 0, k_instance_words.size - 1);
    auto const number = RandomIntInRange<u32>(random_seed, 100, 999);
    fmt::Assign(state.instance_id, "{}-{}", k_instance_words[word_index], number);
    state.last_save_time = TimePoint::Now();
    state.state = AutosaveState::State::Saved;
    state.snapshot = initial_state;
}

void AutosaveToFileIfNeeded(AutosaveState& state, FloePaths const& paths) {
    Optional<StateSnapshot> snapshot {};
    {
        state.mutex.Lock();
        DEFER { state.mutex.Unlock(); };
        switch (state.state) {
            case AutosaveState::State::Idle: return;
            case AutosaveState::State::PendingSave:
                snapshot = state.snapshot;
                state.state = AutosaveState::State::Saved;
                break;
            case AutosaveState::State::Saved: return;
        }
    }
    if (snapshot) {
        TRY_OR(Autosave(state, *snapshot, paths), {
            ReportError(sentry::Error::Level::Error, HashComptime("autosave"), "autosave failed: {}", error);
        });
        ArenaAllocatorWithInlineStorage<1000> scratch_arena {PageAllocator::Instance()};
        static bool first_call = true;
        if (Exchange(first_call, false)) {
            TRY_OR(CleanupOldAutosavesIfNeeded(paths, scratch_arena), {
                ReportError(sentry::Error::Level::Error,
                            HashComptime("autosave cleanup"),
                            "cleanup old autosaves failed: {}",
                            error);
            });
        }
        TRY_OR(CleanupExcessInstanceAutosaves(state, paths, scratch_arena), {
            ReportError(sentry::Error::Level::Error,
                        HashComptime("autosave cleanup"),
                        "cleanup excess autosaves failed: {}",
                        error);
        });
    }
}

bool AutosaveNeeded(AutosaveState const& state) {
    return state.last_save_time.SecondsFromNow() >= k_autosave_interval_seconds;
}

void QueueAutosave(AutosaveState& state, StateSnapshot const& snapshot) {
    state.mutex.Lock();
    DEFER { state.mutex.Unlock(); };
    switch (state.state) {
        case AutosaveState::State::Idle:
        case AutosaveState::State::PendingSave:
            state.snapshot = snapshot;
            state.state = AutosaveState::State::PendingSave;
            break;
        case AutosaveState::State::Saved:
            if (state.snapshot != snapshot) {
                if constexpr (!PRODUCTION_BUILD) {
                    DynamicArrayBounded<char, Kb(4)> diff {};
                    AssignDiffDescription(diff, state.snapshot, snapshot);
                    LogDebug(ModuleName::Main, "Autosave diff: {}", diff);
                }
                state.snapshot = snapshot;
                state.state = AutosaveState::State::PendingSave;
            }
            break;
    }
}

static String TestPresetPath(tests::Tester& tester, String filename) {
    return path::Join(tester.scratch_arena,
                      Array {TestFilesFolder(tester), tests::k_preset_test_files_subdir, filename});
}

TEST_CASE(TestAutosave) {
    AutosaveState state {};
    auto const paths = CreateFloePaths(tester.arena);

    // We need to load some valid state to test autosave.
    auto snapshot = TRY(LoadPresetFile(TestPresetPath(tester, "sine.floe-preset"), tester.scratch_arena));

    InitAutosaveState(state, tester.random_seed, snapshot);

    // We don't need check the result since it's time-based and we don't want to wait in a test.
    AutosaveNeeded(state);

    // main thread
    snapshot.param_values[0] += 1;
    QueueAutosave(state, snapshot);

    // background thread
    AutosaveToFileIfNeeded(state, paths);

    // do it multiple time to check file rotation
    for (auto _ : Range(k_max_autosaves_per_instance + 1)) {
        snapshot.param_values[0] += 1;
        QueueAutosave(state, snapshot);
        AutosaveToFileIfNeeded(state, paths);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterAutosaveTests) { REGISTER_TEST(TestAutosave); }
