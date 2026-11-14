// SPDX-License-Identifier: GPL-3.0-or-later
// sin(x)/x reconstruction of sparse samples for drawing, as bench oscilloscopes do at fast timebases.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace starosc {

// Taps on each side of a point; enough for signals up to about 0.4 of the sample rate.
inline constexpr int kSincTaps = 8;

// Above this many samples a period sin(x)/x brings nothing over straight lines; it only smooths the ADC steps and the
// noise into a wavy trace (bench check 2026-11-06, docs/measurements.md §10).
inline constexpr double kSincMaxSamplesPerPeriod = 20;

// Whether sin(x)/x helps a signal of this many samples a period; 0 or NaN for a signal with no known period.
inline bool sinc_helps(double samples_per_period) { return !(samples_per_period > kSincMaxSamplesPerPeriod); }

// Values at positions start, start + step, … (count of them), in units of sample index into `samples`.
// `before` and `after` hold samples just outside `samples` from the same contiguous run (up to kSincTaps
// each); beyond those the nearest sample is repeated. Lanczos-windowed sinc, normalised so a constant stays
// constant.
std::vector<double> sinc_interpolate(std::span<const float> samples, std::span<const float> before,
                                     std::span<const float> after, double start, double step, std::size_t count);

}  // namespace starosc
