// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "checksum_crc32_file.hpp"

#include "tests/framework.hpp"

TEST_CASE(TestChecksumFileParsing) {

    SUBCASE("empty file") {
        ChecksumFileParser parser {
            .file_data = "",
        };
        auto line = TRY(parser.ReadLine());
        CHECK(!line.HasValue());
    }

    SUBCASE("parses lines correctly") {
        auto const file = R"raw(; comment
0f0f0f0f 1234 /path/to/file
abcdef01 5678 /path/to/another/file)raw"_s;
        ChecksumFileParser parser {
            .file_data = file,
        };

        auto line1 = TRY(parser.ReadLine());
        REQUIRE(line1.HasValue());
        CHECK_EQ(line1.Value().path, "/path/to/file"_s);
        CHECK_EQ(line1.Value().crc32, 0x0f0f0f0fu);
        CHECK_EQ(line1.Value().file_size, 1234u);

        auto line2 = TRY(parser.ReadLine());
        REQUIRE(line2.HasValue());
        CHECK_EQ(line2.Value().path, "/path/to/another/file"_s);
        CHECK_EQ(line2.Value().crc32, 0xabcdef01u);
        CHECK_EQ(line2.Value().file_size, 5678u);
    }

    SUBCASE("handles invalid lines") {
        auto parse_line = [](String line) {
            ChecksumFileParser parser {
                .file_data = line,
            };
            return parser.ReadLine();
        };

        CHECK(parse_line("wf39 qwer path"_s).HasError());
        CHECK(parse_line("fff 12321").HasError());
        CHECK(parse_line("1238").HasError());
        CHECK(parse_line("123 23\npath").HasError());
        CHECK(parse_line("123  23 path").HasError());
    }

    return k_success;
}

TEST_REGISTRATION(RegisterChecksumFileTests) { REGISTER_TEST(TestChecksumFileParsing); }
