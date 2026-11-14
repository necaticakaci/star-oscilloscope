// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/generator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "isds/generator.hpp"

namespace starosc {
namespace {

constexpr double kSineMaxHz = 20e6;
constexpr double kOtherMaxHz = 2e6;
constexpr int kLevelSamples = 2000;  // per period, for generator_levels

}  // namespace

GeneratorLimits generator_limits(Waveform waveform)
{
    GeneratorLimits l;
    l.max_hz = waveform == Waveform::Sine ? kSineMaxHz : kOtherMaxHz;
    l.min_vpp = isds::kAmplitudeFloorVpp;
    l.max_vpp = isds::kAmplitudeMaxVpp;
    l.clean_vpp = isds::kAmplitudeCleanVpp;
    l.low_v = isds::kOutputLowV;
    l.high_v = isds::kOutputHighV;
    return l;
}

GeneratorSettings constrained(GeneratorSettings g)
{
    const GeneratorLimits l = generator_limits(g.waveform);
    const auto hz = [&](double f) { return std::clamp(std::isfinite(f) ? std::round(f) : l.min_hz, l.min_hz, l.max_hz); };
    g.frequency_hz = hz(g.frequency_hz);
    g.amplitude_vpp = std::clamp(std::isfinite(g.amplitude_vpp) ? g.amplitude_vpp : l.min_vpp, l.min_vpp, l.max_vpp);
    g.offset_v = std::clamp(std::isfinite(g.offset_v) ? g.offset_v : 0.0, l.low_v, l.high_v);
    g.duty_percent = std::clamp(g.duty_percent, l.min_duty, l.max_duty);
    g.sweep.start_hz = hz(g.sweep.start_hz);
    g.sweep.stop_hz = hz(g.sweep.stop_hz);
    g.sweep.seconds = std::clamp(std::isfinite(g.sweep.seconds) ? g.sweep.seconds : 1.0, 0.1, 100.0);
    return g;
}

double generator_output(const GeneratorSettings& g, double t)
{
    const double a = g.amplitude_vpp / 2;
    const double phase = g.frequency_hz * t - std::floor(g.frequency_hz * t);  // 0..1
    double v = 0;
    switch (g.waveform) {
    case Waveform::Sine: v = std::sin(2 * std::numbers::pi * phase); break;
    case Waveform::Square: v = phase < g.duty_percent / 100.0 ? 1 : -1; break;
    case Waveform::Triangle:
        v = phase < 0.25 ? 4 * phase : phase < 0.75 ? 2 - 4 * phase : 4 * phase - 4;
        break;
    case Waveform::RampUp: v = 2 * phase - 1; break;
    case Waveform::RampDown: v = 1 - 2 * phase; break;
    }
    return std::clamp(g.offset_v + a * v, isds::kOutputLowV, isds::kOutputHighV);
}

GeneratorLevels generator_levels(const GeneratorSettings& g)
{
    GeneratorLevels out{-INFINITY, INFINITY, 0, false};
    double sum_sq = 0;
    for (int i = 0; i < kLevelSamples; ++i) {
        const double v = generator_output(g, (i + 0.5) / kLevelSamples / g.frequency_hz);
        out.max = std::max(out.max, v);
        out.min = std::min(out.min, v);
        sum_sq += v * v;
    }
    out.rms = std::sqrt(sum_sq / kLevelSamples);
    out.clipped = g.offset_v + g.amplitude_vpp / 2 > isds::kOutputHighV ||
                  g.offset_v - g.amplitude_vpp / 2 < isds::kOutputLowV;
    return out;
}

double sweep_frequency(const SweepSettings& s, double elapsed_s)
{
    const double seconds = std::max(s.seconds, 1e-3);
    const double fraction = std::fmod(std::max(elapsed_s, 0.0), seconds) / seconds;
    if (s.logarithmic && s.start_hz > 0 && s.stop_hz > 0)
        return s.start_hz * std::pow(s.stop_hz / s.start_hz, fraction);
    return s.start_hz + (s.stop_hz - s.start_hz) * fraction;
}

}  // namespace starosc
