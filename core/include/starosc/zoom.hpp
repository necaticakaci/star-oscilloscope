// SPDX-License-Identifier: GPL-3.0-or-later
// Horizontal zoom: which part of the record the screen shows. The zoom timebase moves in the same 1-2-5 steps as
// the main one.
#pragma once

#include <cstdint>
#include <vector>

#include "starosc/settings.hpp"

namespace starosc {

// As fractions of the record.
struct VisibleRange {
    double begin = 0;
    double span = 1;
};

// The whole record when the zoom is off or not faster than the main timebase.
VisibleRange visible_range(const TimebaseSettings& timebase, const ZoomSettings& zoom);

// Zoom timebases faster than the main one, slowest first, as long as at least 4 samples at `sample_rate_hz` cross the
// screen. A rate of 0 stands for an unknown rate and limits nothing.
std::vector<double> zoom_steps(double seconds_per_div, double sample_rate_hz);

// `zoom` on the nearest available step and with the centre where the whole screen is inside the record; off when the
// main timebase is as fast.
ZoomSettings constrained(const ZoomSettings& zoom, const TimebaseSettings& timebase, double sample_rate_hz);

// `steps` steps in the 1-2-5 list (negative: closer), holding still the point at `x_fraction` of the screen. Moving out
// past the main timebase switches the zoom off.
ZoomSettings zoom_stepped(const ZoomSettings& zoom, const TimebaseSettings& timebase, double sample_rate_hz, int steps,
                          double x_fraction);

// The slowest step that shows all of [begin, end), fractions of the record, centred on it; off when no step fits.
ZoomSettings zoom_to_range(const TimebaseSettings& timebase, double sample_rate_hz, double begin, double end);

}  // namespace starosc
