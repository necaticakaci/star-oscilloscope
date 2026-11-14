// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <stdexcept>

#include "starosc/calibration.hpp"
#include "starosc/source.hpp"

using namespace starosc;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

AcquisitionRequest dc_request(double range_v)
{
    AcquisitionRequest r;
    r.sample_rate_hz = 1'000'000;
    r.samples = 4096;
    for (auto& ch : r.channel)
        ch.range = {-range_v, range_v};
    return r;
}

double mean(const std::vector<float>& v)
{
    double s = 0;
    for (float x : v)
        s += x;
    return s / static_cast<double>(v.size());
}

}  // namespace

TEST_CASE("factory calibration lists every stage with identity corrections")
{
    auto sim = make_simulator();
    const UserCalibration cal = sim->factory_calibration();
    CHECK(cal.device_id == "SIMULATOR");
    CHECK(cal.model == "ISDS205X(1.1)");
    REQUIRE(cal.stages.size() == 7);
    CHECK(cal.is_factory());
    CHECK(cal.stages.front().gain_code == 0x109);
    CHECK(cal.stages.front().level_code[0] == 0x62c);
    CHECK_THAT(cal.stages.front().factory_span[0].max_v, WithinAbs(0.11686, 1e-4));
}

TEST_CASE("adopt matches stages by their codes, not their position")
{
    auto sim = make_simulator();
    UserCalibration base = sim->factory_calibration();
    UserCalibration other = base;
    std::swap(other.stages[0], other.stages[6]);
    other.stages[0].channel[1] = {0.01, 0.9};  // now the widest stage
    other.stages.pop_back();                   // drop the finest stage
    CHECK(base.adopt(other) == 6);
    CHECK(base.stages[6].channel[1] == ChannelCorrection{0.01, 0.9});
    CHECK(base.stages[0].channel[1] == ChannelCorrection{});
    CHECK_FALSE(base.is_factory());
}

TEST_CASE("a correction rescales the simulator's readings, and another device's file is refused")
{
    // CH2 of the simulator is a 0.7 V sine; CH1 a 0..2 V square wave.
    auto sim = make_simulator();
    const auto peak = [](const std::vector<float>& v) { return *std::max_element(v.begin(), v.end()); };
    CHECK_THAT(peak(sim->acquire(dc_request(1.0)).channel[1].volts), WithinRel(0.7, 0.1));

    UserCalibration cal = sim->factory_calibration();
    for (auto& s : cal.stages)
        s.channel[1] = {0.0, 0.5};
    CHECK(sim->set_calibration(cal) == 7);
    CHECK_THAT(peak(sim->acquire(dc_request(2.0)).channel[1].volts), WithinRel(1.4, 0.1));
    CHECK_THAT(mean(sim->acquire(dc_request(3.0)).channel[0].volts), WithinAbs(1.0, 0.1));  // CH1 untouched

    cal.device_id = "0000000000000001";
    CHECK_THROWS_AS(sim->set_calibration(cal), std::invalid_argument);
}

TEST_CASE("zero measurement of shorted inputs passes the check; offsets and noise do not")
{
    auto sim = make_simulator();
    const UserCalibration cal = sim->factory_calibration();
    std::vector<StageZero> zero = sim->measure_zero();
    REQUIRE(zero.size() == cal.stages.size());
    CHECK(check_zero(cal, zero).ok());
    for (const StageZero& z : zero)
        CHECK(std::abs(z.mean_v[0]) < 0.05);

    auto offset = zero;
    offset[3].mean_v[1] = 0.5;
    const ZeroProblem off = check_zero(cal, offset);
    CHECK(off.kind == ZeroProblem::Kind::Offset);
    CHECK(off.channel == 1);
    auto noisy = zero;
    noisy[0].noise_v[0] = 0.1;
    CHECK(check_zero(cal, noisy).kind == ZeroProblem::Kind::Noise);
    auto clipped = zero;
    clipped[6].clipped[0] = true;
    CHECK(check_zero(cal, clipped).kind == ZeroProblem::Kind::Clipped);
    CHECK(check_zero(cal, {}).kind == ZeroProblem::Kind::Incomplete);
}
