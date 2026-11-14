// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "starosc/cursor.hpp"
#include "starosc/zoom.hpp"

using namespace starosc;
using Catch::Approx;

namespace {
const TimebaseSettings kTimebase{1e-3, MemoryDepth::Small};
}

TEST_CASE("zoom off shows the whole record")
{
    const VisibleRange r = visible_range(kTimebase, {});
    CHECK(r.begin == 0);
    CHECK(r.span == 1);
    // A zoom timebase no faster than the main one is no zoom.
    CHECK(visible_range(kTimebase, {2e-3, 0.5}).span == 1);
}

TEST_CASE("the visible range stays inside the record")
{
    VisibleRange r = visible_range(kTimebase, {100e-6, 0.5});
    CHECK(r.begin == Approx(0.45));
    CHECK(r.span == Approx(0.1));
    r = visible_range(kTimebase, {100e-6, 0.0});
    CHECK(r.begin == 0);
    r = visible_range(kTimebase, {100e-6, 1.0});
    CHECK(r.begin == Approx(0.9));
}

TEST_CASE("zoom steps are 1-2-5 below the main timebase and need samples on the screen")
{
    std::vector<double> steps = zoom_steps(1e-3, 1e6);
    CHECK(steps.front() == Approx(500e-6));
    CHECK(steps[1] == Approx(200e-6));
    // 4 samples at 1 MSa/s cross the screen at 400 ns/div or slower.
    CHECK(steps.back() == Approx(500e-9));

    steps = zoom_steps(500e-9, 48e6);
    CHECK(steps.front() == Approx(200e-9));
    CHECK(steps.back() == Approx(10e-9));

    CHECK(zoom_steps(1e-6, 1e6).size() == 1);  // only 500 ns/div
    CHECK(zoom_steps(500e-9, 1e6).empty());
}

TEST_CASE("constrained zoom")
{
    SECTION("off stays off and keeps its centre")
    {
        const ZoomSettings z = constrained({0, 0.3}, kTimebase, 1e6);
        CHECK_FALSE(z.on());
        CHECK(z.center == 0.3);
    }
    SECTION("the main timebase catching up switches it off")
    {
        CHECK_FALSE(constrained({1e-3, 0.5}, kTimebase, 1e6).on());
        CHECK_FALSE(constrained({2e-3, 0.5}, kTimebase, 1e6).on());
    }
    SECTION("too fast for the rate moves to the fastest step")
    {
        CHECK(constrained({10e-9, 0.5}, kTimebase, 1e6).seconds_per_div == Approx(500e-9));
    }
    SECTION("no step at all switches it off")
    {
        CHECK_FALSE(constrained({100e-9, 0.5}, {500e-9, MemoryDepth::Small}, 1e6).on());
    }
    SECTION("the centre keeps the screen inside the record")
    {
        CHECK(constrained({100e-6, 0.99}, kTimebase, 1e6).center == Approx(0.95));
        CHECK(constrained({100e-6, 0.01}, kTimebase, 1e6).center == Approx(0.05));
    }
}

TEST_CASE("wheel steps hold the point under the pointer")
{
    const ZoomSettings first = zoom_stepped({}, kTimebase, 1e6, -1, 0.25);
    CHECK(first.seconds_per_div == Approx(500e-6));
    VisibleRange r = visible_range(kTimebase, first);
    CHECK(r.begin + 0.25 * r.span == Approx(0.25));

    const ZoomSettings second = zoom_stepped(first, kTimebase, 1e6, -2, 0.75);
    CHECK(second.seconds_per_div == Approx(100e-6));
    const VisibleRange s = visible_range(kTimebase, second);
    CHECK(s.begin + 0.75 * s.span == Approx(r.begin + 0.75 * r.span));

    SECTION("stepping out past the main timebase switches it off")
    {
        CHECK_FALSE(zoom_stepped(first, kTimebase, 1e6, 1, 0.5).on());
        CHECK_FALSE(zoom_stepped(second, kTimebase, 1e6, 9, 0.5).on());
    }
    SECTION("stepping in stops at the fastest step")
    {
        CHECK(zoom_stepped(first, kTimebase, 1e6, -99, 0.5).seconds_per_div == Approx(500e-9));
    }
    SECTION("no step in from the main timebase when the rate leaves none")
    {
        CHECK_FALSE(zoom_stepped({}, {500e-9, MemoryDepth::Small}, 1e6, -1, 0.5).on());
    }
}

TEST_CASE("zooming to a range shows all of it")
{
    const ZoomSettings z = zoom_to_range(kTimebase, 1e6, 0.30, 0.34);
    CHECK(z.seconds_per_div == Approx(50e-6));  // 40 µs/div would fit; 50 is the next step up
    CHECK(z.center == Approx(0.32));
    const VisibleRange r = visible_range(kTimebase, z);
    CHECK(r.begin <= 0.30);
    CHECK(r.begin + r.span >= 0.34);

    CHECK(zoom_to_range(kTimebase, 1e6, 0.60, 0.20).center == Approx(0.4));
    CHECK(zoom_to_range(kTimebase, 1e6, 0.60, 0.20).seconds_per_div == Approx(500e-6));
    SECTION("nearly the whole record is no zoom")
    {
        CHECK_FALSE(zoom_to_range(kTimebase, 1e6, 0.1, 0.9).on());
    }
    SECTION("a tiny range stops at the fastest step")
    {
        CHECK(zoom_to_range(kTimebase, 1e6, 0.5, 0.5000001).seconds_per_div == Approx(500e-9));
    }
}

TEST_CASE("cursors read the time of what the zoomed screen shows")
{
    ScopeSettings s;
    s.timebase.seconds_per_div = 1e-3;
    s.trigger.pre_trigger = 0.5;
    s.zoom = {100e-6, 0.5};
    CHECK(cursor_time(s, 0.5) == Approx(0).margin(1e-12));
    CHECK(cursor_time(s, 1.0) == Approx(500e-6));
    CHECK(cursor_time(s, 0.0) == Approx(-500e-6));
}
