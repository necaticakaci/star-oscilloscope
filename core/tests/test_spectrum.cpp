// SPDX-License-Identifier: GPL-3.0-or-later
// FFT, window functions, spectrum scaling and the harmonic measurements.
#include <cmath>
#include <complex>
#include <numbers>
#include <random>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "starosc/fft.hpp"
#include "starosc/spectrum.hpp"
#include "starosc/window.hpp"

using namespace starosc;
using Catch::Approx;

namespace {

constexpr double kPi = std::numbers::pi;

std::vector<float> tones(std::size_t n, double rate, std::initializer_list<std::pair<double, double>> f_and_peak,
                         double noise_rms = 0, double dc = 0)
{
    std::mt19937 rng(7);
    std::normal_distribution<double> noise(0, noise_rms > 0 ? noise_rms : 1);
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        double v = dc + (noise_rms > 0 ? noise(rng) : 0);
        for (const auto& [f, peak] : f_and_peak)
            v += peak * std::sin(2 * kPi * f * static_cast<double>(i) / rate + 0.3);
        x[i] = static_cast<float>(v);
    }
    return x;
}

double power_db(double p) { return 10 * std::log10(p); }

ScopeSettings scope_settings()
{
    ScopeSettings s;
    s.channel[0] = {true, 1.0, 1.0, Coupling::DC, 1.0};
    s.channel[1] = {true, 0.5, 0.0, Coupling::DC, 1.0};
    return s;
}

}  // namespace

TEST_CASE("the FFT matches a direct DFT")
{
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> u(-1, 1);
    std::vector<double> x(64);
    for (double& v : x)
        v = u(rng);
    const auto fast = real_fft(x);
    REQUIRE(fast.size() == 33);
    for (std::size_t k = 0; k < fast.size(); ++k) {
        std::complex<double> direct;
        for (std::size_t n = 0; n < x.size(); ++n)
            direct += x[n] * std::polar(1.0, -2 * kPi * static_cast<double>(k * n) / 64.0);
        CHECK(std::abs(fast[k] - direct) < 1e-9);
    }
    std::vector<std::complex<double>> odd(12);
    CHECK_THROWS(fft(odd));
}

TEST_CASE("window functions")
{
    for (int i = 0; i < kWindows; ++i) {
        const auto type = static_cast<Window>(i);
        INFO(window_name(type));
        const auto w = make_window(type, 256);
        REQUIRE(w.size() == 256);
        CHECK(*std::max_element(w.begin(), w.end()) == Approx(1));
        CHECK(*std::min_element(w.begin(), w.end()) > -0.1);  // the flat top dips a little below zero
        // Periodic windows: symmetric about the middle sample.
        for (std::size_t k = 1; k < 128; k += 13)
            CHECK(w[k] == Approx(w[256 - k]).margin(type == Window::DolphChebyshev ? 0.03 : 1e-9));
        CHECK(window_lobe_bins(type) >= 1);
    }
    CHECK(noise_bandwidth_bins(make_window(Window::Rectangular, 1024)) == Approx(1));
    CHECK(noise_bandwidth_bins(make_window(Window::Hann, 1024)) == Approx(1.5));
    CHECK(noise_bandwidth_bins(make_window(Window::Hamming, 1024)) == Approx(1.363).margin(0.002));
    CHECK(noise_bandwidth_bins(make_window(Window::BlackmanHarris, 1024)) == Approx(2.004).margin(0.002));
    CHECK(noise_bandwidth_bins(make_window(Window::FlatTop, 1024)) == Approx(3.77).margin(0.01));
    // A length that is no power of two, as for 1M points from the device's slightly shorter memory.
    const auto cheb = make_window(Window::DolphChebyshev, 1000);
    CHECK(cheb.size() == 1000);
    CHECK(cheb[500] == Approx(1).margin(1e-4));
    CHECK(cheb[0] < 0.01);
}

TEST_CASE("a sine reads its RMS level")
{
    const double rate = 1e6;
    const std::size_t n = 4096;
    // On a bin and halfway between two: the flat top reads the level at its peak either way.
    for (double f : {100 * rate / n, 100.5 * rate / n}) {
        const auto x = tones(n, rate, {{f, 1.0}});
        const auto p = power_spectrum(x, make_window(Window::FlatTop, n), n);
        REQUIRE(p.size() == n / 2 + 1);
        CHECK(level_in_unit(*std::max_element(p.begin(), p.end()), SpectrumUnit::dBV) ==
              Approx(-3.0103).margin(0.02));
    }
    // DC reads its value.
    const auto dc = tones(n, rate, {}, 0, 0.5);
    CHECK(power_spectrum(dc, make_window(Window::Hann, n), n)[0] == Approx(0.25));

    CHECK(level_in_unit(1.0, SpectrumUnit::Vrms) == Approx(1));
    CHECK(level_in_unit(1.0, SpectrumUnit::dBmV) == Approx(60));
    CHECK(level_in_unit(1.0, SpectrumUnit::dBm) == Approx(13.0103));
    CHECK(level_in_unit(0.7746 * 0.7746, SpectrumUnit::dBu) == Approx(0).margin(1e-3));
    CHECK(level_from_dbv(dbv_from_level(-20, SpectrumUnit::dBm), SpectrumUnit::dBm) == Approx(-20));
    CHECK(dbv_from_level(0.1, SpectrumUnit::Vrms) == Approx(-20));
    CHECK(std::isfinite(level_in_unit(0, SpectrumUnit::dBV)));
}

TEST_CASE("harmonic measurements")
{
    const double rate = 1e6;
    const std::size_t n = 65536;
    const double f1 = 10'123;
    const double noise = 1e-3;  // RMS
    // 1 V peak; second harmonic at -40 dBc, third at -50 dBc.
    const auto x = tones(n, rate, {{f1, 1.0}, {2 * f1, 0.01}, {3 * f1, 0.01 / std::sqrt(10.0)}}, noise);
    for (Window type : {Window::Hann, Window::BlackmanHarris, Window::FlatTop, Window::Kaiser, Window::DolphChebyshev}) {
        INFO(window_name(type));
        const auto w = make_window(type, n);
        const auto p = power_spectrum(x, w, n);
        const std::vector<float> pf(p.begin(), p.end());
        const auto a = analyze_spectrum(pf, rate / n, noise_bandwidth_bins(w), window_lobe_bins(type), 5);
        REQUIRE(a.valid);
        CHECK(a.fundamental_hz == Approx(f1).margin(0.5));
        CHECK(power_db(a.fundamental_power) == Approx(-3.0103).margin(0.05));
        REQUIRE(a.harmonics.size() == 4);
        CHECK(a.harmonics[0].frequency_hz == Approx(2 * f1).margin(1));
        CHECK(power_db(a.harmonics[0].power / a.fundamental_power) == Approx(-40).margin(0.3));
        CHECK(power_db(a.harmonics[1].power / a.fundamental_power) == Approx(-50).margin(0.5));
        CHECK(20 * std::log10(a.thd) == Approx(10 * std::log10(1e-4 + 1e-5)).margin(0.3));
        // Signal 0.5 V^2 against noise 1e-6 V^2: 57 dB. The Chebyshev window's flat side lobes, 100 dB under
        // the tone, lie on this noise floor and add to it.
        const double snr_margin = type == Window::DolphChebyshev ? 0.8 : 0.5;
        CHECK(a.snr_db == Approx(power_db(0.5 / (noise * noise))).margin(snr_margin));
        CHECK(a.sinad_db == Approx(power_db(0.5 / (noise * noise + 0.5 * 1.1e-4))).margin(0.5));
        CHECK(a.enob_bits == Approx((a.sinad_db - 1.76) / 6.02));
        CHECK(a.sfdr_db == Approx(40).margin(1.0));
    }
}

TEST_CASE("harmonics above half the sample rate fold back")
{
    const double rate = 1e6;
    const std::size_t n = 8192;
    const double f1 = 300e3;  // the second harmonic, 600 kHz, shows at 400 kHz
    const auto x = tones(n, rate, {{f1, 1.0}, {2 * f1, 0.1}});
    const auto w = make_window(Window::Hann, n);
    const auto p = power_spectrum(x, w, n);
    const std::vector<float> pf(p.begin(), p.end());
    const auto a = analyze_spectrum(pf, rate / n, noise_bandwidth_bins(w), window_lobe_bins(Window::Hann), 3);
    REQUIRE(a.valid);
    CHECK(a.harmonics[0].frequency_hz == Approx(400e3).margin(rate / n));
    CHECK(power_db(a.harmonics[0].power / a.fundamental_power) == Approx(-20).margin(0.2));
    CHECK(a.harmonics[1].frequency_hz == Approx(100e3).margin(rate / n));  // 900 kHz
}

TEST_CASE("a tone inside the DC lobe is no fundamental")
{
    const double rate = 16e6;
    const std::size_t n = 65536;
    // 1 kHz at 16 MSa/s is bin 4.1, inside the lobe of DC with Blackman-Harris.
    const auto x = tones(n, rate, {{1000, 1.0}}, 1e-3, 0.2);
    const auto w = make_window(Window::BlackmanHarris, n);
    const auto p = power_spectrum(x, w, n);
    const std::vector<float> pf(p.begin(), p.end());
    const auto a = analyze_spectrum(pf, rate / n, noise_bandwidth_bins(w), window_lobe_bins(Window::BlackmanHarris), 5);
    CHECK_FALSE(a.valid);
}

TEST_CASE("settings are kept in range")
{
    SpectrumSettings s;
    s.points = 3000;
    s.harmonics = 40;
    s.averages = 0;
    s.start_hz = 5e3;
    s.stop_hz = 1e3;
    const SpectrumSettings c = constrained(s);
    CHECK(c.points == 2048);
    CHECK(c.harmonics == kMaxHarmonics);
    CHECK(c.averages == 1);
    CHECK(c.start_hz == 0);
    CHECK(c.stop_hz == 0);
    s.points = 1;
    CHECK(constrained(s).points == kMinSpectrumPoints);
}

TEST_CASE("the spectrum of the simulator's signals")
{
    auto sim = make_simulator();  // CH1: 0..2 V square at 1 kHz; CH2: 0.7 V sine at 1.5 kHz
    SpectrumEngine engine(*sim);
    SpectrumSettings s;
    s.points = 65536;
    s.window = Window::BlackmanHarris;
    const SpectrumFrame f = engine.acquire(scope_settings(), s);
    CHECK(f.sample_rate_hz == 1'000'000);
    CHECK(f.points == 65536);
    CHECK(f.record == 65536);
    CHECK(f.records == 1);
    CHECK(f.bin_hz == Approx(1e6 / 65536));
    REQUIRE(f.power[0].size() == 32769);
    REQUIRE(f.power[1].size() == 32769);
    CHECK(f.power[2].empty());

    const SpectrumAnalysis& square = f.analysis[0];
    REQUIRE(square.valid);
    CHECK(square.fundamental_hz == Approx(1000).margin(1));
    // Fundamental of a 2 Vpp square wave: 4/pi V peak, 0.900 Vrms.
    CHECK(power_db(square.fundamental_power) == Approx(20 * std::log10(4 / kPi / std::sqrt(2.0))).margin(0.2));
    REQUIRE(square.harmonics.size() == 6);
    CHECK(power_db(square.harmonics[1].power / square.fundamental_power) == Approx(-9.54).margin(0.3));  // 3rd
    CHECK(power_db(square.harmonics[3].power / square.fundamental_power) == Approx(-13.98).margin(0.3));  // 5th
    CHECK(power_db(square.harmonics[0].power / square.fundamental_power) < -30);  // even ones are missing
    // Odd harmonics up to the 7th: sqrt(1/9 + 1/25 + 1/49) = 41.4 %.
    CHECK(square.thd == Approx(0.414).margin(0.01));

    const SpectrumAnalysis& sine = f.analysis[1];
    REQUIRE(sine.valid);
    CHECK(sine.fundamental_hz == Approx(1500).margin(1));
    CHECK(power_db(sine.fundamental_power) == Approx(20 * std::log10(0.7 / std::sqrt(2.0))).margin(0.2));
    CHECK(sine.thd < 0.01);
}

TEST_CASE("averaging and peak hold")
{
    auto sim = make_simulator();
    SpectrumEngine engine(*sim);
    SpectrumSettings s;
    s.points = 4096;
    s.shown = {false, true, false};
    s.averaging = SpectrumAveraging::Average;
    s.averages = 3;
    CHECK(engine.acquire(scope_settings(), s).averaged == 1);
    CHECK(engine.acquire(scope_settings(), s).averaged == 2);
    CHECK(engine.acquire(scope_settings(), s).averaged == 3);
    CHECK(engine.acquire(scope_settings(), s).averaged == 3);
    s.window = Window::Hamming;  // a different spectrum starts over
    CHECK(engine.acquire(scope_settings(), s).averaged == 1);

    s.averaging = SpectrumAveraging::PeakHold;
    const SpectrumFrame first = engine.acquire(scope_settings(), s);
    const SpectrumFrame second = engine.acquire(scope_settings(), s);
    CHECK(second.averaged == 2);
    for (std::size_t k = 0; k < first.power[1].size(); ++k)
        REQUIRE(second.power[1][k] >= first.power[1][k]);
    engine.reset();
    CHECK(engine.acquire(scope_settings(), s).averaged == 1);
}

TEST_CASE("at 48 MSa/s the spectrum comes from the first run or the capture's blocks")
{
    auto sim = make_simulator();
    GeneratorSettings g;
    g.output = true;
    g.frequency_hz = 5e6;
    g.amplitude_vpp = 2;
    sim->set_generator(g);  // CH2 now sees the generator
    SpectrumEngine engine(*sim);
    SpectrumSettings s;
    s.sample_rate_hz = 48'000'000;
    s.shown = {false, true, false};

    SECTION("more points than a block: the first run, zero-padded")
    {
        s.points = 65536;
        const SpectrumFrame f = engine.acquire(scope_settings(), s);
        CHECK(f.sample_rate_hz == 48'000'000);
        CHECK(f.points == 1024);
        CHECK(f.record == 1020);
        CHECK(f.records == 1);
        REQUIRE(f.analysis[1].valid);
        CHECK(f.analysis[1].fundamental_hz == Approx(5e6).margin(f.bin_hz / 2));
        CHECK(power_db(f.analysis[1].fundamental_power) == Approx(20 * std::log10(1 / std::sqrt(2.0))).margin(0.3));
    }
    SECTION("a block or less: averaged over the first run and the blocks")
    {
        s.points = 256;
        const SpectrumFrame f = engine.acquire(scope_settings(), s);
        CHECK(f.points == 256);
        CHECK(f.record == 256);
        CHECK(f.records == 3 + 32);  // three in the first run of 1020, one in each block
        REQUIRE(f.analysis[1].valid);
        CHECK(f.analysis[1].fundamental_hz == Approx(5e6).margin(f.bin_hz / 2));
        CHECK(power_db(f.analysis[1].fundamental_power) == Approx(20 * std::log10(1 / std::sqrt(2.0))).margin(0.3));
    }
}

TEST_CASE("the spectrum of the math trace")
{
    auto sim = make_simulator();
    SpectrumEngine engine(*sim);
    ScopeSettings scope = scope_settings();
    scope.math = {false, MathOperation::Subtract, 0, 1, 1.0, 0.0};  // used even while hidden on the scope
    scope.channel[1].enabled = false;
    SpectrumSettings s;
    s.points = 65536;
    s.shown = {false, false, true};
    const SpectrumFrame f = engine.acquire(scope, s);
    CHECK(f.power[0].empty());
    REQUIRE(f.power[2].size() == 32769);
    REQUIRE(f.analysis[2].valid);
    CHECK(f.analysis[2].fundamental_hz == Approx(1000).margin(1));
    // The sine of CH2 at 1.5 kHz is in the difference too.
    const auto bin = static_cast<std::size_t>(std::lround(1500 / f.bin_hz));
    CHECK(level_in_unit(f.power[2][bin], SpectrumUnit::dBV) == Approx(20 * std::log10(0.7 / std::sqrt(2.0))).margin(1.5));
}

TEST_CASE("a math product and ratio read in their own unit")
{
    // The channels, and a sum or difference, in the chosen unit.
    CHECK(trace_unit(SpectrumUnit::dBm, 0, MathOperation::Multiply).name == "dBm");
    CHECK(trace_unit(SpectrumUnit::dBmV, kChannels, MathOperation::Subtract).name == "dBmV");
    CHECK(trace_unit(SpectrumUnit::dBmV, kChannels, MathOperation::Add).column == "dbmv");
    CHECK(trace_unit(SpectrumUnit::Vrms, 1, MathOperation::Divide).column == "vrms");
    CHECK(trace_unit(SpectrumUnit::Vrms, 1, MathOperation::Divide).unit == SpectrumUnit::Vrms);

    // A product relative to 1 V², whatever offset the chosen dB unit has for volts.
    for (SpectrumUnit unit : {SpectrumUnit::dBV, SpectrumUnit::dBmV, SpectrumUnit::dBu, SpectrumUnit::dBm}) {
        const TraceUnit product = trace_unit(unit, kChannels, MathOperation::Multiply);
        CHECK(product.name == "dB(V²)");
        CHECK(product.column == "db_v2");
        CHECK(level_in_unit(0.01, product.unit) == Approx(-20));
        const TraceUnit ratio = trace_unit(unit, kChannels, MathOperation::Divide);
        CHECK(ratio.name == "dB");
        CHECK(level_in_unit(4, ratio.unit) == Approx(power_db(4)));
    }
    const TraceUnit product = trace_unit(SpectrumUnit::Vrms, kChannels, MathOperation::Multiply);
    CHECK(product.name == "V²");
    CHECK(product.column == "v2rms");
    CHECK(level_in_unit(0.01, product.unit) == Approx(0.1));
    CHECK(trace_unit(SpectrumUnit::Vrms, kChannels, MathOperation::Divide).name == "U");
    CHECK(trace_unit(SpectrumUnit::Vrms, kChannels, MathOperation::Divide).column == "urms");
}
