// SPDX-License-Identifier: GPL-3.0-or-later
// Fast Fourier transform for power-of-two lengths.
#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace starosc {

inline constexpr bool is_power_of_two(std::size_t n) { return n != 0 && (n & (n - 1)) == 0; }

// Forward DFT in place: X[k] = sum x[n] exp(-2 pi i k n / N). The length must be a power of two.
void fft(std::span<std::complex<double>> data);

// DFT of real samples: bins 0 .. N/2 of the full transform. The length must be a power of two, at least 2.
std::vector<std::complex<double>> real_fft(std::span<const double> samples);

}  // namespace starosc
