// SPDX-License-Identifier: GPL-3.0-or-later
// Spectrum self-test: DDS OUT probed on CH2 (x1), CH1 unused.
//
//   star-spectrum-bench [calibration.json] [--at HZ RATE | --pair [V/DIV]]
//
// Measures the generator's sine and square wave, and the input's noise floor, with the Spectrum tab's
// engine: fundamental, level, THD, SNR, SINAD, ENOB, SFDR and the first harmonics, for a few windows,
// lengths and rates. With a calibration file from the app the levels use it. Results for one unit:
// docs/measurements.md §11.
//
// --pair: one signal on both inputs from any source; the generator is left alone. The Spectrum tab's math
// trace (CH1 + CH2, CH1 − CH2, CH1 × CH2) line by line against the channels, and the gain and phase
// difference of the channels that the difference shows. docs/measurements.md §15.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <thread>

#include <QCoreApplication>

#include "calibrationstore.hpp"
#include "starosc/spectrum.hpp"

using namespace starosc;

namespace {

ScopeSettings input(double volts_per_div)
{
    ScopeSettings s;
    s.channel[0].enabled = false;
    s.channel[1] = {true, volts_per_div, 0.0, Coupling::DC, 1.0};
    return s;
}

double db(double ratio) { return 10 * std::log10(ratio); }

void report(const char* what, const SpectrumFrame& f)
{
    const SpectrumAnalysis& a = f.analysis[1];
    if (!a.valid) {
        std::printf("  %-34s no fundamental\n", what);
        return;
    }
    std::printf("  %-34s f %12.3f Hz  %7.2f dBV  THD %6.1f dB  SNR %5.1f  SINAD %5.1f  ENOB %4.2f  SFDR %5.1f%s\n", what,
                a.fundamental_hz, level_in_unit(a.fundamental_power, SpectrumUnit::dBV), 20 * std::log10(a.thd), a.snr_db,
                a.sinad_db, a.enob_bits, a.sfdr_db, f.clipped[1] ? "  CLIP" : "");
    std::printf("  %-34s", "");
    for (std::size_t i = 0; i < a.harmonics.size() && i < 6; ++i) {
        const Harmonic& h = a.harmonics[i];
        if (h.power > 0)
            std::printf("  H%d %6.1f", h.order, db(h.power / a.fundamental_power));
        else
            std::printf("  H%d    ---", h.order);
    }
    std::printf("  dBc   (%zu points, bin %.3g Hz, %zu records)\n", f.points, f.bin_hz, f.records);

    // The largest other peaks, at least 20 bins apart and away from DC and the fundamental.
    const std::vector<float>& p = f.power[1];
    const auto k1 = static_cast<long>(std::lround(a.fundamental_hz / f.bin_hz));
    std::vector<long> peaks;
    for (int n = 0; n < 4; ++n) {
        long best = -1;
        for (long k = 20; k < static_cast<long>(p.size()); ++k) {
            if (std::abs(k - k1) < 20)
                continue;
            bool near = false;
            for (long q : peaks)
                near |= std::abs(k - q) < 20;
            if (!near && (best < 0 || p[static_cast<std::size_t>(k)] > p[static_cast<std::size_t>(best)]))
                best = k;
        }
        if (best < 0)
            break;
        peaks.push_back(best);
    }
    std::printf("  %-34s  spurs:", "");
    const double peak1 = p[static_cast<std::size_t>(k1)];
    for (long k : peaks)
        std::printf("  %.4g Hz %.1f dBc", static_cast<double>(k) * f.bin_hz, db(p[static_cast<std::size_t>(k)] / peak1));
    std::printf("\n");
}

SpectrumFrame averaged(SpectrumEngine& engine, const ScopeSettings& scope, SpectrumSettings s, int spectra)
{
    s.averaging = spectra > 1 ? SpectrumAveraging::Average : SpectrumAveraging::Off;
    s.averages = spectra;
    engine.reset();
    engine.acquire(scope, s);  // the first capture after a stage change is dropped
    engine.reset();
    SpectrumFrame f;
    for (int i = 0; i < spectra; ++i)
        f = engine.acquire(scope, s);
    return f;
}

void settle(Source& src, const GeneratorSettings& g)
{
    src.set_generator(g);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
}

// The largest bin within a few bins of `hz`: a line's level whatever the scalloping, the same for every trace.
double line(const std::vector<float>& power, double hz, double bin_hz)
{
    const auto k = static_cast<long>(std::lround(hz / bin_hz));
    double best = 0;
    for (long j = std::max(1L, k - 3); j <= k + 3 && j < static_cast<long>(power.size()); ++j)
        best = std::max(best, static_cast<double>(power[static_cast<std::size_t>(j)]));
    return best;
}

double dbv(double power) { return level_in_unit(power, SpectrumUnit::dBV); }

int pair(SpectrumEngine& engine, double volts_per_div)
{
    ScopeSettings scope;
    for (ChannelSettings& c : scope.channel)
        c = {true, volts_per_div, 0.0, Coupling::DC, 1.0};
    SpectrumSettings s;
    s.shown = {true, true, true};
    s.window = Window::BlackmanHarris;
    s.points = 65536;
    const std::array operations{MathOperation::Add, MathOperation::Subtract, MathOperation::Multiply};
    std::array<SpectrumFrame, 3> f;
    for (std::size_t i = 0; i < operations.size(); ++i) {
        scope.math.operation = operations[i];
        f[i] = averaged(engine, scope, s, 8);
    }
    const SpectrumFrame& sum = f[0];
    const SpectrumFrame& difference = f[1];
    const SpectrumFrame& product = f[2];
    if (!sum.analysis[0].valid) {
        std::printf("no signal on CH1\n");
        return 1;
    }
    const double f0 = sum.analysis[0].fundamental_hz;
    const double bin = sum.bin_hz;
    std::printf("same signal on both inputs, %.3g V/div DC, 1 MSa/s, Blackman-Harris 64K, 8 averaged%s%s\n", volts_per_div,
                sum.clipped[0] ? ", CH1 CLIP" : "", sum.clipped[1] ? ", CH2 CLIP" : "");
    std::printf("DC: CH1 %.3f V, CH2 %.3f V (magnitude), CH1 x CH2 %.3f V^2\n", std::sqrt(sum.power[0][0]),
                std::sqrt(sum.power[1][0]), std::sqrt(product.power[2][0]));
    std::printf("lines of CH1's fundamental %.3f Hz; levels in dBV (the product in dB re 1 V^2)\n", f0);
    std::printf("  %10s %7s %7s %7s %7s %7s %7s  %7s %7s %8s %8s\n", "Hz", "CH1", "CH2", "sum", "in-phs", "diff", "prod",
                "cancel", "gain", "   phase", "skew");

    // Lines of the channels: where CH1 has a tone within 40 dB of its largest line, and the product's lines up to
    // twice that far.
    double strongest = 0;
    for (int h = 1; h * f0 < sum.sample_rate_hz / 2.0; ++h)
        strongest = std::max(strongest, line(sum.power[0], h * f0, bin));
    for (int h = 1; h * f0 < sum.sample_rate_hz / 2.0 * 0.98; ++h) {
        const double hz = h * f0;
        const double p1 = line(sum.power[0], hz, bin);
        const double p2 = line(sum.power[1], hz, bin);
        const double ps = line(sum.power[2], hz, bin);
        const double pd = line(difference.power[2], hz, bin);
        const double pp = line(product.power[2], hz, bin);
        const bool tone = p1 > strongest * 1e-4 && p2 > 0;
        if (!tone && pp < strongest * 1e-4)
            continue;
        std::printf("  %10.1f %7.1f %7.1f %7.1f", hz, dbv(p1), dbv(p2), dbv(ps));
        if (tone) {
            // In phase the sum is the sum of the amplitudes. The difference is what the channels do not share:
            // |a − b|² = (|a| − |b|)² + 2|a||b|(1 − cos φ).
            const double in_phase = std::pow(std::sqrt(p1) + std::sqrt(p2), 2);
            const double cos_phi = std::clamp((p1 + p2 - pd) / (2 * std::sqrt(p1 * p2)), -1.0, 1.0);
            const double phi = std::acos(cos_phi);
            std::printf(" %7.1f %7.1f %7.1f  %7.1f %7.2f %4.2f deg %6.1f ns\n", dbv(in_phase), dbv(pd), dbv(pp),
                        db(pd / p1), db(p2 / p1), phi * 180 / std::numbers::pi, phi / (2 * std::numbers::pi * hz) * 1e9);
        } else {
            std::printf(" %7s %7.1f %7.1f\n", "", dbv(pd), dbv(pp));
        }
    }
    const SpectrumAnalysis& d = difference.analysis[2];
    if (d.valid)
        std::printf("largest line of the difference: %.3f Hz %.1f dBV (CH1 %.1f, CH2 %.1f dBV there)\n", d.fundamental_hz,
                    dbv(line(difference.power[2], d.fundamental_hz, bin)), dbv(line(difference.power[0], d.fundamental_hz, bin)),
                    dbv(line(difference.power[1], d.fundamental_hz, bin)));
    return 0;
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
    const QStringList args = QCoreApplication::arguments().mid(1);
    const qsizetype at = args.indexOf("--at");
    const qsizetype paired = args.indexOf("--pair");
    const QString calibration = args.isEmpty() || args.front().startsWith("--") ? QString() : args.front();
    if (!calibration.isEmpty()) {
        QString error;
        const auto cal = calibration_store::load(calibration, &error);
        if (!cal) {
            std::fprintf(stderr, "%s: %s\n", qPrintable(calibration), qPrintable(error));
            return 1;
        }
        std::printf("calibration file: %d stages\n", src->set_calibration(*cal));
    } else {
        std::printf("factory calibration (levels about 0.6 dB low, docs/measurements.md §3)\n");
    }
    SpectrumEngine engine(*src);
    if (paired >= 0) {
        const double vdiv = paired + 1 < args.size() ? args.value(paired + 1).toDouble() : 0.5;
        return pair(engine, vdiv > 0 ? vdiv : 0.5);
    }
    SpectrumSettings s;
    s.shown = {false, true, false};
    s.harmonics = 7;
    s.window = Window::BlackmanHarris;

    GeneratorSettings g;
    g.output = true;
    g.frequency_hz = 1000;
    g.amplitude_vpp = 2;

    // --at HZ RATE: one 2 Vpp sine at one rate, the FFT as long as the rate allows.
    if (at >= 0) {
        if (at + 2 >= args.size()) {
            std::fprintf(stderr, "usage: star-spectrum-bench [calibration.json] [--at HZ RATE | --pair [V/DIV]]\n");
            return 1;
        }
        g.frequency_hz = args.value(at + 1).toDouble();
        s.sample_rate_hz = args.value(at + 2).toUInt();
        s.points = 65536;
        settle(*src, g);
        char label[96];
        std::snprintf(label, sizeof label, "%.6g Hz at %.3g Sa/s", g.frequency_hz, static_cast<double>(s.sample_rate_hz));
        report(label, averaged(engine, input(0.5), s, 8));
        src->park_generator();
        return 0;
    }
    settle(*src, g);
    std::printf("1 kHz sine, 2 Vpp (0.707 Vrms, -3.01 dBV), 0.5 V/div, 1 MSa/s, 8 spectra averaged\n");
    char label[96];
    for (Window w : {Window::Hann, Window::BlackmanHarris, Window::FlatTop, Window::Kaiser, Window::DolphChebyshev}) {
        s.window = w;
        s.points = 65536;
        std::snprintf(label, sizeof label, "%s, 64K", window_name(w).data());
        report(label, averaged(engine, input(0.5), s, 8));
    }
    s.window = Window::BlackmanHarris;
    s.points = 1 << 20;
    report("Blackman-Harris, 1M, one spectrum", averaged(engine, input(0.5), s, 1));

    std::printf("1 kHz sine, 6 Vpp, 1 V/div (75 %% of the screen), Blackman-Harris 64K, 8 averaged\n");
    g.amplitude_vpp = 6;
    settle(*src, g);
    s.points = 65536;
    report("6 Vpp", averaged(engine, input(1.0), s, 8));

    std::printf("1 kHz square, 2 Vpp: odd harmonics at -9.54, -13.98, -16.90 dBc\n");
    g.waveform = Waveform::Square;
    g.amplitude_vpp = 2;
    settle(*src, g);
    report("square, Blackman-Harris 64K", averaged(engine, input(0.5), s, 8));
    g.waveform = Waveform::Sine;

    std::printf("higher frequencies, 2 Vpp sine, Blackman-Harris\n");
    for (double f : {100e3, 1e6}) {
        g.frequency_hz = f;
        settle(*src, g);
        s.sample_rate_hz = 16'000'000;
        s.points = 65536;
        std::snprintf(label, sizeof label, "%.0f kHz at 16 MSa/s, 64K", f / 1e3);
        report(label, averaged(engine, input(0.5), s, 8));
    }
    for (double f : {2e6, 5e6, 10e6, 20e6}) {
        g.frequency_hz = f;
        settle(*src, g);
        s.sample_rate_hz = 48'000'000;
        s.points = 256;
        std::snprintf(label, sizeof label, "%.0f MHz at 48 MSa/s, 256", f / 1e6);
        report(label, averaged(engine, input(0.5), s, 8));
    }

    std::printf("noise floor, generator parked, 1 MSa/s, Blackman-Harris 64K, 8 averaged\n");
    src->park_generator();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    s.sample_rate_hz = 1'000'000;
    s.points = 65536;
    for (double vdiv : {0.05, 0.5, 1.0}) {
        const SpectrumFrame f = averaged(engine, input(vdiv), s, 8);
        const SpectrumAnalysis& a = f.analysis[1];
        double sum = 0;
        for (std::size_t k = 10; k < f.power[1].size(); ++k)
            sum += f.power[1][k];
        const double total = sum / f.noise_bandwidth;  // V^2 over the span above DC
        const double per_bin = sum / static_cast<double>(f.power[1].size() - 10);
        std::printf("  %5.0f mV/div: noise %.3f mVrms over 500 kHz, %.1f dBV a bin (RBW %.1f Hz), largest tone %.1f dBV "
                    "at %.0f Hz\n",
                    vdiv * 1000, std::sqrt(total) * 1000, db(per_bin), f.noise_bandwidth * f.bin_hz,
                    a.valid ? level_in_unit(a.fundamental_power, SpectrumUnit::dBV) : -300.0, a.fundamental_hz);
    }
    return 0;
}
