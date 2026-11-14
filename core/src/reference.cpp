// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/reference.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <limits>

namespace starosc {
namespace {

constexpr std::string_view kMagic = "# star-oscilloscope reference";

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

std::optional<double> number(std::string_view s)
{
    s = trim(s);
    if (s == "nan" || s == "NaN")
        return std::numeric_limits<double>::quiet_NaN();
    double v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size())
        return std::nullopt;
    return v;
}

void fail(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}

}  // namespace

double screen_start_s(const ScopeSettings& settings)
{
    return -settings.trigger.pre_trigger * kDivisionsX * settings.timebase.seconds_per_div;
}

std::optional<Reference> make_reference(const Frame& frame, int source)
{
    Reference ref;
    const std::vector<float>* samples = nullptr;
    if (source == kMathSource) {
        if (!frame.settings.math.enabled || frame.math.empty())
            return std::nullopt;
        samples = &frame.math;
        ref.source = "Math";
        ref.unit = std::string(math_unit(frame.settings.math.operation));
        ref.units_per_div = frame.settings.math.units_per_div;
        ref.position_div = frame.settings.math.position_div;
    } else {
        if (source < 0 || source >= kChannels || frame.volts[source].empty())
            return std::nullopt;
        samples = &frame.volts[source];
        ref.source = std::format("CH{}", source + 1);
        ref.units_per_div = frame.settings.channel[source].volts_per_div;
        ref.position_div = frame.settings.channel[source].position_div;
    }
    ref.samples = *samples;
    ref.sample_interval_s = frame.sample_interval_s;
    ref.start_s = (-frame.start_fraction - static_cast<double>(frame.plan.pre_samples)) * frame.sample_interval_s;
    return ref;
}

std::string reference_to_csv(const Reference& ref)
{
    std::string out = std::string(kMagic) + "\n";
    out += std::format("# source,{}\n# unit,{}\n# units_per_div,{}\n# position_div,{}\n", ref.source, ref.unit,
                       ref.units_per_div, ref.position_div);
    out += "time_s,value\n";
    for (std::size_t i = 0; i < ref.samples.size(); ++i) {
        const double t = ref.start_s + static_cast<double>(i) * ref.sample_interval_s;
        if (std::isfinite(ref.samples[i]))
            out += std::format("{:.10g},{:.7g}\n", t, ref.samples[i]);
        else
            out += std::format("{:.10g},nan\n", t);
    }
    return out;
}

std::optional<Reference> reference_from_csv(std::string_view text, std::string* error)
{
    Reference ref;
    std::vector<double> times;
    bool magic = false, header = false;
    std::size_t line_no = 0;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        ++line_no;
        if (line.empty())
            continue;
        if (line.starts_with('#')) {
            if (line == kMagic) {
                magic = true;
                continue;
            }
            const std::string_view body = trim(line.substr(1));
            const std::size_t comma = body.find(',');
            if (comma == std::string_view::npos)
                continue;
            const std::string_view key = trim(body.substr(0, comma));
            const std::string_view value = trim(body.substr(comma + 1));
            if (key == "source")
                ref.source = value;
            else if (key == "unit")
                ref.unit = value;
            else if (key == "units_per_div" || key == "position_div") {
                const auto v = number(value);
                if (!v || !std::isfinite(*v) || (key == "units_per_div" && *v <= 0)) {
                    fail(error, std::format("line {}: bad {}", line_no, key));
                    return std::nullopt;
                }
                (key == "units_per_div" ? ref.units_per_div : ref.position_div) = *v;
            }
            continue;
        }
        if (!header) {
            header = true;
            if (line.starts_with("time"))
                continue;
        }
        const std::size_t comma = line.find(',');
        const auto t = comma == std::string_view::npos ? std::nullopt : number(line.substr(0, comma));
        const auto v = comma == std::string_view::npos ? std::nullopt : number(line.substr(comma + 1));
        if (!t || !v || !std::isfinite(*t)) {
            fail(error, std::format("line {}: expected time,value", line_no));
            return std::nullopt;
        }
        times.push_back(*t);
        ref.samples.push_back(static_cast<float>(*v));
    }
    if (!magic) {
        fail(error, "not a Star Oscilloscope reference");
        return std::nullopt;
    }
    if (ref.samples.size() < 2) {
        fail(error, "fewer than two samples");
        return std::nullopt;
    }
    ref.start_s = times.front();
    ref.sample_interval_s = (times.back() - times.front()) / static_cast<double>(times.size() - 1);
    if (!(ref.sample_interval_s > 0)) {
        fail(error, "times do not increase");
        return std::nullopt;
    }
    return ref;
}

}  // namespace starosc
