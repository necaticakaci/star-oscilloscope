// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "starosc/scale.hpp"

using namespace starosc;

TEST_CASE("math and reference scales run in 1-2-5 steps")
{
    const auto steps = units_per_div_steps();
    CHECK(steps.front() == Catch::Approx(1e-3));
    CHECK(steps[1] == Catch::Approx(2e-3));
    CHECK(steps[2] == Catch::Approx(5e-3));
    CHECK(steps.back() == Catch::Approx(100.0));
}

TEST_CASE("volts per division follow the probe in round steps")
{
    const auto at = [](double probe) { return volts_per_div_steps(probe); };
    CHECK(at(1).front() == Catch::Approx(5e-3));
    CHECK(at(1).back() == Catch::Approx(2.0));
    CHECK(at(10).front() == Catch::Approx(50e-3));
    CHECK(at(10).back() == Catch::Approx(20.0));
    CHECK(at(100).front() == Catch::Approx(0.5));
    CHECK(at(100).back() == Catch::Approx(200.0));
    // A measured factor keeps the round steps of its neighbourhood.
    CHECK(at(10.15).size() == at(10).size());
    CHECK(at(10.15).front() == Catch::Approx(50e-3));
    CHECK(at(10.15).back() == Catch::Approx(20.0));
    CHECK(at(9.8).front() == Catch::Approx(50e-3));
    CHECK(at(9.8).back() == Catch::Approx(20.0));
    CHECK(at(2.5).front() == Catch::Approx(20e-3));
    CHECK(at(2.5).back() == Catch::Approx(5.0));
    CHECK(at(0.1).front() == Catch::Approx(0.5e-3));
    CHECK(at(0.1).back() == Catch::Approx(0.2));
    CHECK_FALSE(at(10'000).empty());
}

TEST_CASE("SI formatting")
{
    CHECK(format_si(1000, "Hz") == "1.000 kHz");
    CHECK(format_si(1234.6, "Hz") == "1.235 kHz");
    CHECK(format_si(0.5, "V") == "500.0 mV");
    CHECK(format_si(-0.0123, "V") == "-12.30 mV");
    CHECK(format_si(0, "V") == "0.000 V");
    CHECK(format_si(2.06, "V") == "2.060 V");
    CHECK(format_si(std::nan(""), "V") == "-----");
    CHECK(format_si(50.0, "%", 3) == "50.0 %");
}

TEST_CASE("control step labels")
{
    CHECK(format_si(500e-6, "s/div", 3, true) == "500 µs/div");
    CHECK(format_si(100e-9, "s/div", 3, true) == "100 ns/div");
    CHECK(format_si(2.0, "V/div", 3, true) == "2 V/div");
    CHECK(format_si(0.005, "V/div", 3, true) == "5 mV/div");
}

TEST_CASE("stepping through 1-2-5 values")
{
    const auto volts = volts_per_div_steps();
    CHECK(step_value(volts, 1.0, 1) == Catch::Approx(2.0));
    CHECK(step_value(volts, 1.0, -1) == Catch::Approx(0.5));
    CHECK(step_value(volts, 0.9, 0) == Catch::Approx(1.0));
    CHECK(step_value(volts, 2.0, 5) == Catch::Approx(2.0));
    CHECK(step_value(volts, 0.005, -3) == Catch::Approx(0.005));
}

TEST_CASE("1-2-5 steps")
{
    const auto volts = volts_per_div_steps();
    REQUIRE(volts.size() == 9);
    CHECK(volts.front() == Catch::Approx(5e-3));
    CHECK(volts.back() == Catch::Approx(2.0));

    const auto seconds = seconds_per_div_steps();
    REQUIRE(seconds.size() == 19);
    CHECK(seconds.front() == Catch::Approx(100e-9));
    CHECK(seconds.back() == Catch::Approx(100e-3));
}
