// SPDX-License-Identifier: GPL-3.0-or-later
// Autoset on the simulator and on synthetic inputs.
#include <algorithm>
#include <cmath>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "starosc/autoset.hpp"
#include "starosc/scale.hpp"

using namespace starosc;
using Catch::Approx;

namespace {

// Settings far from anything useful, to start from.
ScopeSettings lost()
{
    ScopeSettings s;
    s.channel[0] = {false, 5e-3, 3.5, Coupling::DC, 1.0};
    s.channel[1] = {true, 2.0, -4.0, Coupling::DC, 1.0};
    s.timebase = {100e-3, MemoryDepth::Medium};
    s.trigger.source = 1;
    s.trigger.mode = TriggerMode::Normal;
    s.trigger.level_v = 5.0;
    return s;
}

struct Input {
    double dc = 0;
    double amplitude = 0;  // peak
    double frequency = 0;
};

// Inputs quantized to 8 bits over the requested range, as if a stage existed for every range,
// with ±6 V as the widest one.
class SyntheticSource final : public Source {
public:
    explicit SyntheticSource(std::array<Input, kChannels> inputs) : inputs_(inputs) {}

    std::string name() const override { return "synthetic"; }
    bool simulated() const override { return true; }
    std::vector<std::uint32_t> sample_rates() const override
    {
        return {1'000'000, 4'000'000, 8'000'000, 16'000'000, 48'000'000};
    }
    std::size_t max_samples() const override { return 1'000'000; }

    Acquisition acquire(const AcquisitionRequest& request) override
    {
        ++captures;
        Acquisition out;
        out.sample_rate_hz = request.sample_rate_hz;
        for (int ch = 0; ch < kChannels; ++ch) {
            const ChannelRequest& req = request.channel[ch];
            if (!req.enabled)
                continue;
            VoltageRange span = req.range;
            if (span.max_v - span.min_v > 12)
                span = {-6, 6};
            ChannelData& cd = out.channel[ch];
            cd.span = span;
            cd.lsb_v = (span.max_v - span.min_v) / 255;
            cd.volts.resize(request.samples);
            const Input& in = inputs_[ch];
            for (std::size_t i = 0; i < request.samples; ++i) {
                const double t = static_cast<double>(i) / request.sample_rate_hz;
                const double v = in.dc + in.amplitude * std::sin(2 * std::numbers::pi * in.frequency * t + 0.3);
                const double code = std::clamp(std::round((v - span.min_v) / cd.lsb_v), 0.0, 255.0);
                cd.clipped |= code == 0 || code == 255;
                cd.volts[i] = static_cast<float>(span.min_v + code * cd.lsb_v);
            }
        }
        return out;
    }

    UserCalibration factory_calibration() const override { return {}; }
    int set_calibration(const UserCalibration&) override { return 0; }
    std::vector<StageZero> measure_zero() override { return {}; }

    int captures = 0;

private:
    std::array<Input, kChannels> inputs_;
};

}  // namespace

TEST_CASE("autoset finds the simulator's two signals")
{
    auto sim = make_simulator();  // CH1: 1 kHz 0..2 V square; CH2: 1.5 kHz sine, 0.7 V peak
    const AutosetResult r = autoset(*sim, lost());
    const ScopeSettings& s = r.settings;

    CHECK(r.found[0]);
    CHECK(r.found[1]);
    CHECK(s.channel[0].enabled);
    CHECK(s.channel[0].volts_per_div == Approx(1.0));
    CHECK(s.channel[0].position_div == Approx(1.0).margin(0.1));  // middle of 0..2 V at +2 div
    CHECK(s.channel[1].volts_per_div == Approx(0.5));
    CHECK(s.channel[1].position_div == Approx(-2.0).margin(0.1));

    // The current source has a periodic signal, so it stays.
    CHECK(s.trigger.source == 1);
    CHECK(s.trigger.mode == TriggerMode::Auto);
    CHECK(s.trigger.level_v == Approx(0.0).margin(0.05));
    CHECK(r.frequency_hz == Approx(1500).epsilon(0.01));
    CHECK(s.timebase.seconds_per_div == Approx(200e-6));  // 3 periods
    CHECK(s.timebase.memory == MemoryDepth::Medium);
}

TEST_CASE("autoset in X-Y puts both signals through the middle")
{
    auto sim = make_simulator();  // CH1: 1 kHz 0..2 V square; CH2: 1.5 kHz sine around 0 V
    ScopeSettings start = lost();
    start.display = DisplayMode::XY;
    const ScopeSettings s = autoset(*sim, start).settings;

    CHECK(s.display == DisplayMode::XY);
    // The middle of each signal on the centre of the screen, with 6 divisions of room each.
    CHECK(s.channel[0].position_div * s.channel[0].volts_per_div == Approx(-1.0).margin(0.05));
    CHECK(s.channel[0].volts_per_div == Approx(0.5));
    CHECK(s.channel[1].position_div == Approx(0.0).margin(0.1));
    CHECK(s.channel[1].volts_per_div == Approx(0.5));
}

TEST_CASE("autoset triggers on CH1 when it is the source")
{
    auto sim = make_simulator();
    ScopeSettings start = lost();
    start.trigger.source = 0;
    const AutosetResult r = autoset(*sim, start);
    CHECK(r.settings.trigger.source == 0);
    CHECK(r.settings.trigger.level_v == Approx(1.0).margin(0.1));
    CHECK(r.frequency_hz == Approx(1000).epsilon(0.01));
    CHECK(r.settings.timebase.seconds_per_div == Approx(500e-6));  // 5 periods
}

TEST_CASE("autoset resolves a small signal and switches the idle channel off")
{
    SyntheticSource src({Input{0, 0.015, 10e3}, Input{}});
    const AutosetResult r = autoset(src, lost());
    const ScopeSettings& s = r.settings;
    CHECK(r.found[0]);
    CHECK_FALSE(r.found[1]);
    CHECK(s.channel[0].enabled);
    CHECK_FALSE(s.channel[1].enabled);
    CHECK(s.channel[0].volts_per_div == Approx(5e-3));  // 30 mVpp on 6 divisions
    CHECK(s.channel[0].position_div == Approx(0).margin(0.1));
    CHECK(s.trigger.source == 0);  // the old source has no signal
    CHECK(r.frequency_hz == Approx(10e3).epsilon(0.01));
    CHECK(s.timebase.seconds_per_div == Approx(50e-6));
    CHECK(src.captures == 9);  // three passes of settle, fast and slow: the synthetic source has no gaps
}

TEST_CASE("autoset shows a DC level and keeps the timebase")
{
    SyntheticSource src({Input{2.5}, Input{}});
    const AutosetResult r = autoset(src, lost());
    const ScopeSettings& s = r.settings;
    CHECK(r.found[0]);
    CHECK(r.frequency_hz == 0);
    CHECK(s.channel[0].volts_per_div == Approx(1.0));  // the 0 V marker must stay on screen
    CHECK(s.channel[0].position_div == Approx(-2.5).margin(0.02));
    CHECK(s.timebase.seconds_per_div == Approx(100e-3));
}

TEST_CASE("autoset finds a slow signal")
{
    SyntheticSource src({Input{}, Input{0.5, 1.0, 20}});
    const AutosetResult r = autoset(src, lost());
    CHECK(r.found[1]);
    CHECK(r.settings.trigger.source == 1);
    CHECK(r.frequency_hz == Approx(20).epsilon(0.01));
    CHECK(r.settings.timebase.seconds_per_div == Approx(20e-3));
    CHECK(r.settings.trigger.level_v == Approx(0.5).margin(0.05));
}

TEST_CASE("autoset keeps round scales with a measured probe factor")
{
    SyntheticSource src({Input{0, 0.25, 1e3}, Input{}});  // 0.5 Vpp at the BNC
    ScopeSettings start;
    start.channel[0].probe = 10.15;
    const AutosetResult r = autoset(src, start);
    REQUIRE(r.found[0]);
    CHECK(r.settings.channel[0].probe == 10.15);
    const auto steps = volts_per_div_steps(10.15);
    CHECK(std::find_if(steps.begin(), steps.end(), [&](double s) {
              return s == Catch::Approx(r.settings.channel[0].volts_per_div);
          }) != steps.end());
}

TEST_CASE("autoset scales with the probe factor")
{
    SyntheticSource src({Input{0, 0.25, 1e3}, Input{}});  // 0.5 Vpp at the BNC, 5 Vpp at a x10 probe tip
    ScopeSettings start = lost();
    start.channel[0].probe = 10;
    const AutosetResult r = autoset(src, start);
    CHECK(r.settings.channel[0].volts_per_div == Approx(1.0));
    CHECK(r.settings.channel[0].probe == 10);
}

TEST_CASE("autoset leaves the settings alone without a signal")
{
    SyntheticSource src({Input{}, Input{0.005}});
    const AutosetResult r = autoset(src, lost());
    CHECK_FALSE(r.found[0]);
    CHECK_FALSE(r.found[1]);
    CHECK(r.settings.channel[1].volts_per_div == 2.0);
    CHECK(r.settings.trigger.mode == TriggerMode::Normal);
}
