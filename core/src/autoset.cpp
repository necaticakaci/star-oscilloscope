// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/autoset.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "starosc/measure.hpp"
#include "starosc/scale.hpp"

namespace starosc {
namespace {

constexpr double kMinSignalV = 0.02;              // at the BNC
constexpr double kWideV = 1000;                   // no stage covers this, so the source takes its widest
constexpr std::size_t kMidSamples = 64 * 1024;    // at the fastest rate that keeps them contiguous
constexpr double kSlowSeconds = 0.3;              // at the slowest rate: three periods of 10 Hz
constexpr std::size_t kSettleSamples = 4096;
constexpr int kPasses = 3;                        // the widest stage, then two finer ones
constexpr double kMinPeriods = 3;                 // a frequency counts when the capture holds this many periods
constexpr double kMinSwingFraction = 0.8;         // ... and this much of the whole swing
constexpr double kMinPeriodsOnScreen = 2.5;

struct Capture {
    std::uint32_t rate = 0;
    std::size_t samples = 0;
    double seconds() const { return static_cast<double>(samples) / rate; }
};

// What one pass saw on a channel, at the BNC.
struct Seen {
    double min = INFINITY, max = -INFINITY;
    double lsb = 0;
    double frequency = 0;  // 0: no periodic signal
    bool valid() const { return min <= max; }
    double swing() const { return max - min; }
    double middle() const { return (max + min) / 2; }
};

using Ranges = std::array<VoltageRange, kChannels>;

Acquisition acquire(Source& source, const Capture& c, const Ranges& ranges, const ScopeSettings& current)
{
    AcquisitionRequest r;
    r.sample_rate_hz = c.rate;
    r.samples = c.samples;
    for (int ch = 0; ch < kChannels; ++ch)
        r.channel[ch] = {true, ranges[ch], current.channel[ch].coupling};
    return source.acquire(r);
}

// The frequency when the capture holds enough periods, and enough samples per period, to trust it.
// A capture shorter than a period sees only part of a slope, where noise makes false edges; it is
// recognised by a swing well below `swing`, the swing seen over all captures.
double frequency_of(const ChannelData& cd, const Capture& c, double swing)
{
    const Measurements m = measure(cd.volts, 1.0 / c.rate, 4 * cd.lsb_v);
    if (!std::isfinite(m.frequency) || m.frequency * c.seconds() < kMinPeriods || m.frequency > c.rate / 2.5 ||
        m.peak_to_peak < kMinSwingFraction * swing)
        return 0;
    return m.frequency;
}

double round_to(double value, double step) { return std::round(value / step) * step; }

// Smallest V/div step that fits `swing` into `height` divisions and keeps the trace's 0 V marker on screen
// with the signal's middle at `target` divisions.
double pick_volts_per_div(double swing, double middle, double target, double height, double probe)
{
    const double half = kDivisionsY / 2.0;
    const std::vector<double> steps = volts_per_div_steps(probe);
    for (double v : steps)
        if (swing / v <= height && std::abs(target - middle / v) <= half)
            return v;
    return steps.back();
}

}  // namespace

AutosetResult autoset(Source& source, const ScopeSettings& current)
{
    // Captures from the fastest to the slowest. The fastest rate may keep only a few contiguous samples
    // (1020 on the ISDS205X) but is the only one that sees the highest frequencies without aliasing; the middle
    // one is the fastest rate with a long contiguous capture; the slowest reaches down to 10 Hz.
    const std::vector<std::uint32_t> rates = source.sample_rates();
    const auto mid_rate = std::find_if(rates.rbegin(), rates.rend(), [&](std::uint32_t r) {
        return source.max_contiguous_samples(r) >= kMidSamples;
    });
    std::vector<Capture> captures{{rates.back(), std::min(source.max_contiguous_samples(rates.back()), kMidSamples)}};
    if (mid_rate != rates.rend() && *mid_rate != rates.back())
        captures.push_back({*mid_rate, kMidSamples});
    captures.push_back({rates.front(), std::min(source.max_contiguous_samples(rates.front()),
                                                static_cast<std::size_t>(kSlowSeconds * rates.front()))});
    const Capture settle{rates.back(), std::min(captures.front().samples, kSettleSamples)};

    Ranges request;
    request.fill({-kWideV, kWideV});
    std::array<Seen, kChannels> seen{};
    for (int pass = 0; pass < kPasses; ++pass) {
        acquire(source, settle, request, current);  // the first capture after a stage change is dropped
        std::vector<Acquisition> got;
        for (const Capture& c : captures)
            got.push_back(acquire(source, c, request, current));
        for (int ch = 0; ch < kChannels; ++ch) {
            const bool clipped = std::ranges::any_of(got, [ch](const Acquisition& a) { return a.channel[ch].clipped; });
            if (pass > 0 && clipped) {
                // Something the wider stage missed: keep what it saw, ask for more room.
                const Seen& s = seen[ch];
                const double half = std::max(s.swing(), 2 * s.lsb);
                request[ch] = {s.middle() - 2 * half, s.middle() + 2 * half};
                continue;
            }
            Seen s;
            for (const Acquisition& a : got) {
                const ChannelData& cd = a.channel[ch];
                if (cd.volts.empty())
                    continue;
                const auto [lo, hi] = std::minmax_element(cd.volts.begin(), cd.volts.end());
                s.min = std::min<double>(s.min, *lo);
                s.max = std::max<double>(s.max, *hi);
                s.lsb = std::max(s.lsb, cd.lsb_v);
            }
            for (std::size_t i = 0; i < captures.size() && s.frequency == 0; ++i)
                s.frequency = frequency_of(got[i].channel[ch], captures[i], s.swing());
            seen[ch] = s;
            if (s.valid()) {
                const double pad = std::max({0.1 * s.swing(), 2 * s.lsb, 0.005});
                request[ch] = {s.min - pad, s.max + pad};
            }
        }
    }

    AutosetResult result;
    result.settings = current;
    std::vector<int> found;
    for (int ch = 0; ch < kChannels; ++ch) {
        const Seen& s = seen[ch];
        result.found[ch] = s.valid() && (s.swing() >= kMinSignalV || std::abs(s.middle()) >= kMinSignalV);
        if (result.found[ch])
            found.push_back(ch);
    }
    if (found.empty())
        return result;

    ScopeSettings& out = result.settings;
    out.zoom = {};
    // Two traces in Y-T go one above the other; in X-Y both are axes through the middle.
    const bool apart = found.size() == kChannels && current.display == DisplayMode::YT;
    for (int ch = 0; ch < kChannels; ++ch) {
        ChannelSettings& cs = out.channel[ch];
        cs.enabled = result.found[ch];
        if (!cs.enabled)
            continue;
        const double target = apart ? (ch == 0 ? 2.0 : -2.0) : 0.0;
        const double height = apart ? 3.5 : 6.0;
        const double swing = seen[ch].swing() * cs.probe;
        const double middle = seen[ch].middle() * cs.probe;
        cs.volts_per_div = pick_volts_per_div(swing, middle, target, height, cs.probe);
        const double half = kDivisionsY / 2.0;
        cs.position_div = round_to(std::clamp(target - middle / cs.volts_per_div, -half, half), 0.02);
    }

    const auto periodic = [&](int ch) { return result.found[ch] && seen[ch].frequency > 0; };
    int source_ch = std::clamp(current.trigger.source, 0, kChannels - 1);
    if (!periodic(source_ch)) {
        const auto it = std::find_if(found.begin(), found.end(), periodic);
        source_ch = it != found.end() ? *it : (result.found[source_ch] ? source_ch : found.front());
    }
    TriggerSettings& t = out.trigger;
    t.source = source_ch;
    t.mode = TriggerMode::Auto;
    t.edge = TriggerEdge::Rising;
    t.pre_trigger = 0.5;
    const ChannelSettings& tcs = out.channel[source_ch];
    t.level_v = round_to(seen[source_ch].middle() * tcs.probe, tcs.volts_per_div / 50);

    result.frequency_hz = seen[source_ch].frequency;
    if (result.frequency_hz > 0) {
        const std::vector<double> steps = seconds_per_div_steps();
        const auto it = std::find_if(steps.begin(), steps.end(), [&](double s) {
            return kDivisionsX * s * result.frequency_hz >= kMinPeriodsOnScreen;
        });
        out.timebase.seconds_per_div = it != steps.end() ? *it : steps.back();
    }
    return result;
}

}  // namespace starosc
