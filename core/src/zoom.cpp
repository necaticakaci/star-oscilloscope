// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/zoom.hpp"

#include <algorithm>
#include <cmath>

#include "starosc/scale.hpp"

namespace starosc {
namespace {

constexpr double kMinSamplesOnScreen = 4;
constexpr double kFastestStep = 1e-9;
constexpr double kAlmostOne = 0.9999;

// Keeps the centre where the screen stays inside the record.
ZoomSettings at_center(double seconds_per_div, double center, const TimebaseSettings& timebase)
{
    const double half = seconds_per_div / timebase.seconds_per_div / 2;
    return {seconds_per_div, std::clamp(center, half, 1 - half)};
}

}  // namespace

VisibleRange visible_range(const TimebaseSettings& timebase, const ZoomSettings& zoom)
{
    if (!zoom.on() || zoom.seconds_per_div >= timebase.seconds_per_div)
        return {};
    const double span = zoom.seconds_per_div / timebase.seconds_per_div;
    return {std::clamp(zoom.center - span / 2, 0.0, 1 - span), span};
}

std::vector<double> zoom_steps(double seconds_per_div, double sample_rate_hz)
{
    const double fastest =
        sample_rate_hz > 0 ? std::max(kFastestStep, kMinSamplesOnScreen / (kDivisionsX * sample_rate_hz)) : kFastestStep;
    std::vector<double> out;
    for (double decade = 100; decade > 1e-10; decade /= 10)
        for (double m : {5.0, 2.0, 1.0}) {
            const double step = decade * m;
            if (step < seconds_per_div * kAlmostOne && step >= fastest * kAlmostOne)
                out.push_back(step);
        }
    return out;
}

ZoomSettings constrained(const ZoomSettings& zoom, const TimebaseSettings& timebase, double sample_rate_hz)
{
    if (!zoom.on())
        return {0, zoom.center};
    const std::vector<double> steps = zoom_steps(timebase.seconds_per_div, sample_rate_hz);
    if (steps.empty() || zoom.seconds_per_div >= timebase.seconds_per_div * kAlmostOne)
        return {0, zoom.center};
    return at_center(step_value(steps, zoom.seconds_per_div, 0), zoom.center, timebase);
}

ZoomSettings zoom_stepped(const ZoomSettings& zoom, const TimebaseSettings& timebase, double sample_rate_hz, int steps,
                          double x_fraction)
{
    // Slowest first, so a step closer is a step down the list; the main timebase is the end of the list.
    std::vector<double> list = zoom_steps(timebase.seconds_per_div, sample_rate_hz);
    list.insert(list.begin(), timebase.seconds_per_div);
    const double now = zoom.on() ? zoom.seconds_per_div : timebase.seconds_per_div;
    std::size_t at = 0;
    for (std::size_t i = 1; i < list.size(); ++i)
        if (std::abs(std::log(list[i] / now)) < std::abs(std::log(list[at] / now)))
            at = i;
    const auto target = std::clamp<long>(static_cast<long>(at) - steps, 0, static_cast<long>(list.size()) - 1);
    if (target == 0)
        return {0, zoom.center};

    const double next = list[static_cast<std::size_t>(target)];
    const VisibleRange range = visible_range(timebase, zoom);
    const double point = range.begin + x_fraction * range.span;
    const double span = next / timebase.seconds_per_div;
    return at_center(next, point + (0.5 - x_fraction) * span, timebase);
}

ZoomSettings zoom_to_range(const TimebaseSettings& timebase, double sample_rate_hz, double begin, double end)
{
    const std::vector<double> steps = zoom_steps(timebase.seconds_per_div, sample_rate_hz);
    const double wanted = std::abs(end - begin) * timebase.seconds_per_div;
    const double center = (begin + end) / 2;
    if (steps.empty() || wanted > steps.front())
        return {0, center};
    double chosen = steps.back();
    for (double step : steps)
        if (step >= wanted)
            chosen = step;
    return at_center(chosen, center, timebase);
}

}  // namespace starosc
