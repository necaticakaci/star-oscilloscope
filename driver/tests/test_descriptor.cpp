// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "isds/descriptor.hpp"
#include "test_data.hpp"

using namespace isds;

TEST_CASE("parses the ISDS205X(1.1) descriptor")
{
    const Bytes raw = test_data::descriptor();
    const Descriptor d = Descriptor::parse(raw);

    CHECK(d.version == 0x0a);
    CHECK(d.vendor == "YiXingDianZi");
    CHECK(d.model == "ISDS205X(1.1)");
    CHECK(d.device_id == 0x0000000000000001ULL);  // a placeholder: tools/make_golden.py hides the real one
    CHECK(d.base_clock_hz == 48'000'000);
    CHECK(d.sample_rates == std::vector<std::uint32_t>{1'000'000, 4'000'000, 8'000'000, 16'000'000, 48'000'000});
    CHECK(d.gain_codes == std::vector<std::uint16_t>{0x0109, 0x052e});
    CHECK(d.raw == raw);

    REQUIRE(d.levels.size() == 4);
    CHECK(d.levels[0].code == std::array<std::uint16_t, 2>{0x77, 0x76});
    CHECK(d.levels[0].offset_mv[0] == std::vector<std::int32_t>{-9, 2});
    CHECK(d.levels[0].offset_mv[1] == std::vector<std::int32_t>{-76, -323});
    CHECK(d.levels[1].code == std::array<std::uint16_t, 2>{0xd3, 0xd2});
    CHECK(d.levels[1].offset_mv[0] == std::vector<std::int32_t>{17, 105});
    CHECK(d.levels[1].offset_mv[1] == std::vector<std::int32_t>{-19, -71});
    CHECK(d.levels[2].code == std::array<std::uint16_t, 2>{0x200, 0x1fe});
    CHECK(d.levels[2].offset_mv[0] == std::vector<std::int32_t>{29, 122});
    CHECK(d.levels[2].offset_mv[1] == std::vector<std::int32_t>{17, 65});
    CHECK(d.levels[3].code == std::array<std::uint16_t, 2>{0x62c, 0x62c});
    CHECK(d.levels[3].offset_mv[1] == std::vector<std::int32_t>{33, 166});
}

TEST_CASE("only the ISDS205 series is driven")
{
    for (const char* model : {"ISDS205X(1.1)", "ISDS205B", "ISDS205", "ISDS205A(1.0)"})
        CHECK(isds::supported_model(model));
    for (const char* model : {"ISDS210B", "ISDS220", "ISDS2062", "MSO20", "", "ISDS20"})
        CHECK_FALSE(isds::supported_model(model));
}

TEST_CASE("rejects malformed descriptors")
{
    Bytes raw = test_data::descriptor();

    CHECK_THROWS_AS(Descriptor::parse(std::span(raw).first(0x40)), Error);

    Bytes wrong_version = raw;
    wrong_version[0] = 0x09;
    CHECK_THROWS_AS(Descriptor::parse(wrong_version), Error);

    Bytes no_gains = raw;
    no_gains[0x47] = 0;
    CHECK_THROWS_AS(Descriptor::parse(no_gains), Error);

    Bytes huge_level_count = raw;
    huge_level_count[0x4e] = 0xff;  // runs past the end of the buffer
    CHECK_THROWS_AS(Descriptor::parse(huge_level_count), Error);
}
