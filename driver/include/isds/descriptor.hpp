// SPDX-License-Identifier: GPL-3.0-or-later
// The capability/calibration descriptor the device sends during the handshake.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "isds/protocol.hpp"

namespace isds {

// One fine-gain level (command 04): its code per channel and, per channel, the zero offset
// in mV for each coarse gain code (command 06).
struct GainLevel {
    std::array<std::uint16_t, 2> code{};
    std::array<std::vector<std::int32_t>, 2> offset_mv;
};

struct Descriptor {
    std::uint8_t version = 0;
    std::string vendor;
    std::string model;
    std::uint64_t device_id = 0;
    std::uint32_t base_clock_hz = 0;
    std::vector<std::uint32_t> sample_rates;  // ascending as listed by the device
    std::vector<std::uint16_t> gain_codes;    // coarse gain codes G, highest gain first
    std::vector<GainLevel> levels;
    Bytes raw;

    // Throws isds::Error on malformed or unsupported input. Layout verified for version 0x0a.
    static Descriptor parse(std::span<const std::uint8_t> data);
};

// The models this driver drives: the ISDS205 series (205, 205B, 205X, …), whose model names start with "ISDS205". The
// vendor's library gives them all the same acquisition; other families need their own (docs/protocol.md §10).
bool supported_model(std::string_view model);

}  // namespace isds
