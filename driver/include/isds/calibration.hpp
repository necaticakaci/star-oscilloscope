// SPDX-License-Identifier: GPL-3.0-or-later
// Raw ADC code <-> voltage conversion and gain stage selection (see docs/protocol.md §8).
//
//   lsb_mV = 1000 * G / (255 * D)
//   V_mV   = lsb_mV * (raw - 127.5) + offset_mV
//
// G is the coarse gain code (command 06), D the per-channel fine gain code (command 04) and
// offset_mV the per-channel zero offset; all three come from the device descriptor.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "isds/descriptor.hpp"

namespace isds {

enum class Channel { Ch1 = 1, Ch2 = 2 };

inline constexpr std::size_t index(Channel ch) { return static_cast<std::size_t>(ch) - 1; }
inline constexpr Target target(Channel ch) { return static_cast<Target>(static_cast<std::uint8_t>(ch)); }

struct Stage {
    int gain = 0;   // index into Descriptor::gain_codes
    int level = 0;  // index into Descriptor::levels
    bool operator==(const Stage&) const = default;
};

struct Span {
    double min_mv = 0;
    double max_mv = 0;
};

struct TriggerCodes {
    std::uint8_t level = 0;
    std::uint8_t hysteresis = 0;
};

// User correction on top of the factory table (docs/measurements.md §3):
//   V_mV = (V_factory_mV - zero_mv) / gain
struct Correction {
    double zero_mv = 0;
    double gain = 1;
    bool operator==(const Correction&) const = default;
};

class Calibration {
public:
    static constexpr double kAdcMid = 127.5;
    static constexpr double kFullScaleMv = 1000.0;
    static constexpr double kAdcSteps = 255.0;

    explicit Calibration(Descriptor desc);

    const Descriptor& descriptor() const { return desc_; }

    // Every conversion below includes the correction of its stage and channel; the default
    // correction leaves the factory conversion unchanged.
    void set_correction(Stage stage, Channel ch, Correction correction);
    Correction correction(Stage stage, Channel ch) const;
    void clear_corrections();

    std::uint16_t gain_code(Stage stage) const;
    std::uint16_t level_code(Stage stage, Channel ch) const;
    double factory_lsb_mv(Stage stage, Channel ch) const;
    double factory_offset_mv(Stage stage, Channel ch) const;
    double lsb_mv(Stage stage, Channel ch) const;
    double offset_mv(Stage stage, Channel ch) const;
    double zero(Stage stage, Channel ch) const;  // fractional raw code for 0 V
    Span span(Stage stage, Channel ch) const;     // voltage at raw 0 and raw 255

    double to_mv(std::uint8_t raw, Stage stage, Channel ch) const;
    void to_mv(std::span<const std::uint8_t> raw, std::span<double> out, Stage stage, Channel ch) const;

    // Usable stages, finest resolution first. vdso never combines a lower coarse gain with the
    // top fine level, so that unverified combination is left out.
    const std::vector<Stage>& stages() const { return stages_; }

    // Finest stage that covers [min_mv, max_mv] with margin_codes to spare at both ends,
    // otherwise the widest stage.
    Stage select(Channel ch, double min_mv, double max_mv, int margin_codes = 2) const;

    // Codes for command 13. sense_div is the hysteresis in divisions of (max - min) / 10.
    TriggerCodes trigger_codes(Stage stage, Channel ch, double level_mv, double min_mv, double max_mv,
                               double sense_div = 0.4) const;

    // The stage vdso programs right after the handshake (lowest coarse gain, top fine level).
    Stage startup_stage() const;

private:
    void check(Stage stage) const;
    std::size_t slot(Stage stage) const;

    Descriptor desc_;
    std::vector<Stage> stages_;
    int top_level_ = 0;
    std::vector<std::array<Correction, 2>> corrections_;  // per gain x level
};

}  // namespace isds
