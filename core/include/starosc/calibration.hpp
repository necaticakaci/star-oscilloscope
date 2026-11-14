// SPDX-License-Identifier: GPL-3.0-or-later
// Per-device user calibration on top of the factory table (docs/measurements.md §3, §9):
//   V = (V_factory - zero) / gain, per gain stage and channel.
// A device's values are never applied to another device.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "starosc/settings.hpp"

namespace starosc {

struct ChannelCorrection {
    double zero_v = 0;
    double gain = 1;
    bool operator==(const ChannelCorrection&) const = default;
};

struct StageCalibration {
    // Identify the stage independently of its position in the list.
    std::uint16_t gain_code = 0;
    std::array<std::uint16_t, kChannels> level_code{};
    std::array<VoltageRange, kChannels> factory_span{};  // informational
    std::array<ChannelCorrection, kChannels> channel{};

    bool same_stage(const StageCalibration& other) const
    {
        return gain_code == other.gain_code && level_code == other.level_code;
    }
};

struct UserCalibration {
    std::string model;
    std::string device_id;
    std::string zero_time;  // ISO 8601; empty when the zero has not been calibrated
    std::string gain_time;  // ISO 8601; empty when the gain has not been calibrated
    std::vector<StageCalibration> stages;  // finest stage first

    bool is_factory() const;
    // Takes the corrections of `other` for the stages both share; returns how many matched.
    int adopt(const UserCalibration& other);
};

// Result of a zero measurement with the inputs shorted, in factory volts.
struct StageZero {
    std::array<double, kChannels> mean_v{};
    std::array<double, kChannels> noise_v{};  // standard deviation
    std::array<bool, kChannels> clipped{};
};

// What is wrong with a zero measurement, for the interface to put in words.
struct ZeroProblem {
    enum class Kind {
        None,
        Incomplete,  // not every stage measured
        Clipped,     // a channel clips on a stage
        Offset,      // a channel reads a large offset: `value_v`
        Noise,       // a channel shows noise or a signal: `value_v`, its standard deviation
    };
    Kind kind = Kind::None;
    int channel = 0;
    VoltageRange span{};  // of the stage, in factory volts
    double value_v = 0;

    bool ok() const { return kind == Kind::None; }
};

// Checks a zero measurement before it is accepted: an input that is not shorted shows up as a
// large offset or as noise well above the ADC resolution.
ZeroProblem check_zero(const UserCalibration& stages, const std::vector<StageZero>& zero);

}  // namespace starosc
