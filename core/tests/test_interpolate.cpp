// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "starosc/interpolate.hpp"

using namespace starosc;

namespace {
// A sine with `per_period` samples a period, as a 10 MHz sine at 48 MSa/s has 4.8.
std::vector<float> sine(std::size_t n, double per_period, long offset = 0)
{
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = static_cast<float>(std::sin(2 * std::numbers::pi * (static_cast<double>(i) + offset) / per_period + 0.3));
    return v;
}
}  // namespace

TEST_CASE("sin(x)/x brings back a sine from 4.8 samples a period")
{
    const auto v = sine(200, 4.8);
    const auto out = sinc_interpolate(v, {}, {}, 50, 0.1, 1000);
    double worst = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double s = 50 + 0.1 * static_cast<double>(i);
        worst = std::max(worst, std::abs(out[i] - std::sin(2 * std::numbers::pi * s / 4.8 + 0.3)));
    }
    CHECK(worst < 0.01);
}

TEST_CASE("sin(x)/x uses the samples around the window at its edges")
{
    const std::size_t taps = kSincTaps;
    const auto before = sine(taps, 4.8, -static_cast<long>(taps));
    const auto v = sine(48, 4.8);
    const auto after = sine(taps, 4.8, 48);
    const auto with = sinc_interpolate(v, before, after, 0, 0.25, 4 * 48);
    double worst = 0;
    for (std::size_t i = 0; i < with.size(); ++i) {
        const double s = 0.25 * static_cast<double>(i);
        worst = std::max(worst, std::abs(with[i] - std::sin(2 * std::numbers::pi * s / 4.8 + 0.3)));
    }
    CHECK(worst < 0.03);
}

TEST_CASE("sin(x)/x keeps a constant and hits the samples")
{
    const std::vector<float> flat(40, 1.25f);
    for (double x : sinc_interpolate(flat, {}, {}, 0, 0.37, 100))
        CHECK(x == Catch::Approx(1.25).epsilon(1e-9));

    const auto v = sine(64, 6.0);
    const auto at = sinc_interpolate(v, {}, {}, 20, 1, 20);
    for (std::size_t i = 0; i < at.size(); ++i)
        CHECK(at[i] == Catch::Approx(v[20 + i]).margin(1e-6));
}

TEST_CASE("sin(x)/x only for sparsely sampled signals")
{
    CHECK(starosc::sinc_helps(2.4));   // 20 MHz at 48 MSa/s
    CHECK(starosc::sinc_helps(16));
    CHECK_FALSE(starosc::sinc_helps(24));
    CHECK_FALSE(starosc::sinc_helps(48));  // 1 MHz at 48 MSa/s: only smoothed steps and noise
    CHECK(starosc::sinc_helps(0));         // no known period
    CHECK(starosc::sinc_helps(std::numeric_limits<double>::quiet_NaN()));
}
