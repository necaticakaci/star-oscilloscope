// SPDX-License-Identifier: GPL-3.0-or-later
// Finds the gain of every stage with the device's own generator on both inputs (docs/measurements.md §9).
//
// One stage takes its gain from two levels read with a multimeter. A level is read with a sine on top, which steps over
// the ADC's steps: a plain DC level can read half a step off, which is half a percent of a typical level. A multimeter
// on DC reads the mean of it undisturbed. From that stage the gain goes on from stage to stage: the amplitude of a sine
// read on two neighbouring stages gives the ratio of their gains, free of their zeros and of quantization.
//
// The generator's output changes with the inputs' coarse gain (docs/measurements.md §5). So every reading is taken against
// the other channel, which stays on one stage and sees the same signal at the same moment.
#pragma once

#include <array>
#include <functional>
#include <vector>

#include "starosc/calibration.hpp"
#include "starosc/source.hpp"

namespace starosc {

struct SineReading {
    double vpp = 0;
    double mean = 0;
    double residual_rms = 0;  // what the sine leaves over: noise and distortion
    bool clipped = false;
};

// Least squares around `nominal_hz`; the true frequency is taken from the samples, as the generator's clock differs
// from the sampling clock.
SineReading read_sine(const ChannelData& data, double nominal_hz, double rate_hz);

// The stage the multimeter's levels are read on, by its index in UserCalibration::stages: the widest but one, as the
// widest is the least linear (docs/measurements.md §9).
int reference_stage(const std::vector<StageCalibration>& stages);
// A stage to check the result on with the multimeter: the widest of the other coarse gain.
int check_stage(const std::vector<StageCalibration>& stages);

// Two levels for a multimeter on a stage, each with a sine on it that uses a good part of the ADC's codes.
struct LevelPlan {
    int stage = 0;
    std::array<double, 2> volts{};
    double sine_vpp = 0;
};
LevelPlan level_plan(const std::vector<StageCalibration>& stages, int stage);

struct GainTransfer {
    // Of each stage relative to the reference stage, finest stage first as in UserCalibration::stages.
    std::array<std::vector<double>, kChannels> relative;
    // Of CH1 against CH2 on the reference stage.
    double channel_ratio = 1;
    // Largest change of a reading between the repeats of one measurement, as a fraction.
    double worst_spread = 0;
};

using TransferProgress = std::function<void(int done, int total)>;

// Each of these takes a while and leaves the generator on. The source must read in factory volts, and both inputs must
// be on the generator's output. They throw std::exception when the signal is not what it should be, for example with a
// probe on ×10.
void check_generator_on_inputs(Source& source);
GainTransfer transfer_gains(Source& source, const TransferProgress& progress = {});
// Sets the generator to about `volts` with the sine on it, both channels on `stage`, and returns the mean each reads.
// The output stays there for the multimeter.
std::array<double, kChannels> measure_level(Source& source, int stage, double volts, double sine_vpp);

// The gain of a stage from two levels: `read` what the channel read, `actual` what the multimeter said.
double gain_from_levels(std::array<double, 2> read, std::array<double, 2> actual);

// Sets the gains that `reference` on the reference stage gives; zeros and everything else stay.
void apply_gains(UserCalibration& calibration, const GainTransfer& transfer, const std::array<double, kChannels>& reference);

// A request that selects these stages, by their index in UserCalibration::stages.
AcquisitionRequest request_at_stages(const std::vector<StageCalibration>& stages, const std::array<int, kChannels>& stage,
                                     std::uint32_t rate_hz, std::size_t samples);

}  // namespace starosc
