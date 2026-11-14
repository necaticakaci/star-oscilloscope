// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/engine.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

#include "starosc/trigger.hpp"

namespace starosc {
namespace {

// Below the top rate, every this many frames a short capture at the top rate looks for frequencies the
// frame's rate would alias. Also done whenever the rate changes.
constexpr unsigned kAliasCheckEvery = 10;
constexpr double kAliasMinPeriods = 3;
constexpr double kAliasMinSwing = 0.5;  // of the frame's peak-to-peak: a real part of the signal, not noise

}  // namespace

void measure_frame(Frame& frame)
{
    const MeasureThresholds& thresholds = frame.settings.thresholds;
    std::array<Edges, kChannels> edges;
    for (int ch = 0; ch < kChannels; ++ch)
        if (!frame.volts[ch].empty())
            frame.measurements[ch] =
                measure(frame.volts[ch], frame.sample_interval_s, 4 * frame.lsb_v[ch], thresholds, &edges[ch]);
    frame.pair = measure_pair({frame.volts[0], frame.measurements[0], edges[0]},
                              {frame.volts[1], frame.measurements[1], edges[1]}, frame.sample_interval_s);

    if (frame.math.empty())
        return;
    std::vector<float> finite;
    finite.reserve(frame.math.size());
    for (float x : frame.math)
        if (std::isfinite(x))
            finite.push_back(x);
    double noise = frame.math_noise_floor;
    const MathOperation operation = frame.settings.math.operation;
    if (operation == MathOperation::Multiply || operation == MathOperation::Divide) {
        const auto [lo, hi] = std::minmax_element(finite.begin(), finite.end());
        noise = finite.empty() ? 0 : 0.05 * (*hi - *lo);  // no simple lsb for products and ratios
    }
    frame.math_measurements = measure(finite, frame.sample_interval_s, noise, thresholds);
}

std::optional<Frame> Engine::acquire(const ScopeSettings& settings, bool force)
{
    std::vector<RateCapacity> rates;
    for (std::uint32_t rate : source_.sample_rates())
        rates.push_back(source_.capacity(rate));
    const AcquisitionPlan plan = plan_acquisition(settings.timebase, settings.trigger.pre_trigger, rates);

    const int source_ch = std::clamp(settings.trigger.source, 0, kChannels - 1);
    AcquisitionRequest request;
    request.sample_rate_hz = plan.sample_rate_hz;
    request.samples = plan.capture_samples;
    request.trigger_source = source_ch;
    request.trigger_level_v = settings.trigger.level_v / settings.channel[source_ch].probe;
    const MathSettings& ms = settings.math;
    for (int ch = 0; ch < kChannels; ++ch) {
        const ChannelSettings& cs = settings.channel[ch];
        // The trigger source and the math sources are captured even when hidden.
        const bool math_source = ms.enabled && (ch == ms.a || ch == ms.b);
        request.channel[ch] = {cs.enabled || ch == source_ch || math_source, screen_range_at_input(cs), cs.coupling};
    }

    const Acquisition acq = source_.acquire(request);

    // The window, one sample more for drawing, has to stay inside one contiguous run of the capture.
    const std::vector<float>& trig = acq.channel[source_ch].volts;
    const std::size_t captured = trig.size();
    const std::size_t needed = plan.window_samples + 1;
    const std::size_t post = plan.window_samples - plan.pre_samples;
    const ChannelSettings& tcs = settings.channel[source_ch];
    const double hysteresis = settings.trigger.hysteresis_div * tcs.volts_per_div / tcs.probe;
    std::optional<double> position;
    std::size_t last_run_end = 0;  // end of the last run long enough for the window
    for (std::size_t begin = 0; begin < captured;) {
        const std::size_t end = plan.layout.run_end(begin, captured);
        if (end - begin > needed) {
            last_run_end = end;
            if (!force && !position) {
                // The trigger nearest the middle of where it may lie: the window then sits in the middle of the run,
                // with as much of the capture on either side, and the overview strip keeps its frame in one place.
                const std::span<const float> run(trig.data() + begin, end - begin);
                const std::size_t last = run.size() - post - 1;
                const double middle = (static_cast<double>(plan.pre_samples) + static_cast<double>(last)) / 2;
                if (const auto p = find_trigger_near(run, request.trigger_level_v, hysteresis, settings.trigger.edge,
                                                     plan.pre_samples, last, middle))
                    position = *p + static_cast<double>(begin);
            }
        }
        begin = end;
    }
    if (!position && settings.trigger.mode != TriggerMode::Auto && !force)
        return std::nullopt;

    Frame frame;
    frame.sequence = ++sequence_;
    frame.settings = settings;
    frame.plan = plan;
    frame.sample_interval_s = 1.0 / acq.sample_rate_hz;
    frame.triggered = position.has_value();

    // Untriggered frames show the most recent window.
    const double start = position ? *position - static_cast<double>(plan.pre_samples)
                                  : static_cast<double>(last_run_end > needed ? last_run_end - needed : 0);
    const auto first = static_cast<std::size_t>(std::floor(std::max(start, 0.0)));
    frame.start_fraction = std::max(start, 0.0) - static_cast<double>(first);
    if (captured > 0) {
        frame.overview.window_begin = std::max(start, 0.0) / static_cast<double>(captured);
        frame.overview.window_end =
            std::min(1.0, (std::max(start, 0.0) + static_cast<double>(plan.window_samples)) / static_cast<double>(captured));
    }
    const std::size_t run_begin = plan.layout.run_begin(first);

    for (int ch = 0; ch < kChannels; ++ch) {
        const ChannelSettings& cs = settings.channel[ch];
        const ChannelData& cd = acq.channel[ch];
        frame.sensitive[ch] = cd.sensitive;
        if (!cs.enabled || cd.volts.empty())
            continue;
        const std::size_t run_end = plan.layout.run_end(first, cd.volts.size());
        const std::size_t end = std::min(run_end, first + needed);
        const auto copy = [&](std::size_t from, std::size_t to, std::vector<float>& out) {
            out.reserve(to - from);
            for (std::size_t i = from; i < to; ++i)
                out.push_back(static_cast<float>(cd.volts[i] * cs.probe));
        };
        copy(first, end, frame.volts[ch]);
        const auto margin = static_cast<std::size_t>(kSincTaps);
        copy(std::max(run_begin, first > margin ? first - margin : 0), first, frame.before[ch]);
        copy(end, std::min(run_end, end + margin), frame.after[ch]);
        frame.clipped[ch] = cd.clipped;
        frame.lsb_v[ch] = cd.lsb_v * cs.probe;


        const std::size_t n = cd.volts.size();
        const std::size_t bins = std::min(Overview::kBins, n);
        auto& lo = frame.overview.min[ch];
        auto& hi = frame.overview.max[ch];
        lo.resize(bins);
        hi.resize(bins);
        for (std::size_t b = 0; b < bins; ++b) {
            const auto bin_begin = cd.volts.begin() + static_cast<std::ptrdiff_t>(b * n / bins);
            const auto bin_end = cd.volts.begin() + static_cast<std::ptrdiff_t>((b + 1) * n / bins);
            const auto [mn, mx] = std::minmax_element(bin_begin, bin_end);
            lo[b] = static_cast<float>(*mn * cs.probe);
            hi[b] = static_cast<float>(*mx * cs.probe);
        }
    }

    if (ms.enabled)
        add_math(frame, acq, first);
    measure_frame(frame);


    const RateCapacity& top = rates.back();
    if (plan.sample_rate_hz >= top.rate_hz) {
        aliased_ = {};
        alias_rate_ = 0;
    } else if (plan.sample_rate_hz != alias_rate_ || ++frames_since_alias_check_ >= kAliasCheckEvery) {
        check_aliasing(request, top, frame);
        alias_rate_ = plan.sample_rate_hz;
        frames_since_alias_check_ = 0;
    }
    frame.aliased = aliased_;
    return frame;
}

void Engine::add_math(Frame& frame, const Acquisition& acq, std::size_t first)
{
    const MathSettings& ms = frame.settings.math;
    const int a = std::clamp(ms.a, 0, kChannels - 1);
    const int b = std::clamp(ms.b, 0, kChannels - 1);
    const std::vector<float>& va = acq.channel[a].volts;
    const std::vector<float>& vb = acq.channel[b].volts;
    const std::size_t size = std::min(va.size(), vb.size());
    if (size == 0)
        return;
    const double pa = frame.settings.channel[a].probe;
    const double pb = frame.settings.channel[b].probe;
    const double lsb_a = acq.channel[a].lsb_v * pa;
    const double lsb_b = acq.channel[b].lsb_v * pb;
    const auto math_of = [&](std::size_t from, std::size_t to) {
        std::vector<float> xa, xb;
        for (std::size_t i = from; i < to; ++i) {
            xa.push_back(static_cast<float>(va[i] * pa));
            xb.push_back(static_cast<float>(vb[i] * pb));
        }
        return compute_math(ms.operation, xa, xb, 2 * lsb_b);  // within 2 lsb of zero the divisor is noise
    };
    const std::size_t run_begin = frame.plan.layout.run_begin(first);
    const std::size_t run_end = frame.plan.layout.run_end(first, size);
    const std::size_t end = std::min(run_end, first + frame.plan.window_samples + 1);
    const auto margin = static_cast<std::size_t>(kSincTaps);
    frame.math = math_of(first, end);
    frame.math_before = math_of(std::max(run_begin, first > margin ? first - margin : 0), first);
    frame.math_after = math_of(end, std::min(run_end, end + margin));
    frame.math_noise_floor = 4 * (lsb_a + lsb_b);
}

void Engine::check_aliasing(const AcquisitionRequest& request, const RateCapacity& top, Frame& frame)
{
    AcquisitionRequest fast = request;
    fast.sample_rate_hz = top.rate_hz;
    fast.samples = std::min<std::size_t>(source_.max_contiguous_samples(top.rate_hz), 4096);
    const Acquisition acq = source_.acquire(fast);
    const double seconds = static_cast<double>(fast.samples) / top.rate_hz;
    for (int ch = 0; ch < kChannels; ++ch) {
        const ChannelData& cd = acq.channel[ch];
        aliased_[ch] = false;
        if (!frame.settings.channel[ch].enabled || cd.volts.empty())
            continue;
        const Measurements m = measure(cd.volts, 1.0 / top.rate_hz, 4 * cd.lsb_v);
        const double frame_swing = frame.measurements[ch].peak_to_peak / frame.settings.channel[ch].probe;
        aliased_[ch] = std::isfinite(m.frequency) && m.frequency * seconds >= kAliasMinPeriods &&
                       m.frequency > frame.plan.sample_rate_hz / 2.0 && m.peak_to_peak >= kAliasMinSwing * frame_swing;
    }
}

}  // namespace starosc
