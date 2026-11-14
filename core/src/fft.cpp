// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/fft.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace starosc {

void fft(std::span<std::complex<double>> data)
{
    const std::size_t n = data.size();
    if (!is_power_of_two(n))
        throw std::invalid_argument("FFT length must be a power of two");
    if (n < 2)
        return;

    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }

    // Twiddles of the last stage; earlier stages use every (n / len)-th of them.
    std::vector<double> wr(n / 2), wi(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) {
        const double angle = -2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n);
        wr[k] = std::cos(angle);
        wi[k] = std::sin(angle);
    }
    // Butterflies on real and imaginary parts: std::complex multiplication checks for infinities and is slow.
    auto* d = reinterpret_cast<double*>(data.data());
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t half = len / 2;
        const std::size_t stride = n / len;
        for (std::size_t i = 0; i < n; i += len)
            for (std::size_t k = 0; k < half; ++k) {
                double* a = d + 2 * (i + k);
                double* b = d + 2 * (i + k + half);
                const double cr = wr[k * stride], ci = wi[k * stride];
                const double tr = cr * b[0] - ci * b[1];
                const double ti = cr * b[1] + ci * b[0];
                b[0] = a[0] - tr;
                b[1] = a[1] - ti;
                a[0] += tr;
                a[1] += ti;
            }
    }
}

std::vector<std::complex<double>> real_fft(std::span<const double> samples)
{
    const std::size_t n = samples.size();
    if (n < 2 || !is_power_of_two(n))
        throw std::invalid_argument("FFT length must be a power of two, at least 2");
    // Even samples as real parts and odd ones as imaginary parts: one transform of half the length.
    const std::size_t m = n / 2;
    std::vector<std::complex<double>> z(m);
    for (std::size_t k = 0; k < m; ++k)
        z[k] = {samples[2 * k], samples[2 * k + 1]};
    fft(z);

    std::vector<std::complex<double>> out(m + 1);
    for (std::size_t k = 0; k <= m; ++k) {
        const std::complex<double> a = z[k % m];
        const std::complex<double> b = std::conj(z[(m - k) % m]);
        const std::complex<double> even = (a + b) * 0.5;
        const std::complex<double> odd = (a - b) * std::complex<double>(0, -0.5);
        const double angle = -2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n);
        out[k] = even + std::complex<double>(std::cos(angle), std::sin(angle)) * odd;
    }
    return out;
}

}  // namespace starosc
