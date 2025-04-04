// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package_format.hpp"

#include "tests/framework.hpp"

namespace package {

ErrorCodeCategory const g_package_error_category = {
    .category_id = "PK",
    .message =
        [](Writer const& writer, ErrorCode e) {
            return writer.WriteChars(({
                String s {};
                switch ((PackageError)e.code) {
                    case PackageError::FileCorrupted: s = "package file is corrupted"_s; break;
                    case PackageError::NotFloePackage: s = "not a valid Floe package"_s; break;
                    case PackageError::InvalidLibrary: s = "library is invalid"_s; break;
                    case PackageError::AccessDenied: s = "access denied"_s; break;
                    case PackageError::FilesystemError: s = "filesystem error"_s; break;
                    case PackageError::NotEmpty: s = "directory not empty"_s; break;
                }
                s;
            }));
        },
};

} // namespace package

static String TestLibFolder(tests::Tester& tester) {
    return path::Join(
        tester.scratch_arena,
        Array {tests::TestFilesFolder(tester), tests::k_libraries_test_files_subdir, "Test-Lib-1"});
}

static String TestPresetsFolder(tests::Tester& tester) {
    return path::Join(tester.scratch_arena,
                      Array {tests::TestFilesFolder(tester), tests::k_preset_test_files_subdir});
}

static ErrorCodeOr<sample_lib::Library*> LoadTestLibrary(tests::Tester& tester) {
    auto const test_floe_lua_path =
        (String)path::Join(tester.scratch_arena, Array {TestLibFolder(tester), "floe.lua"});
    ASSERT(path::IsAbsolute(test_floe_lua_path));
    auto reader = TRY(Reader::FromFile(test_floe_lua_path));
    auto lib_outcome =
        sample_lib::ReadLua(reader, test_floe_lua_path, tester.scratch_arena, tester.scratch_arena);
    if (lib_outcome.HasError()) {
        tester.log.Error("Failed to read library from test lua file: {}", lib_outcome.Error().message);
        return lib_outcome.Error().code;
    }
    auto lib = lib_outcome.ReleaseValue();
    return lib;
}

static ErrorCodeOr<Span<u8 const>> CreateValidTestPackage(tests::Tester& tester) {
    DynamicArray<u8> zip_data {tester.scratch_arena};
    auto writer = dyn::WriterFor(zip_data);
    auto package = package::WriterCreate(writer);
    DEFER { package::WriterDestroy(package); };

    auto lib = TRY(LoadTestLibrary(tester));
    TRY(package::WriterAddLibrary(package, *lib, tester.scratch_arena, "tester"));

    TRY(package::WriterAddPresetsFolder(package, TestPresetsFolder(tester), tester.scratch_arena, "tester"));

    package::WriterFinalise(package);
    return zip_data.ToOwnedSpan();
}

static ErrorCodeOr<Span<u8 const>> CreateEmptyTestPackage(tests::Tester& tester) {
    DynamicArray<u8> zip_data {tester.scratch_arena};
    auto writer = dyn::WriterFor(zip_data);
    auto package = package::WriterCreate(writer);
    DEFER { package::WriterDestroy(package); };

    package::WriterFinalise(package);
    return zip_data.ToOwnedSpan();
}

static ErrorCodeOr<void> ReadTestPackage(tests::Tester& tester, Span<u8 const> zip_data) {
    auto reader = Reader::FromMemory(zip_data);
    DynamicArray<char> error_buffer {tester.scratch_arena};

    package::PackageReader package {reader};
    auto outcome = package::ReaderInit(package);
    if (outcome.HasError()) {
        TEST_FAILED("Failed to create package reader: {}. error_log: {}",
                    ErrorCode {outcome.Error()},
                    error_buffer);
    }
    DEFER { package::ReaderDeinit(package); };
    CHECK(error_buffer.size == 0);

    package::PackageComponentIndex iterator = 0;

    usize components_found = 0;
    while (true) {
        auto const component = ({
            auto const o = package::IteratePackageComponents(package, iterator, tester.scratch_arena);
            if (o.HasError()) {
                TEST_FAILED("Failed to read package component: {}, error_log: {}",
                            ErrorCode {o.Error()},
                            error_buffer);
            }
            o.Value();
        });
        if (!component) break;
        CHECK(error_buffer.size == 0);

        ++components_found;
        switch (component->type) {
            case package::ComponentType::Library: {
                REQUIRE(component->library);
                CHECK_EQ(component->library->name, "Test Lua"_s);
                break;
            }
            case package::ComponentType::Presets: {
                break;
            }
            case package::ComponentType::Count: PanicIfReached();
        }
    }

    CHECK_EQ(components_found, 2u);

    return k_success;
}

TEST_CASE(TestRelativePathIfInFolder) {
    CHECK_EQ(package::detail::RelativePathIfInFolder("/a/b/c", "/a/b"), "c"_s);
    CHECK_EQ(package::detail::RelativePathIfInFolder("/a/b/c", "/a/b/"), "c"_s);
    CHECK_EQ(package::detail::RelativePathIfInFolder("/a/b/c", "/a"), "b/c"_s);
    CHECK(!package::detail::RelativePathIfInFolder("/aa/b/c", "/a"));
    CHECK(!package::detail::RelativePathIfInFolder("/a/b/c", "/a/d"));
    CHECK(!package::detail::RelativePathIfInFolder("/a/b/c", "/a/b/c"));
    CHECK(!package::detail::RelativePathIfInFolder("", ""));
    CHECK(!package::detail::RelativePathIfInFolder("", "/a"));
    CHECK(!package::detail::RelativePathIfInFolder("/a", ""));
    return k_success;
}

TEST_CASE(TestPackageFormat) {
    SUBCASE("valid package") {
        auto const zip_data = TRY(CreateValidTestPackage(tester));
        CHECK_NEQ(zip_data.size, 0uz);
        TRY(ReadTestPackage(tester, zip_data));
    }

    SUBCASE("invalid package") {
        auto const zip_data = TRY(CreateEmptyTestPackage(tester));
        CHECK_NEQ(zip_data.size, 0uz);

        auto reader = Reader::FromMemory(zip_data);
        DynamicArray<char> error_buffer {tester.scratch_arena};

        package::PackageReader package {reader};
        auto outcome = package::ReaderInit(package);
        CHECK(outcome.HasError());
    }

    return k_success;
}

TEST_REGISTRATION(RegisterPackageFormatTests) {
    REGISTER_TEST(TestPackageFormat);
    REGISTER_TEST(TestRelativePathIfInFolder);
}
