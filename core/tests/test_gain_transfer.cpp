// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "starosc/gain_transfer.hpp"

using namespace starosc;
using Catch::Approx;

namespace {

// As measured on one device (docs/measurements.md §3).
const std::vector<std::array<double, 2>> kGains = {{0.980, 0.980}, {0.930, 0.938}, {0.934, 0.942}, {0.931, 0.940},
                                                   {0.934, 0.936}, {0.940, 0.944}, {0.934, 0.938}};
constexpr std::size_t kReference = 5;

ChannelData sineData(double vpp, double mean, double hz, double rate, std::size_t n)
{
    ChannelData d;
    d.volts.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        d.volts[i] = static_cast<float>(mean + vpp / 2 * std::sin(2 * std::numbers::pi * hz * static_cast<double>(i) / rate + 0.7));
    return d;
}

void checkGains(const GainTransfer& t)
{
    for (std::size_t i = 0; i < kGains.size(); ++i)
        for (std::size_t ch = 0; ch < 2; ++ch)
            CHECK(t.relative[ch][i] == Approx(kGains[i][ch] / kGains[kReference][ch]).epsilon(0.001));
    CHECK(t.channel_ratio == Approx(kGains[kReference][0] / kGains[kReference][1]).epsilon(0.001));
}

}  // namespace

TEST_CASE("a sine is read through a difference of clocks")
{
    constexpr double kRate = 1e6;
    for (double off : {-200e-6, -105e-6, 0.0, 300e-6}) {
        const SineReading r = read_sine(sineData(2.0, 0.25, 1000 * (1 + off), kRate, 50'000), 1000, kRate);
        CHECK(r.vpp == Approx(2.0).epsilon(1e-5));
        CHECK(r.mean == Approx(0.25).epsilon(1e-4));
        CHECK(r.residual_rms < 1e-4);
    }
    CHECK(read_sine(ChannelData{}, 1000, kRate).vpp == 0);
}

TEST_CASE("the gain of every stage is found against the reference stage")
{
    SimulatedBench bench;
    bench.stage_gain = kGains;
    auto source = make_simulator(bench);
    check_generator_on_inputs(*source);
    const GainTransfer t = transfer_gains(*source);
    checkGains(t);
    CHECK(t.worst_spread < 0.003);
}

TEST_CASE("the output following the coarse gain does not enter the gains")
{
    SimulatedBench bench;
    bench.stage_gain = kGains;
    bench.output_gain_per_fine_input = 1.10;     // measured: about 10 % for each input on G 0x109
    bench.output_shift_per_fine_input = -0.065;  // and about 65 mV of level, at 1 V
    auto source = make_simulator(bench);
    checkGains(transfer_gains(*source));
}

TEST_CASE("the stages for the multimeter are the widest but one and one of the other coarse gain")
{
    const auto stages = make_simulator()->factory_calibration().stages;
    CHECK(reference_stage(stages) == 5);
    CHECK(check_stage(stages) == 3);
    CHECK(stages[3].gain_code != stages[5].gain_code);

    const LevelPlan plan = level_plan(stages, 5);
    CHECK(plan.stage == 5);
    CHECK(plan.volts[0] > 1);
    CHECK(plan.volts[1] < -1);
    // With the sine on it, a level stays inside the span of both channels, with some room.
    for (std::size_t ch = 0; ch < 2; ++ch) {
        CHECK(plan.volts[0] + plan.sine_vpp / 2 < 0.95 * stages[5].factory_span[ch].max_v);
        CHECK(plan.volts[1] - plan.sine_vpp / 2 > 0.95 * stages[5].factory_span[ch].min_v);
    }
}

TEST_CASE("two levels give the gain of their stage, also where the output follows the coarse gain")
{
    SimulatedBench bench;
    bench.stage_gain = kGains;
    bench.output_gain_per_fine_input = 1.10;
    bench.output_shift_per_fine_input = -0.065;
    auto source = make_simulator(bench);
    const auto stages = source->factory_calibration().stages;
    for (int stage : {reference_stage(stages), check_stage(stages)}) {
        const LevelPlan plan = level_plan(stages, stage);
        const auto plus = measure_level(*source, stage, plan.volts[0], plan.sine_vpp);
        const double upper = simulated_bench_output(*source);
        const auto minus = measure_level(*source, stage, plan.volts[1], plan.sine_vpp);
        const double lower = simulated_bench_output(*source);
        for (std::size_t ch = 0; ch < 2; ++ch)
            CHECK(gain_from_levels({plus[ch], minus[ch]}, {upper, lower}) ==
                  Approx(kGains[static_cast<std::size_t>(stage)][ch]).epsilon(0.002));
    }
}

TEST_CASE("gains are set from the reference stage, and the zeros stay")
{
    UserCalibration cal = make_simulator()->factory_calibration();
    cal.stages[0].channel[1].zero_v = 0.012;
    GainTransfer t;
    t.relative[0].assign(cal.stages.size(), 1.0);
    t.relative[1].assign(cal.stages.size(), 1.1);
    apply_gains(cal, t, {0.93, 0.95});
    CHECK(cal.stages[3].channel[0].gain == Approx(0.93));
    CHECK(cal.stages[0].channel[1].gain == Approx(1.045));
    CHECK(cal.stages[0].channel[1].zero_v == 0.012);
}

TEST_CASE("a generator that is not on the inputs is told apart")
{
    SimulatedBench good;
    good.stage_gain = kGains;
    auto source = make_simulator(good);
    CHECK_NOTHROW(check_generator_on_inputs(*source));

    SimulatedBench tenth = good;
    for (auto& stage : tenth.stage_gain)
        stage[1] *= 0.1;  // CH2 probe on ×10
    auto withProbe = make_simulator(tenth);
    CHECK_THROWS_WITH(check_generator_on_inputs(*withProbe), "CH2 reads about a tenth of the generator: is its probe on ×10?");

    SimulatedBench none = good;
    for (auto& stage : none.stage_gain)
        stage[0] = 0;  // CH1 not connected
    auto notConnected = make_simulator(none);
    CHECK_THROWS_WITH(check_generator_on_inputs(*notConnected), "no signal from the generator on CH1");
}
