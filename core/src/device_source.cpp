// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <format>
#include <optional>

#include "isds/device.hpp"
#include "isds/generator.hpp"
#include "isds_calibration.hpp"
#include "starosc/source.hpp"

namespace starosc {
namespace {

using detail::to_channel;

constexpr std::uint32_t kZeroRate = 1'000'000;
constexpr std::size_t kZeroSamples = 100'000;

class DeviceSource final : public Source {
public:
    explicit DeviceSource(std::unique_ptr<isds::Device> device) : device_(std::move(device)) {}

    std::string name() const override { return device_->descriptor().model; }
    bool simulated() const override { return false; }
    std::vector<std::uint32_t> sample_rates() const override { return device_->descriptor().sample_rates; }
    std::size_t max_samples() const override { return device_->max_capture_samples(); }
    CaptureLayout layout(std::uint32_t rate) const override
    {
        const std::size_t block = device_->block_samples(rate);
        return block ? CaptureLayout{device_->max_contiguous_samples(rate), block} : CaptureLayout{};
    }
    std::size_t reliable_samples(std::uint32_t rate) const override { return device_->reliable_samples(rate); }

    Acquisition acquire(const AcquisitionRequest& request) override
    {
        const isds::Calibration& cal = device_->calibration();
        if (request.sample_rate_hz != rate_) {
            device_->set_sample_rate(request.sample_rate_hz);
            rate_ = request.sample_rate_hz;
        }

        std::array<isds::Stage, kChannels> stages{};
        for (int ch = 0; ch < kChannels; ++ch) {
            const ChannelRequest& req = request.channel[ch];
            const auto channel = to_channel(ch);
            // The device always samples both channels; a disabled one keeps its last stage.
            stages[ch] = req.enabled ? cal.select(channel, req.range.min_v * 1000, req.range.max_v * 1000)
                                     : stage_[ch].value_or(device_->stage(channel));
            if (stage_[ch] != stages[ch]) {
                device_->set_stage(channel, stages[ch]);
                stage_[ch] = stages[ch];
            }
            if (req.enabled && coupling_[ch] != req.coupling) {
                device_->set_coupling(channel,
                                      req.coupling == Coupling::AC ? isds::Coupling::AC : isds::Coupling::DC);
                coupling_[ch] = req.coupling;
            }
        }

        const ChannelRequest& source = request.channel[request.trigger_source];
        const TriggerKey key{request.trigger_source, request.trigger_level_v, source.range.min_v, source.range.max_v,
                             stages[request.trigger_source]};
        if (trigger_ != key) {
            device_->set_trigger(to_channel(request.trigger_source), request.trigger_level_v * 1000,
                                 source.range.min_v * 1000, source.range.max_v * 1000);
            trigger_ = key;
        }

        const isds::CaptureData data = device_->capture(request.samples);
        Acquisition out;
        out.sample_rate_hz = data.sample_rate_hz;
        for (int ch = 0; ch < kChannels; ++ch)
            out.channel[ch].sensitive = cal.gain_code(stages[ch]) == cal.gain_code(cal.stages().front());
        for (int ch = 0; ch < kChannels; ++ch) {
            if (!request.channel[ch].enabled)
                continue;
            const auto channel = to_channel(ch);
            const isds::Stage stage = data.stage[ch];
            const double lsb_mv = cal.lsb_mv(stage, channel);
            const double offset_mv = cal.offset_mv(stage, channel);
            ChannelData& cd = out.channel[ch];
            cd.lsb_v = lsb_mv / 1000;
            const isds::Span span = cal.span(stage, channel);
            cd.span = {span.min_mv / 1000, span.max_mv / 1000};
            cd.volts.resize(data.raw[ch].size());
            for (std::size_t i = 0; i < data.raw[ch].size(); ++i) {
                const std::uint8_t raw = data.raw[ch][i];
                cd.clipped |= raw == 0 || raw == 255;
                cd.volts[i] = static_cast<float>((lsb_mv * (raw - isds::Calibration::kAdcMid) + offset_mv) / 1000);
            }
        }
        return out;
    }

    UserCalibration factory_calibration() const override
    {
        return detail::factory_calibration(device_->calibration(), device_->descriptor().model, device_id());
    }

    int set_calibration(const UserCalibration& calibration) override
    {
        const int matched = detail::apply_calibration(
            device_->calibration(), device_id(), calibration,
            [this](isds::Stage s, isds::Channel ch, isds::Correction c) { device_->set_correction(s, ch, c); });
        trigger_.reset();  // the trigger codes depend on the correction
        return matched;
    }

    std::vector<StageZero> measure_zero() override
    {
        const isds::Calibration& cal = device_->calibration();
        device_->set_sample_rate(kZeroRate);
        rate_ = kZeroRate;
        for (int ch = 0; ch < kChannels; ++ch) {
            device_->set_coupling(to_channel(ch), isds::Coupling::DC);
            coupling_[ch] = Coupling::DC;
        }
        std::vector<StageZero> out;
        for (isds::Stage stage : cal.stages()) {
            for (int ch = 0; ch < kChannels; ++ch) {
                device_->set_stage(to_channel(ch), stage);
                stage_[ch] = stage;
            }
            const isds::Span span = cal.span(stage, isds::Channel::Ch1);
            device_->set_trigger(isds::Channel::Ch1, 0, span.min_mv, span.max_mv);
            device_->capture(4096);  // the first capture after a stage change is discarded
            const isds::CaptureData data = device_->capture(kZeroSamples);
            out.push_back(detail::zero_of(cal, stage, {data.raw[0], data.raw[1]}));
        }
        trigger_.reset();
        return out;
    }

    bool has_generator() const override { return device_->has_generator(); }
    bool has_generator_levels() const override { return device_->has_generator_level_control(); }

    void set_generator(const GeneratorSettings& settings) override
    {
        if (!device_->has_generator())
            return;
        const GeneratorSettings g = constrained(settings);
        if (!g.output && device_->generator_output())
            park_generator();
        isds::GeneratorSettings wire = device_->generator();
        wire.waveform = static_cast<isds::Waveform>(g.waveform);
        wire.frequency_hz = static_cast<std::uint32_t>(g.frequency_hz);
        wire.duty_percent = g.duty_percent;
        wire.amplitude = isds::amplitude_code_for_vpp(g.amplitude_vpp);
        wire.offset = isds::offset_code_for_volts(g.offset_v);
        device_->set_generator(wire);  // only stored while the output is off
        if (g.output && !device_->generator_output())
            device_->set_generator_output(true);
    }

    void park_generator() override
    {
        // Without levels parking is only switching the output on and off, which would let the wave out for a moment.
        if (!device_->has_generator_level_control())
            return;
        isds::GeneratorSettings parked = device_->generator();
        parked.amplitude = isds::amplitude_code_for_vpp(0);
        parked.offset = isds::offset_code_for_volts(0);
        device_->set_generator(parked);
        if (!device_->generator_output())
            device_->set_generator_output(true);  // levels are sent only while the output is on
        device_->set_generator_output(false);
    }

private:
    struct TriggerKey {
        int source;
        double level_v, min_v, max_v;
        isds::Stage stage;
        bool operator==(const TriggerKey&) const = default;
    };

    std::string device_id() const { return std::format("{:016X}", device_->descriptor().device_id); }

    std::unique_ptr<isds::Device> device_;
    std::uint32_t rate_ = 0;
    std::array<std::optional<isds::Stage>, kChannels> stage_{};
    std::array<std::optional<Coupling>, kChannels> coupling_{};
    std::optional<TriggerKey> trigger_;
};

}  // namespace

std::unique_ptr<Source> open_device() { return std::make_unique<DeviceSource>(isds::Device::open()); }

}  // namespace starosc
