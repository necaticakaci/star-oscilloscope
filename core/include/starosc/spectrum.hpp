// SPDX-License-Identifier: GPL-3.0-or-later
// Spectrum analysis: windowed FFT of a capture, averaging and peak hold, and the harmonic measurements
// (THD, SNR, SINAD, ENOB, SFDR).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "starosc/settings.hpp"
#include "starosc/source.hpp"
#include "starosc/window.hpp"

namespace starosc {

// RMS volts, or dB relative to 1 V, 1 mV, 0.7746 V (dBu) and 1 mW into 50 ohm (dBm).
enum class SpectrumUnit { Vrms, dBV, dBmV, dBu, dBm };
inline constexpr int kSpectrumUnits = 5;

enum class SpectrumAveraging { Off, Average, PeakHold };

// Traces of the spectrum: the two channels and the math trace of the scope settings.
inline constexpr int kSpectrumSources = kChannels + 1;
inline constexpr std::size_t kMinSpectrumPoints = 8;
inline constexpr std::size_t kMaxSpectrumPoints = std::size_t{1} << 20;
inline constexpr int kMinHarmonics = 2;
inline constexpr int kMaxHarmonics = 10;
inline constexpr int kMaxSpectrumAverages = 256;
inline constexpr int kSpectrumDivisionsX = 10;
inline constexpr int kSpectrumDivisionsY = 10;

struct SpectrumSettings {
    std::uint32_t sample_rate_hz = 1'000'000;  // one of the source's rates; the span is half of it
    std::size_t points = 65536;                // FFT length, a power of two
    Window window = Window::Hann;
    SpectrumUnit unit = SpectrumUnit::dBV;
    SpectrumAveraging averaging = SpectrumAveraging::Off;
    int averages = 8;                          // spectra in the running average
    std::array<bool, kSpectrumSources> shown{true, true, false};
    int harmonics = 7;                         // highest harmonic counted in THD
    int marker_source = 0;                     // trace whose fundamental and harmonics are marked; -1 none
    // Display
    bool log_frequency = false;
    double start_hz = 0;                       // 0 and 0: the whole span
    double stop_hz = 0;
    double top_dbv = 10;                       // reference level at the top of the screen, dB relative to 1 Vrms
    double db_per_div = 10;
};

std::string_view unit_name(SpectrumUnit unit);
bool unit_is_db(SpectrumUnit unit);

// The unit a trace's level reads in. The channels, and a math sum or difference, are volts and read in the chosen
// unit. A product is in V² and a ratio has none: in Vrms they read their own RMS value, in a dB unit dB relative to
// 1 V² or to 1, as dBV is to 1 V. The offsets of dBmV, dBu and dBm are for volts and are not added; the screen draws
// these traces on the axis' numbers.
struct TraceUnit {
    SpectrumUnit unit = SpectrumUnit::dBV;  // the level is level_in_unit(power, unit)
    std::string name;                       // "dBV", "V", "dB(V²)", "V²", "dB", "U"
    std::string column;                     // for a CSV header: "dbv", "vrms", "db_v2", "v2rms", "db", "urms"
};
TraceUnit trace_unit(SpectrumUnit unit, int source, MathOperation operation);
// Level of `power` (Vrms squared) in `unit`; zero power gives a very low level instead of -infinity.
double level_in_unit(double power, SpectrumUnit unit);
// dB relative to 1 Vrms from a level in a dB unit, and back.
double dbv_from_level(double level, SpectrumUnit unit);
double level_from_dbv(double dbv, SpectrumUnit unit);

// Points are a power of two in range; the averages and harmonics in range.
SpectrumSettings constrained(const SpectrumSettings& settings);

// One-sided power spectrum of `samples`, windowed by `window` (as long as the samples) and zero-padded to
// `points`: bins 0 .. points/2, in Vrms squared, scaled so that a sine's peak bin reads its RMS value squared.
std::vector<double> power_spectrum(std::span<const float> samples, std::span<const double> window, std::size_t points);

struct Harmonic {
    int order = 0;               // 2, 3, ...
    double frequency_hz = 0;     // where it falls in the span (folded when above half the sample rate)
    double power = 0;            // Vrms squared; 0 when it falls on the fundamental or DC
};

struct SpectrumAnalysis {
    bool valid = false;
    double fundamental_hz = 0;
    double fundamental_power = 0;  // Vrms squared, summed over the tone's bins
    std::vector<Harmonic> harmonics;
    double thd = 0;                // sqrt(sum of harmonic powers / fundamental power)
    double snr_db = 0;             // fundamental to noise, harmonics and DC left out
    double sinad_db = 0;           // fundamental to noise and harmonics
    double enob_bits = 0;          // (SINAD - 1.76 dB) / 6.02 dB
    double sfdr_db = 0;            // fundamental to the largest other bin, DC left out
    double noise_power = 0;        // Vrms squared, over the whole span
};

// Measures a power spectrum from power_spectrum(): the largest tone above DC is the fundamental.
// `noise_bandwidth` is in bins of the spectrum (the window's, times points / window length).
SpectrumAnalysis analyze_spectrum(std::span<const float> power, double bin_hz, double noise_bandwidth,
                                  int lobe_bins, int harmonics);

struct SpectrumFrame {
    std::uint64_t sequence = 0;
    ScopeSettings scope;           // channel and math settings the frame was acquired with
    SpectrumSettings settings;
    std::uint32_t sample_rate_hz = 0;
    std::size_t points = 0;        // FFT length used; at most a block at a rate with gaps between blocks
    std::size_t record = 0;        // samples of each FFT (the rest of `points` is zero padding)
    std::size_t records = 0;       // FFTs averaged within the capture
    double bin_hz = 0;
    double noise_bandwidth = 1;    // in bins
    int averaged = 1;              // spectra in the average or peak hold so far
    // Bins 0 .. points/2 in Vrms squared, averaged or held; empty for a trace not shown.
    std::array<std::vector<float>, kSpectrumSources> power;
    std::array<SpectrumAnalysis, kSpectrumSources> analysis;
    std::array<bool, kChannels> clipped{};
};

class SpectrumEngine {
public:
    explicit SpectrumEngine(Source& source) : source_(source) {}

    // Captures at the spectrum's rate with the channel ranges, coupling and probes of `scope`. At a rate whose
    // capture comes in blocks with gaps (48 MSa/s), the FFT is at most a block long and the capture's blocks
    // are averaged.
    SpectrumFrame acquire(const ScopeSettings& scope, const SpectrumSettings& settings);
    // Restarts the average and the peak hold. A change of settings that alters the spectrum does it too.
    void reset() { count_ = 0; }

private:
    const std::vector<double>& window(Window type, std::size_t n);

    Source& source_;
    std::uint64_t sequence_ = 0;
    std::vector<double> window_;
    Window window_type_ = Window::Rectangular;
    std::array<std::vector<double>, kSpectrumSources> accumulated_;
    int count_ = 0;
    std::vector<double> key_;  // settings the accumulated spectra were made with
};

}  // namespace starosc
