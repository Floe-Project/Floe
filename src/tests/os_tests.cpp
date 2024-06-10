// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "os/filesystem.hpp"
#include "tests/framework.hpp"

TEST_CASE(TestFileApi) {
    auto& scratch_arena = tester.scratch_arena;
    auto const filename = path::Join(scratch_arena, Array {tests::TempFolder(tester), "newfile.txt"});
    auto const binary_filename = path::Join(scratch_arena, Array {tests::TempFolder(tester), "binfile.dat"});
    constexpr auto k_file_contents = "hello friends\n"_s;

    SUBCASE("Create a file") {
        auto f = OpenFile(filename, FileMode::Write);
        REQUIRE(f.HasValue());
        _ = f.Value().Write(k_file_contents.ToByteSpan());
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
        for (_ : Range(1000000))
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
                        DebugLn("Running Read from one large file and write to another");
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
                    auto it = DirectoryIterator::Create(a, dir, "sef9823ksdjf39s*");
                    REQUIRE(it.HasValue());
                    REQUIRE(!it.Value().HasMoreFiles());
                }

                SUBCASE("non existent dir") {
                    REQUIRE(DirectoryIterator::Create(a, "C:/seflskflks"_s, "*").HasError());
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
            const auto dir = TRY(KnownDirectoryWithSubdirectories(a,
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
        _ = Delete(temp_dir_path, {});
        TRY(CreateDirectory(temp_dir_path));

        auto make_path = [&](Span<String const> sub_paths) {
            DynamicArray<String> parts {a};
            dyn::Append(parts, temp_dir_path);
            dyn::AppendSpan(parts, sub_paths);
            return String(path::Join(a, parts));
        };

        auto make_dir = [&](Span<String const> name) -> ErrorCodeOr<String> {
            const auto dir = make_path(name);
            TRY(CreateDirectory(dir, {.create_intermediate_directories = true}));
            return dir;
        };

        auto make_file_containing_its_own_subpath = [&](Span<String const> path) -> ErrorCodeOr<String> {
            const auto f = make_path(path);
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
            REQUIRE(TRY(GetFileType(existing_files[0])) == FileType::RegularFile);
            REQUIRE(TRY(GetFileType(make_path(Array {"dir2"_s, "subdir"}))) == FileType::Directory);
            REQUIRE(TRY(GetFileType(existing_files[1])) == FileType::RegularFile);
            REQUIRE(TRY(GetFileType(existing_files[2])) == FileType::RegularFile);

            for (auto& f : TRY(GetFilesRecursive(a, dir2))) {
                const auto file_type = TRY(GetFileType(f));
                if (file_type == FileType::RegularFile) {
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

TEST_CASE(TestReadingDirectoryChanges) {
    auto& a = tester.scratch_arena;

    auto const dir = (String)path::Join(a, Array {tests::TempFolder(tester), "ReadDirectoryChanges test"});
    _ = Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively, .fail_if_not_exists = false});
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

    for (auto const recursive : Array {true, false}) {
        CAPTURE(recursive);

        auto watcher = TRY(CreateDirectoryWatcher(a));
        DEFER { DestoryDirectoryWatcher(watcher); };

        auto const dirs_to_watch = Array {DirectoryToWatch {
            .path = dir,
            .recursive = recursive,
        }};

        TRY(ReadDirectoryChanges(watcher,
                                 dirs_to_watch,
                                 a,
                                 [&](String path, ErrorCodeOr<DirectoryWatcher::FileChange> change) {
                                     if (change.HasValue())
                                         tester.log.DebugLn("Change in {}", path);
                                     else
                                         tester.log.DebugLn("Error in {}: {}", path, change.Error());
                                     REQUIRE(false);
                                 }));

        struct TestFileChange {
            String subpath;
            DirectoryWatcher::FileChange::Type type;
        };
        auto check = [&](Span<TestFileChange const> expected_changes) -> ErrorCodeOr<void> {
            SleepThisThread(1); // give the watcher time to detect the changes
            DynamicArray<TestFileChange> changes {a};
            TRY(ReadDirectoryChanges(
                watcher,
                dirs_to_watch,
                a,
                [&](String path, ErrorCodeOr<DirectoryWatcher::FileChange> change_outcome) {
                    CHECK(path::Equal(path, dir));
                    const auto change = REQUIRE_UNWRAP(change_outcome);
                    tester.log.DebugLn("Change in {}, type {}, subdir {}", path, change.type, change.subpath);
                    dyn::Append(changes,
                                {
                                    .subpath = a.Clone(change.subpath),
                                    .type = change.type,
                                });
                }));

            CHECK_OP(changes.size, >=, expected_changes.size);
            for (const auto& expected : expected_changes) {
                CAPTURE(expected.subpath);
                CAPTURE(expected.type);
                const auto found = FindIf(changes, [&](const TestFileChange& c) {
                    return c.subpath == expected.subpath && c.type == expected.type;
                });
                CHECK(found);
            }
            if (changes.size != expected_changes.size) {
                tester.log.DebugLn(
                    "ReadDirectoryChanges resulted different changes than expected. Expected:");
                for (const auto& change : expected_changes) {
                    tester.log.DebugLn("  {} {}",
                                       change.subpath,
                                       DirectoryWatcher::FileChange::TypeToString(change.type));
                }
                tester.log.DebugLn("Actual:");
                for (const auto& change : changes) {
                    tester.log.DebugLn("  {} {}",
                                       change.subpath,
                                       DirectoryWatcher::FileChange::TypeToString(change.type));
                }
            }
            return k_success;
        };

        SUBCASE("delete is detected") {
            TRY(Delete(file.full_path, {}));
            TRY(check(Array {TestFileChange {file.subpath, DirectoryWatcher::FileChange::Type::Deleted}}));
        }

        SUBCASE("modify is detected") {
            TRY(WriteFile(file.full_path, "new data"));
            TRY(check(Array {TestFileChange {file.subpath, DirectoryWatcher::FileChange::Type::Modified}}));
        }

        SUBCASE("rename is detected") {
            auto const new_file = TestPath::Create(a, dir, "file1_renamed.txt");
            TRY(MoveFile(file.full_path, new_file.full_path, ExistingDestinationHandling::Fail));
            TRY(check(Array {
                TestFileChange {file.subpath, DirectoryWatcher::FileChange::Type::RenamedOldName},
                TestFileChange {new_file.subpath, DirectoryWatcher::FileChange::Type::RenamedNewName}}));
        }

        if (recursive) {
            SUBCASE("delete in subfolder is detected") {
                TRY(Delete(subfile.full_path, {}));
                TRY(check(
                    Array {TestFileChange {subfile.subpath, DirectoryWatcher::FileChange::Type::Deleted}}));
            }

            SUBCASE("modify is detected") {
                TRY(WriteFile(subfile.full_path, "new data"));
                TRY(check(
                    Array {TestFileChange {subfile.subpath, DirectoryWatcher::FileChange::Type::Modified}}));
            }

            SUBCASE("rename is detected") {
                auto const new_subfile =
                    TestPath::Create(a, dir, path::Join(a, Array {subdir.subpath, "file2_renamed.txt"}));
                TRY(MoveFile(subfile.full_path, new_subfile.full_path, ExistingDestinationHandling::Fail));
                TRY(check(Array {
                    TestFileChange {subfile.subpath, DirectoryWatcher::FileChange::Type::RenamedOldName},
                    TestFileChange {new_subfile.subpath,
                                    DirectoryWatcher::FileChange::Type::RenamedNewName}}));
            }
        } else {
            SUBCASE("delete in subfolder is not detected") {
                TRY(Delete(subfile.full_path, {}));
                TRY(check({}));
            }
        }
    }

    return k_success;
}

TEST_CASE(TestTimer) {
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
                atomic.Store(1);
                WakeWaitingThreads(atomic, wake_mode);
            },
            "thread");

        while (atomic.Load() == 1)
            WaitIfValueIsExpected(atomic, 1, {});

        thread.Join();
    }

    {
        Atomic<u32> atomic {0};
        CHECK_EQ(WaitIfValueIsExpected(atomic, 0, 1u), WaitResult::TimedOut);
    }
    return k_success;
}

int global_int = 0;

TEST_CASE(TestThread) {
    Thread thread;
    REQUIRE(!thread.Joinable());

    thread.Start(
        []() {
            global_int = 1;
            SleepThisThread(1);
        },
        "test thread");

    REQUIRE(thread.Joinable());
    thread.Join();

    REQUIRE(global_int == 1);
    return k_success;
}

TEST_REGISTRATION(RegisterOsTests) {
    REGISTER_TEST(TestThread);
    REGISTER_TEST(TestFutex);
    REGISTER_TEST(TestMutex);
    REGISTER_TEST(TestFilesystem);
    REGISTER_TEST(TestFileApi);
    REGISTER_TEST(TestReadingDirectoryChanges);
    REGISTER_TEST(TestFileApi);
    REGISTER_TEST(TestTimer);
}
