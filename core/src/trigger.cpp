// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/trigger.hpp"

#include <algorithm>
#include <cmath>

namespace starosc {

namespace {

// Hands each armed crossing in [first, last) to `found`, in order, for as long as it returns true.
template <typename Found>
void crossings(std::span<const float> samples, double level, double hysteresis, TriggerEdge edge, std::size_t first,
               std::size_t last, Found found)
{
    const double h = std::max(hysteresis, 0.0);
    const bool want_rising = edge != TriggerEdge::Falling;
    const bool want_falling = edge != TriggerEdge::Rising;
    bool armed_rising = false;
    bool armed_falling = false;

    // A crossing between samples i-1 and i lies in [i-1, i), so i runs up to `last` inclusive.
    const std::size_t end = std::min(last + 1, samples.size());
    for (std::size_t i = 0; i < end; ++i) {
        const double x = samples[i];
        if (i > 0) {
            const double prev = samples[i - 1];
            const double position = static_cast<double>(i - 1) + (level - prev) / (x - prev);
            const bool in_window = position >= static_cast<double>(first) && position < static_cast<double>(last);
            if (armed_rising && prev < level && x >= level) {
                if (want_rising && in_window && !found(position))
                    return;
                armed_rising = false;
            }
            if (armed_falling && prev > level && x <= level) {
                if (want_falling && in_window && !found(position))
                    return;
                armed_falling = false;
            }
        }
        if (x < level - h)
            armed_rising = true;
        if (x > level + h)
            armed_falling = true;
    }
}

}  // namespace

std::optional<double> find_trigger(std::span<const float> samples, double level, double hysteresis,
                                   TriggerEdge edge, std::size_t first, std::size_t last)
{
    std::optional<double> trigger;
    crossings(samples, level, hysteresis, edge, first, last, [&](double position) {
        trigger = position;
        return false;
    });
    return trigger;
}

std::optional<double> find_trigger_near(std::span<const float> samples, double level, double hysteresis,
                                        TriggerEdge edge, std::size_t first, std::size_t last, double target)
{
    std::optional<double> nearest;
    crossings(samples, level, hysteresis, edge, first, last, [&](double position) {
        if (!nearest || std::abs(position - target) < std::abs(*nearest - target))
            nearest = position;
        return position < target;  // the ones after are only farther
    });
    return nearest;
}

}  // namespace starosc
