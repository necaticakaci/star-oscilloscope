// SPDX-License-Identifier: GPL-3.0-or-later
// Frames are compared with bytes the vendor library sent (trace/run1/usb.log).
#include <catch2/catch_test_macros.hpp>

#include "isds/protocol.hpp"

using namespace isds;

namespace {
Bytes hex(const std::string& s)
{
    Bytes out;
    for (std::size_t i = 0; i + 1 < s.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
    return out;
}
}  // namespace

TEST_CASE("oscilloscope frames match vendor traffic")
{
    CHECK(osc_value(Target::Device, OscCommand::Start) == hex("02ff120000000000"));
    CHECK(osc_value(Target::Ch1, OscCommand::ChannelOn) == hex("0201010000000000"));
    CHECK(osc_u32(Target::Device, OscCommand::SampleRate, 48'000'000) == hex("02ff080000000402dc6c00"));
    CHECK(osc_u32(Target::Device, OscCommand::SampleRate, 1'000'000) == hex("02ff0800000004000f4240"));
    CHECK(osc_u16(Target::Ch1, OscCommand::Gain, 0x052e) == hex("020106000000020" "52e"));
    CHECK(osc_u16(Target::Ch2, OscCommand::Level, 0x01fe) == hex("02020400000002" "01fe"));
    CHECK(osc_value(Target::Ch1, OscCommand::Coupling, 1) == hex("0201190000000100"));
    CHECK(osc_value(Target::Device, OscCommand::Capture, 0x4000) == hex("02ff020000400000"));
    CHECK(osc_value(Target::Ch2, OscCommand::ChannelOff) == hex("0202100000000000"));
    CHECK(trigger_frame(0x73, 0x08) == hex("02ff130000000000730008"));
}

TEST_CASE("DDS frames match vendor traffic")
{
    CHECK(dds_value(Target::Ch1, DdsCommand::Frequency, 1000) == hex("010142000003e800"));
    CHECK(dds_value(Target::Ch1, DdsCommand::Frequency, 1'000'000) == hex("010142000f424000"));
    CHECK(dds_value(Target::Device, DdsCommand::OutputOn) == hex("01ff010000000000"));
}

TEST_CASE("acknowledgements and heartbeats")
{
    const Bytes frame = osc_u16(Target::Ch1, OscCommand::Gain, 0x0109);
    CHECK(is_ack_for(hex("02010601"), frame));
    CHECK_FALSE(is_ack_for(hex("02020601"), frame));
    CHECK_FALSE(is_ack_for(hex("0201"), frame));
    CHECK(is_ack_for(hex("02ff1310"), trigger_frame(0, 0)));

    CHECK(is_heartbeat(hex("00fffe10")));
    CHECK_FALSE(is_heartbeat(hex("00fffe1000")));
    CHECK(Bytes(kHeartbeatReply.begin(), kHeartbeatReply.end()) == hex("0000fe0000000200"));
    CHECK(to_hex(hex("00ff7a")) == "00ff7a");
}
