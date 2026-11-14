// SPDX-License-Identifier: GPL-3.0-or-later
// User-facing oscilloscope settings, independent of the hardware.
#pragma once

#include <array>
#include <utility>

#include "starosc/measure.hpp"

namespace starosc {

inline constexpr int kDivisionsX = 10;
inline constexpr int kDivisionsY = 8;
inline constexpr int kChannels = 2;
inline constexpr double kMinProbe = 0.1;
inline constexpr double kMaxProbe = 10'000;

enum class Coupling { DC, AC };
enum class TriggerEdge { Rising, Falling, Either };
enum class TriggerMode { Auto, Normal, Single };
enum class MemoryDepth { Small, Medium, Large };

struct ChannelSettings {
    bool enabled = true;
    double volts_per_div = 1.0;  // at the probe tip
    double position_div = 0.0;   // trace position on screen, + is up
    Coupling coupling = Coupling::DC;
    double probe = 1.0;          // attenuation factor
    double custom_probe = 0.0;   // the last factor typed by hand, kept on offer beside ×1, ×10 and ×100; 0: none

    bool operator==(const ChannelSettings&) const = default;
};

struct TimebaseSettings {
    double seconds_per_div = 500e-6;
    MemoryDepth memory = MemoryDepth::Small;

    bool operator==(const TimebaseSettings&) const = default;
};

// The part of the record the screen shows: a faster timebase around a point of the record. Only the display
// changes; what is captured and measured follows the main timebase.
struct ZoomSettings {
    double seconds_per_div = 0;  // 0: off
    double center = 0.5;         // fraction of the record

    bool on() const { return seconds_per_div > 0; }
    bool operator==(const ZoomSettings&) const = default;
};

struct TriggerSettings {
    int source = 0;                // channel index
    TriggerEdge edge = TriggerEdge::Rising;
    TriggerMode mode = TriggerMode::Auto;
    double level_v = 0.0;          // at the probe tip
    double pre_trigger = 0.5;      // fraction of the window before the trigger point
    double hysteresis_div = 0.4;   // trigger sensitivity, as in the vendor software

    bool operator==(const TriggerSettings&) const = default;
};

enum class MathOperation { Add, Subtract, Multiply, Divide };
inline constexpr int kMathOperations = 4;

// A math trace computed sample by sample from two channels, at the probe tips.
struct MathSettings {
    bool enabled = false;
    MathOperation operation = MathOperation::Subtract;
    int a = 0;                    // channel index
    int b = 1;
    double units_per_div = 1.0;   // V for + and −, V² for ×, a plain ratio for ÷
    double position_div = 0.0;

    bool operator==(const MathSettings&) const = default;
};

// Y-T: voltage against time. X-Y: CH2 against CH1, both on their own scale and position.
enum class DisplayMode { YT, XY };

struct ScopeSettings {
    std::array<ChannelSettings, kChannels> channel{};
    TimebaseSettings timebase{};
    TriggerSettings trigger{};
    MathSettings math{};
    DisplayMode display = DisplayMode::YT;
    // The channel positions of the display mode not shown, back when it is: Y-T usually keeps the traces apart,
    // X-Y wants both in the middle. 0 until that mode was first shown.
    std::array<double, kChannels> other_position_div{};
    ZoomSettings zoom{};
    MeasureThresholds thresholds{};

    bool operator==(const ScopeSettings&) const = default;
};

// The settings shown in `mode`: the channel positions of the mode left are kept for its return, and those kept for
// `mode` come back.
inline ScopeSettings with_display(ScopeSettings s, DisplayMode mode)
{
    if (s.display == mode)
        return s;
    for (int ch = 0; ch < kChannels; ++ch)
        std::swap(s.channel[ch].position_div, s.other_position_div[ch]);
    s.display = mode;
    return s;
}

enum class CursorMode { Off, Time, Voltage };

struct CursorSettings {
    CursorMode mode = CursorMode::Off;
    int source = 0;                          // channel the voltage cursors measure
    std::array<double, 2> x{0.3, 0.7};       // fraction of the screen width
    std::array<double, 2> y_div{2.0, -2.0};  // divisions from the screen centre, + is up
};

// Input voltage range shown on screen for a channel (at the BNC, i.e. divided by the probe factor).
struct VoltageRange {
    double min_v = 0;
    double max_v = 0;
};

inline VoltageRange screen_range_at_input(const ChannelSettings& ch)
{
    const double half = kDivisionsY / 2.0;
    return {(-half - ch.position_div) * ch.volts_per_div / ch.probe,
            (half - ch.position_div) * ch.volts_per_div / ch.probe};
}

}  // namespace starosc
