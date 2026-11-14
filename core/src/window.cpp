// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/window.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <initializer_list>
#include <numbers>

#include "starosc/fft.hpp"

namespace starosc {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTukeyAlpha = 0.5;
constexpr double kKaiserBeta = 9;
constexpr double kChebyshevAttenuationDb = 100;
constexpr double kGaussianSigma = 0.4;

// a0 - a1 cos(2 pi t) + a2 cos(4 pi t) - ...
double cosine_sum(double t, std::initializer_list<double> a)
{
    double w = 0, sign = 1;
    int k = 0;
    for (double c : a) {
        w += sign * c * std::cos(2 * kPi * k * t);
        sign = -sign;
        ++k;
    }
    return w;
}

double bessel_i0(double x)
{
    double sum = 1, term = 1;
    for (int k = 1; k < 200; ++k) {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
        if (term < 1e-17 * sum)
            break;
    }
    return sum;
}

// Symmetric Dolph-Chebyshev window of even length `n`, from the Chebyshev polynomial on the unit circle
// (the even-length method of SciPy's chebwin).
std::vector<double> chebyshev(std::size_t n)
{
    const double order = static_cast<double>(n) - 1;
    const double beta = std::cosh(std::acosh(std::pow(10.0, kChebyshevAttenuationDb / 20)) / order);
    std::vector<std::complex<double>> p(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double x = beta * std::cos(kPi * static_cast<double>(k) / static_cast<double>(n));
        double t;
        if (x > 1)
            t = std::cosh(order * std::acosh(x));
        else if (x < -1)
            t = -std::cosh(order * std::acosh(-x));  // odd order
        else
            t = std::cos(order * std::acos(x));
        p[k] = std::polar(t, kPi * static_cast<double>(k) / static_cast<double>(n));
    }
    fft(p);
    const std::size_t half = n / 2 + 1;
    std::vector<double> w;
    w.reserve(n);
    for (std::size_t k = half - 1; k >= 1; --k)
        w.push_back(p[k].real());
    for (std::size_t k = 1; k < half; ++k)
        w.push_back(p[k].real());
    return w;
}

// The Chebyshev window of a length that is no power of two: computed at the next power of two and resampled.
std::vector<double> chebyshev_any(std::size_t n)
{
    if (is_power_of_two(n))
        return chebyshev(n);
    const std::size_t m = std::bit_ceil(n);
    const std::vector<double> big = chebyshev(m);
    std::vector<double> w(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double pos = static_cast<double>(i) * static_cast<double>(m - 1) / static_cast<double>(n - 1);
        const auto j = std::min(static_cast<std::size_t>(pos), m - 2);
        const double f = pos - static_cast<double>(j);
        w[i] = big[j] * (1 - f) + big[j + 1] * f;
    }
    return w;
}

}  // namespace

std::string_view window_name(Window window)
{
    switch (window) {
    case Window::Rectangular: return "Rectangular";
    case Window::Triangular: return "Triangular (Bartlett)";
    case Window::Welch: return "Welch";
    case Window::Hann: return "Hann";
    case Window::Hamming: return "Hamming";
    case Window::BartlettHann: return "Bartlett-Hann";
    case Window::Cosine: return "Cosine";
    case Window::Tukey: return "Tukey (0.5)";
    case Window::Blackman: return "Blackman";
    case Window::BlackmanHarris: return "Blackman-Harris";
    case Window::BlackmanNuttall: return "Blackman-Nuttall";
    case Window::Nuttall: return "Nuttall";
    case Window::FlatTop: return "Flat top";
    case Window::Kaiser: return "Kaiser (beta 9)";
    case Window::DolphChebyshev: return "Dolph-Chebyshev (100 dB)";
    case Window::Gaussian: return "Gaussian (0.4)";
    case Window::Parzen: return "Parzen";
    case Window::Bohman: return "Bohman";
    }
    return "?";
}

int window_lobe_bins(Window window)
{
    switch (window) {
    case Window::Rectangular: return 1;
    case Window::Triangular:
    case Window::Welch:
    case Window::Hann:
    case Window::Hamming:
    case Window::BartlettHann:
    case Window::Cosine:
    case Window::Tukey: return 2;
    case Window::Blackman:
    case Window::Gaussian:
    case Window::Bohman: return 3;
    case Window::BlackmanHarris:
    case Window::BlackmanNuttall:
    case Window::Nuttall:
    case Window::Kaiser:
    case Window::DolphChebyshev:
    case Window::Parzen: return 4;
    case Window::FlatTop: return 5;
    }
    return 4;
}

std::vector<double> make_window(Window window, std::size_t n)
{
    if (n == 0)
        return {};
    if (window == Window::DolphChebyshev && n >= 4) {
        std::vector<double> w = chebyshev_any(n);
        const double peak = *std::max_element(w.begin(), w.end());
        for (double& x : w)
            x /= peak;
        return w;
    }
    std::vector<double> w(n);
    const auto N = static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / N;  // 0 .. 1
        const double x = 2 * t - 1;                   // -1 .. 1
        const double ax = std::abs(x);
        double v = 1;
        switch (window) {
        case Window::Rectangular:
        case Window::DolphChebyshev: v = 1; break;  // too short for the Chebyshev polynomial
        case Window::Triangular: v = 1 - ax; break;
        case Window::Welch: v = 1 - x * x; break;
        case Window::Hann: v = cosine_sum(t, {0.5, 0.5}); break;
        case Window::Hamming: v = cosine_sum(t, {0.54, 0.46}); break;
        case Window::BartlettHann: v = 0.62 - 0.48 * std::abs(t - 0.5) - 0.38 * std::cos(2 * kPi * t); break;
        case Window::Cosine: v = std::sin(kPi * t); break;
        case Window::Tukey:
            if (t < kTukeyAlpha / 2)
                v = 0.5 * (1 - std::cos(2 * kPi * t / kTukeyAlpha));
            else if (t > 1 - kTukeyAlpha / 2)
                v = 0.5 * (1 - std::cos(2 * kPi * (1 - t) / kTukeyAlpha));
            break;
        case Window::Blackman: v = cosine_sum(t, {0.42, 0.5, 0.08}); break;
        case Window::BlackmanHarris: v = cosine_sum(t, {0.35875, 0.48829, 0.14128, 0.01168}); break;
        case Window::BlackmanNuttall: v = cosine_sum(t, {0.3635819, 0.4891775, 0.1365995, 0.0106411}); break;
        case Window::Nuttall: v = cosine_sum(t, {0.355768, 0.487396, 0.144232, 0.012604}); break;
        case Window::FlatTop:
            v = cosine_sum(t, {0.21557895, 0.41663158, 0.277263158, 0.083578947, 0.006947368});
            break;
        case Window::Kaiser: v = bessel_i0(kKaiserBeta * std::sqrt(std::max(0.0, 1 - x * x))) / bessel_i0(kKaiserBeta); break;
        case Window::Gaussian: v = std::exp(-0.5 * (x / kGaussianSigma) * (x / kGaussianSigma)); break;
        case Window::Parzen: v = ax <= 0.5 ? 1 - 6 * ax * ax + 6 * ax * ax * ax : 2 * (1 - ax) * (1 - ax) * (1 - ax); break;
        case Window::Bohman: v = (1 - ax) * std::cos(kPi * ax) + std::sin(kPi * ax) / kPi; break;
        }
        w[i] = v;
    }
    const double peak = *std::max_element(w.begin(), w.end());
    if (peak > 0)
        for (double& v : w)
            v /= peak;
    return w;
}

double noise_bandwidth_bins(std::span<const double> window)
{
    double sum = 0, squares = 0;
    for (double w : window) {
        sum += w;
        squares += w * w;
    }
    return sum > 0 ? static_cast<double>(window.size()) * squares / (sum * sum) : 1;
}

}  // namespace starosc
