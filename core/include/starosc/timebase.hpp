// SPDX-License-Identifier: GPL-3.0-or-later
// Maps the timebase setting to a sample rate and capture length.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "starosc/settings.hpp"

namespace starosc {

// How the samples of a capture follow each other at one rate. The ISDS205X delivers 48 MSa/s captures as a first
// run of 1020 contiguous samples, then 256-sample blocks with gaps between them; at its other rates a capture is
// contiguous.
struct CaptureLayout {
    std::size_t first_block = 0;  // samples in the first run; 0: the whole capture is contiguous
    std::size_t block = 0;        // samples in each following block

    bool contiguous() const { return first_block == 0; }
    // [begin, end) of the contiguous run holding `index`, within a capture of `size` samples.
    std::size_t run_begin(std::size_t index) const;
    std::size_t run_end(std::size_t index, std::size_t size) const;
    // Shortest run of a long capture.
    std::size_t shortest_run(std::size_t size) const;
};

// What a source can capture at one rate.
struct RateCapacity {
    std::uint32_t rate_hz = 0;
    std::size_t max_samples = 0;
    CaptureLayout layout;
    std::size_t reliable_samples = 0;  // longest capture that rarely loses samples; 0: max_samples
};

struct AcquisitionPlan {
    std::uint32_t sample_rate_hz = 0;
    std::size_t window_samples = 0;   // samples spanning the screen width
    std::size_t capture_samples = 0;  // samples to capture (window + room to search for a trigger)
    std::size_t pre_samples = 0;      // window samples before the trigger point
    CaptureLayout layout;             // the screen window must stay inside one run
};

// Upper bound on the samples shown per screen for each memory depth setting.
std::size_t max_window_samples(MemoryDepth depth);

// Picks the fastest rate whose window fits the memory depth setting and where a trigger can be searched for:
// a contiguous capture must hold two windows; a capture in blocks needs blocks a little longer than the window,
// since the window has to sit inside one block, or else a first run of two windows, and then captures only that
// run. Otherwise captures at least twice the window, and 16K samples, when memory allows. A rate is used only for
// captures up to its reliable_samples: longer windows go to a slower rate. `rates` must be sorted ascending.
AcquisitionPlan plan_acquisition(const TimebaseSettings& timebase, double pre_trigger, std::span<const RateCapacity> rates);

}  // namespace starosc
