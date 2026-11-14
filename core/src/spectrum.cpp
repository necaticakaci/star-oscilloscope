// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/spectrum.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <limits>

#include "starosc/fft.hpp"
#include "starosc/math.hpp"

namespace starosc {
namespace {

// At a rate whose capture comes in blocks, this many blocks are captured and their spectra averaged.
constexpr std::size_t kBlocksPerCapture = 32;
constexpr double kPowerFloor = 1e-30;  // -300 dBV
constexpr double kDbmOffset = 13.010299956639812;  // 10 log10(1 V^2 / 50 ohm / 1 mW)
constexpr double kDbuOffset = 2.2184874961635637;  // 20 log10(1 V / 0.7746 V)

double unit_offset_db(SpectrumUnit unit)
{
    switch (unit) {
    case SpectrumUnit::Vrms:
    case SpectrumUnit::dBV: return 0;
    case SpectrumUnit::dBmV: return 60;
    case SpectrumUnit::dBu: return kDbuOffset;
    case SpectrumUnit::dBm: return kDbmOffset;
    }
    return 0;
}

double db(double ratio) { return 10 * std::log10(ratio); }

}  // namespace

std::string_view unit_name(SpectrumUnit unit)
{
    switch (unit) {
    case SpectrumUnit::Vrms: return "V";
    case SpectrumUnit::dBV: return "dBV";
    case SpectrumUnit::dBmV: return "dBmV";
    case SpectrumUnit::dBu: return "dBu";
    case SpectrumUnit::dBm: return "dBm";
    }
    return "?";
}

bool unit_is_db(SpectrumUnit unit) { return unit != SpectrumUnit::Vrms; }

TraceUnit trace_unit(SpectrumUnit unit, int source, MathOperation operation)
{
    const bool volts = source < kChannels || operation == MathOperation::Add || operation == MathOperation::Subtract;
    if (volts) {
        std::string name(unit_name(unit));
        std::string column = unit == SpectrumUnit::Vrms ? "vrms" : name;
        std::transform(column.begin(), column.end(), column.begin(), [](unsigned char c) { return std::tolower(c); });
        return {unit, std::move(name), std::move(column)};
    }
    const bool product = operation == MathOperation::Multiply;
    if (unit_is_db(unit))
        return {SpectrumUnit::dBV, product ? "dB(V²)" : "dB", product ? "db_v2" : "db"};
    return {SpectrumUnit::Vrms, product ? "V²" : "U", product ? "v2rms" : "urms"};
}

double level_in_unit(double power, SpectrumUnit unit)
{
    power = std::max(power, kPowerFloor);
    if (unit == SpectrumUnit::Vrms)
        return std::sqrt(power);
    return db(power) + unit_offset_db(unit);
}

double dbv_from_level(double level, SpectrumUnit unit)
{
    if (unit == SpectrumUnit::Vrms)
        return 20 * std::log10(std::max(level, 1e-15));
    return level - unit_offset_db(unit);
}

double level_from_dbv(double dbv, SpectrumUnit unit)
{
    if (unit == SpectrumUnit::Vrms)
        return std::pow(10.0, dbv / 20);
    return dbv + unit_offset_db(unit);
}

SpectrumSettings constrained(const SpectrumSettings& settings)
{
    SpectrumSettings s = settings;
    s.points = std::bit_floor(std::clamp(s.points, kMinSpectrumPoints, kMaxSpectrumPoints));
    s.averages = std::clamp(s.averages, 1, kMaxSpectrumAverages);
    s.harmonics = std::clamp(s.harmonics, kMinHarmonics, kMaxHarmonics);
    s.marker_source = std::clamp(s.marker_source, -1, kSpectrumSources - 1);
    s.db_per_div = std::clamp(s.db_per_div, 0.1, 50.0);
    s.top_dbv = std::clamp(s.top_dbv, -200.0, 100.0);
    if (!(s.start_hz >= 0) || !(s.stop_hz > s.start_hz))
        s.start_hz = s.stop_hz = 0;
    return s;
}

std::vector<double> power_spectrum(std::span<const float> samples, std::span<const double> window, std::size_t points)
{
    std::vector<double> x(points, 0.0);
    const std::size_t n = std::min({samples.size(), window.size(), points});
    double sum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = samples[i] * window[i];
        sum += window[i];
    }
    const auto bins = real_fft(x);
    std::vector<double> power(bins.size());
    const double scale = sum > 0 ? 1 / (sum * sum) : 0;
    for (std::size_t k = 0; k < bins.size(); ++k) {
        const bool edge = k == 0 || k == bins.size() - 1;  // DC and half the sample rate have no mirror image
        power[k] = std::norm(bins[k]) * scale * (edge ? 1 : 2);
    }
    return power;
}

SpectrumAnalysis analyze_spectrum(std::span<const float> power, double bin_hz, double noise_bandwidth, int lobe_bins,
                                  int harmonics)
{
    SpectrumAnalysis a;
    const std::size_t n = power.size();
    const auto lobe = static_cast<std::size_t>(std::max(lobe_bins, 1));
    const std::size_t span = lobe + 1;  // bins on each side of a tone's peak that hold its power
    if (n < 2 * span + 4 || !(bin_hz > 0) || !(noise_bandwidth > 0))
        return a;

    std::size_t k1 = span + 1;
    for (std::size_t k = span + 1; k < n; ++k)
        if (power[k] > power[k1])
            k1 = k;
    if (!(power[k1] > 0))
        return a;
    double weight = 0, moment = 0;
    for (std::size_t k = k1 - span; k <= std::min(n - 1, k1 + span); ++k) {
        weight += power[k];
        moment += power[k] * static_cast<double>(k);
    }
    a.fundamental_hz = moment / weight * bin_hz;

    // A strong tone leaks into bins far beyond its main lobe, more with windows of high side lobes. Those
    // bins are the tone's power, not noise: a tone takes its skirt for as long as it stays falling and above
    // the floor, the median bin.
    std::vector<float> sorted(power.begin() + static_cast<std::ptrdiff_t>(span) + 1, power.end());
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
    const double skirt_floor = 4.0 * sorted[sorted.size() / 2];

    enum : char { Free, DcBin, FundamentalBin, HarmonicBin };
    std::vector<char> owner(n, Free);
    // Power of the tone whose peak is at `peak`, from the bins no other tone took.
    const auto take = [&](std::size_t peak, char who) {
        double sum = 0;
        const auto add = [&](std::size_t k) {
            sum += power[k];
            owner[k] = who;
        };
        const std::size_t lo = peak > span ? peak - span : 0;
        const std::size_t hi = std::min(n - 1, peak + span);
        for (std::size_t k = lo; k <= hi; ++k)
            if (owner[k] == Free)
                add(k);
        for (std::size_t k = lo; k > 0 && owner[k - 1] == Free && power[k - 1] > skirt_floor &&
                                 power[k - 1] < 2 * power[k];
             --k)
            add(k - 1);
        for (std::size_t k = hi; k + 1 < n && owner[k + 1] == Free && power[k + 1] > skirt_floor &&
                                 power[k + 1] < 2 * power[k];
             ++k)
            add(k + 1);
        return sum / noise_bandwidth;
    };

    take(0, DcBin);
    if (owner[k1] != Free)
        return a;  // the strongest bin above DC is DC's own skirt: a tone this close to DC cannot be told apart
    a.fundamental_power = take(k1, FundamentalBin);

    // Spurious-free range: the fundamental's peak bin against the largest bin that is neither it nor DC.
    double spur = 0;
    for (std::size_t k = span + 1; k < n; ++k)
        if (owner[k] == Free)
            spur = std::max(spur, static_cast<double>(power[k]));
    a.sfdr_db = spur > 0 ? db(power[k1] / spur) : std::numeric_limits<double>::infinity();

    const double rate = bin_hz * 2 * static_cast<double>(n - 1);
    double harmonic_power = 0;
    for (int h = 2; h <= harmonics; ++h) {
        Harmonic hm;
        hm.order = h;
        double f = std::fmod(h * a.fundamental_hz, rate);
        if (f > rate / 2)
            f = rate - f;  // folded back into the span
        hm.frequency_hz = f;
        const auto expected = static_cast<std::size_t>(std::llround(f / bin_hz));
        std::size_t peak = n;
        for (std::size_t k = expected > lobe ? expected - lobe : 0; k <= std::min(n - 1, expected + lobe); ++k)
            if (owner[k] == Free && (peak == n || power[k] > power[peak]))
                peak = k;
        if (peak < n) {
            hm.power = take(peak, HarmonicBin);
            harmonic_power += hm.power;
        }
        a.harmonics.push_back(hm);
    }

    // Noise: the mean of the bins left, over the whole span above DC.
    double noise_sum = 0;
    std::size_t noise_bins = 0;
    for (std::size_t k = span + 1; k < n; ++k)
        if (owner[k] == Free) {
            noise_sum += power[k];
            ++noise_bins;
        }
    const auto band = static_cast<double>(n - span - 1);
    a.noise_power = noise_bins ? noise_sum / static_cast<double>(noise_bins) * band / noise_bandwidth : 0;

    const double p1 = a.fundamental_power;
    a.thd = std::sqrt(harmonic_power / p1);
    a.snr_db = a.noise_power > 0 ? db(p1 / a.noise_power) : std::numeric_limits<double>::infinity();
    const double distortion = a.noise_power + harmonic_power;
    a.sinad_db = distortion > 0 ? db(p1 / distortion) : std::numeric_limits<double>::infinity();
    a.enob_bits = (a.sinad_db - 1.76) / 6.02;
    a.valid = true;
    return a;
}

const std::vector<double>& SpectrumEngine::window(Window type, std::size_t n)
{
    if (window_.size() != n || window_type_ != type) {
        window_ = make_window(type, n);
        window_type_ = type;
    }
    return window_;
}

SpectrumFrame SpectrumEngine::acquire(const ScopeSettings& scope, const SpectrumSettings& requested)
{
    const SpectrumSettings s = constrained(requested);
    const std::vector<std::uint32_t> rates = source_.sample_rates();
    std::uint32_t rate = rates.front();
    for (std::uint32_t r : rates)
        if (r <= s.sample_rate_hz)
            rate = r;

    // Records: one at the start of a contiguous capture. In a capture of blocks, more points than a block holds come
    // from the first run alone, zero-padded to a power of two; fewer from every record that fits inside a run.
    const CaptureLayout layout = source_.layout(rate);
    std::size_t points = s.points;
    std::size_t record = 0, capture = 0;
    std::vector<std::size_t> starts;
    if (layout.contiguous()) {
        record = std::min(points, source_.max_samples());
        capture = record;
        starts.push_back(0);
    } else if (const std::size_t first = std::min(layout.first_block, source_.max_samples());
               points > layout.block && first > layout.block) {
        points = std::min(points, std::bit_ceil(first));
        record = std::min(points, first);
        capture = first;
        starts.push_back(0);
    } else {
        points = std::min(points, std::bit_floor(layout.block));
        record = points;
        capture = std::min(source_.max_samples(), layout.first_block + kBlocksPerCapture * layout.block);
        for (std::size_t j = 0; j + record <= first; j += record)
            starts.push_back(j);
        for (std::size_t b = layout.first_block; b + layout.block <= capture; b += layout.block)
            for (std::size_t j = 0; j + record <= layout.block; j += record)
                starts.push_back(b + j);
    }

    SpectrumFrame frame;
    frame.sequence = ++sequence_;
    frame.scope = scope;
    frame.settings = s;
    frame.settings.sample_rate_hz = rate;
    frame.sample_rate_hz = rate;
    frame.points = points;
    frame.record = record;
    frame.records = starts.size();
    frame.bin_hz = static_cast<double>(rate) / static_cast<double>(points);
    const std::vector<double>& w = window(s.window, record);
    frame.noise_bandwidth = noise_bandwidth_bins(w) * static_cast<double>(points) / static_cast<double>(record);

    const MathSettings& ms = scope.math;
    const int ma = std::clamp(ms.a, 0, kChannels - 1);
    const int mb = std::clamp(ms.b, 0, kChannels - 1);
    const bool math = s.shown[kChannels];
    AcquisitionRequest request;
    request.sample_rate_hz = rate;
    request.samples = capture;
    bool any = false;
    for (int ch = 0; ch < kChannels; ++ch) {
        const ChannelSettings& cs = scope.channel[ch];
        const bool needed = s.shown[ch] || (math && (ch == ma || ch == mb));
        request.channel[ch] = {needed, screen_range_at_input(cs), cs.coupling};
        any |= needed;
    }
    if (!any)
        return frame;
    const Acquisition acq = source_.acquire(request);

    // Spectra already averaged or held were made with the same rate, length, window, ranges and traces.
    std::vector<double> key{static_cast<double>(rate), static_cast<double>(points), static_cast<double>(s.window),
                            static_cast<double>(s.averaging)};
    for (int ch = 0; ch < kChannels; ++ch) {
        const ChannelSettings& cs = scope.channel[ch];
        key.insert(key.end(), {cs.volts_per_div, cs.position_div, static_cast<double>(cs.coupling), cs.probe});
    }
    key.insert(key.end(), {static_cast<double>(ms.operation), static_cast<double>(ma), static_cast<double>(mb)});
    for (bool shown : s.shown)
        key.push_back(shown ? 1 : 0);
    if (key != key_) {
        key_ = std::move(key);
        count_ = 0;
    }

    const auto at_tip = [&](int ch) {
        const ChannelData& cd = acq.channel[ch];
        std::vector<float> v(cd.volts.size());
        for (std::size_t i = 0; i < v.size(); ++i)
            v[i] = static_cast<float>(cd.volts[i] * scope.channel[ch].probe);
        return v;
    };
    for (int ch = 0; ch < kChannels; ++ch)
        frame.clipped[ch] = request.channel[ch].enabled && acq.channel[ch].clipped;

    const int lobe = static_cast<int>(
        std::ceil(window_lobe_bins(s.window) * static_cast<double>(points) / static_cast<double>(record) - 1e-9));
    for (int src = 0; src < kSpectrumSources; ++src) {
        if (!s.shown[src])
            continue;
        std::vector<float> x;
        if (src < kChannels) {
            x = at_tip(src);
        } else {
            const double lsb_b = acq.channel[mb].lsb_v * scope.channel[mb].probe;
            x = compute_math(ms.operation, at_tip(ma), at_tip(mb), 2 * lsb_b);
            for (float& v : x)
                if (!std::isfinite(v))
                    v = 0;  // a ratio without a divisor; NaN would spread over the whole spectrum
        }
        if (x.size() < capture)
            continue;
        std::vector<double> spectrum(points / 2 + 1, 0.0);
        for (std::size_t start : starts) {
            const std::vector<double> p = power_spectrum(std::span<const float>(x).subspan(start, record), w, points);
            for (std::size_t k = 0; k < p.size(); ++k)
                spectrum[k] += p[k] / static_cast<double>(starts.size());
        }

        std::vector<double>& acc = accumulated_[static_cast<std::size_t>(src)];
        if (count_ == 0 || acc.size() != spectrum.size() || s.averaging == SpectrumAveraging::Off) {
            acc = std::move(spectrum);
        } else if (s.averaging == SpectrumAveraging::Average) {
            // A plain average of the first spectra, then an exponential one over `averages` spectra.
            const double n = std::min(count_ + 1, s.averages);
            for (std::size_t k = 0; k < acc.size(); ++k)
                acc[k] += (spectrum[k] - acc[k]) / n;
        } else {
            for (std::size_t k = 0; k < acc.size(); ++k)
                acc[k] = std::max(acc[k], spectrum[k]);
        }
        frame.power[src].assign(acc.begin(), acc.end());
        frame.analysis[src] = analyze_spectrum(frame.power[src], frame.bin_hz, frame.noise_bandwidth, lobe, s.harmonics);
    }
    count_ = s.averaging == SpectrumAveraging::Off ? 1 : std::min(count_ + 1, 1 << 30);
    frame.averaged = s.averaging == SpectrumAveraging::Average ? std::min(count_, s.averages) : count_;
    return frame;
}

}  // namespace starosc
