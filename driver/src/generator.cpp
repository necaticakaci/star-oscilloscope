// SPDX-License-Identifier: GPL-3.0-or-later
#include "isds/generator.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace isds {
namespace {

struct Point {
    std::uint32_t code;
    double value;
};

// docs/measurements.md §5, 1 kHz sine. Amplitude in Vpp at an offset near 0 V; the last point is clipped.
constexpr Point kAmplitudePoints[] = {{0x540, 0.079}, {0x600, 0.152}, {0x700, 0.392}, {0x800, 1.06},
                                      {0x900, 2.55},  {0x9c0, 5.61},  {0xa20, 9.7}};
// Offset in volts at the lowest amplitude. The ends are the output limits.
constexpr Point kOffsetPoints[] = {{0x000, 4.86},  {0x1ff, 4.07},  {0x3ff, 2.65},  {0x5ff, 1.26}, {0x7ff, -0.18},
                                   {0x9ff, -1.48}, {0xbff, -2.85}, {0xdff, -4.25}, {0xfff, -4.79}};

double interpolate(double x, double x0, double x1, double y0, double y1) { return y0 + (y1 - y0) * (x - x0) / (x1 - x0); }

// Linear between the points, or linear in log10(value); held at the ends.
double value_at(std::span<const Point> points, std::uint32_t code, bool log)
{
    const auto y = [log](const Point& p) { return log ? std::log10(p.value) : p.value; };
    if (code <= points.front().code)
        return points.front().value;
    for (std::size_t i = 1; i < points.size(); ++i)
        if (code <= points[i].code) {
            const double r = interpolate(code, points[i - 1].code, points[i].code, y(points[i - 1]), y(points[i]));
            return log ? std::pow(10.0, r) : r;
        }
    return points.back().value;
}

// Inverse of value_at; the values must run in one direction.
std::uint32_t code_at(std::span<const Point> points, double value, bool log)
{
    const auto y = [log](const Point& p) { return log ? std::log10(p.value) : p.value; };
    const double v = log ? std::log10(std::max(value, 1e-9)) : value;
    const bool rising = y(points.back()) > y(points.front());
    const auto not_past = [rising](double a, double b) { return rising ? a <= b : a >= b; };
    if (not_past(v, y(points.front())))
        return points.front().code;
    for (std::size_t i = 1; i < points.size(); ++i)
        if (not_past(v, y(points[i])))
            return static_cast<std::uint32_t>(
                std::lround(interpolate(v, y(points[i - 1]), y(points[i]), points[i - 1].code, points[i].code)));
    return points.back().code;
}

}  // namespace

std::uint32_t waveform_code(Waveform w)
{
    return 1u << static_cast<int>(w);
}

std::uint32_t amplitude_code(int zoom)
{
    zoom = std::clamp(zoom, kZoomMin, kZoomMax);
    const double gain_db = 20.0 * std::log10(zoom / 100.0);
    const double control_v = (gain_db - 10.0) / 40.0 + 1.65;
    return static_cast<std::uint32_t>(control_v * 4095.0 / 3.3);
}

std::uint32_t offset_code(int bias)
{
    return static_cast<std::uint32_t>(kBiasMax - std::clamp(bias, kBiasMin, kBiasMax));
}

std::uint32_t duty_code(int percent)
{
    return static_cast<std::uint32_t>(100 - std::clamp(percent, 1, 99));
}

std::uint32_t amplitude_code_for_vpp(double vpp) { return code_at(kAmplitudePoints, vpp, true); }

double vpp_for_amplitude_code(std::uint32_t code) { return value_at(kAmplitudePoints, code, true); }

std::uint32_t offset_code_for_volts(double volts) { return code_at(kOffsetPoints, volts, false); }

double volts_for_offset_code(std::uint32_t code) { return value_at(kOffsetPoints, code, false); }

Bytes sweep_frame(std::uint32_t start_hz, std::uint32_t stop_hz, std::uint32_t step_hz, std::uint32_t time)
{
    Bytes payload;
    for (std::uint32_t v : {start_hz, stop_hz, step_hz, time})
        for (int shift : {24, 16, 8, 0})
            payload.push_back(static_cast<std::uint8_t>(v >> shift));
    return payload_frame(Kind::Dds, Target::Ch1, static_cast<std::uint8_t>(DdsCommand::Sweep), payload);
}

bool has_generator(std::string_view model)
{
    return model.starts_with("ISDS205B") || model.starts_with("ISDS205X");
}

bool has_level_control(std::string_view model)
{
    return model.starts_with("ISDS205X");
}

}  // namespace isds
