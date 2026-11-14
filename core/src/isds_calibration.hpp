// SPDX-License-Identifier: GPL-3.0-or-later
// Bridges isds::Calibration and starosc::UserCalibration for the device source and the simulator.
#pragma once

#include <array>
#include <functional>
#include <span>
#include <string>

#include "isds/calibration.hpp"
#include "starosc/calibration.hpp"

namespace starosc::detail {

UserCalibration factory_calibration(const isds::Calibration& cal, std::string model, std::string device_id);

// Calls `set` for every stage of `cal` that `user` has; returns how many matched. Throws
// std::invalid_argument when `user` belongs to another device.
int apply_calibration(const isds::Calibration& cal, const std::string& device_id, const UserCalibration& user,
                      const std::function<void(isds::Stage, isds::Channel, isds::Correction)>& set);

// Mean, noise and clipping of one capture per channel, in factory volts.
StageZero zero_of(const isds::Calibration& cal, isds::Stage stage,
                  const std::array<std::span<const std::uint8_t>, kChannels>& raw);

inline isds::Channel to_channel(int ch) { return ch == 0 ? isds::Channel::Ch1 : isds::Channel::Ch2; }

}  // namespace starosc::detail
