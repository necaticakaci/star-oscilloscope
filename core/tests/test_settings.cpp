// SPDX-License-Identifier: GPL-3.0-or-later
// The display modes keep their own channel positions.
#include <catch2/catch_test_macros.hpp>

#include "starosc/settings.hpp"

using namespace starosc;

TEST_CASE("Y-T and X-Y keep their own channel positions")
{
    ScopeSettings yt;
    yt.channel[0].position_div = 2;
    yt.channel[1].position_div = -2;

    // The first X-Y starts with both traces through the middle; Y-T's are kept.
    ScopeSettings xy = with_display(yt, DisplayMode::XY);
    CHECK(xy.display == DisplayMode::XY);
    CHECK(xy.channel[0].position_div == 0);
    CHECK(xy.channel[1].position_div == 0);

    // Moved in X-Y, then back: Y-T as it was, and X-Y's kept for next time.
    xy.channel[0].position_div = 0.5;
    const ScopeSettings back = with_display(xy, DisplayMode::YT);
    CHECK(back.channel[0].position_div == 2);
    CHECK(back.channel[1].position_div == -2);
    CHECK(with_display(back, DisplayMode::XY).channel[0].position_div == 0.5);

    // Asking for the mode shown changes nothing.
    CHECK(with_display(yt, DisplayMode::YT) == yt);
}
