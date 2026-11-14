// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/measure.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <numbers>
#include <utility>

namespace starosc {
namespace {

// A waveform with fewer samples than this between a quarter and three quarters of its swing has two flat levels.
constexpr double kMaxTransitionShare = 0.25;
// A flat level holds at least this share of the samples on its side of the middle.
constexpr double kMinLevelShare = 0.4;
constexpr int kHistogramBins = 128;
// An edge shorter than this many samples cannot be told from a step: a step with one sample halfway up it reads 1.6
// samples between 10 % and 90 %.
constexpr double kResolvedEdgeSamples = 2;
constexpr double kSameFrequency = 0.01;

struct Levels {
    double top = 0, base = 0;
    bool pulse = false;
};

// The flat tops and bottoms of a pulse, from the histogram; a level that is not flat falls back to the extreme.
Levels find_levels(std::span<const float> v, double lo, double hi, double noise_floor)
{
    const double swing = hi - lo;
    const double width = swing / kHistogramBins;
    std::array<std::size_t, kHistogramBins> count{};
    std::array<double, kHistogramBins> sum{};
    std::size_t transition = 0;
    for (float x : v) {
        const double at = (x - lo) / swing;
        const int bin = std::min(static_cast<int>(at * kHistogramBins), kHistogramBins - 1);
        ++count[static_cast<std::size_t>(bin)];
        sum[static_cast<std::size_t>(bin)] += x;
        transition += at > 0.25 && at < 0.75;
    }
    Levels out{hi, lo};
    if (static_cast<double>(transition) >= kMaxTransitionShare * static_cast<double>(v.size()))
        return out;
    out.pulse = true;

    // A level spreads over 4 % of the swing or over the noise (the floor is 4 lsb), whichever is wider.
    const int band = std::clamp(static_cast<int>(std::ceil(std::max(0.04 * swing, 0.75 * noise_floor) / width)), 1,
                                kHistogramBins / 2);
    const auto level = [&](int from, int to, double fallback) {
        std::size_t side = 0, best = 0;
        int best_from = from;
        for (int b = from; b < to; ++b)
            side += count[static_cast<std::size_t>(b)];
        for (int b = from; b + band <= to; ++b) {
            std::size_t in_band = 0;
            for (int k = b; k < b + band; ++k)
                in_band += count[static_cast<std::size_t>(k)];
            if (in_band > best) {
                best = in_band;
                best_from = b;
            }
        }
        if (best == 0 || static_cast<double>(best) < kMinLevelShare * static_cast<double>(side))
            return fallback;
        double total = 0;
        for (int k = best_from; k < best_from + band; ++k)
            total += sum[static_cast<std::size_t>(k)];
        return total / static_cast<double>(best);
    };
    out.top = level(kHistogramBins / 2, kHistogramBins, hi);
    out.base = level(0, kHistogramBins / 2, lo);
    return out;
}

// Crossings of `mid` with hysteresis: an edge counts once the signal has gone past mid ± hysteresis, and it sits at
// the last crossing of mid before that. Fractional sample positions.
Edges find_edges(std::span<const float> v, double mid, double hysteresis)
{
    Edges out;
    enum class Side { Unknown, Below, Above } side = Side::Unknown;
    double up = 0, down = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const double x = v[i];
        if (i > 0) {
            const double before = v[i - 1];
            if (before < mid && x >= mid)
                up = static_cast<double>(i - 1) + (mid - before) / (x - before);
            else if (before >= mid && x < mid)
                down = static_cast<double>(i - 1) + (before - mid) / (before - x);
        }
        if (x <= mid - hysteresis) {
            if (side == Side::Above)
                out.falling.push_back(down);
            side = Side::Below;
        } else if (x >= mid + hysteresis) {
            if (side == Side::Below)
                out.rising.push_back(up);
            side = Side::Above;
        }
    }
    return out;
}

// Mean time in samples from the last sample beyond `from` to the first beyond `to`, over the edges `at`. `others`
// are the opposite edges, which bound the search. Edges cut off by the ends of the samples do not count.
double mean_edge_samples(std::span<const float> v, const std::vector<double>& at, const std::vector<double>& others,
                         double from, double to)
{
    const bool rising = to > from;
    const auto before_from = [&](double x) { return rising ? x < from : x > from; };
    const auto past_to = [&](double x) { return rising ? x >= to : x <= to; };
    double total = 0;
    std::size_t n = 0;
    auto other = others.begin();
    for (double c : at) {
        while (other != others.end() && *other < c)
            ++other;
        const auto first = other == others.begin() ? std::size_t{0} : static_cast<std::size_t>(*std::prev(other));
        const auto last = other == others.end() ? v.size() - 1 : static_cast<std::size_t>(*other);

        auto i = static_cast<std::size_t>(c);
        while (i > first && !before_from(v[i]))
            --i;
        if (!before_from(v[i]))
            continue;
        auto k = static_cast<std::size_t>(c) + 1;
        while (k < last && !past_to(v[k]))
            ++k;
        if (k > last || k >= v.size() || !past_to(v[k]))
            continue;
        const double start = static_cast<double>(i) + (from - v[i]) / (v[i + 1] - v[i]);
        const double stop = static_cast<double>(k - 1) + (to - v[k - 1]) / (v[k] - v[k - 1]);
        total += stop - start;
        ++n;
    }
    return n ? total / static_cast<double>(n) : Measurements::kNone;
}

// Angle of the component at `w` radians per sample in [from, to), without the DC part.
double angle_at(std::span<const float> v, std::size_t from, std::size_t to, double w)
{
    double mean = 0;
    for (std::size_t i = from; i < to; ++i)
        mean += v[i];
    mean /= static_cast<double>(to - from);
    const double step_c = std::cos(w), step_s = std::sin(w);
    double c = 1, s = 0, re = 0, im = 0;
    for (std::size_t i = from; i < to; ++i) {
        const double x = v[i] - mean;
        re += x * c;
        im += x * s;
        const double next = c * step_c - s * step_s;
        s = s * step_c + c * step_s;
        c = next;
    }
    return std::atan2(im, re);
}

// The element of `edges`, which is not empty and ascends, nearest to `x`.
double nearest_edge(const std::vector<double>& edges, double x)
{
    const auto after = std::lower_bound(edges.begin(), edges.end(), x);
    if (after == edges.begin())
        return *after;
    if (after == edges.end() || x - *std::prev(after) < *after - x)
        return *std::prev(after);
    return *after;
}

// Mean of (b - a) over each edge of `a` and the first edge of `b` after it.
double mean_width(const std::vector<double>& a, const std::vector<double>& b)
{
    double total = 0;
    std::size_t n = 0;
    auto next = b.begin();
    for (double x : a) {
        while (next != b.end() && *next < x)
            ++next;
        if (next == b.end())
            break;
        total += *next - x;
        ++n;
    }
    return n ? total / static_cast<double>(n) : Measurements::kNone;
}

}  // namespace

std::string_view parameter_name(Parameter p)
{
    switch (p) {
    case Parameter::Frequency: return "Frequency";
    case Parameter::Period: return "Period";
    case Parameter::RiseTime: return "Rise time";
    case Parameter::FallTime: return "Fall time";
    case Parameter::PositiveWidth: return "Positive width";
    case Parameter::NegativeWidth: return "Negative width";
    case Parameter::Duty: return "Duty cycle";
    case Parameter::PeakToPeak: return "Peak-to-peak";
    case Parameter::Amplitude: return "Amplitude";
    case Parameter::Max: return "Maximum";
    case Parameter::Min: return "Minimum";
    case Parameter::Top: return "Top";
    case Parameter::Base: return "Base";
    case Parameter::Mean: return "Mean";
    case Parameter::Rms: return "RMS";
    case Parameter::AcRms: return "AC RMS";
    case Parameter::Overshoot: return "Overshoot";
    case Parameter::Undershoot: return "Undershoot";
    case Parameter::Delay: return "Delay";
    case Parameter::Phase: return "Phase";
    }
    return "";
}

std::string_view parameter_unit(Parameter p)
{
    switch (p) {
    case Parameter::Frequency: return "Hz";
    case Parameter::Period:
    case Parameter::RiseTime:
    case Parameter::FallTime:
    case Parameter::PositiveWidth:
    case Parameter::NegativeWidth:
    case Parameter::Delay: return "s";
    case Parameter::Duty:
    case Parameter::Overshoot:
    case Parameter::Undershoot: return "%";
    case Parameter::Phase: return "°";
    default: return "V";
    }
}

MeasureThresholds constrained(MeasureThresholds t)
{
    t.low = std::clamp(t.low, 1.0, 97.0);
    t.mid = std::clamp(t.mid, t.low + 1, 98.0);
    t.high = std::clamp(t.high, t.mid + 1, 99.0);
    return t;
}

double Measurements::value(Parameter p) const
{
    switch (p) {
    case Parameter::Frequency: return frequency;
    case Parameter::Period: return period;
    case Parameter::RiseTime: return rise_time;
    case Parameter::FallTime: return fall_time;
    case Parameter::PositiveWidth: return positive_width;
    case Parameter::NegativeWidth: return negative_width;
    case Parameter::Duty: return duty;
    case Parameter::PeakToPeak: return peak_to_peak;
    case Parameter::Amplitude: return amplitude;
    case Parameter::Max: return max;
    case Parameter::Min: return min;
    case Parameter::Top: return top;
    case Parameter::Base: return base;
    case Parameter::Mean: return mean;
    case Parameter::Rms: return rms;
    case Parameter::AcRms: return ac_rms;
    case Parameter::Overshoot: return overshoot;
    case Parameter::Undershoot: return undershoot;
    case Parameter::Delay:
    case Parameter::Phase: break;
    }
    return kNone;
}

bool Measurements::unresolved(Parameter p) const
{
    return (p == Parameter::RiseTime && rise_unresolved) || (p == Parameter::FallTime && fall_unresolved);
}

double PairMeasurements::value(Parameter p) const
{
    return p == Parameter::Delay ? delay : p == Parameter::Phase ? phase : Measurements::kNone;
}

Measurements measure(std::span<const float> samples, double sample_interval_s, double noise_floor,
                     const MeasureThresholds& thresholds, Edges* edges_out)
{
    Measurements m;
    if (edges_out)
        *edges_out = {};
    if (samples.empty())
        return m;

    const auto [lo, hi] = std::minmax_element(samples.begin(), samples.end());
    m.min = *lo;
    m.max = *hi;
    m.peak_to_peak = m.max - m.min;
    double sum = 0, sum_sq = 0;
    for (float x : samples) {
        sum += x;
        sum_sq += static_cast<double>(x) * x;
    }
    const auto n = static_cast<double>(samples.size());
    m.mean = sum / n;
    m.rms = std::sqrt(sum_sq / n);
    m.ac_rms = std::sqrt(std::max(0.0, sum_sq / n - m.mean * m.mean));
    m.top = m.max;
    m.base = m.min;
    m.amplitude = m.peak_to_peak;

    if (m.peak_to_peak <= noise_floor || m.peak_to_peak <= 0)
        return m;
    const Levels levels = find_levels(samples, m.min, m.max, noise_floor);
    m.top = levels.top;
    m.base = levels.base;
    m.pulse = levels.pulse;
    m.amplitude = m.top - m.base;
    m.overshoot = 100 * (m.max - m.top) / m.amplitude;
    m.undershoot = 100 * (m.base - m.min) / m.amplitude;

    const auto at_percent = [&](double percent) { return m.base + percent / 100 * m.amplitude; };
    const double low = at_percent(thresholds.low), mid = at_percent(thresholds.mid), high = at_percent(thresholds.high);
    const double hysteresis = std::min({0.1 * m.amplitude, (mid - m.base) / 2, (m.top - mid) / 2});
    Edges edges = find_edges(samples, mid, hysteresis);

    m.resolution_s = kResolvedEdgeSamples * sample_interval_s;
    const double rise = mean_edge_samples(samples, edges.rising, edges.falling, low, high);
    const double fall = mean_edge_samples(samples, edges.falling, edges.rising, high, low);
    m.rise_time = rise * sample_interval_s;
    m.fall_time = fall * sample_interval_s;
    m.rise_unresolved = rise < kResolvedEdgeSamples;
    m.fall_unresolved = fall < kResolvedEdgeSamples;
    m.positive_width = mean_width(edges.rising, edges.falling) * sample_interval_s;
    m.negative_width = mean_width(edges.falling, edges.rising) * sample_interval_s;

    // Rising edges, or falling ones when only those repeat: two periods on the screen with the trigger in the middle
    // have the other rising edges exactly at its ends.
    const bool by_rising = edges.rising.size() >= 2;
    const std::vector<double>& cycle = by_rising ? edges.rising : edges.falling;
    const std::vector<double>& opposite = by_rising ? edges.falling : edges.rising;
    if (cycle.size() >= 2) {
        const double span = cycle.back() - cycle.front();
        m.period = span / static_cast<double>(cycle.size() - 1) * sample_interval_s;
        m.frequency = 1.0 / m.period;
        double part = 0;  // of each cycle, up to the opposite edge
        auto next = opposite.begin();
        for (std::size_t i = 0; i + 1 < cycle.size(); ++i) {
            while (next != opposite.end() && *next < cycle[i])
                ++next;
            if (next != opposite.end() && *next < cycle[i + 1])
                part += *next - cycle[i];
        }
        const double share = 100.0 * part / span;
        m.duty = by_rising ? share : 100 - share;
    }
    if (edges_out)
        *edges_out = std::move(edges);
    return m;
}

PairMeasurements measure_pair(const PairInput& trace_a, const PairInput& trace_b, double sample_interval_s)
{
    PairMeasurements out;
    const std::vector<double>& rising_a = trace_a.edges.rising;
    const std::vector<double>& rising_b = trace_b.edges.rising;
    if (rising_a.empty() || rising_b.empty())
        return out;
    const double period_a_s = trace_a.measurements.period;
    const double period_b_s = trace_b.measurements.period;
    const bool periodic = std::isfinite(period_a_s) && std::isfinite(period_b_s);
    if (periodic && std::abs(period_a_s - period_b_s) > kSameFrequency * period_a_s)
        return out;
    const auto wrapped = [](double degrees) {
        const double phase = std::remainder(degrees, 360.0);
        return phase == -180 ? 180 : phase;
    };

    if (periodic && !(trace_a.measurements.pulse && trace_b.measurements.pulse)) {
        // A whole number of periods, the same samples on both traces.
        const std::vector<double>& cycle = rising_a.size() >= 2 ? rising_a : trace_a.edges.falling;
        const auto from = static_cast<std::size_t>(std::ceil(cycle.front()));
        const auto length = static_cast<std::size_t>(std::lround(cycle.back() - cycle.front()));
        const std::size_t to = std::min({from + length, trace_a.samples.size(), trace_b.samples.size()});
        if (to <= from)
            return out;
        const double w = 2 * std::numbers::pi * sample_interval_s / period_a_s;
        const double angle = std::remainder(angle_at(trace_b.samples, from, to, w) - angle_at(trace_a.samples, from, to, w),
                                            2 * std::numbers::pi);
        out.delay = angle / w * sample_interval_s;
        out.phase = wrapped(angle * 180 / std::numbers::pi);
        return out;
    }

    // The first edge of A picks the edge of B to pair it with. Later edges keep to that pairing, so a delay of half a
    // period does not flip between plus and minus. An edge with no partner (the ends of the record) is out; B may lack
    // the very edge that A starts with, so the pairing is taken within one period.
    double first = nearest_edge(rising_b, rising_a.front()) - rising_a.front();
    if (!periodic) {
        out.delay = first * sample_interval_s;
        return out;
    }
    const double period = period_a_s / sample_interval_s;
    first = std::remainder(first, period);
    double total = 0;
    std::size_t n = 0;
    for (double edge : rising_a) {
        const double partner = nearest_edge(rising_b, edge + first);
        if (std::abs(partner - edge - first) > period / 4)
            continue;
        total += partner - edge;
        ++n;
    }
    out.delay = (n ? total / static_cast<double>(n) : first) * sample_interval_s;
    out.phase = wrapped(out.delay / period_a_s * 360);
    return out;
}

void Statistics::add(double x)
{
    if (!std::isfinite(x))
        return;
    if (count_ == 0) {
        min_ = max_ = x;
    } else {
        min_ = std::min(min_, x);
        max_ = std::max(max_, x);
    }
    ++count_;
    const double delta = x - mean_;
    mean_ += delta / static_cast<double>(count_);
    m2_ += delta * (x - mean_);
}

}  // namespace starosc
