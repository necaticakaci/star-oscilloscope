// SPDX-License-Identifier: GPL-3.0-or-later
// Software edge trigger (the ISDS205X has no hardware trigger).
#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "starosc/settings.hpp"

namespace starosc {

// Finds the first edge crossing `level` whose position lies in [first, last).
// The trigger arms once the signal has been at least `hysteresis` on the far side of the level,
// so noise around the level does not retrigger. Returns the crossing as a fractional sample index.
std::optional<double> find_trigger(std::span<const float> samples, double level, double hysteresis,
                                   TriggerEdge edge, std::size_t first, std::size_t last);

// Likewise, the crossing in [first, last) nearest `target`; the earlier one of two equally near.
std::optional<double> find_trigger_near(std::span<const float> samples, double level, double hysteresis,
                                        TriggerEdge edge, std::size_t first, std::size_t last, double target);

}  // namespace starosc
