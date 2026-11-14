// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/scale.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace starosc {
namespace {

std::vector<double> steps_125(double first, double last)
{
    std::vector<double> out;
    for (double decade = first; decade <= last * 1.0001; decade *= 10)
        for (double m : {1.0, 2.0, 5.0})
            if (decade * m >= first * 0.9999 && decade * m <= last * 1.0001)
                out.push_back(decade * m);
    return out;
}

}  // namespace

std::vector<double> volts_per_div_steps(double probe)
{
    auto steps = steps_125(1e-4, 1e5);
    steps.erase(std::remove_if(steps.begin(), steps.end(),
                               [probe](double v) { return v < 5e-3 * 0.9 * probe || v > 2.0 * 1.1 * probe; }),
                steps.end());
    return steps;
}

std::vector<double> seconds_per_div_steps() { return steps_125(100e-9, 100e-3); }

std::vector<double> units_per_div_steps() { return steps_125(1e-3, 100); }

double step_value(std::span<const double> steps, double current, int delta)
{
    if (steps.empty())
        return current;
    std::size_t closest = 0;
    for (std::size_t i = 1; i < steps.size(); ++i)
        if (std::abs(std::log(steps[i] / current)) < std::abs(std::log(steps[closest] / current)))
            closest = i;
    const auto target = std::clamp<long>(static_cast<long>(closest) + delta, 0, static_cast<long>(steps.size()) - 1);
    return steps[static_cast<std::size_t>(target)];
}

std::string format_si(double value, std::string_view unit, int significant_digits, bool trim_zeros)
{
    if (!std::isfinite(value))
        return "-----";

    struct Prefix {
        double scale;
        const char* name;
    };
    static constexpr Prefix prefixes[] = {{1e9, "G"},  {1e6, "M"},  {1e3, "k"},  {1, ""},
                                          {1e-3, "m"}, {1e-6, "µ"}, {1e-9, "n"}, {1e-12, "p"}};

    const double magnitude = std::abs(value);
    Prefix chosen{1, ""};
    if (magnitude > 0) {
        chosen = prefixes[std::size(prefixes) - 1];
        for (const Prefix& p : prefixes)
            if (magnitude >= p.scale * (1 - 0.5e-4)) {
                chosen = p;
                break;
            }
    }

    const double scaled = value / chosen.scale;
    const int integer_digits =
        scaled == 0 ? 1 : std::max(1, static_cast<int>(std::floor(std::log10(std::abs(scaled)))) + 1);
    const int decimals = std::max(0, significant_digits - integer_digits);
    std::string number = std::format("{:.{}f}", scaled, decimals);
    if (trim_zeros && number.find('.') != std::string::npos) {
        number.erase(number.find_last_not_of('0') + 1);
        if (number.back() == '.')
            number.pop_back();
    }
    if (number == "-0")
        number = "0";
    return std::format("{} {}{}", number, chosen.name, unit);
}

}  // namespace starosc
