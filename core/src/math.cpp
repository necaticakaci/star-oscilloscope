// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace starosc {

std::string_view math_unit(MathOperation op)
{
    switch (op) {
    case MathOperation::Add:
    case MathOperation::Subtract: return "V";
    case MathOperation::Multiply: return "V²";
    case MathOperation::Divide: return "U";
    }
    return "V";
}

std::string_view math_symbol(MathOperation op)
{
    switch (op) {
    case MathOperation::Add: return "+";
    case MathOperation::Subtract: return "−";
    case MathOperation::Multiply: return "×";
    case MathOperation::Divide: return "÷";
    }
    return "?";
}

std::vector<float> compute_math(MathOperation op, std::span<const float> a, std::span<const float> b,
                                double smallest_divisor)
{
    const std::size_t n = std::min(a.size(), b.size());
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        switch (op) {
        case MathOperation::Add: out[i] = a[i] + b[i]; break;
        case MathOperation::Subtract: out[i] = a[i] - b[i]; break;
        case MathOperation::Multiply: out[i] = a[i] * b[i]; break;
        case MathOperation::Divide:
            out[i] = std::abs(b[i]) < smallest_divisor ? std::numeric_limits<float>::quiet_NaN() : a[i] / b[i];
            break;
        }
    }
    return out;
}

}  // namespace starosc
