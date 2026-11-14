// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "starosc/trigger.hpp"

using namespace starosc;
using Catch::Approx;

namespace {
std::vector<float> sine(std::size_t n, double period, double phase = 0)
{
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = static_cast<float>(std::sin(2 * std::numbers::pi * (static_cast<double>(i) / period) + phase));
    return v;
}
}  // namespace

TEST_CASE("interpolates the crossing position")
{
    const std::vector<float> v{-1, -1, 1, 1};
    const auto p = find_trigger(v, 0.0, 0.1, TriggerEdge::Rising, 0, v.size());
    REQUIRE(p);
    CHECK(*p == Approx(1.5));
}

TEST_CASE("finds rising, falling and either edges of a sine")
{
    const auto v = sine(1000, 100);  // rising zero crossings at 0, 100, 200, ...
    auto rising = find_trigger(v, 0.0, 0.2, TriggerEdge::Rising, 30, v.size());
    REQUIRE(rising);
    CHECK(*rising == Approx(100).margin(0.01));

    auto falling = find_trigger(v, 0.0, 0.2, TriggerEdge::Falling, 30, v.size());
    REQUIRE(falling);
    CHECK(*falling == Approx(50).margin(0.01));

    auto either = find_trigger(v, 0.0, 0.2, TriggerEdge::Either, 30, v.size());
    REQUIRE(either);
    CHECK(*either == Approx(50).margin(0.01));

    auto level = find_trigger(v, 0.5, 0.2, TriggerEdge::Rising, 0, v.size());
    REQUIRE(level);
    CHECK(*level == Approx(100.0 / 12).margin(0.05));  // sin(x) = 0.5 at x = pi/6
}

TEST_CASE("respects the search window")
{
    const auto v = sine(1000, 100);
    CHECK_FALSE(find_trigger(v, 0.0, 0.2, TriggerEdge::Rising, 101, 199));
    CHECK(find_trigger(v, 0.0, 0.2, TriggerEdge::Rising, 101, 201));
}

TEST_CASE("hysteresis rejects noise around the level")
{
    std::vector<float> v{0.02f, -0.03f, 0.01f, -0.02f, 0.03f, -0.01f, 0.02f};
    CHECK_FALSE(find_trigger(v, 0.0, 0.1, TriggerEdge::Rising, 0, v.size()));
    CHECK(find_trigger(v, 0.0, 0.0, TriggerEdge::Rising, 0, v.size()));

    v.insert(v.end(), {-0.5f, 0.5f});
    const auto p = find_trigger(v, 0.0, 0.1, TriggerEdge::Rising, 0, v.size());
    REQUIRE(p);
    CHECK(*p == Approx(7.5));
}

TEST_CASE("no trigger when the level is out of reach")
{
    const auto v = sine(1000, 100);
    CHECK_FALSE(find_trigger(v, 1.5, 0.1, TriggerEdge::Either, 0, v.size()));
}

TEST_CASE("the trigger nearest a target")
{
    // A sine of 20 samples: rising crossings at 0, 20, 40, ...
    std::vector<float> v(200);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<float>(std::sin(2 * std::numbers::pi * static_cast<double>(i) / 20));
    const auto near = find_trigger_near(v, 0.0, 0.1, TriggerEdge::Rising, 5, 190, 97);
    REQUIRE(near);
    CHECK(*near == Approx(100).margin(0.5));
    const auto before = find_trigger_near(v, 0.0, 0.1, TriggerEdge::Rising, 5, 190, 88);
    REQUIRE(before);
    CHECK(*before == Approx(80).margin(0.5));
    // At the start of the range it is the first one, as find_trigger finds.
    CHECK(*find_trigger_near(v, 0.0, 0.1, TriggerEdge::Rising, 5, 190, 0) == Approx(*find_trigger(v, 0.0, 0.1, TriggerEdge::Rising, 5, 190)));
    CHECK_FALSE(find_trigger_near(v, 2.0, 0.1, TriggerEdge::Rising, 5, 190, 97));
}
