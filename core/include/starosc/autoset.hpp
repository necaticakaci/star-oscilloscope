// SPDX-License-Identifier: GPL-3.0-or-later
// Autoset: looks at both inputs and sets V/div, position, timebase and trigger so that the signals
// fit the screen, like the Auto key of bench oscilloscopes.
#pragma once

#include <array>

#include "starosc/settings.hpp"
#include "starosc/source.hpp"

namespace starosc {

struct AutosetResult {
    ScopeSettings settings;
    std::array<bool, kChannels> found{};  // a swing or DC level above 20 mV at the BNC
    double frequency_hz = 0;              // of the trigger source; 0 when it has no periodic signal
};

// Captures both inputs a few times (about a second in all) and returns `current` adjusted:
// - Channels with a signal are switched on, the others off. One signal is centred on 6 divisions;
//   two share the screen, CH1 above and CH2 below, on 3.5 divisions each.
// - The trigger goes to the middle of a periodic signal, the current source first. Mode becomes Auto.
// - The timebase shows 2.5 to 6 periods; without a periodic signal it is kept.
// Coupling, probe factors, memory depth and trigger sensitivity are kept. When no input carries a
// signal, the settings come back unchanged. Throws what Source::acquire throws.
AutosetResult autoset(Source& source, const ScopeSettings& current);

}  // namespace starosc
