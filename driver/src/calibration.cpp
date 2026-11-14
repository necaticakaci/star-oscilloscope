// SPDX-License-Identifier: GPL-3.0-or-later
#include "isds/calibration.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace isds {

Calibration::Calibration(Descriptor desc) : desc_(std::move(desc))
{
    if (desc_.gain_codes.empty() || desc_.levels.empty())
        throw Error("descriptor has no calibration data");

    const auto top = std::max_element(desc_.levels.begin(), desc_.levels.end(),
                                      [](const GainLevel& a, const GainLevel& b) { return a.code[0] < b.code[0]; });
    top_level_ = static_cast<int>(top - desc_.levels.begin());
    corrections_.resize(desc_.gain_codes.size() * desc_.levels.size());

    for (int g = 0; g < static_cast<int>(desc_.gain_codes.size()); ++g)
        for (int l = 0; l < static_cast<int>(desc_.levels.size()); ++l)
            if (!(g > 0 && l == top_level_))
                stages_.push_back({g, l});
    std::stable_sort(stages_.begin(), stages_.end(),
                     [this](Stage a, Stage b) { return lsb_mv(a, Channel::Ch1) < lsb_mv(b, Channel::Ch1); });
}

void Calibration::check(Stage stage) const
{
    if (stage.gain < 0 || stage.gain >= static_cast<int>(desc_.gain_codes.size()) || stage.level < 0 ||
        stage.level >= static_cast<int>(desc_.levels.size()))
        throw Error(std::format("invalid stage (gain {}, level {})", stage.gain, stage.level));
}

std::size_t Calibration::slot(Stage stage) const
{
    check(stage);
    return static_cast<std::size_t>(stage.gain) * desc_.levels.size() + static_cast<std::size_t>(stage.level);
}

void Calibration::set_correction(Stage stage, Channel ch, Correction correction)
{
    if (!std::isfinite(correction.zero_mv) || !std::isfinite(correction.gain) || correction.gain <= 0)
        throw Error(std::format("invalid correction (zero {} mV, gain {})", correction.zero_mv, correction.gain));
    corrections_[slot(stage)][index(ch)] = correction;
}

Correction Calibration::correction(Stage stage, Channel ch) const { return corrections_[slot(stage)][index(ch)]; }

void Calibration::clear_corrections() { std::ranges::fill(corrections_, std::array<Correction, 2>{}); }

std::uint16_t Calibration::gain_code(Stage stage) const
{
    check(stage);
    return desc_.gain_codes[static_cast<std::size_t>(stage.gain)];
}

std::uint16_t Calibration::level_code(Stage stage, Channel ch) const
{
    check(stage);
    return desc_.levels[static_cast<std::size_t>(stage.level)].code[index(ch)];
}

double Calibration::factory_lsb_mv(Stage stage, Channel ch) const
{
    return kFullScaleMv * gain_code(stage) / (kAdcSteps * level_code(stage, ch));
}

double Calibration::factory_offset_mv(Stage stage, Channel ch) const
{
    check(stage);
    return desc_.levels[static_cast<std::size_t>(stage.level)].offset_mv[index(ch)][static_cast<std::size_t>(stage.gain)];
}

double Calibration::lsb_mv(Stage stage, Channel ch) const
{
    return factory_lsb_mv(stage, ch) / correction(stage, ch).gain;
}

double Calibration::offset_mv(Stage stage, Channel ch) const
{
    const Correction c = correction(stage, ch);
    return (factory_offset_mv(stage, ch) - c.zero_mv) / c.gain;
}

double Calibration::zero(Stage stage, Channel ch) const
{
    return kAdcMid - offset_mv(stage, ch) / lsb_mv(stage, ch);
}

Span Calibration::span(Stage stage, Channel ch) const
{
    return {to_mv(0, stage, ch), to_mv(255, stage, ch)};
}

double Calibration::to_mv(std::uint8_t raw, Stage stage, Channel ch) const
{
    return lsb_mv(stage, ch) * (raw - kAdcMid) + offset_mv(stage, ch);
}

void Calibration::to_mv(std::span<const std::uint8_t> raw, std::span<double> out, Stage stage, Channel ch) const
{
    if (out.size() < raw.size())
        throw Error("output buffer too small");
    const double lsb = lsb_mv(stage, ch);
    const double offset = offset_mv(stage, ch);
    for (std::size_t i = 0; i < raw.size(); ++i)
        out[i] = lsb * (raw[i] - kAdcMid) + offset;
}

Stage Calibration::select(Channel ch, double min_mv, double max_mv, int margin_codes) const
{
    for (Stage stage : stages_) {
        const Span s = span(stage, ch);
        const double pad = margin_codes * lsb_mv(stage, ch);
        if (s.min_mv + pad <= min_mv && max_mv <= s.max_mv - pad)
            return stage;
    }
    return stages_.back();
}

TriggerCodes Calibration::trigger_codes(Stage stage, Channel ch, double level_mv, double min_mv, double max_mv,
                                        double sense_div) const
{
    const double lsb = lsb_mv(stage, ch);
    // Truncation toward zero and round-half-to-even mirror the Python reference model.
    const auto level = static_cast<long>(zero(stage, ch) + level_mv / lsb);
    const auto hysteresis = static_cast<long>(std::nearbyint(sense_div * (max_mv - min_mv) / 10.0 / lsb));
    return {static_cast<std::uint8_t>(std::clamp(level, 0L, 255L)),
            static_cast<std::uint8_t>(std::clamp(hysteresis, 0L, 255L))};
}

Stage Calibration::startup_stage() const
{
    return {static_cast<int>(desc_.gain_codes.size()) - 1, top_level_};
}

}  // namespace isds
