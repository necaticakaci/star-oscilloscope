// SPDX-License-Identifier: GPL-3.0-or-later
// The math trace: sum, difference, product or ratio of two channels.
#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "starosc/settings.hpp"

namespace starosc {

// "V", "V²" or "U" (a ratio has no unit; bench scopes write U).
std::string_view math_unit(MathOperation op);
// "+", "−", "×", "÷"
std::string_view math_symbol(MathOperation op);

// a op b for each sample pair; the shorter input sets the length. A ratio whose divisor is closer to zero than
// `smallest_divisor` is NaN, and drawing and measuring skip it.
std::vector<float> compute_math(MathOperation op, std::span<const float> a, std::span<const float> b,
                                double smallest_divisor);

}  // namespace starosc
