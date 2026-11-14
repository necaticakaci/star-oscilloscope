// SPDX-License-Identifier: GPL-3.0-or-later
// Reference waveforms: a stored trace shown next to the live ones, saved as CSV.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "starosc/engine.hpp"

namespace starosc {

inline constexpr int kReferences = 2;
// Sources a reference can be taken from: the channels, then the math trace.
inline constexpr int kMathSource = kChannels;

struct Reference {
    std::string source;           // "CH1", "CH2" or "Math"
    std::string unit = "V";
    std::vector<float> samples;   // NaN where the source had no value
    double sample_interval_s = 0;
    double start_s = 0;           // time of samples[0] from the trigger point
    double units_per_div = 1;     // starts as the source's scale; the reference keeps its own afterwards
    double position_div = 0;
    bool shown = true;

    bool empty() const { return samples.empty(); }
};

// Time of the screen's left edge from the trigger point.
double screen_start_s(const ScopeSettings& settings);

// The trace `source` (0 or 1 for a channel, kMathSource for the math trace) of `frame`, with its scale and
// position. Nothing when the frame has no such trace.
std::optional<Reference> make_reference(const Frame& frame, int source);

// CSV: header lines start with '#', then "time_s,value" rows.
std::string reference_to_csv(const Reference& ref);
std::optional<Reference> reference_from_csv(std::string_view text, std::string* error = nullptr);

}  // namespace starosc
