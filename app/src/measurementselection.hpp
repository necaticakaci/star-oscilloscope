// SPDX-License-Identifier: GPL-3.0-or-later
// Which measurements the table shows.
#pragma once

#include <array>
#include <string_view>
#include <vector>

#include <QString>

#include "starosc/measure.hpp"
#include "starosc/settings.hpp"

// A value in fixed-width form: digits stay in place when the prefix changes. `voltage_unit` replaces V for the math
// trace, whose values may be V² or a ratio. Pads with no-break spaces.
QString formatMeasurement(starosc::Parameter p, double value, std::string_view voltage_unit = "V");

// "Frequency", "Peak-to-peak", in the language of the interface. The settings keep the core's English names.
QString parameterName(starosc::Parameter p);

// "Freq", "Vpp": the name used on the display.
QString shortParameterName(starosc::Parameter p);


struct MeasurementSelection {
    // Traces: the channels, then the math trace.
    static constexpr int kTraces = starosc::kChannels + 1;

    std::array<std::vector<starosc::Parameter>, kTraces> trace;
    std::vector<starosc::Parameter> pair;  // between CH1 and CH2

    bool operator==(const MeasurementSelection&) const = default;

    static MeasurementSelection defaults()
    {
        MeasurementSelection s;
        for (auto& list : s.trace)
            for (starosc::Parameter p : starosc::kParameters)
                for (starosc::Parameter d : starosc::kDefaultParameters)
                    if (p == d)
                        list.push_back(p);
        return s;
    }
};
