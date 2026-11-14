// SPDX-License-Identifier: GPL-3.0-or-later
// The calibration must reproduce both the vendor library output and the Python reference model.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstring>
#include <optional>

#include "isds/calibration.hpp"
#include "test_data.hpp"

using namespace isds;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

Calibration make_calibration() { return Calibration(Descriptor::parse(test_data::descriptor())); }

Channel channel(const std::string& s) { return s == "1" ? Channel::Ch1 : Channel::Ch2; }

std::optional<Stage> find_stage(const Calibration& cal, Channel ch, std::uint16_t g, std::uint16_t d)
{
    const auto& desc = cal.descriptor();
    for (int gi = 0; gi < static_cast<int>(desc.gain_codes.size()); ++gi)
        for (int li = 0; li < static_cast<int>(desc.levels.size()); ++li) {
            const Stage s{gi, li};
            if (cal.gain_code(s) == g && cal.level_code(s, ch) == d)
                return s;
        }
    return std::nullopt;
}

struct VendorRecord {
    std::uint16_t g = 0;
    std::uint16_t d = 0;
    Channel ch = Channel::Ch1;
    std::vector<std::uint8_t> raw;
    std::vector<double> volts;
};

template <typename T>
T take(const Bytes& data, std::size_t& pos)
{
    T value;
    REQUIRE(pos + sizeof(T) <= data.size());
    std::memcpy(&value, data.data() + pos, sizeof(T));  // file is little-endian, as is every supported host
    pos += sizeof(T);
    return value;
}

std::vector<VendorRecord> vendor_records()
{
    const Bytes data = test_data::read_file("vendor_captures.bin");
    REQUIRE(data.size() >= 12);
    REQUIRE(std::string(data.begin(), data.begin() + 8) == "ISDSGLD1");
    std::size_t pos = 8;
    const auto count = take<std::uint32_t>(data, pos);
    std::vector<VendorRecord> records;
    for (std::uint32_t i = 0; i < count; ++i) {
        VendorRecord r;
        r.g = take<std::uint16_t>(data, pos);
        r.d = take<std::uint16_t>(data, pos);
        r.ch = take<std::uint8_t>(data, pos) == 1 ? Channel::Ch1 : Channel::Ch2;
        take<std::uint8_t>(data, pos);
        const auto n = take<std::uint16_t>(data, pos);
        for (std::size_t k = 0; k < n; ++k)
            r.raw.push_back(take<std::uint8_t>(data, pos));
        for (std::size_t k = 0; k < n; ++k)
            r.volts.push_back(take<double>(data, pos));
        records.push_back(std::move(r));
    }
    return records;
}

}  // namespace

TEST_CASE("reproduces vendor ReadVoltageDatas output for every recorded stage")
{
    const Calibration cal = make_calibration();
    const auto records = vendor_records();
    REQUIRE(records.size() == 14);
    for (const auto& r : records) {
        INFO("G=" << r.g << " D=" << r.d << " CH" << static_cast<int>(r.ch));
        const auto stage = find_stage(cal, r.ch, r.g, r.d);
        REQUIRE(stage);
        std::vector<double> mv(r.raw.size());
        cal.to_mv(r.raw, mv, *stage, r.ch);
        double worst = 0;
        for (std::size_t i = 0; i < mv.size(); ++i)
            worst = std::max(worst, std::abs(mv[i] / 1000.0 - r.volts[i]));
        CHECK(worst < 1e-12);
    }
}

TEST_CASE("matches the Python reference model per stage")
{
    const Calibration cal = make_calibration();
    const auto rows = test_data::read_csv("model_stages.csv");
    REQUIRE(rows.size() == 2 * cal.stages().size());
    for (const auto& row : rows) {
        const Stage stage = cal.stages().at(std::stoul(row.at("order")));
        const Channel ch = channel(row.at("channel"));
        INFO("order " << row.at("order") << " CH" << row.at("channel"));
        CHECK(stage.gain == std::stoi(row.at("gain_index")));
        CHECK(stage.level == std::stoi(row.at("level_index")));
        CHECK(cal.gain_code(stage) == std::stoi(row.at("g")));
        CHECK(cal.level_code(stage, ch) == std::stoi(row.at("d")));
        CHECK_THAT(cal.lsb_mv(stage, ch), WithinRel(std::stod(row.at("lsb_mv")), 1e-14));
        CHECK(cal.offset_mv(stage, ch) == std::stod(row.at("offset_mv")));
        CHECK_THAT(cal.zero(stage, ch), WithinAbs(std::stod(row.at("zero")), 1e-12));
        CHECK_THAT(cal.span(stage, ch).min_mv, WithinAbs(std::stod(row.at("span_min_mv")), 1e-9));
        CHECK_THAT(cal.span(stage, ch).max_mv, WithinAbs(std::stod(row.at("span_max_mv")), 1e-9));
    }
}

TEST_CASE("stage selection and trigger codes match the Python reference model")
{
    const Calibration cal = make_calibration();
    const auto rows = test_data::read_csv("model_select.csv");
    REQUIRE_FALSE(rows.empty());
    for (const auto& row : rows) {
        const Channel ch = channel(row.at("channel"));
        const double lo = std::stod(row.at("min_mv"));
        const double hi = std::stod(row.at("max_mv"));
        INFO("CH" << row.at("channel") << " " << lo << ".." << hi << " mV");
        const Stage stage = cal.select(ch, lo, hi);
        CHECK(cal.gain_code(stage) == std::stoi(row.at("g")));
        CHECK(cal.level_code(stage, ch) == std::stoi(row.at("d")));
        const TriggerCodes codes = cal.trigger_codes(stage, ch, 0, lo, hi);
        CHECK(codes.level == std::stoi(row.at("trigger_level_code")));
        CHECK(codes.hysteresis == std::stoi(row.at("trigger_hysteresis_code")));
    }
}

TEST_CASE("selection never clips when a covering stage exists")
{
    const Calibration cal = make_calibration();
    for (Channel ch : {Channel::Ch1, Channel::Ch2})
        for (double r = 10; r < 5000; r *= 1.07) {
            const Stage s = cal.select(ch, -r, r);
            const Span span = cal.span(s, ch);
            if (s != cal.stages().back()) {
                CHECK(span.min_mv < -r);
                CHECK(span.max_mv > r);
            }
        }
}

TEST_CASE("startup stage is the one vdso programs after the handshake")
{
    const Calibration cal = make_calibration();
    const Stage s = cal.startup_stage();
    CHECK(cal.gain_code(s) == 0x052e);
    CHECK(cal.level_code(s, Channel::Ch1) == 0x062c);
    CHECK(cal.level_code(s, Channel::Ch2) == 0x062c);
}

TEST_CASE("user correction maps factory readings to (V - zero) / gain")
{
    Calibration cal = make_calibration();
    const Stage s = cal.stages()[3];
    const double factory = cal.to_mv(200, s, Channel::Ch1);
    const Span factory_span = cal.span(s, Channel::Ch1);

    cal.set_correction(s, Channel::Ch1, {-31.5, 0.93});
    CHECK_THAT(cal.to_mv(200, s, Channel::Ch1), WithinRel((factory + 31.5) / 0.93, 1e-12));
    CHECK_THAT(cal.span(s, Channel::Ch1).max_mv, WithinRel((factory_span.max_mv + 31.5) / 0.93, 1e-12));
    CHECK_THAT(cal.factory_lsb_mv(s, Channel::Ch1), WithinRel(cal.lsb_mv(s, Channel::Ch1) * 0.93, 1e-12));
    // Other stages and the other channel keep the factory conversion.
    CHECK(cal.correction(s, Channel::Ch2) == Correction{});
    CHECK(cal.correction(cal.stages()[2], Channel::Ch1) == Correction{});

    // The trigger code still points at the raw code where the corrected voltage is reached.
    const TriggerCodes t = cal.trigger_codes(s, Channel::Ch1, 0, -1000, 1000);
    CHECK(std::abs(cal.to_mv(t.level, s, Channel::Ch1)) <= cal.lsb_mv(s, Channel::Ch1));

    cal.clear_corrections();
    CHECK(cal.to_mv(200, s, Channel::Ch1) == factory);
    CHECK_THROWS_AS(cal.set_correction(s, Channel::Ch1, {0, 0}), Error);
    CHECK_THROWS_AS(cal.set_correction(s, Channel::Ch1, {NAN, 1}), Error);
}
