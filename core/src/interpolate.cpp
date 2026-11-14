// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/interpolate.hpp"

#include <cmath>
#include <numbers>

namespace starosc {
namespace {

double sinc(double x)
{
    if (std::abs(x) < 1e-9)
        return 1;
    const double px = std::numbers::pi * x;
    return std::sin(px) / px;
}

}  // namespace

std::vector<double> sinc_interpolate(std::span<const float> samples, std::span<const float> before,
                                     std::span<const float> after, double start, double step, std::size_t count)
{
    std::vector<double> out(count);
    if (samples.empty())
        return out;
    const auto n = static_cast<long>(samples.size());
    const auto lead = static_cast<long>(before.size());
    const auto trail = static_cast<long>(after.size());
    // Sample k of the extended run: before, samples, after; the ends repeat.
    const auto at = [&](long k) -> double {
        if (k < -lead)
            k = -lead;
        if (k >= n + trail)
            k = n + trail - 1;
        if (k < 0)
            return before[static_cast<std::size_t>(lead + k)];
        if (k >= n)
            return after[static_cast<std::size_t>(k - n)];
        return samples[static_cast<std::size_t>(k)];
    };
    for (std::size_t i = 0; i < count; ++i) {
        const double s = start + step * static_cast<double>(i);
        const auto base = static_cast<long>(std::floor(s));
        double sum = 0, weights = 0;
        for (long k = base - kSincTaps + 1; k <= base + kSincTaps; ++k) {
            const double d = s - static_cast<double>(k);
            const double w = sinc(d) * sinc(d / kSincTaps);
            sum += w * at(k);
            weights += w;
        }
        out[i] = weights != 0 ? sum / weights : at(base);
    }
    return out;
}

}  // namespace starosc
