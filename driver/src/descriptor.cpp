// SPDX-License-Identifier: GPL-3.0-or-later
#include "isds/descriptor.hpp"

#include <algorithm>
#include <format>

namespace isds {
namespace {

constexpr std::size_t kMinSize = 0xa0;
constexpr std::uint8_t kSupportedVersion = 0x0a;

constexpr std::size_t kVendorAt = 0x01;
constexpr std::size_t kModelAt = 0x11;
constexpr std::size_t kNameSize = 16;
constexpr std::size_t kBaseClockAt = 0x28;
constexpr std::size_t kRateCountAt = 0x32;
constexpr std::size_t kRateDividersAt = 0x34;  // [01][divider] pairs
constexpr std::size_t kGainCountAt = 0x47;
constexpr std::size_t kGainCodesAt = 0x48;
constexpr std::size_t kDeviceIdSize = 8;       // last bytes, little-endian

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

    std::uint8_t u8(std::size_t at) const
    {
        check(at, 1);
        return data_[at];
    }

    std::uint16_t be16(std::size_t at) const
    {
        check(at, 2);
        return static_cast<std::uint16_t>(data_[at] << 8 | data_[at + 1]);
    }

    std::uint32_t be32(std::size_t at) const
    {
        check(at, 4);
        return std::uint32_t{data_[at]} << 24 | std::uint32_t{data_[at + 1]} << 16 |
               std::uint32_t{data_[at + 2]} << 8 | std::uint32_t{data_[at + 3]};
    }

    std::int32_t be_i32(std::size_t at) const { return static_cast<std::int32_t>(be32(at)); }

    std::string cstr(std::size_t at, std::size_t size) const
    {
        check(at, size);
        auto begin = data_.begin() + static_cast<std::ptrdiff_t>(at);
        auto end = std::find(begin, begin + static_cast<std::ptrdiff_t>(size), std::uint8_t{0});
        return {begin, end};
    }

private:
    void check(std::size_t at, std::size_t size) const
    {
        if (at + size > data_.size())
            throw Error(std::format("descriptor truncated: need {} bytes at {:#x}, have {}", size, at, data_.size()));
    }

    std::span<const std::uint8_t> data_;
};

}  // namespace

Descriptor Descriptor::parse(std::span<const std::uint8_t> data)
{
    if (data.size() < kMinSize)
        throw Error(std::format("descriptor too short ({} bytes)", data.size()));
    if (data[0] != kSupportedVersion)
        throw Error(std::format("unsupported descriptor version {:#04x}", data[0]));

    const Reader in(data);
    Descriptor desc;
    desc.version = data[0];
    desc.vendor = in.cstr(kVendorAt, kNameSize);
    desc.model = in.cstr(kModelAt, kNameSize);
    for (std::size_t i = 0; i < kDeviceIdSize; ++i)
        desc.device_id |= std::uint64_t{data[data.size() - kDeviceIdSize + i]} << (8 * i);

    desc.base_clock_hz = in.be32(kBaseClockAt);
    const std::size_t rate_count = in.u8(kRateCountAt);
    for (std::size_t i = 0; i < rate_count; ++i) {
        const std::uint8_t divider = in.u8(kRateDividersAt + 2 * i);
        if (divider == 0)
            throw Error("descriptor has a zero sample rate divider");
        desc.sample_rates.push_back(desc.base_clock_hz / divider);
    }

    const std::size_t gain_count = in.u8(kGainCountAt);
    if (gain_count == 0)
        throw Error("descriptor lists no gain codes");
    for (std::size_t i = 0; i < gain_count; ++i)
        desc.gain_codes.push_back(in.be16(kGainCodesAt + 2 * i));

    std::size_t pos = kGainCodesAt + 2 * gain_count + 2;  // two unknown bytes follow the gain codes
    const std::size_t level_count = in.u8(pos++);
    if (level_count == 0)
        throw Error("descriptor lists no gain levels");
    for (std::size_t l = 0; l < level_count; ++l) {
        GainLevel level;
        for (std::size_t ch = 0; ch < 2; ++ch) {
            level.code[ch] = in.be16(pos);
            pos += 2;
            if (level.code[ch] == 0)
                throw Error("descriptor has a zero gain level code");
            for (std::size_t g = 0; g < gain_count; ++g, pos += 4)
                level.offset_mv[ch].push_back(in.be_i32(pos));
        }
        desc.levels.push_back(std::move(level));
    }

    desc.raw.assign(data.begin(), data.end());
    return desc;
}

bool supported_model(std::string_view model) { return model.starts_with("ISDS205"); }

}  // namespace isds
