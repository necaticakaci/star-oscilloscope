// SPDX-License-Identifier: GPL-3.0-or-later
// Standard 1-2-5 control steps and SI formatting for readouts.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace starosc {

// In 1-2-5 steps at the probe tip: about 5 mV/div .. 2 V/div at the BNC. A factor like 10.15 keeps round steps
// (50 mV/div .. 20 V/div), so the BNC range may differ by up to 10 %.
std::vector<double> volts_per_div_steps(double probe = 1);
std::vector<double> seconds_per_div_steps();  // 100 ns/div .. 100 ms/div
std::vector<double> units_per_div_steps();    // scales of math traces and references: 1 m .. 100 a division

// Moves `delta` steps from the step closest to `current`, clamped to the ends of `steps`.
double step_value(std::span<const double> steps, double current, int delta);

// format_si(1234.5, "Hz") == "1.235 kHz"; NaN and infinities become "-----".
// With trim_zeros, trailing zeros are dropped: format_si(0.5e-3, "s/div", 3, true) == "500 µs/div".
std::string format_si(double value, std::string_view unit, int significant_digits = 4, bool trim_zeros = false);

}  // namespace starosc
