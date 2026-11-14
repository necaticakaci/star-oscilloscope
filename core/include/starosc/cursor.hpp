// SPDX-License-Identifier: GPL-3.0-or-later
// Cursor readouts and reading a frame at a screen position.
#pragma once

#include <array>

#include "starosc/engine.hpp"
#include "starosc/settings.hpp"

namespace starosc {

// Time of a vertical cursor at a screen position, relative to the trigger point.
double cursor_time(const ScopeSettings& settings, double x_fraction);

// Voltage of a horizontal cursor in a channel's units (at the probe tip).
double cursor_volts(const ChannelSettings& channel, double y_div);

struct CursorReadout {
    std::array<double, 2> time_s{};
    double delta_t = 0;
    double inverse_delta_t = Measurements::kNone;  // 1/|Δt|, none when the cursors coincide
    std::array<double, 2> volts{};                 // voltage cursors on the source channel
    double delta_v = 0;                            // V2 - V1
};

CursorReadout cursor_readout(const ScopeSettings& settings, const CursorSettings& cursors);

// Linearly interpolated sample of `channel` at a position in the record (not zoomed: on the screen); none outside
// the data.
double value_at(const Frame& frame, int channel, double x_fraction);

}  // namespace starosc
