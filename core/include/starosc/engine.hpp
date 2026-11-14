// SPDX-License-Identifier: GPL-3.0-or-later
// One acquisition cycle: plan, capture, trigger, cut the screen window, measure.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "starosc/interpolate.hpp"
#include "starosc/math.hpp"
#include "starosc/measure.hpp"
#include "starosc/settings.hpp"
#include "starosc/source.hpp"
#include "starosc/timebase.hpp"

namespace starosc {

// Min/max envelope of the whole capture, to show where the screen window sits in memory.
struct Overview {
    static constexpr std::size_t kBins = 400;
    std::array<std::vector<float>, kChannels> min, max;  // per bin, at the probe tip
    double window_begin = 0;                             // screen window as fractions of the capture
    double window_end = 1;
};

struct Frame {
    std::uint64_t sequence = 0;
    ScopeSettings settings;           // settings the frame was acquired with
    AcquisitionPlan plan;
    double sample_interval_s = 0;
    // Samples of the screen window, at the probe tip. Sample i is displayed at
    // (i - start_fraction) / plan.window_samples of the screen width.
    std::array<std::vector<float>, kChannels> volts;
    // Up to kSincTaps samples just before and after `volts`, from the same contiguous run, for sin(x)/x drawing.
    std::array<std::vector<float>, kChannels> before, after;
    double start_fraction = 0;
    bool triggered = false;
    std::array<bool, kChannels> clipped{};
    std::array<bool, kChannels> sensitive{};  // ChannelData::sensitive, for both inputs
    // A short capture at the top rate found the signal faster than half the frame's sample rate, so the
    // frame shows an alias. Checked every few frames; set only below the top rate.
    std::array<bool, kChannels> aliased{};
    std::array<double, kChannels> lsb_v{};  // at the probe tip
    std::array<Measurements, kChannels> measurements{};
    PairMeasurements pair;  // CH1 to CH2
    // The math trace when enabled, over the same window and margins as volts; NaN where a ratio is undefined.
    std::vector<float> math, math_before, math_after;
    Measurements math_measurements;
    double math_noise_floor = 0;  // for + and −, in volts
    Overview overview;
};

// Fills in the measurements of a frame from its samples, with settings.thresholds.
void measure_frame(Frame& frame);

class Engine {
public:
    explicit Engine(Source& source) : source_(source) {}

    // Returns nothing when Normal or Single mode found no trigger. With `force`, returns an
    // untriggered frame in any mode.
    std::optional<Frame> acquire(const ScopeSettings& settings, bool force = false);

private:
    void add_math(Frame& frame, const Acquisition& acq, std::size_t first);
    void check_aliasing(const AcquisitionRequest& request, const RateCapacity& top, Frame& frame);

    Source& source_;
    std::uint64_t sequence_ = 0;
    std::array<bool, kChannels> aliased_{};  // result of the last check
    std::uint32_t alias_rate_ = 0;           // rate that check was for
    unsigned frames_since_alias_check_ = 0;
};

}  // namespace starosc
