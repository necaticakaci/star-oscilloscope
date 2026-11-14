// SPDX-License-Identifier: GPL-3.0-or-later
// Generator and sampling self-test: DDS OUT probed on CH2 (x1), CH1 unused.
//
//   star-loopback [calibration.json] [--pulses | --delay | --external | --step | --peaks | --sinc | --stages | --dds-state |
//                 --grain [CH1-POS CH2-POS]] [--range V] [--probe F]
//
// Prints what the output does when parked, its amplitude, offset, waveforms and duty against the
// settings, the pulse measurements of a square wave, its frequency response, and whether the captures
// at each sample rate are contiguous (no samples dropped). With --pulses, only the pulse measurements.
// With --delay, only the delay between the channels: then CH1 and CH2 (x1) are both on DDS OUT.
// With --external, the generator is left alone and another source on CH1 and CH2 (x1) is measured.
// With --step, likewise, and the source is a clean square wave of about 10 kHz: the step response of each channel.
// --range sets the input range of those two, ±V at the BNC, default 1.8; a x10 probe needs about 0.18 for 2 Vpp.
// With --peaks, the largest spectral peaks on each channel and their difference, on a wide range: what is on the inputs.
// With --sinc, how well sin(x)/x brings back samples it did not see; a clean sine on both channels, the generator left alone.
// With --stages, the frequency response of every gain stage; CH1 and CH2 on DDS OUT, --probe gives their attenuation.
// With --dds-state, what the generator gives for each setting of the two inputs' coarse gain; likewise on DDS OUT.
// With --grain, how grainy each channel draws: its noise against its ADC step at 0.5 V/div and 16 MSa/s, at position 0 and
// at the given positions (div). Whatever is on the inputs; the generator is left alone. docs/measurements.md §16.
// With --gain-transfer, the gain of every stage against the reference stage, found with the generator; likewise on DDS OUT, x1.
// A calibration file is then what they are compared with, and the readings stay in factory volts.
// With --contiguity N, only whether N captures of 1M samples at each rate, of the first run at the top rate, come without
// lost samples; DDS OUT on CH2.
// With --gain-levels, levels that a multimeter on DDS OUT reads while they stand, each for --hold seconds, and what each
// stage reads of them: a check of --gain-transfer against something other than the generator.
// With a calibration file from the app (Acquire > Calibration…, Export) the readings use it.
// Results for one unit: docs/measurements.md §5 and §10.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <vector>
#include <chrono>
#include <numbers>
#include <optional>
#include <thread>

#include <QCoreApplication>

#include "calibrationstore.hpp"
#include "starosc/interpolate.hpp"
#include "starosc/measure.hpp"
#include "starosc/gain_transfer.hpp"
#include "starosc/source.hpp"
#include "starosc/spectrum.hpp"
#include "starosc/window.hpp"

using namespace starosc;

namespace {

constexpr double kPi = std::numbers::pi;

Acquisition capture(Source& src, double lo, double hi, std::uint32_t rate, std::size_t samples)
{
    AcquisitionRequest r{rate, samples, {}, 1, 0};
    r.channel[0].enabled = false;
    r.channel[1] = {true, {lo, hi}, Coupling::DC};
    src.acquire({rate, 4096, r.channel, 1, 0});  // the first capture after a stage change is dropped
    return src.acquire(r);
}

void settle(Source& src, const GeneratorSettings& g)
{
    src.set_generator(g);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
}

// Peak-to-peak of a sine of frequency f by least squares (a·cos + b·sin + c): free of noise peaks.
double fitted_vpp(const std::vector<float>& v, double f, double rate)
{
    double s[3][3] = {}, b[3] = {};
    for (std::size_t i = 0; i < v.size(); ++i) {
        const double ph = 2 * kPi * f * static_cast<double>(i) / rate;
        const double x[3] = {std::cos(ph), std::sin(ph), 1};
        for (int r = 0; r < 3; ++r) {
            b[r] += x[r] * v[i];
            for (int c = 0; c < 3; ++c)
                s[r][c] += x[r] * x[c];
        }
    }
    for (int k = 0; k < 3; ++k)
        for (int r = k + 1; r < 3; ++r) {
            const double m = s[r][k] / s[k][k];
            for (int c = k; c < 3; ++c)
                s[r][c] -= m * s[k][c];
            b[r] -= m * b[k];
        }
    double x[3];
    for (int r = 2; r >= 0; --r) {
        double sum = b[r];
        for (int c = r + 1; c < 3; ++c)
            sum -= s[r][c] * x[c];
        x[r] = sum / s[r][r];
    }
    return 2 * std::hypot(x[0], x[1]);
}

// Jumps in the phase of a sine with a period of 160 samples, segment by segment: samples the device dropped.
// The DDS runs about 105 ppm slow against the sample clock (docs/measurements.md §4).
int phase_jumps(const std::vector<float>& v)
{
    constexpr std::size_t kPeriod = 160;
    const double w = 2 * kPi / kPeriod * (1 - 105e-6);
    double previous = 0, unwrap = 0;
    int jumps = 0;
    std::size_t last = 0;
    for (std::size_t s = 0; s + kPeriod <= v.size(); s += kPeriod) {
        double mean = 0, i0 = 0, q0 = 0;
        for (std::size_t i = s; i < s + kPeriod; ++i)
            mean += v[i];
        mean /= kPeriod;
        for (std::size_t i = s; i < s + kPeriod; ++i) {
            i0 += (v[i] - mean) * std::cos(w * static_cast<double>(i));
            q0 += (v[i] - mean) * std::sin(w * static_cast<double>(i));
        }
        double phase = std::atan2(q0, i0);
        if (s > 0) {
            while (phase + unwrap - previous > kPi)
                unwrap -= 2 * kPi;
            while (phase + unwrap - previous < -kPi)
                unwrap += 2 * kPi;
        }
        phase += unwrap;
        if (s > 0 && std::abs(phase - previous) / w > 0.3 && (jumps == 0 || s - last > kPeriod)) {
            ++jumps;
            last = s;
        }
        previous = phase;
    }
    return jumps;
}

void levels(const char* what, const Acquisition& a, std::uint32_t rate)
{
    const ChannelData& cd = a.channel[1];
    const Measurements m = measure(cd.volts, 1.0 / rate, 4 * cd.lsb_v);
    std::printf("  %-26s max %+7.3f  min %+7.3f  pp %6.3f  mean %+7.3f  f %10.1f  duty %5.1f%s\n", what, m.max, m.min,
                m.peak_to_peak, m.mean, m.frequency, m.duty, cd.clipped ? "  CLIP" : "");
}

// Means over several captures of what measure() says about a pulse wave; an edge that took too few samples shows as 0.
void pulses(Source& src)
{
    std::printf("pulse measurements, 2 Vpp, means over 16 captures; times in µs, an edge too fast for the rate as <\n");
    std::printf("  %-22s %6s %6s %6s %6s %6s %8s %8s %9s %9s %6s %10s\n", "", "top", "base", "ampl", "over%", "under%", "rise",
                "fall", "+width", "-width", "duty", "f");
    struct Case {
        const char* name;
        Waveform waveform;
        double frequency;
        int duty;
        std::uint32_t rate;
    };
    const std::uint32_t top = src.sample_rates().back();
    const Case cases[] = {
        {"square 1 kHz", Waveform::Square, 1e3, 50, 1'000'000},    {"square 1 kHz", Waveform::Square, 1e3, 50, 16'000'000},
        {"square 10 kHz", Waveform::Square, 10e3, 50, 16'000'000}, {"square 10 kHz 25%", Waveform::Square, 10e3, 25, 16'000'000},
        {"square 100 kHz", Waveform::Square, 100e3, 50, 16'000'000}, {"square 100 kHz", Waveform::Square, 100e3, 50, top},
        {"square 1 MHz", Waveform::Square, 1e6, 50, top},          {"sine 1 kHz", Waveform::Sine, 1e3, 50, 1'000'000},
        {"sine 100 kHz", Waveform::Sine, 100e3, 50, 16'000'000},   {"triangle 1 kHz", Waveform::Triangle, 1e3, 50, 1'000'000},
    };
    for (const Case& c : cases) {
        GeneratorSettings g;
        g.output = true;
        g.amplitude_vpp = 2;
        g.waveform = c.waveform;
        g.frequency_hz = c.frequency;
        g.duty_percent = c.duty;
        settle(src, g);
        std::array<Statistics, 11> s;
        const bool blocks = c.rate == top;
        const std::size_t n = blocks ? src.max_contiguous_samples(c.rate) : std::min<std::size_t>(200'000, static_cast<std::size_t>(c.rate / c.frequency * 20));
        bool rise_limited = false, fall_limited = false;
        for (int k = 0; k < 16; ++k) {
            const ChannelData cd = capture(src, -1.8, 1.8, c.rate, n).channel[1];
            const Measurements m = measure(cd.volts, 1.0 / c.rate, 4 * cd.lsb_v);
            rise_limited |= m.rise_unresolved;
            fall_limited |= m.fall_unresolved;
            const double values[] = {m.top, m.base, m.amplitude, m.overshoot, m.undershoot, m.rise_time * 1e6, m.fall_time * 1e6,
                                     m.positive_width * 1e6, m.negative_width * 1e6, m.duty, m.frequency};
            for (std::size_t i = 0; i < s.size(); ++i)
                s[i].add(values[i]);
        }
        const auto rate = static_cast<double>(c.rate) / 1e6;
        std::printf("  %-17s %2.0fM  %6.3f %6.3f %6.3f %6.1f %6.1f %s%7.3f %s%7.3f %9.3f %9.3f %6.1f %10.1f\n", c.name, rate, s[0].mean(),
                    s[1].mean(), s[2].mean(), s[3].mean(), s[4].mean(), rise_limited ? "<" : " ",
                    rise_limited ? 2e-6 / rate * 1e6 : s[5].mean(), fall_limited ? "<" : " ",
                    fall_limited ? 2e-6 / rate * 1e6 : s[6].mean(), s[7].mean(), s[8].mean(), s[9].mean(), s[10].mean());
    }
}

// What the delay and phase measurements read with one signal on both channels: the delay between the inputs themselves.
void delays(Source& src)
{
    std::printf("delay and phase, CH1 and CH2 on the same signal, 2 Vpp, over 16 captures\n");
    std::printf("  %-18s %6s %12s %12s %10s %10s\n", "", "rate", "delay ns", "std dev ns", "samples", "phase °");
    struct Case {
        const char* name;
        Waveform waveform;
        double frequency;
        std::uint32_t rate;
    };
    const std::uint32_t top = src.sample_rates().back();
    const Case cases[] = {
        {"sine 100 Hz", Waveform::Sine, 100, 1'000'000},       {"sine 1 kHz", Waveform::Sine, 1e3, 1'000'000},
        {"sine 10 kHz", Waveform::Sine, 10e3, 16'000'000},     {"sine 100 kHz", Waveform::Sine, 100e3, 16'000'000},
        {"sine 1 MHz", Waveform::Sine, 1e6, 16'000'000},       {"sine 1 MHz", Waveform::Sine, 1e6, top},
        {"sine 5 MHz", Waveform::Sine, 5e6, top},              {"square 1 kHz", Waveform::Square, 1e3, 16'000'000},
        {"square 10 kHz", Waveform::Square, 10e3, 16'000'000}, {"square 100 kHz", Waveform::Square, 100e3, 16'000'000},
        {"square 1 MHz", Waveform::Square, 1e6, top},
    };
    for (const Case& c : cases) {
        GeneratorSettings g;
        g.output = true;
        g.amplitude_vpp = 2;
        g.waveform = c.waveform;
        g.frequency_hz = c.frequency;
        settle(src, g);
        const std::size_t n = c.rate == top ? src.max_contiguous_samples(c.rate) : static_cast<std::size_t>(c.rate / c.frequency * 20);
        AcquisitionRequest r{c.rate, n, {}, 1, 0};
        r.channel[0] = {true, {-1.8, 1.8}, Coupling::DC};
        r.channel[1] = r.channel[0];
        src.acquire({c.rate, 4096, r.channel, 1, 0});  // the first capture after a stage change is dropped
        Statistics delay, phase;
        for (int k = 0; k < 16; ++k) {
            const Acquisition a = src.acquire(r);
            std::array<Edges, 2> edges;
            std::array<Measurements, 2> m;
            for (int ch = 0; ch < 2; ++ch)
                m[ch] = measure(a.channel[ch].volts, 1.0 / c.rate, 4 * a.channel[ch].lsb_v, {}, &edges[ch]);
            const PairMeasurements p = measure_pair({a.channel[0].volts, m[0], edges[0]}, {a.channel[1].volts, m[1], edges[1]}, 1.0 / c.rate);
            delay.add(p.delay);
            phase.add(p.phase);
        }
        std::printf("  %-18s %5.0fM %12.2f %12.2f %10.3f %10.3f\n", c.name, c.rate / 1e6, delay.mean() * 1e9, delay.stddev() * 1e9,
                    delay.mean() * c.rate, phase.mean());
    }
}

// Another source on both channels: its frequency, level and pulse measurements at a few rates, and the delay between
// the channels. Means over 16 captures.
void external(Source& src, double range)
{
    const std::uint32_t top = src.sample_rates().back();
    double frequency = Measurements::kNone;
    for (std::uint32_t rate : {1'000'000u, 16'000'000u, top}) {
        const std::size_t n = rate == top ? src.max_contiguous_samples(rate) : 100'000;
        const ChannelData cd = capture(src, -range, range, rate, n).channel[1];
        frequency = measure(cd.volts, 1.0 / rate, 4 * cd.lsb_v).frequency;
        if (std::isfinite(frequency) && frequency * 8 < rate)
            break;
    }
    if (!std::isfinite(frequency)) {
        std::printf("no signal found on CH2\n");
        return;
    }
    std::printf("about %.1f Hz; times in µs, an edge too fast for the rate as <\n", frequency);
    std::printf("  %-8s %10s %7s %7s %7s %7s %7s %6s %6s %8s %8s %9s %9s %6s\n", "", "f", "fit Vpp", "mean", "top", "base", "ampl",
                "over%", "under%", "rise", "fall", "+width", "-width", "duty");

    std::vector<std::uint32_t> rates;
    if (frequency < 20e3)
        rates = {1'000'000, 16'000'000, top};
    else
        rates = {16'000'000, top};
    for (std::uint32_t rate : rates) {
        const std::size_t n = rate == top ? src.max_contiguous_samples(rate)
                                          : std::clamp<std::size_t>(static_cast<std::size_t>(rate / frequency * 20), 4096, 200'000);
        AcquisitionRequest r{rate, n, {}, 1, 0};
        r.channel[0] = {true, {-range, range}, Coupling::DC};
        r.channel[1] = r.channel[0];
        src.acquire({rate, 4096, r.channel, 1, 0});  // the first capture after a stage change is dropped
        std::array<std::array<Statistics, 13>, 2> s;
        std::array<bool, 2> clipped{}, rise_limited{}, fall_limited{};
        Statistics delay, phase;
        for (int k = 0; k < 16; ++k) {
            const Acquisition a = src.acquire(r);
            std::array<Edges, 2> edges;
            std::array<Measurements, 2> m;
            for (std::size_t ch = 0; ch < 2; ++ch) {
                const ChannelData& cd = a.channel[ch];
                m[ch] = measure(cd.volts, 1.0 / rate, 4 * cd.lsb_v, {}, &edges[ch]);
                clipped[ch] = clipped[ch] || cd.clipped;
                rise_limited[ch] = rise_limited[ch] || m[ch].rise_unresolved;
                fall_limited[ch] = fall_limited[ch] || m[ch].fall_unresolved;
                const double f = std::isfinite(m[ch].frequency) ? m[ch].frequency : frequency;
                const double values[] = {m[ch].frequency, fitted_vpp(cd.volts, f, rate), m[ch].mean, m[ch].top, m[ch].base, m[ch].amplitude,
                                         m[ch].overshoot, m[ch].undershoot, m[ch].rise_time * 1e6, m[ch].fall_time * 1e6,
                                         m[ch].positive_width * 1e6, m[ch].negative_width * 1e6, m[ch].duty};
                for (std::size_t i = 0; i < s[ch].size(); ++i)
                    s[ch][i].add(values[i]);
            }
            const PairMeasurements p = measure_pair({a.channel[0].volts, m[0], edges[0]}, {a.channel[1].volts, m[1], edges[1]}, 1.0 / rate);
            delay.add(p.delay);
            phase.add(p.phase);
        }
        for (std::size_t ch = 0; ch < 2; ++ch) {
            const auto& c = s[ch];
            const double limit = 2.0 / rate * 1e6;
            std::printf("  CH%zu %3.0fM %10.1f %7.3f %7.3f %7.3f %7.3f %7.4f %6.1f %6.1f %s%7.3f %s%7.3f %9.3f %9.3f %6.1f%s\n", ch + 1, rate / 1e6,
                        c[0].mean(), c[1].mean(), c[2].mean(), c[3].mean(), c[4].mean(), c[5].mean(), c[6].mean(), c[7].mean(),
                        rise_limited[ch] ? "<" : " ", rise_limited[ch] ? limit : c[8].mean(), fall_limited[ch] ? "<" : " ",
                        fall_limited[ch] ? limit : c[9].mean(), c[10].mean(), c[11].mean(), c[12].mean(), clipped[ch] ? "  CLIP" : "");
        }
        std::printf("  CH1→CH2  delay %.2f ns (std dev %.2f), phase %.3f°\n", delay.mean() * 1e9, delay.stddev() * 1e9, phase.mean());
    }
}

// Least squares a·cos + b·sin + c at frequency f: the peak-to-peak and the RMS of what the sine leaves over, which is the
// noise and the distortion of the signal.
std::pair<double, double> sine_fit(const std::vector<float>& v, double f, double rate)
{
    double s[3][4] = {};
    for (std::size_t i = 0; i < v.size(); ++i) {
        const double ph = 2 * kPi * f * static_cast<double>(i) / rate;
        const double x[3] = {std::cos(ph), std::sin(ph), 1};
        for (int r = 0; r < 3; ++r) {
            s[r][3] += x[r] * v[i];
            for (int c = 0; c < 3; ++c)
                s[r][c] += x[r] * x[c];
        }
    }
    for (int k = 0; k < 3; ++k)
        for (int r = 0; r < 3; ++r)
            if (r != k) {
                const double m = s[r][k] / s[k][k];
                for (int c = k; c < 4; ++c)
                    s[r][c] -= m * s[k][c];
            }
    const double a = s[0][3] / s[0][0], b = s[1][3] / s[1][1], c = s[2][3] / s[2][2];
    double residual = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const double ph = 2 * kPi * f * static_cast<double>(i) / rate;
        const double e = v[i] - a * std::cos(ph) - b * std::sin(ph) - c;
        residual += e * e;
    }
    return {2 * std::hypot(a, b), std::sqrt(residual / static_cast<double>(v.size()))};
}

// Keeps every k-th sample of a top-rate capture, computes the ones between from them with sin(x)/x and with straight
// lines, and compares both with the real samples. Thinning by k stands for a signal k times faster at the top rate.
void sinc_check(Source& src, double range)
{
    const std::uint32_t rate = src.sample_rates().back();
    const std::size_t n = src.max_contiguous_samples(rate);
    AcquisitionRequest r{rate, n, {}, 1, 0};
    r.channel[0] = {true, {-range, range}, Coupling::DC};
    r.channel[1] = r.channel[0];
    src.acquire({rate, 4096, r.channel, 1, 0});  // the first capture after a stage change is dropped

    constexpr int kCaptures = 48;
    std::vector<Acquisition> captures;
    Statistics period;
    for (int i = 0; i < kCaptures; ++i) {
        captures.push_back(src.acquire(r));
        period.add(measure(captures.back().channel[0].volts, 1.0 / rate, 4 * captures.back().channel[0].lsb_v).period);
    }
    const double frequency = 1 / period.mean();
    std::printf("about %.0f Hz, %.1f samples a period at %.0f MSa/s, 1 lsb %.2f mV\n", frequency, rate * period.mean(), rate / 1e6,
                captures[0].channel[0].lsb_v * 1e3);
    for (std::size_t ch = 0; ch < 2; ++ch) {
        Statistics vpp, residual;
        for (const Acquisition& a : captures) {
            const auto [pp, rest] = sine_fit(a.channel[ch].volts, frequency, rate);
            vpp.add(pp);
            residual.add(rest);
        }
        std::printf("  CH%zu: sine %.1f mVpp (std dev %.2f over the captures), what the fit leaves %.2f mVrms (%.2f %% of the sine's RMS)\n", ch + 1,
                    vpp.mean() * 1e3, vpp.stddev() * 1e3, residual.mean() * 1e3, 100 * residual.mean() / (vpp.mean() / 2 / std::sqrt(2.0)));
        std::printf("    %-8s %-14s %-14s %-14s %-14s\n", "keep 1 in", "samples/period", "sin(x)/x mVrms", "lines mVrms", "as if at 48 MSa/s");
        for (int k : {2, 3, 4, 6}) {
            double sinc_sum = 0, line_sum = 0;
            std::size_t count = 0;
            for (const Acquisition& a : captures) {
                const std::vector<float>& v = a.channel[ch].volts;
                std::vector<float> kept;
                for (std::size_t i = 0; i < v.size(); i += static_cast<std::size_t>(k))
                    kept.push_back(v[i]);
                const std::size_t margin = static_cast<std::size_t>((kSincTaps + 1) * k);
                for (std::size_t i = margin; i + margin < v.size(); ++i) {
                    if (i % static_cast<std::size_t>(k) == 0)
                        continue;
                    const double position = static_cast<double>(i) / k;
                    const double sinc = sinc_interpolate(kept, {}, {}, position, 0, 1)[0];
                    const auto low = static_cast<std::size_t>(position);
                    const double line = kept[low] + (position - static_cast<double>(low)) * (kept[low + 1] - kept[low]);
                    sinc_sum += (sinc - v[i]) * (sinc - v[i]);
                    line_sum += (line - v[i]) * (line - v[i]);
                    ++count;
                }
            }
            std::printf("    %-8d %-14.1f %-14.2f %-14.2f %.1f MHz\n", k, rate * period.mean() / k, std::sqrt(sinc_sum / static_cast<double>(count)) * 1e3,
                        std::sqrt(line_sum / static_cast<double>(count)) * 1e3, frequency * k / 1e6);
        }
    }
}

// Every gain stage against frequency, each against its own reading at 1 kHz. What the generator and the probes lose is
// the same in every stage, so the differences between the rows belong to the stages.
void stage_sweep(Source& src, double probe)
{
    // Just inside the span of each stage of the ISDS205X, so that this stage and no wider one is chosen.
    constexpr double kRanges[] = {0.04, 0.2, 0.55, 1.05, 1.15, 2.9, 5.0};
    constexpr double kFrequencies[] = {1e3, 10e3, 100e3, 500e3, 1e6, 2e6, 5e6, 10e6, 20e6};
    constexpr double kMaxVpp = 6;  // above this the generator's sine distorts
    constexpr int kCaptures = 8;
    const std::uint32_t top = src.sample_rates().back();

    std::printf("each stage against its reading at 1 kHz; ! means clipped\n");
    std::printf("  %-4s %-8s %-8s %-9s", "", "range V", "lsb mV", "1 kHz mVpp");
    for (double f : kFrequencies)
        if (f > 1e3)
            std::printf(" %7.0fk", f / 1e3);
    std::printf("\n");
    for (double range : kRanges) {
        GeneratorSettings g;
        g.output = true;
        g.amplitude_vpp = std::clamp(2 * range * 0.6 * probe, 0.1, kMaxVpp);
        std::array<std::vector<double>, 2> reading;
        std::array<std::vector<bool>, 2> clipped;
        std::array<double, 2> lsb{};
        for (double f : kFrequencies) {
            g.frequency_hz = f;
            settle(src, g);
            const std::uint32_t rate = f <= 100e3 ? 16'000'000 : top;
            const std::size_t n = rate == top ? src.max_contiguous_samples(rate)
                                              : std::clamp<std::size_t>(static_cast<std::size_t>(rate / f * 20), 4096, 200'000);
            AcquisitionRequest r{rate, n, {}, 1, 0};
            r.channel[0] = {true, {-range, range}, Coupling::DC};
            r.channel[1] = r.channel[0];
            src.acquire({rate, 4096, r.channel, 1, 0});  // the first capture after a stage change is dropped
            std::array<double, 2> sum{};
            std::array<bool, 2> clip{};
            for (int k = 0; k < kCaptures; ++k) {
                const Acquisition a = src.acquire(r);
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    sum[ch] += sine_fit(a.channel[ch].volts, f, rate).first;
                    clip[ch] = clip[ch] || a.channel[ch].clipped;
                    lsb[ch] = a.channel[ch].lsb_v;
                }
            }
            for (std::size_t ch = 0; ch < 2; ++ch) {
                reading[ch].push_back(sum[ch] / kCaptures);
                clipped[ch].push_back(clip[ch]);
            }
        }
        for (std::size_t ch = 0; ch < 2; ++ch) {
            std::printf("  CH%zu %-8.2f %-8.2f %-9.1f", ch + 1, range, lsb[ch] * 1e3, reading[ch][0] * 1e3);
            for (std::size_t i = 1; i < reading[ch].size(); ++i)
                std::printf(" %7.3f%s", reading[ch][i] / reading[ch][0], clipped[ch][i] ? "!" : " ");
            std::printf("\n");
        }
        std::fflush(stdout);
    }
}

// The generator's output changes with the coarse gain of the inputs (G 0x109 or 0x52e), though nothing is sent to the
// generator: docs/measurements.md §5. Reads the output at 1 kHz for each setting, on two neighbouring stages of alike
// resolution, one on each side of the change.
void dds_state(Source& src, double probe)
{
    constexpr double kFine = 1.05, kCoarse = 1.15;
    constexpr std::uint32_t kRate = 1'000'000;
    struct State {
        const char* name;
        double ch1, ch2;
    };
    constexpr State kStates[] = {{"both 0x52e", kCoarse, kCoarse}, {"CH1 0x109", kFine, kCoarse}, {"CH2 0x109", kCoarse, kFine}, {"both 0x109", kFine, kFine}};
    std::printf("generator setting at 1 kHz; Vpp and mean in V at the probe tip, read on CH2 / CH1\n  %-8s", "set Vpp");
    for (const State& s : kStates)
        std::printf(" %-26s", s.name);
    std::printf("\n");
    for (double vpp : {0.2, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0}) {
        GeneratorSettings g;
        g.output = true;
        g.amplitude_vpp = vpp;
        g.frequency_hz = 1000;
        settle(src, g);
        std::printf("  %-8.1f", vpp);
        for (const State& s : kStates) {
            AcquisitionRequest r{kRate, 100'000, {}, 1, 0};
            r.channel[0] = {true, {-s.ch1, s.ch1}, Coupling::DC};
            r.channel[1] = {true, {-s.ch2, s.ch2}, Coupling::DC};
            src.acquire({kRate, 4096, r.channel, 1, 0});  // the first capture after a stage change is dropped
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::array<double, 2> sum{}, mean{};
            for (int k = 0; k < 4; ++k) {
                const Acquisition a = src.acquire(r);
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    sum[ch] += sine_fit(a.channel[ch].volts, 1000 * (1 - 105e-6), kRate).first;
                    mean[ch] += measure(a.channel[ch].volts, 1.0 / kRate).mean;
                }
            }
            std::printf(" %5.2f/%5.2f %+5.2f/%+5.2f ", sum[1] / 4 * probe, sum[0] / 4 * probe, mean[1] / 4 * probe, mean[0] / 4 * probe);
        }
        std::printf("\n");
        std::fflush(stdout);
    }
}

// The method of the gain wizard: docs/measurements.md §9.
void gain_transfer(Source& src, const UserCalibration* file)
{
    src.set_calibration(src.factory_calibration());
    check_generator_on_inputs(src);
    const UserCalibration factory = src.factory_calibration();
    const std::size_t count = factory.stages.size();
    const auto reference = static_cast<std::size_t>(reference_stage(factory.stages));
    for (int run = 0; run < 2; ++run) {
        const auto start = std::chrono::steady_clock::now();
        const GainTransfer t = transfer_gains(src);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("run %d: %.1f s, worst spread between repeats %.3f %%, CH1 / CH2 on the reference stage %.4f\n", run + 1, seconds,
                    t.worst_spread * 100, t.channel_ratio);
        for (std::size_t ch = 0; ch < 2; ++ch) {
            std::printf("  CH%zu %-24s %-10s %-10s %s\n", ch + 1, "span mV", "relative", file ? "file" : "", file ? "difference %" : "");
            for (std::size_t i = 0; i < count; ++i) {
                const VoltageRange& s = factory.stages[i].factory_span[ch];
                std::printf("      %8.0f .. %8.0f     %-10.4f", s.min_v * 1e3, s.max_v * 1e3, t.relative[ch][i]);
                if (file && i < file->stages.size()) {
                    const double theirs = file->stages[i].channel[ch].gain / file->stages[reference].channel[ch].gain;
                    std::printf(" %-10.4f %+.2f", theirs, 100 * (t.relative[ch][i] / theirs - 1));
                }
                std::printf("\n");
            }
        }
        std::fflush(stdout);
    }
    const UserCalibration& stages = factory;
    std::printf("stages for the multimeter: reference %d, check %d\n", reference_stage(stages.stages), check_stage(stages.stages));
}

void gain_levels(Source& src, double hold_s)
{
    src.set_calibration(src.factory_calibration());
    const UserCalibration factory = src.factory_calibration();
    struct Level {
        int stage;
        double volts, sine_vpp;
    };
    constexpr Level kLevels[] = {{6, 2.4, 3.0}, {6, -2.4, 3.0}, {6, 2.4, 0.4}, {5, 1.6, 2.0}, {5, -1.6, 2.0}, {3, 0.45, 0.64}, {3, -0.45, 0.64}};
    int number = 0;
    for (const Level& level : kLevels) {
        GeneratorSettings g;
        g.output = true;
        g.amplitude_vpp = level.sine_vpp;
        g.offset_v = level.volts;
        settle(src, g);
        AcquisitionRequest r = request_at_stages(factory.stages, {level.stage, level.stage}, 1'000'000, 4096);
        src.acquire(r);
        r.samples = 50'000;
        double sum[2] = {}, vpp[2] = {};
        int count = 0;
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < hold_s) {
            const Acquisition a = src.acquire(r);
            for (std::size_t ch = 0; ch < 2; ++ch) {
                const SineReading s = read_sine(a.channel[ch], 1000, 1e6);
                sum[ch] += s.mean;
                vpp[ch] += s.vpp;
            }
            ++count;
        }
        std::printf("%d: stage %d, set %+.2f V, sine %.2f Vpp: mean %.5f %.5f V, sine %.4f %.4f Vpp (%d captures)\n", ++number, level.stage,
                    level.volts, level.sine_vpp, sum[0] / count, sum[1] / count, vpp[0] / count, vpp[1] / count, count);
        std::fflush(stdout);
    }
}

// A step test (a jump larger than the sine can make) missed most losses; the phase of each period does not.
void contiguity(Source& src, int captures)
{
    std::printf("contiguity, 2 Vpp sine of 160 samples a period, %d captures of 1M samples at each rate "
                "(of the first run at the top rate)\n", captures);
    GeneratorSettings g;
    g.output = true;
    g.amplitude_vpp = 2;
    for (std::uint32_t rate : src.sample_rates()) {
        // At the top rate only the first run is contiguous; blocks follow it (docs/measurements.md §10).
        const std::size_t n = rate >= src.sample_rates().back() ? src.max_contiguous_samples(rate) : src.max_samples();
        g.frequency_hz = rate / 160.0;
        settle(src, g);
        std::printf("  %2u MSa/s, %zu samples: phase jumps", rate / 1'000'000, n);
        int total = 0, clean = 0;
        for (int k = 0; k < captures; ++k) {
            const int jumps = phase_jumps(capture(src, -1.5, 1.5, rate, n).channel[1].volts);
            total += jumps;
            clean += jumps == 0;
            if (captures <= 10)
                std::printf(" %d", jumps);
        }
        std::printf("  (%d of %d captures clean, %d jumps)\n", clean, captures, total);
        std::fflush(stdout);
    }
}

void show_peaks(const char* name, const std::vector<float>& v, double rate)
{
    constexpr std::size_t kPoints = 1u << 17;
    const auto window = make_window(Window::Hann, kPoints);
    const auto power = power_spectrum(std::span(v).first(kPoints), window, kPoints);
    std::vector<std::pair<double, std::size_t>> found;
    for (std::size_t k = 3; k + 2 < power.size(); ++k)
        if (power[k] > power[k - 1] && power[k] >= power[k + 1] && power[k] > power[k - 2] && power[k] > power[k + 2])
            found.push_back({power[k], k});
    std::sort(found.rbegin(), found.rend());
    const Measurements m = measure(v, 1.0 / rate);
    std::printf("  %-8s max %+7.3f  min %+7.3f  mean %+7.3f  rms %6.3f |", name, m.max, m.min, m.mean, m.rms);
    for (std::size_t i = 0; i < std::min<std::size_t>(found.size(), 6); ++i)
        std::printf("  %.0f Hz %.0f mVrms", static_cast<double>(found[i].second) * rate / kPoints, std::sqrt(found[i].first) * 1e3);
    std::printf("\n");
}

// A channel whose noise is well under one ADC step holds its code for many samples and draws as a staircase; with half a
// step of noise or more it flickers between codes and draws as a smooth, slightly fuzzy line.
struct Grain {
    double noise_lsb = 0;  // RMS, from the second difference: a slow signal adds next to nothing to it
    double same = 0;       // share of samples on the same code as the one before
    double reversals = 0;  // share of code changes that go back at once: flicker, against steps that stay
};

Grain grain(const ChannelData& cd)
{
    Grain g;
    const std::vector<float>& v = cd.volts;
    if (v.size() < 3 || !(cd.lsb_v > 0))
        return g;
    double d2 = 0;
    std::size_t same = 0, changes = 0, reversals = 0;
    for (std::size_t i = 1; i + 1 < v.size(); ++i) {
        const long a = std::lround((v[i] - v[i - 1]) / cd.lsb_v);
        const long b = std::lround((v[i + 1] - v[i]) / cd.lsb_v);
        if (a == 0) {
            ++same;
        } else {
            ++changes;
            if ((a > 0 && b < 0) || (a < 0 && b > 0))
                ++reversals;
        }
        const double c = (v[i + 1] - 2.0 * v[i] + v[i - 1]) / cd.lsb_v;
        d2 += c * c;
    }
    const auto n = static_cast<double>(v.size() - 2);
    g.noise_lsb = std::sqrt(d2 / n / 6);  // white noise of σ gives a second difference of σ√6
    g.same = static_cast<double>(same) / n;
    g.reversals = changes ? static_cast<double>(reversals) / static_cast<double>(changes) : 0;
    return g;
}

void grain_check(Source& src, double position1, double position2)
{
    constexpr std::uint32_t kRate = 16'000'000;
    const std::array<std::array<double, 2>, 2> positions{{{0, 0}, {position1, position2}}};
    for (const auto& pos : positions) {
        AcquisitionRequest r{kRate, 200'000, {}, 1, 0};
        for (int ch = 0; ch < kChannels; ++ch) {
            const ChannelSettings cs{true, 0.5, pos[static_cast<std::size_t>(ch)], Coupling::DC, 1.0};
            r.channel[ch] = {true, screen_range_at_input(cs), Coupling::DC};
        }
        src.acquire({kRate, 4096, r.channel, 1, 0});  // the first capture after a stage change is dropped
        const Acquisition a = src.acquire(r);
        std::printf("0.5 V/div, 16 MSa/s, CH1 at %+.1f div, CH2 at %+.1f div\n", pos[0], pos[1]);
        for (int ch = 0; ch < kChannels; ++ch) {
            const ChannelData& cd = a.channel[ch];
            const Grain g = grain(cd);
            std::printf("  CH%d  1 lsb %.1f mV, span %+.2f..%+.2f V%s  noise %.2f lsb (%.1f mVrms)  same code as before %4.1f %%  "
                        "changes going back at once %4.1f %%\n",
                        ch + 1, cd.lsb_v * 1e3, cd.span.min_v, cd.span.max_v, cd.clipped ? " CLIP" : "", g.noise_lsb,
                        g.noise_lsb * cd.lsb_v * 1e3, g.same * 100, g.reversals * 100);
        }
    }
}

// What is on the two inputs, whatever it is: the mains hum of an open input shows here at once.
void peaks(Source& src)
{
    constexpr std::uint32_t kRate = 1'000'000;
    AcquisitionRequest r{kRate, 200'000, {}, 1, 0};
    r.channel[0] = {true, {-6, 6}, Coupling::DC};
    r.channel[1] = r.channel[0];
    src.acquire({kRate, 4096, r.channel, 1, 0});  // the first capture after a stage change is dropped
    const Acquisition a = src.acquire(r);
    std::printf("wide range, 1 lsb %.1f mV, clipped %d %d\n", a.channel[0].lsb_v * 1e3, a.channel[0].clipped, a.channel[1].clipped);
    show_peaks("CH1", a.channel[0].volts, kRate);
    show_peaks("CH2", a.channel[1].volts, kRate);
    std::vector<float> difference(a.channel[0].volts.size());
    for (std::size_t i = 0; i < difference.size(); ++i)
        difference[i] = a.channel[0].volts[i] - a.channel[1].volts[i];
    show_peaks("CH1-CH2", difference, kRate);
}

// The step response of each channel from a clean, slow square wave on both: averaged over the edges, the slow part as
// 1 - a·exp(-t/τ), the sine response and channel phase difference that follow from it, and the edge time at the top
// rate. docs/measurements.md §13.
void step_response(Source& src, double range)
{
    constexpr std::uint32_t kRate = 16'000'000;
    constexpr std::size_t kSamples = 320'000;
    const double dt = 1.0 / kRate;
    AcquisitionRequest r{kRate, kSamples, {}, 1, 0};
    r.channel[0] = {true, {-range, range}, Coupling::DC};
    r.channel[1] = r.channel[0];
    src.acquire({kRate, 4096, r.channel, 1, 0});  // the first capture after a stage change is dropped

    std::array<std::vector<double>, 2> sum;
    std::array<int, 2> edges{};
    std::array<Statistics, 2> base;
    double high_part = 0;
    for (int k = 0; k < 8; ++k) {
        const Acquisition a = src.acquire(r);
        for (std::size_t ch = 0; ch < 2; ++ch) {
            const std::vector<float>& v = a.channel[ch].volts;
            Edges e;
            const Measurements m = measure(v, dt, 4 * a.channel[ch].lsb_v, {}, &e);
            if (!std::isfinite(m.positive_width)) {
                std::printf("no square wave on CH%zu\n", ch + 1);
                return;
            }
            high_part = m.positive_width;
            base[ch].add(m.base);
            if (sum[ch].empty())
                sum[ch].assign(static_cast<std::size_t>(high_part / dt * 0.98), 0.0);
            for (double edge : e.rising) {
                const auto at = static_cast<std::size_t>(edge);
                if (at + sum[ch].size() >= v.size())
                    continue;
                for (std::size_t i = 0; i < sum[ch].size(); ++i)
                    sum[ch][i] += v[at + i];
                ++edges[ch];
            }
        }
    }

    std::printf("high part %.1f µs; level after a rising edge as a share of the step\n", high_part * 1e6);
    const double times[] = {0.25, 0.5, 1, 2, 5, 10, 20, 30, 40, 60, 80, 100, 150, 200, 300, 400};
    std::printf("  t µs ");
    for (double t : times)
        std::printf(" %6.2f", t);
    std::printf("\n");
    constexpr double kFrequencies[] = {1e3, 10e3, 100e3, 500e3};
    std::array<std::array<std::complex<double>, std::size(kFrequencies)>, 2> response;
    for (std::size_t ch = 0; ch < 2; ++ch) {
        std::vector<double>& mean = sum[ch];
        for (double& x : mean)
            x /= edges[ch];
        const std::size_t size = mean.size();
        double top = 0;
        for (std::size_t i = size - size / 20; i < size; ++i)
            top += mean[i];
        top /= static_cast<double>(size / 20);
        const double step = top - base[ch].mean();
        const auto level = [&](std::size_t i) { return (mean[i] - base[ch].mean()) / step; };
        std::printf("  CH%zu  ", ch + 1);
        for (double t : times) {
            const auto i = static_cast<std::size_t>(t * 1e-6 / dt);
            if (i < size)
                std::printf(" %6.3f", level(i));
            else
                std::printf(" %6s", "-");
        }
        std::printf("\n");

        // A straight line through ln(1 - level) from 1 µs on, until what is left nears the noise.
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int points = 0;
        for (auto i = static_cast<std::size_t>(1e-6 / dt); i < size && 1 - level(i) >= 0.012; ++i) {
            const double t = static_cast<double>(i) * dt;
            const double y = std::log(1 - level(i));
            sx += t;
            sy += y;
            sxx += t * t;
            sxy += t * y;
            ++points;
        }
        const double slope = (points * sxy - sx * sy) / (points * sxx - sx * sx);
        const double part = std::exp((sy - slope * sx) / points);
        const double tau = -1 / slope;
        std::printf("        slow part %.1f %% of the step, time constant %.1f µs\n", part * 100, tau * 1e6);
        std::printf("        sine response from that:");
        for (std::size_t f = 0; f < std::size(kFrequencies); ++f) {
            response[ch][f] = (1 - part) + part / std::complex<double>(1, 2 * kPi * kFrequencies[f] * tau);
            std::printf("  %.0f kHz %.3f", kFrequencies[f] / 1e3, std::abs(response[ch][f]));
        }
        std::printf("\n");
    }
    std::printf("  phase of CH2 behind CH1 from that:");
    for (std::size_t f = 0; f < std::size(kFrequencies); ++f)
        std::printf("  %.0f kHz %+.2f°", kFrequencies[f] / 1e3, std::arg(response[0][f] / response[1][f]) * 180 / kPi);
    std::printf("\n");

    // At the top rate a capture is short, so few of them hold an edge; only those that see both levels count.
    const std::uint32_t top_rate = src.sample_rates().back();
    AcquisitionRequest fast{top_rate, src.max_contiguous_samples(top_rate), r.channel, 1, 0};
    src.acquire({top_rate, 4096, r.channel, 1, 0});
    std::array<Statistics, 2> rise, fall;
    for (int k = 0; k < 600; ++k) {
        const Acquisition a = src.acquire(fast);
        for (std::size_t ch = 0; ch < 2; ++ch) {
            const Measurements m = measure(a.channel[ch].volts, 1.0 / top_rate, 4 * a.channel[ch].lsb_v);
            if (m.peak_to_peak < 0.75 * (sum[ch].back() - base[ch].mean()))
                continue;
            rise[ch].add(m.rise_time);
            fall[ch].add(m.fall_time);
        }
    }
    for (std::size_t ch = 0; ch < 2; ++ch)
        std::printf("  CH%zu at %.0f MSa/s: rise %.1f ns (std dev %.1f, %zu edges), fall %.1f ns (std dev %.1f, %zu edges)\n", ch + 1, top_rate / 1e6,
                    rise[ch].mean() * 1e9, rise[ch].stddev() * 1e9, rise[ch].count(), fall[ch].mean() * 1e9, fall[ch].stddev() * 1e9, fall[ch].count());
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    auto src = open_device();
    if (!src->has_generator()) {
        std::fprintf(stderr, "this device has no generator\n");
        return 1;
    }
    bool only_pulses = false, only_delay = false, only_external = false, only_step = false, only_peaks = false, only_sinc = false,
         only_stages = false, only_dds_state = false, only_gain_transfer = false, only_gain_levels = false;
    int only_contiguity = 0;
    bool only_grain = false;
    double grain_position1 = 2.2, grain_position2 = -2.0;
    double probe = 1;
    const char* calibration = nullptr;
    double range = 1.8;
    double hold = 25;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--pulses") == 0)
            only_pulses = true;
        else if (std::strcmp(argv[i], "--delay") == 0)
            only_delay = true;
        else if (std::strcmp(argv[i], "--external") == 0)
            only_external = true;
        else if (std::strcmp(argv[i], "--step") == 0)
            only_step = true;
        else if (std::strcmp(argv[i], "--peaks") == 0)
            only_peaks = true;
        else if (std::strcmp(argv[i], "--grain") == 0) {
            only_grain = true;
            if (i + 2 < argc && std::strchr("+-0123456789.", argv[i + 1][0]) && std::strchr("+-0123456789.", argv[i + 2][0])) {
                grain_position1 = std::atof(argv[++i]);
                grain_position2 = std::atof(argv[++i]);
            }
        }
        else if (std::strcmp(argv[i], "--sinc") == 0)
            only_sinc = true;
        else if (std::strcmp(argv[i], "--stages") == 0)
            only_stages = true;
        else if (std::strcmp(argv[i], "--dds-state") == 0)
            only_dds_state = true;
        else if (std::strcmp(argv[i], "--gain-transfer") == 0)
            only_gain_transfer = true;
        else if (std::strcmp(argv[i], "--gain-levels") == 0)
            only_gain_levels = true;
        else if (std::strcmp(argv[i], "--contiguity") == 0 && i + 1 < argc)
            only_contiguity = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--hold") == 0 && i + 1 < argc)
            hold = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--probe") == 0 && i + 1 < argc)
            probe = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--range") == 0 && i + 1 < argc)
            range = std::atof(argv[++i]);
        else
            calibration = argv[i];
    }
    std::optional<UserCalibration> file;
    if (calibration) {
        QString error;
        file = calibration_store::load(calibration, &error);
        if (!file) {
            std::fprintf(stderr, "%s: %s\n", calibration, qPrintable(error));
            return 1;
        }
        std::printf("calibration file: %d stages\n", src->set_calibration(*file));
    } else {
        std::printf("factory calibration (readings about 6.5 %% low, docs/measurements.md §3)\n");
    }
    if (only_contiguity > 0) {
        contiguity(*src, only_contiguity);
        src->park_generator();
        return 0;
    }
    if (only_gain_levels) {
        gain_levels(*src, hold);
        src->park_generator();
        return 0;
    }
    if (only_gain_transfer) {
        gain_transfer(*src, file ? &*file : nullptr);
        src->park_generator();
        return 0;
    }
    if (only_external) {
        external(*src, range);
        return 0;
    }
    if (only_step) {
        step_response(*src, range);
        return 0;
    }
    if (only_peaks) {
        peaks(*src);
        return 0;
    }
    if (only_grain) {
        grain_check(*src, grain_position1, grain_position2);
        return 0;
    }
    if (only_sinc) {
        sinc_check(*src, range);
        return 0;
    }
    if (only_stages) {
        stage_sweep(*src, probe);
        src->park_generator();
        return 0;
    }
    if (only_dds_state) {
        dds_state(*src, probe);
        src->park_generator();
        return 0;
    }
    if (only_pulses || only_delay) {
        if (only_pulses)
            pulses(*src);
        else
            delays(*src);
        src->park_generator();
        return 0;
    }


    std::printf("parking (wide range, then ±0.1 V)\n");
    levels("before", capture(*src, -6, 6, 1'000'000, 100'000), 1'000'000);
    src->park_generator();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    levels("parked", capture(*src, -0.1, 0.1, 1'000'000, 100'000), 1'000'000);

    std::printf("amplitude, 1 kHz sine\n");
    GeneratorSettings g;
    g.output = true;
    g.frequency_hz = 1000;
    char label[64];
    for (double vpp : {0.2, 0.5, 1.0, 2.0, 3.0, 5.0}) {
        g.amplitude_vpp = vpp;
        settle(*src, g);
        const double pad = 0.25 * vpp + 0.15;
        std::snprintf(label, sizeof label, "%.1f Vpp", vpp);
        levels(label, capture(*src, -vpp / 2 - pad, vpp / 2 + pad, 1'000'000, 100'000), 1'000'000);
    }

    std::printf("offset, 0.1 Vpp\n");
    g.amplitude_vpp = 0.1;
    for (double off : {-3.0, -1.5, -0.5, 0.0, 0.5, 1.5, 3.0}) {
        g.offset_v = off;
        settle(*src, g);
        std::snprintf(label, sizeof label, "%+.1f V", off);
        levels(label, capture(*src, off - 0.3, off + 0.3, 1'000'000, 100'000), 1'000'000);
    }

    std::printf("waveforms, 1 kHz 2 Vpp\n");
    g.offset_v = 0;
    g.amplitude_vpp = 2;
    for (int w = 1; w < kWaveforms; ++w) {
        g.waveform = static_cast<Waveform>(w);
        settle(*src, g);
        levels(w == 1 ? "square" : w == 2 ? "triangle" : w == 3 ? "ramp up" : "ramp down",
               capture(*src, -1.8, 1.8, 1'000'000, 100'000), 1'000'000);
    }
    g.waveform = Waveform::Square;
    g.duty_percent = 25;
    settle(*src, g);
    levels("square, duty 25 %", capture(*src, -1.8, 1.8, 1'000'000, 100'000), 1'000'000);

    pulses(*src);


    std::printf("frequency response, 1 Vpp sine, against 1 kHz (DDS, probe and input together)\n");
    g = GeneratorSettings{};
    g.output = true;
    g.amplitude_vpp = 1;
    double reference = 0;
    for (double f : {1e3, 10e3, 100e3, 500e3, 1e6, 2e6, 5e6, 10e6, 20e6}) {
        g.frequency_hz = f;
        settle(*src, g);
        // Up to 1 MHz a long capture at 16 MSa/s; above, single contiguous blocks at the top rate.
        const bool blocks = f > 1e6;
        const std::uint32_t rate = blocks ? src->sample_rates().back() : 16'000'000;
        const std::size_t n = blocks ? src->max_contiguous_samples(rate) : static_cast<std::size_t>(rate / f * 50);
        const int repeats = blocks ? 16 : 2;
        double sum = 0;
        for (int k = 0; k < repeats; ++k)
            sum += fitted_vpp(capture(*src, -1.5, 1.5, rate, n).channel[1].volts, f, rate);
        const double vpp = sum / repeats;
        if (reference == 0)
            reference = vpp;
        std::printf("  %9.0f Hz  Vpp %.4f  %6.3f  %+6.2f dB\n", f, vpp, vpp / reference, 20 * std::log10(vpp / reference));
    }

    contiguity(*src, 5);
    src->park_generator();
    return 0;
}
