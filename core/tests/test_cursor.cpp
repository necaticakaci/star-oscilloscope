// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "starosc/cursor.hpp"

using namespace starosc;
using Catch::Approx;

namespace {
ScopeSettings settings()
{
    ScopeSettings s;
    s.channel[0] = {true, 1.0, 1.0, Coupling::DC, 1.0};
    s.channel[1] = {true, 0.5, -2.0, Coupling::DC, 1.0};
    s.timebase.seconds_per_div = 500e-6;
    s.trigger.pre_trigger = 0.5;
    return s;
}
}  // namespace

TEST_CASE("time cursors are relative to the trigger point")
{
    const ScopeSettings s = settings();
    CHECK(cursor_time(s, 0.5) == Approx(0).margin(1e-15));
    CHECK(cursor_time(s, 0.7) == Approx(1e-3));
    CHECK(cursor_time(s, 0.0) == Approx(-2.5e-3));

    CursorSettings c;
    c.x = {0.3, 0.7};
    const CursorReadout r = cursor_readout(s, c);
    CHECK(r.delta_t == Approx(2e-3));
    CHECK(r.inverse_delta_t == Approx(500));

    c.x = {0.4, 0.4};
    CHECK(std::isnan(cursor_readout(s, c).inverse_delta_t));
}

TEST_CASE("voltage cursors follow the source channel's scale and position")
{
    const ScopeSettings s = settings();
    CHECK(cursor_volts(s.channel[0], 2.0) == Approx(1.0));
    CHECK(cursor_volts(s.channel[1], -1.0) == Approx(0.5));

    CursorSettings c;
    c.source = 1;
    c.y_div = {-3.0, -1.0};
    const CursorReadout r = cursor_readout(s, c);
    CHECK(r.volts[0] == Approx(-0.5));
    CHECK(r.volts[1] == Approx(0.5));
    CHECK(r.delta_v == Approx(1.0));
}

TEST_CASE("value at a screen position interpolates the frame")
{
    Frame f;
    f.plan.window_samples = 4;
    f.start_fraction = 0.5;
    f.volts[0] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};

    CHECK(value_at(f, 0, 0.0) == Approx(0.5));
    CHECK(value_at(f, 0, 0.5) == Approx(2.5));
    CHECK(value_at(f, 0, 0.875) == Approx(4.0));
    CHECK(std::isnan(value_at(f, 0, 1.0)));
    CHECK(std::isnan(value_at(f, 1, 0.5)));
}
