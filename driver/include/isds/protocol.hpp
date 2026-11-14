// SPDX-License-Identifier: GPL-3.0-or-later
// Wire format of the Instrustar ISDS205X USB protocol (see docs/protocol.md).
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace isds {

using Bytes = std::vector<std::uint8_t>;

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr std::uint16_t kVendorId = 0xD4A2;
inline constexpr std::uint16_t kProductId = 0x5661;
inline constexpr int kInterface = 0;
inline constexpr std::uint8_t kEpOut = 0x01;     // commands
inline constexpr std::uint8_t kEpCtrlIn = 0x81;  // acks, handshake, heartbeat
inline constexpr std::uint8_t kEpDataIn = 0x82;  // samples

// The first 1024 samples of every capture are left over from the previous one,
// followed by ~4 samples of transition.
inline constexpr std::size_t kSettleSamples = 1028;
inline constexpr std::size_t kCaptureGranularity = 1024;

// At the highest sample rate (48 MSa/s) the device takes samples faster than USB 2.0 carries them. A capture
// starts with the leftover samples, then the 1024 its FIFO took at the full rate, contiguous; the rest comes in
// 256-sample blocks with gaps of varying length, up to several microseconds, between them. After the settle
// samples 1020 contiguous ones remain, as many as the vendor software shows at that rate. This holds once the
// device has been primed by a slower capture (Device::open does it); unprimed, only the first 252 samples
// were contiguous, and the 1020 samples vdso returns span four blocks (bench checks 2026-11-02 and 2026-11-08).
inline constexpr std::size_t kTopRateBlockSamples = 256;
inline constexpr std::size_t kTopRateFifoSamples = 1024;
inline constexpr std::size_t kContiguousSamplesAtTopRate = kCaptureGranularity + kTopRateFifoSamples - kSettleSamples;

// At 16 MSa/s the host has to take 32 MB/s through that FIFO, which holds about 64 µs. Now and then it falls behind
// and samples are lost, some 0.15 to 0.5 times per million samples on Windows and over usbip (docs/measurements.md §10).
// Captures up to kReliableSamplesAtLossyRate meet a loss only rarely.
inline constexpr std::uint32_t kLossyRateHz = 16'000'000;
inline constexpr std::size_t kReliableSamplesAtLossyRate = 64 * 1024;

inline constexpr std::array<std::uint8_t, 10> kSync{0xff, 0xff, 0xff, 0xff, 0xff, 0x11, 0x22, 0x33, 0x44, 0x55};
inline constexpr std::array<std::uint8_t, 4> kHeartbeat{0x00, 0xff, 0xfe, 0x10};
inline constexpr std::array<std::uint8_t, 8> kHeartbeatReply{0x00, 0x00, 0xfe, 0x00, 0x00, 0x00, 0x02, 0x00};
inline constexpr std::uint8_t kConfirm = 0xaa;
inline constexpr std::size_t kNonceSize = 5;

enum class Kind : std::uint8_t { System = 0x00, Dds = 0x01, Osc = 0x02 };
enum class Target : std::uint8_t { Ch1 = 0x01, Ch2 = 0x02, Device = 0xff };

enum class OscCommand : std::uint8_t {
    ChannelOn = 0x01,
    Capture = 0x02,     // value: samples per channel; not acknowledged, data follows on kEpDataIn
    Level = 0x04,       // u16 payload: fine gain code D
    Gain = 0x06,        // u16 payload: coarse gain code G
    SampleRate = 0x08,  // u32 payload: Hz
    ChannelOff = 0x10,
    Start = 0x12,
    Trigger = 0x13,     // see trigger_frame()
    Coupling = 0x19,    // value: 0 = DC, 1 = AC
};

// DDS commands (docs/protocol.md §9); value encodings are in isds/generator.hpp.
enum class DdsCommand : std::uint8_t {
    OutputOn = 0x01,   // target Device
    OutputOff = 0x10,  // target Device
    Start = 0x12,      // target Device; resets the generator
    Waveform = 0x40,
    Frequency = 0x42,  // Hz
    Sweep = 0x43,      // 16-byte payload, see sweep_frame()
    Duty = 0x44,       // 100 - duty
    SweepWaveform = 0x45,
    SweepDuty = 0x46,
    Amplitude = 0x47,
    Offset = 0x48,
};

// 8-byte value form: [kind][target][cmd][u32 BE value][00].
Bytes value_frame(Kind kind, Target target, std::uint8_t command, std::uint32_t value = 0);

// Payload form: [kind][target][cmd][u32 BE length][payload].
Bytes payload_frame(Kind kind, Target target, std::uint8_t command, std::span<const std::uint8_t> payload);

Bytes osc_value(Target target, OscCommand command, std::uint32_t value = 0);
Bytes osc_u16(Target target, OscCommand command, std::uint16_t value);
Bytes osc_u32(Target target, OscCommand command, std::uint32_t value);
Bytes dds_value(Target target, DdsCommand command, std::uint32_t value = 0);

// Command 13 does not follow either form: zero length field, then [00][level][00][hysteresis].
Bytes trigger_frame(std::uint8_t level_code, std::uint8_t hysteresis_code);

// The device acknowledges a command with [kind][target][cmd][status].
bool is_ack_for(std::span<const std::uint8_t> reply, std::span<const std::uint8_t> frame);

bool is_heartbeat(std::span<const std::uint8_t> message);

std::string to_hex(std::span<const std::uint8_t> data);

}  // namespace isds
