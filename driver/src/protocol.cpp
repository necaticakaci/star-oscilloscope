// SPDX-License-Identifier: GPL-3.0-or-later
#include "isds/protocol.hpp"

#include <algorithm>

namespace isds {
namespace {

void append_be32(Bytes& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

Bytes header(Kind kind, Target target, std::uint8_t command)
{
    return {static_cast<std::uint8_t>(kind), static_cast<std::uint8_t>(target), command};
}

}  // namespace

Bytes value_frame(Kind kind, Target target, std::uint8_t command, std::uint32_t value)
{
    Bytes frame = header(kind, target, command);
    append_be32(frame, value);
    frame.push_back(0x00);
    return frame;
}

Bytes payload_frame(Kind kind, Target target, std::uint8_t command, std::span<const std::uint8_t> payload)
{
    Bytes frame = header(kind, target, command);
    append_be32(frame, static_cast<std::uint32_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

Bytes osc_value(Target target, OscCommand command, std::uint32_t value)
{
    return value_frame(Kind::Osc, target, static_cast<std::uint8_t>(command), value);
}

Bytes osc_u16(Target target, OscCommand command, std::uint16_t value)
{
    const std::array<std::uint8_t, 2> payload{static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)};
    return payload_frame(Kind::Osc, target, static_cast<std::uint8_t>(command), payload);
}

Bytes osc_u32(Target target, OscCommand command, std::uint32_t value)
{
    Bytes payload;
    append_be32(payload, value);
    return payload_frame(Kind::Osc, target, static_cast<std::uint8_t>(command), payload);
}

Bytes dds_value(Target target, DdsCommand command, std::uint32_t value)
{
    return value_frame(Kind::Dds, target, static_cast<std::uint8_t>(command), value);
}

Bytes trigger_frame(std::uint8_t level_code, std::uint8_t hysteresis_code)
{
    Bytes frame = header(Kind::Osc, Target::Device, static_cast<std::uint8_t>(OscCommand::Trigger));
    append_be32(frame, 0);
    frame.insert(frame.end(), {0x00, level_code, 0x00, hysteresis_code});
    return frame;
}

bool is_ack_for(std::span<const std::uint8_t> reply, std::span<const std::uint8_t> frame)
{
    return reply.size() >= 3 && frame.size() >= 3 && std::equal(frame.begin(), frame.begin() + 3, reply.begin());
}

bool is_heartbeat(std::span<const std::uint8_t> message)
{
    return std::ranges::equal(message, kHeartbeat);
}

std::string to_hex(std::span<const std::uint8_t> data)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (std::uint8_t b : data) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0f]);
    }
    return out;
}

}  // namespace isds
