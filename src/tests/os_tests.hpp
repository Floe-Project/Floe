// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <time.h>

#include "os/filesystem.hpp"
#include "tests/framework.hpp"

TEST_CASE(TestEpochTime) {
    auto const ns = NanosecondsSinceEpoch();
    auto const t = LocalTimeFromNanosecondsSinceEpoch(ns);

    auto std_time = time(nullptr);
    auto std_local_time = *localtime(&std_time); // NOLINT(concurrency-mt-unsafe)

    auto check_approx = [&](s64 a, s64 b, Optional<s64> wrap_max) {
        auto b_below = b - 1;
        if (wrap_max && b_below < 0) b_below = wrap_max.Value();
        auto b_above = b + 1;
        if (wrap_max && b_above > wrap_max.Value()) b_above = 0;
        CHECK(a == b || a == b_below || a == b_above);
    };

    check_approx(t.year, (std_local_time.tm_year + 1900), {});
    check_approx(t.months_since_jan, std_local_time.tm_mon, 11);
    check_approx(t.day_of_month, std_local_time.tm_mday, 31);
    check_approx(t.hour, std_local_time.tm_hour, 23);
    check_approx(t.minute, std_local_time.tm_min, 59);
    check_approx(t.second, std_local_time.tm_sec, 59);

    return k_success;
}

TEST_CASE(TestFileApi) {
    auto& scratch_arena = tester.scratch_arena;
    auto const filename = path::Join(scratch_arena, Array {tests::TempFolder(tester), "newfile.txt"});
    auto const binary_filename = path::Join(scratch_arena, Array {tests::TempFolder(tester), "binfile.dat"});
    constexpr auto k_file_contents = "hello friends\n"_s;

    SUBCASE("Create a file") {
        auto f = OpenFile(filename, FileMode::Write);
        REQUIRE(f.HasValue());
        auto _ = f.Value().Write(k_file_contents.ToByteSpan());
    }

    SUBCASE("Move a file") {
        auto f = OpenFile(filename, FileMode::Read);
        auto f2 = Move(f);
    }

    struct Foo {
        f32 f1, f2;
    };
    Foo const foo {2, 4};

    SUBCASE("Binary file") {
        SUBCASE("Create and write to a binary file") {
            auto f = OpenFile(binary_filename, FileMode::Write);
            REQUIRE(f.HasValue());
            auto const num_written = f.Value().Write(AsBytes(foo));
            REQUIRE(num_written.HasValue());
            REQUIRE(num_written.Value() == sizeof(Foo));
        }

        SUBCASE("Read binary file") {
            auto f = OpenFile(binary_filename, FileMode::Read);
            REQUIRE(f.HasValue());
            Foo result {};
            auto read_o = f.Value().Read(&result, sizeof(result));
            REQUIRE(read_o.HasValue());
        }
    }

    SUBCASE("Read from one large file and write to another") {
        constexpr String k_str = "filedata\n";
        DynamicArray<char> file_data {tester.scratch_arena};
        constexpr usize k_one_hundred_mb {1024 * 1024 * 100};
        file_data.Reserve(k_one_hundred_mb);
        for (auto _ : Range(1000000))
            dyn::AppendSpan(file_data, k_str);
        auto const big_file_name =
            path::Join(scratch_arena, Array {tests::TempFolder(tester), "big file.txt"});
        if (WriteFile(big_file_name, file_data.Items().ToByteSpan()).HasValue()) {
            if (auto f = OpenFile("big file.txt", FileMode::Read); f.HasValue()) {
                usize const offset = k_str.size * 100;
                String const section = String(file_data).SubSpan(offset, k_str.size * 900000);
                constexpr String k_out_file_name {"section of big file.txt"};
                if (ReadSectionOfFileAndWriteToOtherFile(f.Value(), offset, section.size, k_out_file_name)
                        .Succeeded()) {
                    if (auto const resulting_file_data =
                            ReadEntireFile(k_out_file_name, tester.scratch_arena);
                        resulting_file_data.HasValue()) {
                        tester.log.DebugLn("Running Read from one large file and write to another");
                        REQUIRE(resulting_file_data.Value().size == section.size);
                        REQUIRE(MemoryIsEqual(resulting_file_data.Value().data, section.data, section.size));
                    }
                }
            }
        }
    }

    SUBCASE("Read and write binary array") {
        Array<int, 5> const data {1, 2, 4, 5, 6};
        auto const array_filename =
            path::Join(scratch_arena, Array {tests::TempFolder(tester), "array_binary.dat"});

        auto o = WriteFile(array_filename, data.Items().ToByteSpan());
        REQUIRE(o.HasValue());
        REQUIRE(o.Value() == (data.size * (sizeof(*data.data))));

        auto f = ReadEntireFile(array_filename, tester.scratch_arena);
        REQUIRE(f.HasValue());
    }

    SUBCASE("Opening a file") {
        auto f = OpenFile(filename, FileMode::Read);
        REQUIRE(f.HasValue());
        auto file_data = f.Value().ReadWholeFile(tester.scratch_arena);
        REQUIRE(file_data.HasValue());
        CAPTURE(file_data.Value().size);
        auto const data = String {file_data.Value().data, file_data.Value().size};
        REQUIRE(data == k_file_contents);
    }

    SUBCASE("Try opening a file that does not exist") {
        auto const f = OpenFile("foo", FileMode::Read);
        REQUIRE(f.HasError());
    }

    SUBCASE("Try reading an entire file that does not exist") {
        auto const data = ReadEntireFile("foo", tester.scratch_arena);
        REQUIRE(data.HasError());
    }
    return k_success;
}

TEST_CASE(TestFilesystem) {
    auto& a = tester.scratch_arena;

    SUBCASE("DirectoryIterator") {
        SUBCASE("on an empty dir") {
            auto temp_dir =
                DynamicArray<char>::FromOwnedSpan(TRY(KnownDirectory(a, KnownDirectories::Temporary)), a);
            path::JoinAppend(temp_dir, "empty_dir"_s);

            TRY(CreateDirectory(temp_dir, {.create_intermediate_directories = true}));
            DEFER {
                if (auto o = Delete(temp_dir, {.type = DeleteOptions::Type::DirectoryOnlyIfEmpty});
                    o.HasError())
                    LOG_WARNING("failed to delete temp dir: {}", o.Error());
            };

            auto o = DirectoryIterator::Create(a, temp_dir);
            REQUIRE(o.HasValue());
            REQUIRE(!o.Value().HasMoreFiles());
        }

        SUBCASE("basic actions") {
            if (auto o = ConvertToAbsolutePath(a, "."); o.HasValue()) {
                auto& dir = o.Value();

                SUBCASE("all files") {
                    if (auto it_outcome = DirectoryIterator::Create(a, dir); it_outcome.HasValue()) {
                        auto& it = it_outcome.Value();
                        while (it.HasMoreFiles()) {
                            auto const& entry = it.Get();
                            REQUIRE(entry.path.size);
                            tester.log.DebugLn("directory entry: {}, type: {}", entry.path, (int)entry.type);

                            if (auto inc_outcome = it.Increment(); inc_outcome.HasError()) {
                                LOG_WARNING("failed to increment DirectoryIterator for reason {}",
                                            inc_outcome.Error());
                                break;
                            }
                        }
                    } else {
                        LOG_WARNING("failed to create DirectoryIterator at dir {} for reason {}",
                                    dir,
                                    it_outcome.Error());
                    }
                }

                SUBCASE("no files matching pattern") {
                    auto it = DirectoryIterator::Create(a,
                                                        dir,
                                                        {
                                                            .wildcard = "sef9823ksdjf39s*",
                                                            .get_file_size = false,
                                                        });
                    REQUIRE(it.HasValue());
                    REQUIRE(!it.Value().HasMoreFiles());
                }

                SUBCASE("non existent dir") {
                    REQUIRE(DirectoryIterator::Create(a,
                                                      "C:/seflskflks"_s,
                                                      {
                                                          .wildcard = "*",
                                                          .get_file_size = false,
                                                      })
                                .HasError());
                }

            } else {
                LOG_WARNING("Could not convert to abs path: {}", o.Error());
            }
        }
    }

    SUBCASE("String") {
        auto check = [&](String str, bool expecting_success) {
            CAPTURE(str);
            CAPTURE(expecting_success);
            auto o = ConvertToAbsolutePath(a, str);
            if (!expecting_success) {
                REQUIRE(o.HasError());
                return;
            }
            REQUIRE(o.HasValue());
            tester.log.DebugLn(o.Value());
            REQUIRE(path::IsAbsolute(o.Value()));
        };

        check("foo", true);
        check("something/foo.bar", true);
        check("/something/foo.bar", true);
    }

    SUBCASE("KnownDirectory") {
        for (auto const i : Range(ToInt(KnownDirectories::Count))) {
            auto type = (KnownDirectories)i;
            auto known_folder = KnownDirectory(a, type);
            String type_name;
            switch (type) {
                case KnownDirectories::Temporary: type_name = "Temporary"; break;
                case KnownDirectories::Logs: type_name = "Logs"; break;
                case KnownDirectories::Prefs: type_name = "Prefs"; break;
                case KnownDirectories::AllUsersData: type_name = "AllUsersData"; break;
                case KnownDirectories::Documents: type_name = "Documents"; break;
                case KnownDirectories::Data: type_name = "Data"; break;
                case KnownDirectories::PluginSettings: type_name = "PluginSettings"; break;
                case KnownDirectories::AllUsersSettings: type_name = "AllUsersSettings"; break;
                case KnownDirectories::Downloads: type_name = "Downloads"; break;
                case KnownDirectories::ClapPlugin: type_name = "Clap plugin"; break;
                case KnownDirectories::Vst3Plugin: type_name = "VST3 plugin"; break;
                case KnownDirectories::Count: PanicIfReached();
            }
            if (known_folder.HasValue()) {
                DEFER { a.Free(known_folder.Value().ToByteSpan()); };
                tester.log.DebugLn("Found {} dir: {} ", type_name, known_folder.Value());
                CHECK(path::IsAbsolute(known_folder.Value()));
            } else {
                LOG_WARNING("Error trying to find {} dir: {}", type_name, known_folder.Error());
            }
        }
    }

    SUBCASE("DeleteDirectory") {
        auto test_delete_directory = [&a, &tester]() -> ErrorCodeOr<void> {
            auto const dir = TRY(KnownDirectoryWithSubdirectories(a,
                                                                  KnownDirectories::Temporary,
                                                                  Array {"test"_s, "framework_dir"}));
            TRY(CreateDirectory(dir, {.create_intermediate_directories = true}));

            // create files and folders within the dir
            {
                DynamicArray<char> file = {dir, a};
                path::JoinAppend(file, "test_file1.txt"_s);
                TRY(WriteFile(file, "data"_s.ToByteSpan()));

                dyn::Resize(file, dir.size);
                path::JoinAppend(file, "test_file2.txt"_s);
                TRY(WriteFile(file, "data"_s.ToByteSpan()));

                dyn::Resize(file, dir.size);
                path::JoinAppend(file, "folder"_s);
                TRY(CreateDirectory(file));
            }

            TRY(Delete(dir, {}));
            CHECK(GetFileType(dir).HasError());
            return k_success;
        };

        if (auto o = test_delete_directory(); o.HasError())
            LOG_WARNING("Failed to test DeleteDirectory: {}", o.Error());
    }

    SUBCASE("CreateDirectory") {
        auto const dir = path::Join(a, Array {tests::TempFolder(tester), "CreateDirectory test"});
        TRY(CreateDirectory(dir, {.create_intermediate_directories = false}));
        CHECK(TRY(GetFileType(dir)) == FileType::Directory);
        TRY(Delete(dir, {}));
        return k_success;
    }

    SUBCASE("MoveDirectoryContents") {
        auto const temp_dir_path =
            path::Join(a, Array {tests::TempFolder(tester), "MoveDirectoryContents test"});
        auto _ = Delete(temp_dir_path, {});
        TRY(CreateDirectory(temp_dir_path));

        auto make_path = [&](Span<String const> sub_paths) {
            DynamicArray<String> parts {a};
            dyn::Append(parts, temp_dir_path);
            dyn::AppendSpan(parts, sub_paths);
            return String(path::Join(a, parts));
        };

        auto make_dir = [&](Span<String const> name) -> ErrorCodeOr<String> {
            auto const dir = make_path(name);
            TRY(CreateDirectory(dir, {.create_intermediate_directories = true}));
            return dir;
        };

        auto make_file_containing_its_own_subpath = [&](Span<String const> path) -> ErrorCodeOr<String> {
            auto const f = make_path(path);
            TRY(WriteFile(f, path::Join(a, path)));
            return f;
        };

        auto const dir1 = TRY(make_dir(Array {"dir1"_s}));
        TRY(make_dir(Array {"dir1"_s, "subdir"}));
        TRY(make_file_containing_its_own_subpath(Array {"dir1"_s, "file1"}));
        TRY(make_file_containing_its_own_subpath(Array {"dir1"_s, "subdir"_s, "file2"}));
        TRY(make_file_containing_its_own_subpath(Array {"dir1"_s, "subdir"_s, "file3"}));

        auto const dir2 = TRY(make_dir(Array {"dir2"_s}));
        auto const dir2_file1 = TRY(make_file_containing_its_own_subpath(Array {"dir2"_s, "file1"}));

        auto check_dest_contents = [&]() -> ErrorCodeOr<void> {
            DynamicArray<String> existing_files {a};
            dyn::Append(existing_files, make_path(Array {"dir2"_s, "file1"}));
            dyn::Append(existing_files, make_path(Array {"dir2"_s, "subdir", "file2"}));
            dyn::Append(existing_files, make_path(Array {"dir2"_s, "subdir", "file3"}));

            REQUIRE(TRY(GetFileType(dir2)) == FileType::Directory);
            REQUIRE(TRY(GetFileType(existing_files[0])) == FileType::File);
            REQUIRE(TRY(GetFileType(make_path(Array {"dir2"_s, "subdir"}))) == FileType::Directory);
            REQUIRE(TRY(GetFileType(existing_files[1])) == FileType::File);
            REQUIRE(TRY(GetFileType(existing_files[2])) == FileType::File);

            for (auto& f : TRY(GetFilesRecursive(a, dir2))) {
                auto const file_type = TRY(GetFileType(f));
                if (file_type == FileType::File) {
                    bool found = false;
                    for (auto& existing : existing_files)
                        if (path::Equal(existing, f)) found = true;
                    CAPTURE(f);
                    REQUIRE(found);
                }
            }
            return k_success;
        };

        SUBCASE("fails when moving into existing dir") {
            auto o = MoveDirectoryContents(dir1, dir2, ExistingDestinationHandling::Fail);
            REQUIRE(o.HasError());
            REQUIRE(o.Error() == FilesystemError::PathAlreadyExists);
        }

        SUBCASE("move into empty destination succeeds") {
            TRY(Delete(dir2_file1, {}));
            REQUIRE(GetFileType(dir2_file1).HasError());
            auto o = MoveDirectoryContents(dir1, dir2, ExistingDestinationHandling::Fail);
            REQUIRE(!o.HasError());
            TRY(check_dest_contents());
        }

        SUBCASE("move while overwriting files succeeds") {
            auto o = MoveDirectoryContents(dir1, dir2, ExistingDestinationHandling::Overwrite);
            REQUIRE(!o.HasError());

            auto const data = TRY(ReadEntireFile(dir2_file1, a));
            REQUIRE(path::Equal(data, "dir1/file1"_s));
            TRY(check_dest_contents());
        }

        SUBCASE("move while skipping files succeeds") {
            auto o = MoveDirectoryContents(dir1, dir2, ExistingDestinationHandling::Skip);
            REQUIRE(!o.HasError());

            auto const data = TRY(ReadEntireFile(dir2_file1, a));
            REQUIRE(path::Equal(data, "dir2/file1"_s));
            TRY(check_dest_contents());
        }

        SUBCASE("moving into non-existent destination fails") {
#if _WIN32
            auto const made_up_dir = "C:/skkfjhsef"_s;
#else
            auto const made_up_dir = "/Users/samwindell/skfeskhfj"_s;
#endif
            auto o = MoveDirectoryContents(made_up_dir, dir2, ExistingDestinationHandling::Skip);
            REQUIRE(o.HasError());
        }

        SUBCASE("moving into a existing file fails") {
            auto o = MoveDirectoryContents(dir1, dir2_file1, ExistingDestinationHandling::Skip);
            REQUIRE(o.HasError());
        }
    }
    return k_success;
}

TEST_CASE(TestDirectoryWatcher) {
    auto& a = tester.scratch_arena;

    for (auto const recursive : Array {true, false}) {
        CAPTURE(recursive);

        auto const dir = (String)path::Join(a, Array {tests::TempFolder(tester), "directory-watcher-test"});
        auto _ =
            Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively, .fail_if_not_exists = false});
        TRY(CreateDirectory(dir, {.create_intermediate_directories = false, .fail_if_exists = true}));

        struct TestPath {
            static TestPath Create(ArenaAllocator& a, String root_dir, String subpath) {
                auto const full = path::Join(a, Array {root_dir, subpath});
                return TestPath {
                    .full_path = full,
                    .subpath = full.SubSpan(full.size - subpath.size, subpath.size),
                };
            }
            String full_path;
            String subpath;
        };

        auto const file = TestPath::Create(a, dir, "file1.txt");
        TRY(WriteFile(file.full_path, "data"));

        auto const subdir = TestPath::Create(a, dir, "subdir");
        TRY(CreateDirectory(subdir.full_path,
                            {.create_intermediate_directories = false, .fail_if_exists = true}));

        auto const subfile = TestPath::Create(a, dir, path::Join(a, Array {subdir.subpath, "file2.txt"}));
        TRY(WriteFile(subfile.full_path, "data"));

        auto watcher = TRY(CreateDirectoryWatcher(a));
        DEFER { DestoryDirectoryWatcher(watcher); };

        auto const dirs_to_watch = Array {DirectoryToWatch {
            .path = dir,
            .recursive = recursive,
        }};
        auto const args = PollDirectoryChangesArgs {
            .dirs_to_watch = dirs_to_watch,
            .retry_failed_directories = false,
            .result_arena = a,
            .scratch_arena = a,
        };

        if (auto const dir_changes_span = TRY(PollDirectoryChanges(watcher, args)); dir_changes_span.size) {
            tester.log.DebugLn("Unexpected result");
            for (auto const& dir_changes : dir_changes_span) {
                tester.log.DebugLn("  {}", dir_changes.linked_dir_to_watch->path);
                tester.log.DebugLn("  {}", dir_changes.error);
                for (auto const& subpath_changeset : dir_changes.subpath_changesets)
                    tester.log.DebugLn("    {} {}",
                                       subpath_changeset.subpath,
                                       DirectoryWatcher::ChangeType::ToString(subpath_changeset.changes));
            }
            REQUIRE(false);
        }

        auto check = [&](Span<DirectoryWatcher::DirectoryChanges::Change const> expected_changes)
            -> ErrorCodeOr<void> {
            auto found_expected = a.NewMultiple<bool>(expected_changes.size);

            // we give the watcher some time and a few attempts to detect the changes
            for (auto const _ : Range(100)) {
                SleepThisThread(2);
                auto const directory_changes_span = TRY(PollDirectoryChanges(watcher, args));

                for (auto const& directory_changes : directory_changes_span) {
                    auto const& path = directory_changes.linked_dir_to_watch->path;

                    CHECK(path::Equal(path, dir));
                    if (directory_changes.error) {
                        tester.log.DebugLn("Error in {}: {}", path, *directory_changes.error);
                        continue;
                    }
                    CHECK(!directory_changes.error.HasValue());

                    for (auto const& subpath_changeset : directory_changes.subpath_changesets) {
                        if (subpath_changeset.changes & DirectoryWatcher::ChangeType::ManualRescanNeeded) {
                            tester.log.ErrorLn("Manual rescan needed for {}", path);
                            continue;
                        }

                        bool was_expected = false;
                        for (auto const [index, expected] : Enumerate(expected_changes)) {
                            if (path::Equal(subpath_changeset.subpath, expected.subpath) &&
                                (!subpath_changeset.file_type.HasValue() ||
                                 subpath_changeset.file_type.Value() == expected.file_type)) {
                                if (expected.changes & subpath_changeset.changes) {
                                    was_expected = true;
                                    found_expected[index] = true;
                                    break;
                                }
                            }
                        }

                        tester.log.DebugLn("{} change: \"{}\" {{ {} }} in \"{}\"",
                                           was_expected ? "Expected" : "Unexpected",
                                           subpath_changeset.subpath,
                                           DirectoryWatcher::ChangeType::ToString(subpath_changeset.changes),
                                           path);
                    }
                }

                if (ContainsOnly(found_expected, true)) break;
            }

            for (auto const [index, expected] : Enumerate(expected_changes)) {
                CAPTURE(expected.subpath);
                CAPTURE(DirectoryWatcher::ChangeType::ToString(expected.changes));
                if (!found_expected[index]) {
                    tester.log.DebugLn("Expected change not found: {} {}",
                                       expected.subpath,
                                       DirectoryWatcher::ChangeType::ToString(expected.changes));
                }
                CHECK(found_expected[index]);
            }

            return k_success;
        };

        SUBCASE(recursive ? "recursive"_s : "non-recursive"_s) {
            SUBCASE("delete is detected") {
                TRY(Delete(file.full_path, {}));
                TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                    file.subpath,
                    FileType::File,
                    DirectoryWatcher::ChangeType::Deleted,
                }}));
            }

            SUBCASE("modify is detected") {
                TRY(WriteFile(file.full_path, "new data"));
                TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                    file.subpath,
                    FileType::File,
                    DirectoryWatcher::ChangeType::Modified,
                }}));
            }

            SUBCASE("rename is detected") {
                auto const new_file = TestPath::Create(a, dir, "file1_renamed.txt");
                TRY(MoveFile(file.full_path, new_file.full_path, ExistingDestinationHandling::Fail));
                TRY(check(Array {
                    DirectoryWatcher::DirectoryChanges::Change {
                        file.subpath,
                        FileType::File,
                        IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                 : DirectoryWatcher::ChangeType::RenamedOldName,
                    },
                    DirectoryWatcher::DirectoryChanges::Change {
                        new_file.subpath,
                        FileType::File,
                        IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                 : DirectoryWatcher::ChangeType::RenamedNewName,
                    },
                }));
            }

            // On Windows, the root folder does not receive events
            if constexpr (!IS_WINDOWS) {
                SUBCASE("deleting root is detected") {
                    auto const delete_outcome =
                        Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively});
                    if (!delete_outcome.HasError()) {
                        auto args2 = args;
                        bool found_delete_self = false;
                        for (auto const _ : Range(4)) {
                            SleepThisThread(5);
                            auto const directory_changes_span = TRY(PollDirectoryChanges(watcher, args2));
                            for (auto const& directory_changes : directory_changes_span) {
                                for (auto const& subpath_changeset : directory_changes.subpath_changesets) {
                                    if (subpath_changeset.subpath.size == 0 &&
                                        subpath_changeset.changes & DirectoryWatcher::ChangeType::Deleted) {
                                        CHECK(subpath_changeset.file_type == FileType::Directory);
                                        found_delete_self = true;
                                        args2.dirs_to_watch = {};
                                        break;
                                    }
                                }
                            }
                            if (found_delete_self) break;
                        }
                        CHECK(found_delete_self);
                    } else {
                        tester.log.DebugLn(
                            "Failed to delete root watched dir: {}. This is probably normal behaviour",
                            delete_outcome.Error());
                    }
                }
            }

            SUBCASE("no crash moving root dir") {
                auto const dir_name = fmt::Format(a, "{}-moved", dir);
                auto const move_outcome = MoveFile(dir, dir_name, ExistingDestinationHandling::Fail);
                if (!move_outcome.HasError()) {
                    DEFER { auto _ = Delete(dir_name, {.type = DeleteOptions::Type::DirectoryRecursively}); };
                    // On Linux, we don't get any events. Perhaps a MOVE only triggers when the underlying
                    // file object really moves and perhaps a rename like this doesn't do that. Either way I
                    // think we just need to check nothing bad happens in this case and that will do.
                } else {
                    tester.log.DebugLn(
                        "Failed to move root watched dir: {}. This is probably normal behaviour",
                        move_outcome.Error());
                }
            }

            // Wine seems to have trouble with recursive watching
            static auto const recursive_supported = !IsRunningUnderWine();

            if (recursive && recursive_supported) {
                SUBCASE("delete in subfolder is detected") {
                    TRY(Delete(subfile.full_path, {}));
                    TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                        subfile.subpath,
                        FileType::File,
                        DirectoryWatcher::ChangeType::Deleted,
                    }}));
                }

                SUBCASE("modify is detected") {
                    TRY(WriteFile(subfile.full_path, "new data"));
                    TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                        subfile.subpath,
                        FileType::File,
                        DirectoryWatcher::ChangeType::Modified,
                    }}));
                }

                SUBCASE("rename is detected") {
                    auto const new_subfile =
                        TestPath::Create(a, dir, path::Join(a, Array {subdir.subpath, "file2_renamed.txt"}));
                    TRY(MoveFile(subfile.full_path,
                                 new_subfile.full_path,
                                 ExistingDestinationHandling::Fail));
                    TRY(check(Array {
                        DirectoryWatcher::DirectoryChanges::Change {
                            subfile.subpath,
                            FileType::File,
                            IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                     : DirectoryWatcher::ChangeType::RenamedOldName,
                        },
                        DirectoryWatcher::DirectoryChanges::Change {
                            new_subfile.subpath,
                            FileType::File,
                            IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                     : DirectoryWatcher::ChangeType::RenamedNewName,
                        },
                    }));
                }

                SUBCASE("deleting subfolder is detected") {
                    TRY(Delete(subdir.full_path, {.type = DeleteOptions::Type::DirectoryRecursively}));
                    TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                        subdir.subpath,
                        FileType::Directory,
                        DirectoryWatcher::ChangeType::Deleted,
                    }}));
                }

                SUBCASE("newly created subfolder is watched") {
                    // create a new subdir
                    auto const subdir2 = TestPath::Create(a, dir, "subdir2");
                    TRY(CreateDirectory(subdir2.full_path,
                                        {.create_intermediate_directories = false, .fail_if_exists = true}));

                    // create a file within it
                    auto const subfile2 =
                        TestPath::Create(a, dir, path::Join(a, Array {subdir2.subpath, "file2.txt"}));
                    TRY(WriteFile(subfile2.full_path, "data"));

                    if constexpr (IS_WINDOWS) {
                        // Windows doesn't seem to give us the subdir2 'added' event
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    } else {
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subdir2.subpath,
                                FileType::Directory,
                                DirectoryWatcher::ChangeType::Added,
                            },
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    }
                }

                SUBCASE("moved subfolder is still watched") {
                    auto const subdir_moved = TestPath::Create(a, dir, "subdir-moved");
                    TRY(MoveFile(subdir.full_path,
                                 subdir_moved.full_path,
                                 ExistingDestinationHandling::Fail));

                    auto const subfile2 =
                        TestPath::Create(a,
                                         dir,
                                         path::Join(a, Array {subdir_moved.subpath, "file-in-moved.txt"}));
                    TRY(WriteFile(subfile2.full_path, "data"));

                    if constexpr (IS_WINDOWS) {
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    } else {
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subdir.subpath,
                                FileType::Directory,
                                IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                         : DirectoryWatcher::ChangeType::RenamedOldName,
                            },
                            DirectoryWatcher::DirectoryChanges::Change {
                                subdir_moved.subpath,
                                FileType::Directory,
                                IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                         : DirectoryWatcher::ChangeType::RenamedNewName,
                            },
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    }
                }
            } else {
                SUBCASE("delete in subfolder is not detected") {
                    TRY(Delete(subfile.full_path, {}));

                    for (auto const _ : Range(2)) {
                        SleepThisThread(2);
                        auto const directory_changes_span = TRY(PollDirectoryChanges(watcher, args));
                        for (auto const& directory_changes : directory_changes_span)
                            for (auto const& subpath_changeset : directory_changes.subpath_changesets)
                                CHECK(!path::Equal(subpath_changeset.subpath, subfile.subpath));
                    }
                }
            }
        }
    }

    return k_success;
}

TEST_CASE(TestDirectoryWatcherErrors) {
    auto& a = tester.scratch_arena;

    auto const dir =
        (String)path::Join(a, Array {tests::TempFolder(tester), "directory-watcher-errors-test"});

    auto watcher = TRY(CreateDirectoryWatcher(a));
    DEFER { DestoryDirectoryWatcher(watcher); };

    {
        auto const outcome = PollDirectoryChanges(watcher,
                                                  PollDirectoryChangesArgs {
                                                      .dirs_to_watch = Array {DirectoryToWatch {
                                                          .path = dir,
                                                          .recursive = false,
                                                      }},
                                                      .retry_failed_directories = false,
                                                      .result_arena = a,
                                                      .scratch_arena = a,
                                                  });

        // we're not expecting a top-level error, that should only be for if the whole watching system fails
        REQUIRE(outcome.HasValue());

        auto const directory_changes_span = outcome.Value();
        REQUIRE_EQ(directory_changes_span.size, 1u);
        auto const& directory_changes = directory_changes_span[0];
        REQUIRE(directory_changes.error.HasValue());
        CHECK(directory_changes.error.Value() == FilesystemError::PathDoesNotExist);
    }

    // retrying should not repeat the error unless retry_failed_directories is set
    {
        auto const outcome = PollDirectoryChanges(watcher,
                                                  PollDirectoryChangesArgs {
                                                      .dirs_to_watch = Array {DirectoryToWatch {
                                                          .path = dir,
                                                          .recursive = false,
                                                      }},
                                                      .retry_failed_directories = false,
                                                      .result_arena = a,
                                                      .scratch_arena = a,
                                                  });

        CHECK(outcome.HasValue());
        CHECK(outcome.Value().size == 0);
    }

    // the error should repeat if retry_failed_directories is set
    {
        auto const outcome = PollDirectoryChanges(watcher,
                                                  PollDirectoryChangesArgs {
                                                      .dirs_to_watch = Array {DirectoryToWatch {
                                                          .path = dir,
                                                          .recursive = false,
                                                      }},
                                                      .retry_failed_directories = true,
                                                      .result_arena = a,
                                                      .scratch_arena = a,
                                                  });

        CHECK(outcome.HasValue());
        auto const directory_changes_span = outcome.Value();
        REQUIRE_EQ(directory_changes_span.size, 1u);
        auto const& directory_changes = directory_changes_span[0];
        REQUIRE(directory_changes.error.HasValue());
        CHECK(directory_changes.error.Value() == FilesystemError::PathDoesNotExist);
    }

    return k_success;
}

TEST_CASE(TestTimePoint) {
    Stopwatch const sw;

    auto t1 = TimePoint::Now();
    SleepThisThread(1);
    REQUIRE(t1.Raw());
    auto t2 = TimePoint::Now();

    auto us = SecondsToMicroseconds(t2 - t1);
    REQUIRE(us >= 0.0);
    REQUIRE(ApproxEqual(SecondsToMilliseconds(t2 - t1), us / 1000.0, 0.1));
    REQUIRE(ApproxEqual(t2 - t1, us / (1000.0 * 1000.0), 0.1));

    tester.log.DebugLn("Time has passed: {}", sw);
    return k_success;
}

TEST_CASE(TestMutex) {
    Mutex m;
    m.Lock();
    m.TryLock();
    m.Unlock();
    return k_success;
}

TEST_CASE(TestFutex) {
    for (auto wake_mode : Array {NumWaitingThreads::One, NumWaitingThreads::All}) {
        Atomic<u32> atomic {0};

        Thread thread;
        thread.Start(
            [&]() {
                SleepThisThread(1);
                atomic.Store(1, StoreMemoryOrder::Relaxed);
                WakeWaitingThreads(atomic, wake_mode);
            },
            "thread");

        while (atomic.Load(LoadMemoryOrder::Relaxed) == 1)
            WaitIfValueIsExpected(atomic, 1, {});

        thread.Join();
    }

    {
        Atomic<u32> atomic {0};
        CHECK_EQ(WaitIfValueIsExpected(atomic, 0, 1u), WaitResult::TimedOut);
    }
    return k_success;
}

int g_global_int = 0;

TEST_CASE(TestThread) {
    Thread thread;
    REQUIRE(!thread.Joinable());

    thread.Start(
        []() {
            g_global_int = 1;
            SleepThisThread(1);
        },
        "test thread");

    REQUIRE(thread.Joinable());
    thread.Join();

    REQUIRE(g_global_int == 1);
    return k_success;
}

TEST_REGISTRATION(RegisterOsTests) {
    REGISTER_TEST(TestEpochTime);
    REGISTER_TEST(TestThread);
    REGISTER_TEST(TestFutex);
    REGISTER_TEST(TestMutex);
    REGISTER_TEST(TestFilesystem);
    REGISTER_TEST(TestFileApi);
    REGISTER_TEST(TestDirectoryWatcher);
    REGISTER_TEST(TestDirectoryWatcherErrors);
    REGISTER_TEST(TestFileApi);
    REGISTER_TEST(TestTimePoint);
}
