// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/calibration.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

#include "isds_calibration.hpp"

namespace starosc {
namespace {

// A shorted input reads within a few codes of zero (docs/measurements.md §2: at most about 4).
constexpr double kMaxZeroCodes = 12;
constexpr double kMaxNoiseCodes = 4;
constexpr double kNoiseFloorV = 3e-3;

}  // namespace

bool UserCalibration::is_factory() const
{
    return std::ranges::all_of(stages, [](const StageCalibration& s) {
        return std::ranges::all_of(s.channel, [](const ChannelCorrection& c) { return c == ChannelCorrection{}; });
    });
}

int UserCalibration::adopt(const UserCalibration& other)
{
    int matched = 0;
    for (StageCalibration& stage : stages) {
        const auto it = std::ranges::find_if(other.stages, [&](const StageCalibration& o) { return o.same_stage(stage); });
        if (it != other.stages.end()) {
            stage.channel = it->channel;
            ++matched;
        }
    }
    return matched;
}

ZeroProblem check_zero(const UserCalibration& stages, const std::vector<StageZero>& zero)
{
    using Kind = ZeroProblem::Kind;
    if (zero.size() != stages.stages.size())
        return {Kind::Incomplete};
    for (std::size_t i = 0; i < zero.size(); ++i)
        for (int ch = 0; ch < kChannels; ++ch) {
            const VoltageRange span = stages.stages[i].factory_span[ch];
            const double lsb = (span.max_v - span.min_v) / 255;
            if (zero[i].clipped[ch])
                return {Kind::Clipped, ch, span};
            if (std::abs(zero[i].mean_v[ch]) > kMaxZeroCodes * lsb)
                return {Kind::Offset, ch, span, zero[i].mean_v[ch]};
            if (zero[i].noise_v[ch] > kMaxNoiseCodes * lsb + kNoiseFloorV)
                return {Kind::Noise, ch, span, zero[i].noise_v[ch]};
        }
    return {};
}

namespace detail {

UserCalibration factory_calibration(const isds::Calibration& cal, std::string model, std::string device_id)
{
    UserCalibration out;
    out.model = std::move(model);
    out.device_id = std::move(device_id);
    for (isds::Stage stage : cal.stages()) {
        StageCalibration s;
        s.gain_code = cal.gain_code(stage);
        for (int ch = 0; ch < kChannels; ++ch) {
            const isds::Channel channel = to_channel(ch);
            s.level_code[ch] = cal.level_code(stage, channel);
            const double lsb = cal.factory_lsb_mv(stage, channel);
            const double offset = cal.factory_offset_mv(stage, channel);
            s.factory_span[ch] = {(lsb * (0 - isds::Calibration::kAdcMid) + offset) / 1000,
                                  (lsb * (255 - isds::Calibration::kAdcMid) + offset) / 1000};
        }
        out.stages.push_back(s);
    }
    return out;
}

int apply_calibration(const isds::Calibration& cal, const std::string& device_id, const UserCalibration& user,
                      const std::function<void(isds::Stage, isds::Channel, isds::Correction)>& set)
{
    if (user.device_id != device_id)
        throw std::invalid_argument(
            std::format("calibration of device {} cannot be applied to device {}", user.device_id, device_id));
    UserCalibration own = factory_calibration(cal, user.model, device_id);
    const int matched = own.adopt(user);
    for (std::size_t i = 0; i < own.stages.size(); ++i)
        for (int ch = 0; ch < kChannels; ++ch) {
            const ChannelCorrection& c = own.stages[i].channel[ch];
            set(cal.stages()[i], to_channel(ch), {c.zero_v * 1000, c.gain});
        }
    return matched;
}

StageZero zero_of(const isds::Calibration& cal, isds::Stage stage,
                  const std::array<std::span<const std::uint8_t>, kChannels>& raw)
{
    StageZero z;
    for (int ch = 0; ch < kChannels; ++ch) {
        const isds::Channel channel = to_channel(ch);
        const double lsb = cal.factory_lsb_mv(stage, channel);
        const double offset = cal.factory_offset_mv(stage, channel);
        double sum = 0, sum2 = 0;
        for (std::uint8_t r : raw[ch]) {
            const double v = (lsb * (r - isds::Calibration::kAdcMid) + offset) / 1000;
            sum += v;
            sum2 += v * v;
            z.clipped[ch] = z.clipped[ch] || r == 0 || r == 255;
        }
        const auto n = static_cast<double>(std::max<std::size_t>(raw[ch].size(), 1));
        z.mean_v[ch] = sum / n;
        z.noise_v[ch] = std::sqrt(std::max(0.0, sum2 / n - z.mean_v[ch] * z.mean_v[ch]));
    }
    return z;
}

}  // namespace detail

}  // namespace starosc
