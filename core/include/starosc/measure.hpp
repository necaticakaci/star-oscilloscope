// SPDX-License-Identifier: GPL-3.0-or-later
// Automatic waveform measurements and running statistics.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace starosc {

enum class Parameter {
    Frequency,
    Period,
    RiseTime,
    FallTime,
    PositiveWidth,
    NegativeWidth,
    Duty,
    PeakToPeak,
    Amplitude,
    Max,
    Min,
    Top,
    Base,
    Mean,
    Rms,
    AcRms,
    Overshoot,
    Undershoot,
    // Between CH1 and CH2.
    Delay,
    Phase,
};

// Measured on one trace, in table order.
inline constexpr std::array kParameters{
    Parameter::Frequency, Parameter::Period,     Parameter::RiseTime,  Parameter::FallTime, Parameter::PositiveWidth,
    Parameter::NegativeWidth, Parameter::Duty,   Parameter::PeakToPeak, Parameter::Amplitude, Parameter::Max,
    Parameter::Min,       Parameter::Top,        Parameter::Base,      Parameter::Mean,     Parameter::Rms,
    Parameter::AcRms,     Parameter::Overshoot,  Parameter::Undershoot};
// Measured between CH1 and CH2.
inline constexpr std::array kPairParameters{Parameter::Delay, Parameter::Phase};
// Shown until the user chooses otherwise.
inline constexpr std::array kDefaultParameters{Parameter::Frequency, Parameter::Period, Parameter::Duty,
                                               Parameter::PeakToPeak, Parameter::Max,   Parameter::Min,
                                               Parameter::Mean,       Parameter::Rms};

std::string_view parameter_name(Parameter p);
std::string_view parameter_unit(Parameter p);  // V, s, Hz, "%" or "°"

// Reference levels of the timing measurements, in percent of the distance from base to top.
struct MeasureThresholds {
    double low = 10, mid = 50, high = 90;

    bool operator==(const MeasureThresholds&) const = default;
};

// Returns `t` with each level in 1..99 and at least one percent apart, in order.
MeasureThresholds constrained(MeasureThresholds t);

struct Measurements {
    static constexpr double kNone = std::numeric_limits<double>::quiet_NaN();
    double max = kNone, min = kNone, peak_to_peak = kNone, mean = kNone, rms = kNone, ac_rms = kNone;
    // The flat levels of a pulse; a waveform without flat levels, such as a sine, has max and min here.
    double top = kNone, base = kNone, amplitude = kNone;
    bool pulse = false;  // short edges between two levels, unlike a sine
    double overshoot = kNone, undershoot = kNone;  // percent of the amplitude
    double frequency = kNone, period = kNone, duty = kNone;  // duty in percent
    double rise_time = kNone, fall_time = kNone;             // means over the edges in the samples
    double positive_width = kNone, negative_width = kNone;
    // The edge took so few samples that its real time is unknown: below the value of resolution_s.
    bool rise_unresolved = false, fall_unresolved = false;
    double resolution_s = kNone;

    double value(Parameter p) const;
    bool unresolved(Parameter p) const;
};

// Where a trace crosses its middle threshold, in fractional sample positions, ascending.
struct Edges {
    std::vector<double> rising, falling;
};

// Timing parameters need a swing above `noise_floor` (volts); frequency, period and duty also need two edges of one
// direction. With `edges`, also returns the edges found.
Measurements measure(std::span<const float> samples, double sample_interval_s, double noise_floor = 0,
                     const MeasureThresholds& thresholds = {}, Edges* edges = nullptr);

// Between two traces of the same capture.
struct PairMeasurements {
    double delay = Measurements::kNone;  // seconds, positive when B follows A
    double phase = Measurements::kNone;  // degrees, -180..180, positive when B follows A

    double value(Parameter p) const;
};

// One trace as measure_pair needs it.
struct PairInput {
    std::span<const float> samples;
    const Measurements& measurements;
    const Edges& edges;
};

// Signals with a period need the same frequency within 1 %, otherwise the result would mean nothing.
// - Two pulses: the rising edges of A against the matching edges of B.
// - Other signals with a period: the phase of the fundamental. A slow edge can only be timed to about one ADC step
//   divided by its slope, which on a 1 kHz sine is microseconds; the fundamental gives nanoseconds.
// - No period: the first rising edge of A against the nearest of B, and no phase.
PairMeasurements measure_pair(const PairInput& a, const PairInput& b, double sample_interval_s);

class Statistics {
public:
    void add(double x);
    void reset() { *this = Statistics(); }
    std::size_t count() const { return count_; }
    double min() const { return count_ ? min_ : Measurements::kNone; }
    double max() const { return count_ ? max_ : Measurements::kNone; }
    double mean() const { return count_ ? mean_ : Measurements::kNone; }
    double stddev() const { return count_ > 1 ? std::sqrt(m2_ / static_cast<double>(count_ - 1)) : Measurements::kNone; }

private:
    std::size_t count_ = 0;
    double min_ = 0, max_ = 0, mean_ = 0, m2_ = 0;
};

}  // namespace starosc
