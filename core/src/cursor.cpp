// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/cursor.hpp"

#include <cmath>

#include "starosc/zoom.hpp"

namespace starosc {

double cursor_time(const ScopeSettings& settings, double x_fraction)
{
    const VisibleRange range = visible_range(settings.timebase, settings.zoom);
    const double in_record = range.begin + x_fraction * range.span;
    return (in_record - settings.trigger.pre_trigger) * kDivisionsX * settings.timebase.seconds_per_div;
}

double cursor_volts(const ChannelSettings& channel, double y_div)
{
    return (y_div - channel.position_div) * channel.volts_per_div;
}

CursorReadout cursor_readout(const ScopeSettings& settings, const CursorSettings& cursors)
{
    CursorReadout r;
    const ChannelSettings& source = settings.channel[cursors.source == 1 ? 1 : 0];
    for (int i = 0; i < 2; ++i) {
        r.time_s[i] = cursor_time(settings, cursors.x[i]);
        r.volts[i] = cursor_volts(source, cursors.y_div[i]);
    }
    r.delta_t = r.time_s[1] - r.time_s[0];
    if (r.delta_t != 0)
        r.inverse_delta_t = 1.0 / std::abs(r.delta_t);
    r.delta_v = r.volts[1] - r.volts[0];
    return r;
}

double value_at(const Frame& frame, int channel, double x_fraction)
{
    const auto& v = frame.volts[channel];
    if (v.empty() || frame.plan.window_samples == 0)
        return Measurements::kNone;
    const double position = x_fraction * static_cast<double>(frame.plan.window_samples) + frame.start_fraction;
    if (position < 0 || position > static_cast<double>(v.size() - 1))
        return Measurements::kNone;
    const auto i = static_cast<std::size_t>(std::floor(position));
    if (i + 1 >= v.size())
        return v[i];
    const double f = position - static_cast<double>(i);
    return v[i] + f * (v[i + 1] - v[i]);
}

}  // namespace starosc
