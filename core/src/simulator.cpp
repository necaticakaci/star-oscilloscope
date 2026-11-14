// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>

#include "isds/calibration.hpp"
#include "isds/generator.hpp"
#include "isds/protocol.hpp"
#include "isds_calibration.hpp"
#include "starosc/source.hpp"

namespace starosc {
namespace {

// Handshake descriptor of a real ISDS205X(1.1); gives the simulator realistic gain stages and offsets. Its last eight
// bytes, the device's serial number, are a placeholder (1), as in tests/data.
constexpr std::uint8_t kDescriptor[] = {
    0x0a, 0x59, 0x69, 0x58, 0x69, 0x6e, 0x67, 0x44, 0x69, 0x61, 0x6e, 0x5a, 0x69, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x53, 0x44, 0x53, 0x32, 0x30, 0x35, 0x58, 0x28, 0x31, 0x2e, 0x31, 0x29, 0x00, 0x00,
    0x00, 0x01, 0x01, 0x02, 0x00, 0x18, 0x6a, 0x00, 0x02, 0xdc, 0x6c, 0x00, 0x04, 0x0f, 0x02, 0x02,
    0x01, 0x02, 0x05, 0x01, 0x30, 0x01, 0x0c, 0x01, 0x06, 0x01, 0x03, 0x01, 0x01, 0x08, 0x00, 0xff,
    0xff, 0xfe, 0x0c, 0x00, 0x00, 0x01, 0xf4, 0x02, 0x01, 0x09, 0x05, 0x2e, 0x00, 0x00, 0x04, 0x00,
    0x77, 0xff, 0xff, 0xff, 0xf7, 0x00, 0x00, 0x00, 0x02, 0x00, 0x76, 0xff, 0xff, 0xff, 0xb4, 0xff,
    0xff, 0xfe, 0xbd, 0x00, 0xd3, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x69, 0x00, 0xd2, 0xff,
    0xff, 0xff, 0xed, 0xff, 0xff, 0xff, 0xb9, 0x02, 0x00, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00,
    0x7a, 0x01, 0xfe, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x41, 0x06, 0x2c, 0x00, 0x00, 0x00,
    0x21, 0x00, 0x00, 0x00, 0xa6, 0x06, 0x2c, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0xa6, 0x00,
    0x04, 0x00, 0x00, 0x64, 0xff, 0xff, 0xe8, 0x90, 0x00, 0x00, 0x17, 0x70, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x04, 0x02, 0x01, 0x02, 0x0d, 0x00,
    0x32, 0x00, 0x20, 0x00, 0x10, 0x01, 0xf0, 0x01, 0x60, 0x01, 0x30, 0x01, 0x18, 0x01, 0x0c, 0x01,
    0x08, 0x01, 0x06, 0x01, 0x04, 0x01, 0x03, 0x01, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x64, 0x00, 0x00, 0x00, 0x80, 0x00, 0x08, 0x10, 0x0a, 0x01, 0xc0, 0x01, 0x60, 0x01, 0x30,
    0x01, 0x18, 0x01, 0x0c, 0x01, 0x08, 0x01, 0x06, 0x01, 0x04, 0x01, 0x03, 0x01, 0x02, 0x00, 0x04,
    0x00, 0x00, 0x01, 0x01, 0x01, 0x05, 0x01, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x0a, 0x11,
    0xe1, 0xa3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00,
    0x00, 0x0a, 0x01, 0x31, 0x2d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x1c, 0x00, 0x00, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x0a, 0x01, 0x31, 0x2d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f,
    0x00, 0x64, 0x00, 0x64, 0x27, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr double kTwoPi = 2 * std::numbers::pi;

class Simulator final : public Source {
public:
    explicit Simulator(std::string_view model = kSimulatedModel)
        : cal_(isds::Descriptor::parse(kDescriptor)), model_(model), rng_(20261101)
    {
    }
    explicit Simulator(const SimulatedBench& bench) : Simulator() { bench_ = bench; }

    std::string name() const override { return "Simulator"; }
    bool simulated() const override { return true; }
    std::vector<std::uint32_t> sample_rates() const override { return cal_.descriptor().sample_rates; }
    std::size_t max_samples() const override { return 1024 * 1024 - isds::kSettleSamples; }
    // Like the device, so that plans match.
    CaptureLayout layout(std::uint32_t rate) const override
    {
        if (rate < sample_rates().back())
            return {};
        return {isds::kContiguousSamplesAtTopRate, isds::kTopRateBlockSamples};
    }
    std::size_t reliable_samples(std::uint32_t rate) const override
    {
        return rate >= isds::kLossyRateHz && rate < sample_rates().back() ? isds::kReliableSamplesAtLossyRate : max_samples();
    }

    Acquisition acquire(const AcquisitionRequest& request) override
    {
        const double dt = 1.0 / request.sample_rate_hz;
        const auto duration = std::chrono::duration<double>(static_cast<double>(request.samples) * dt);
        if (!bench_)
            std::this_thread::sleep_for(std::max<std::chrono::duration<double>>(duration, std::chrono::milliseconds(15)));
        time_ += std::uniform_real_distribution<double>(0, 1e-3)(rng_);  // free-running signals

        // How many inputs are on the lower coarse gain: the generator's output follows that.
        Acquisition out;
        out.sample_rate_hz = request.sample_rate_hz;
        int fineInputs = 0;
        for (int ch = 0; ch < kChannels; ++ch) {
            const ChannelRequest& req = request.channel[ch];
            const auto channel = ch == 0 ? isds::Channel::Ch1 : isds::Channel::Ch2;
            out.channel[ch].sensitive = cal_.gain_code(cal_.select(channel, req.range.min_v * 1000, req.range.max_v * 1000)) ==
                                        cal_.gain_code(cal_.stages().front());
            if (out.channel[ch].sensitive)
                ++fineInputs;
        }
        lastFineInputs_ = fineInputs;
        for (int ch = 0; ch < kChannels; ++ch) {
            const ChannelRequest& req = request.channel[ch];
            if (!req.enabled)
                continue;
            const auto channel = ch == 0 ? isds::Channel::Ch1 : isds::Channel::Ch2;
            const isds::Stage stage = cal_.select(channel, req.range.min_v * 1000, req.range.max_v * 1000);
            const auto& stages = cal_.stages();
            const auto stageIndex = static_cast<std::size_t>(std::ranges::find(stages, stage) - stages.begin());
            // The simulated hardware follows the factory table; a user correction changes only the conversion.
            const double raw_lsb_mv = cal_.factory_lsb_mv(stage, channel);
            const double raw_offset_mv = cal_.factory_offset_mv(stage, channel);
            const double lsb_mv = cal_.lsb_mv(stage, channel);
            const double offset_mv = cal_.offset_mv(stage, channel);
            std::normal_distribution<double> noise(0, 0.4 * raw_lsb_mv / 1000 + 0.6e-3);

            ChannelData& data = out.channel[ch];
            data.lsb_v = lsb_mv / 1000;
            const isds::Span span = cal_.span(stage, channel);
            data.span = {span.min_mv / 1000, span.max_mv / 1000};
            data.volts.resize(request.samples);
            for (std::size_t i = 0; i < request.samples; ++i) {
                const double t = time_ + static_cast<double>(i) * dt;
                double v = bench_ ? benchSignal(t, fineInputs) * bench_->stage_gain[stageIndex][static_cast<std::size_t>(ch)]
                                  : signal(ch, t);
                v += noise(rng_);
                if (req.coupling == Coupling::AC)
                    v -= dc(ch);
                const double code = std::clamp(std::round((v * 1000 - raw_offset_mv) / raw_lsb_mv + 127.5), 0.0, 255.0);
                data.clipped |= code == 0 || code == 255;
                data.volts[i] = static_cast<float>((lsb_mv * (code - 127.5) + offset_mv) / 1000);
            }
        }
        time_ += static_cast<double>(request.samples) * dt;
        return out;
    }

    UserCalibration factory_calibration() const override
    {
        return detail::factory_calibration(cal_, cal_.descriptor().model, kDeviceId);
    }

    int set_calibration(const UserCalibration& calibration) override
    {
        return detail::apply_calibration(
            cal_, kDeviceId, calibration,
            [this](isds::Stage s, isds::Channel ch, isds::Correction c) { cal_.set_correction(s, ch, c); });
    }

    // Shorted inputs: noise around the factory zero of each stage.
    std::vector<StageZero> measure_zero() override
    {
        std::vector<StageZero> out;
        for (isds::Stage stage : cal_.stages()) {
            std::array<std::vector<std::uint8_t>, kChannels> raw;
            for (int ch = 0; ch < kChannels; ++ch) {
                const auto channel = detail::to_channel(ch);
                std::normal_distribution<double> noise(0, 0.6);  // ADC codes
                const double zero = 127.5 - cal_.factory_offset_mv(stage, channel) / cal_.factory_lsb_mv(stage, channel);
                raw[ch].resize(4096);
                for (auto& r : raw[ch])
                    r = static_cast<std::uint8_t>(std::clamp(std::round(zero + noise(rng_)), 0.0, 255.0));
            }
            out.push_back(detail::zero_of(cal_, stage, {raw[0], raw[1]}));
        }
        return out;
    }

    bool has_generator() const override { return isds::has_generator(model_); }
    bool has_generator_levels() const override { return isds::has_level_control(model_); }
    void set_generator(const GeneratorSettings& settings) override
    {
        if (has_generator())
            generator_ = constrained(settings);
    }
    void park_generator() override
    {
        if (has_generator_levels())
            generator_.output = false;
    }

    // The level of the output, which is what a multimeter on DC reads.
    double benchLevel() const
    {
        return bench_ ? generator_.offset_v + lastFineInputs_ * bench_->output_shift_per_fine_input : 0;
    }

private:
    static constexpr const char* kDeviceId = "SIMULATOR";

    // CH1: 1 kHz, 0..2 V square wave with finite rise time and ringing (like the front-panel calibration output).
    // CH2: 1.5 kHz sine, 0.7 V amplitude; while the generator output is on, the generator, as if DDS OUT were
    // probed on CH2.
    double signal(int ch, double t) const
    {
        if (ch == 1)
            return generator_.output ? generator_output(generator_, t) : 0.7 * std::sin(kTwoPi * 1500 * t);
        const double phase = t * 1000 - std::floor(t * 1000);
        const bool high = phase < 0.5;
        const double since_edge = (high ? phase : phase - 0.5) / 1000;
        const double target = high ? 2.0 : 0.0;
        const double from = high ? 0.0 : 2.0;
        const double settle = (from - target) * std::exp(-since_edge / 0.15e-6);
        const double ring = (target - from) * 0.04 * std::exp(-since_edge / 2e-6) * std::sin(kTwoPi * 3e6 * since_edge);
        return target + settle + ring;
    }

    // The generator's output, as the inputs' coarse gain makes it.
    double benchSignal(double t, int fineInputs) const
    {
        const double a = std::pow(bench_->output_gain_per_fine_input, fineInputs);
        return (generator_output(generator_, t) - generator_.offset_v) * a + generator_.offset_v +
               fineInputs * bench_->output_shift_per_fine_input;
    }

    // DC component of signal(), which AC coupling removes.
    double dc(int ch) const
    {
        if (ch == 0)
            return 1.0;
        if (!generator_.output)
            return 0.0;
        const double high = generator_.waveform == Waveform::Square ? generator_.duty_percent / 100.0 : 0.5;
        return generator_.offset_v + generator_.amplitude_vpp / 2 * (2 * high - 1);
    }

    isds::Calibration cal_;
    std::string model_;  // what the generator follows
    GeneratorSettings generator_;
    std::optional<SimulatedBench> bench_;
    int lastFineInputs_ = 0;
    std::mt19937 rng_;
    double time_ = 0;
};

}  // namespace

std::unique_ptr<Source> make_simulator(std::string_view model) { return std::make_unique<Simulator>(model); }

std::unique_ptr<Source> make_simulator(const SimulatedBench& bench) { return std::make_unique<Simulator>(bench); }

double simulated_bench_output(const Source& source)
{
    const auto* simulator = dynamic_cast<const Simulator*>(&source);
    return simulator ? simulator->benchLevel() : 0;
}

}  // namespace starosc
