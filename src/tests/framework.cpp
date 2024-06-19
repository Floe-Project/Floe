// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "framework.hpp"

#include "foundation/memory/allocators.hpp"
#include "foundation/utils/format.hpp"
#include "foundation/utils/path.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

namespace tests {

struct TestFailed {};

void TestLogger::LogFunction(String str, LogLevel level, bool add_newline) {
    ArenaAllocatorWithInlineStorage<1000> arena;
    DynamicArray<char> buf {arena};
    if (add_newline && tester.current_test_case) fmt::Append(buf, "[ {} ] ", tester.current_test_case->title);
    if (level == LogLevel::Error) dyn::AppendSpan(buf, ANSI_COLOUR_SET_FOREGROUND_RED);
    fmt::Append(buf, "{}", str);
    if (level == LogLevel::Error) dyn::AppendSpan(buf, ANSI_COLOUR_RESET);
    if (add_newline) dyn::Append(buf, '\n');
    StdPrint(StdStream::Err, buf);
}

void RegisterTest(Tester& tester, TestFunction f, String title) {
    dyn::Append(tester.test_cases, TestCase {f, title});
}

String TempFolder(Tester& tester) {
    if (!tester.test_output_folder) {
        tester.test_output_folder = ({
            auto const o = KnownDirectoryWithSubdirectories(tester.arena,
                                                            KnownDirectories::Temporary,
                                                            Array {"Floe"_s, "tests"});
            if (o.HasError()) {
                Check(tester,
                      false,
                      fmt::Format(tester.scratch_arena, "failed to get tests output dir: {}", o.Error()),
                      FailureAction::FailAndExitTest);
                return {"ERROR"};
            }
            o.Value();
        });

        StdPrint(StdStream::Err,
                 fmt::Format(tester.scratch_arena, "Test output folder: {}\n", *tester.test_output_folder));
    }
    return *tester.test_output_folder;
}

static Optional<String> SearchUpwardsFromExeForFolder(Tester& tester, String folder_name) {
    auto const path_outcome = CurrentExecutablePath(tester.scratch_arena);
    if (path_outcome.HasError()) {
        tester.log.ErrorLn("failed to get the current exe path: {}", path_outcome.Error());
        return nullopt;
    }

    auto dir = String(path_outcome.Value());
    DynamicArray<char> buf {dir, tester.scratch_arena};

    constexpr usize k_max_folder_heirarchy = 20;
    for (auto _ : Range(k_max_folder_heirarchy)) {
        auto const opt_dir = path::Directory(dir);
        if (!opt_dir.HasValue()) break;
        ASSERT(dir.size != opt_dir->size);
        dir = *opt_dir;

        dyn::Resize(buf, dir.size);
        path::JoinAppend(buf, folder_name);
        if (auto const o = GetFileType(buf); o.HasValue() && o.Value() == FileType::Directory)
            return tester.arena.Clone(buf);
    }

    tester.log.ErrorLn("cannot find {} folder", folder_name);
    return nullopt;
}

String TestFilesFolder(Tester& tester) {
    if (!tester.test_files_folder) {
        auto const opt_folder = SearchUpwardsFromExeForFolder(tester, "test_files");
        if (!opt_folder) {
            Check(tester, false, "failed to find test_files folder", FailureAction::FailAndExitTest);
            tester.test_files_folder = "ERROR";
        } else {
            tester.test_files_folder = *opt_folder;
        }
    }
    return *tester.test_files_folder;
}

Optional<String> BuildResourcesFolder(Tester& tester) {
    if (!tester.build_resources_folder)
        tester.build_resources_folder.Emplace(
            SearchUpwardsFromExeForFolder(tester, k_build_resources_subdir));
    return *tester.build_resources_folder;
}

void* CreateOrFetchFixturePointer(Tester& tester,
                                  CreateFixturePointer create,
                                  DeleteFixturePointer delete_fixture) {
    if (!tester.fixture_pointer) {
        tester.fixture_pointer = create(tester.fixture_arena, tester);
        ASSERT(tester.fixture_pointer, "create function should return a fixture");
    }
    if (!tester.delete_fixture) tester.delete_fixture = delete_fixture;
    return tester.fixture_pointer;
}

int RunAllTests(Tester& tester, Optional<String> filter_pattern) {
    cli_out.InfoLn("Running tests ...");
    Stopwatch const overall_stopwatch;

    for (auto& test_case : tester.test_cases) {
        if (filter_pattern && !MatchWildcard(*filter_pattern, test_case.title)) continue;

        tester.current_test_case = &test_case;
        tester.log.DebugLn("Running ...");

        tester.subcases_passed.Clear();
        tester.fixture_pointer = nullptr;
        tester.delete_fixture = nullptr;
        tester.fixture_arena.ResetCursorAndConsolidateRegions();

        Stopwatch const stopwatch;

        bool run_test = true;
        do {
            tester.scratch_arena.ResetCursorAndConsolidateRegions();
            tester.should_reenter = false;
            tester.subcases_current_max_level = 0;
            dyn::Clear(tester.subcases_stack);

            try {
                auto const result = test_case.f(tester);
                if (result.outcome.HasError()) {
                    tester.should_reenter = false;
                    tester.current_test_case->failed = true;
                    tester.log.ErrorLn("Failed: test returned an error:\n{}", result.outcome.Error());
                    if (result.stacktrace.HasValue()) {
                        ASSERT(result.stacktrace.Value().size);
                        auto const str = StacktraceString(result.stacktrace.Value(), tester.scratch_arena);
                        tester.log.InfoLn("Stacktrace:\n{}", str);
                    }
                }
            } catch (TestFailed const& e) {
                (void)e;
            } catch (...) {
                tester.should_reenter = false;
                tester.current_test_case->failed = true;
                tester.log.ErrorLn("Failed: an exception was thrown");
            }

            if (!tester.should_reenter) run_test = false;
        } while (run_test);

        if (tester.delete_fixture) tester.delete_fixture(tester.fixture_pointer, tester.fixture_arena);

        if (!test_case.failed)
            tester.log.DebugLn(ANSI_COLOUR_FOREGROUND_GREEN("Passed") " ({})\n", stopwatch);
        else
            tester.log.ErrorLn("Failed\n");
    }
    tester.current_test_case = nullptr;

    cli_out.InfoLn("Summary");
    cli_out.InfoLn("--------");
    cli_out.InfoLn("Assertions: {}", tester.num_assertions);
    cli_out.InfoLn("Tests: {}", tester.test_cases.size);
    cli_out.InfoLn("Time taken: {.2}s", overall_stopwatch.SecondsElapsed());

    if (tester.num_warnings == 0)
        cli_out.InfoLn("Warnings: " ANSI_COLOUR_FOREGROUND_GREEN("0"));
    else
        cli_out.InfoLn("Warnings: " ANSI_COLOUR_SET_FOREGROUND_RED "{}" ANSI_COLOUR_RESET,
                       tester.num_warnings);

    auto const num_failed = CountIf(tester.test_cases, [](TestCase const& t) { return t.failed; });
    if (num_failed == 0) {
        cli_out.InfoLn("Failed: " ANSI_COLOUR_FOREGROUND_GREEN("0"));
        cli_out.InfoLn("Result: " ANSI_COLOUR_FOREGROUND_GREEN("Success"));
    } else {
        DynamicArray<char> failed_test_names {tester.scratch_arena};
        for (auto& test_case : tester.test_cases) {
            if (test_case.failed) {
                fmt::Append(failed_test_names,
                            " ({}{})",
                            test_case.title,
                            num_failed == 1 ? "" : " and others");
                break;
            }
        }

        cli_out.InfoLn("Failed: " ANSI_COLOUR_SET_FOREGROUND_RED "{}" ANSI_COLOUR_RESET "{}",
                       num_failed,
                       failed_test_names);
        cli_out.InfoLn("Result: " ANSI_COLOUR_SET_FOREGROUND_RED "Failure");
    }

    return num_failed ? 1 : 0;
}

void Check(Tester& tester,
           bool expression,
           String message,
           FailureAction failure_aciton,
           String file,
           int line) {
    ++tester.num_assertions;
    if (!expression) {
        String pretext = "REQUIRE failed";
        if (failure_aciton == FailureAction::FailAndContinue)
            pretext = "CHECK failed";
        else if (failure_aciton == FailureAction::LogWarningAndContinue)
            pretext = "WARNING issued";

        tester.log.ErrorLn("{}: {}", pretext, message);
        tester.log.ErrorLn("  File      {}:{}", file, line);
        for (auto const& s : tester.subcases_stack)
            tester.log.ErrorLn("  SUBCASE   {}", s.name);

        auto capture = tester.capture_buffer.UsedStackData();
        if (capture.size) {
            auto capture_str = String {(char const*)capture.data, capture.size};

            while (auto pos = Find(capture_str, '\n')) {
                String const part {capture_str.data, *pos};
                tester.log.ErrorLn(part);

                capture_str.RemovePrefix(*pos + 1);
            }
            if (capture_str.size) tester.log.ErrorLn(capture_str);
        }

        DumpCurrentStackTraceToStderr();

        if (failure_aciton != FailureAction::LogWarningAndContinue) {
            tester.should_reenter = false;
            tester.current_test_case->failed = true;
        } else {
            ++tester.num_warnings;
        }
        if (failure_aciton == FailureAction::FailAndExitTest) throw TestFailed();
    }
}

Subcase::Subcase(Tester& tester, String name, String file, int line) : tester(tester), entered(false) {
    // if a Subcase on the same level has already been entered
    if (tester.subcases_stack.size < tester.subcases_current_max_level) {
        tester.should_reenter = true;
        return;
    }

    // push the current signature to the stack so we can check if the
    // current stack + the current new subcase have been traversed
    dyn::Append(tester.subcases_stack, SubcaseSignature {name, file, line});
    if (tester.subcases_passed.Contains(tester.subcases_stack)) {
        // pop - revert to previous stack since we've already passed this
        dyn::Pop(tester.subcases_stack);
        return;
    }

    tester.subcases_current_max_level = tester.subcases_stack.size;
    entered = true;

    DynamicArray<char> buf {tester.scratch_arena};
    for (auto const& subcase : tester.subcases_stack) {
        fmt::Append(buf, "\"{}\"", subcase.name);
        if (&subcase != &Last(tester.subcases_stack)) dyn::AppendSpan(buf, " -> ");
    }
    tester.log.DebugLn(buf);
}

Subcase::~Subcase() {
    if (entered) {
        // only mark the subcase stack as passed if no subcases have been skipped
        if (tester.should_reenter == false) tester.subcases_passed.Add(tester.subcases_stack);
        dyn::Pop(tester.subcases_stack);
    }
}

} // namespace tests
