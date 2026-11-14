// SPDX-License-Identifier: GPL-3.0-or-later
// DDS function generator settings in volts and hertz, and the output they give.
#pragma once

namespace starosc {

enum class Waveform { Sine, Square, Triangle, RampUp, RampDown };
inline constexpr int kWaveforms = 5;

// Software sweep: the frequency runs from start to stop in `seconds`, then starts over.
struct SweepSettings {
    bool enabled = false;
    double start_hz = 100;
    double stop_hz = 10'000;
    double seconds = 1.0;
    bool logarithmic = true;
    bool operator==(const SweepSettings&) const = default;
};

struct GeneratorSettings {
    bool output = false;
    Waveform waveform = Waveform::Sine;
    double frequency_hz = 1000;
    double amplitude_vpp = 1.0;  // without a load
    double offset_v = 0.0;
    int duty_percent = 50;       // square wave only
    SweepSettings sweep;
    bool operator==(const GeneratorSettings&) const = default;
};

// Typical limits of the ISDS205X (isds/generator.hpp).
struct GeneratorLimits {
    double min_hz = 1, max_hz = 0;
    double min_vpp = 0, max_vpp = 0;
    double clean_vpp = 0;    // the wave distorts above
    double low_v = 0, high_v = 0;  // output limits
    int min_duty = 1, max_duty = 99;
};

GeneratorLimits generator_limits(Waveform waveform);

// Every value moved into the limits of its waveform; sweep frequencies included.
GeneratorSettings constrained(GeneratorSettings settings);

// Output without a load at time `t` seconds, clipped at the output limits. A period starts at t = 0:
// sine and triangle rise from the offset, square starts high, ramps start at an end.
double generator_output(const GeneratorSettings& settings, double t);

struct GeneratorLevels {
    double max = 0, min = 0, rms = 0;
    bool clipped = false;  // the wave reaches an output limit
};

GeneratorLevels generator_levels(const GeneratorSettings& settings);

// Sweep frequency `elapsed_s` seconds after the sweep started.
double sweep_frequency(const SweepSettings& sweep, double elapsed_s);

}  // namespace starosc
