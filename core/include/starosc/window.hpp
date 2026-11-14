// SPDX-License-Identifier: GPL-3.0-or-later
// Window functions for spectrum analysis, the set of the vendor's software.
#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace starosc {

enum class Window {
    Rectangular,
    Triangular,
    Welch,
    Hann,
    Hamming,
    BartlettHann,
    Cosine,
    Tukey,  // alpha 0.5
    Blackman,
    BlackmanHarris,  // 4 terms
    BlackmanNuttall,
    Nuttall,
    FlatTop,
    Kaiser,          // beta 9
    DolphChebyshev,  // 100 dB side lobes
    Gaussian,        // sigma 0.4
    Parzen,
    Bohman,
};
inline constexpr int kWindows = 18;

std::string_view window_name(Window window);

// Half width of the main lobe, from the peak to the first null, in bins, rounded up. A tone's power is
// summed over this many bins and one more on each side of its peak.
int window_lobe_bins(Window window);

// Periodic (DFT-even) window of `n` samples, largest value 1.
std::vector<double> make_window(Window window, std::size_t n);

// Equivalent noise bandwidth in bins of a transform as long as the window: n * sum(w^2) / sum(w)^2.
double noise_bandwidth_bins(std::span<const double> window);

}  // namespace starosc
