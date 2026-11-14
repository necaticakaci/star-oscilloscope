// SPDX-License-Identifier: GPL-3.0-or-later
// End-to-end acquisition through the simulator.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "starosc/engine.hpp"

using namespace starosc;
using Catch::Approx;

namespace {
ScopeSettings defaults()
{
    ScopeSettings s;
    s.channel[0] = {true, 1.0, 1.0, Coupling::DC, 1.0};
    s.channel[1] = {true, 0.5, -2.0, Coupling::DC, 1.0};
    s.timebase = {500e-6, MemoryDepth::Small};
    s.trigger.level_v = 1.0;
    return s;
}
}  // namespace

TEST_CASE("a frame can be measured again with other thresholds")
{
    auto sim = make_simulator();
    Engine engine(*sim);
    auto frame = engine.acquire(defaults());
    REQUIRE(frame);
    const double wide = frame->measurements[1].rise_time;  // the sine on CH2
    REQUIRE(std::isfinite(wide));

    frame->settings.thresholds = {25, 50, 75};
    measure_frame(*frame);
    CHECK(frame->measurements[1].rise_time < 0.8 * wide);
}

TEST_CASE("delay needs the same frequency on both channels")
{
    auto sim = make_simulator();
    Engine engine(*sim);
    ScopeSettings s = defaults();
    auto frame = engine.acquire(s);
    REQUIRE(frame);
    CHECK(std::isnan(frame->pair.delay));  // 1 kHz and 1.5 kHz

    // Both channels showing the same samples, one shifted by 10 of them.
    const std::size_t n = frame->volts[1].size();
    frame->volts[0].assign(frame->volts[1].begin() + 10, frame->volts[1].end());
    frame->volts[1].resize(n - 10);
    frame->lsb_v[0] = frame->lsb_v[1];
    measure_frame(*frame);
    CHECK(frame->pair.delay == Approx(10 * frame->sample_interval_s).epsilon(0.05));
    CHECK(frame->pair.phase == Approx(10 * frame->sample_interval_s * 1500 * 360).epsilon(0.05));
}

TEST_CASE("triggered frame puts the edge at the pre-trigger point")
{
    auto sim = make_simulator();
    Engine engine(*sim);
    const auto frame = engine.acquire(defaults());
    REQUIRE(frame);
    REQUIRE(frame->triggered);
    CHECK(frame->plan.sample_rate_hz == 1'000'000);

    const auto& ch1 = frame->volts[0];
    const std::size_t pre = frame->plan.pre_samples;
    REQUIRE(ch1.size() == frame->plan.window_samples + 1);
    CHECK(ch1[pre] < 1.0f);
    CHECK(ch1[pre + 1] >= 1.0f);

    CHECK(frame->measurements[0].frequency == Approx(1000).epsilon(0.01));
    CHECK(frame->measurements[0].peak_to_peak == Approx(2.0).epsilon(0.1));
    CHECK(frame->measurements[1].frequency == Approx(1500).epsilon(0.01));
    CHECK(frame->measurements[1].peak_to_peak == Approx(1.4).epsilon(0.1));
}

TEST_CASE("overview covers the whole capture and marks the screen window")
{
    auto sim = make_simulator();
    Engine engine(*sim);
    const auto frame = engine.acquire(defaults());
    REQUIRE(frame);
    const Overview& o = frame->overview;
    REQUIRE(o.min[0].size() == Overview::kBins);
    REQUIRE(o.max[0].size() == Overview::kBins);
    CHECK(o.window_begin >= 0);
    CHECK(o.window_end <= 1);
    CHECK(o.window_end - o.window_begin ==
          Approx(static_cast<double>(frame->plan.window_samples) / frame->plan.capture_samples).epsilon(1e-6));
    for (std::size_t b = 0; b < Overview::kBins; ++b)
        CHECK(o.min[0][b] <= o.max[0][b]);
}

TEST_CASE("the trigger nearest the middle puts the window in the middle of the capture")
{
    // CH1 is a 1 kHz square wave: a rising edge every 1000 samples at 1 MSa/s, many in the capture. Whichever lies
    // nearest the middle triggers, so the window moves by at most half a period from frame to frame.
    auto sim = make_simulator();
    Engine engine(*sim);
    ScopeSettings s = defaults();
    s.timebase.seconds_per_div = 500e-6;
    for (int i = 0; i < 20; ++i) {
        const auto frame = engine.acquire(s);
        REQUIRE(frame);
        REQUIRE(frame->triggered);
        const Overview& o = frame->overview;
        const double middle = (1 - (o.window_end - o.window_begin)) / 2;
        const double half_period = 500.0 / static_cast<double>(frame->plan.capture_samples);
        CHECK(std::abs(o.window_begin - middle) <= half_period + 0.002);
    }
}

TEST_CASE("frames say which inputs are on their sensitive stages")
{
    auto sim = make_simulator();
    Engine engine(*sim);
    ScopeSettings s = defaults();  // 1 V/div and 0.5 V/div: the coarse stages
    auto frame = engine.acquire(s, true);
    REQUIRE(frame);
    CHECK_FALSE(frame->sensitive[0]);
    CHECK_FALSE(frame->sensitive[1]);
    s.channel[0].volts_per_div = 0.1;  // ±0.4 V on the screen: the finest stages
    s.channel[0].position_div = 0;
    frame = engine.acquire(s, true);
    REQUIRE(frame);
    CHECK(frame->sensitive[0]);
    CHECK_FALSE(frame->sensitive[1]);
}

TEST_CASE("probe factor scales the displayed voltage")
{
    auto sim = make_simulator();
    Engine engine(*sim);
    ScopeSettings s = defaults();
    s.channel[0].probe = 10;
    s.channel[0].volts_per_div = 10;
    s.trigger.level_v = 10;
    const auto frame = engine.acquire(s);
    REQUIRE(frame);
    CHECK(frame->measurements[0].peak_to_peak == Approx(20).epsilon(0.1));
}

TEST_CASE("trigger modes without a trigger")
{
    auto sim = make_simulator();
    Engine engine(*sim);
    ScopeSettings s = defaults();
    s.trigger.level_v = 3.5;  // above the 2 V square wave

    s.trigger.mode = TriggerMode::Normal;
    CHECK_FALSE(engine.acquire(s));
    s.trigger.mode = TriggerMode::Single;
    CHECK_FALSE(engine.acquire(s));

    const auto forced = engine.acquire(s, true);
    REQUIRE(forced);
    CHECK_FALSE(forced->triggered);

    s.trigger.mode = TriggerMode::Auto;
    const auto autoframe = engine.acquire(s);
    REQUIRE(autoframe);
    CHECK_FALSE(autoframe->triggered);
}

TEST_CASE("disabled channels carry no samples")
{
    auto sim = make_simulator();
    Engine engine(*sim);
    ScopeSettings s = defaults();
    s.channel[1].enabled = false;
    const auto frame = engine.acquire(s);
    REQUIRE(frame);
    CHECK(frame->volts[1].empty());
    CHECK_FALSE(frame->volts[0].empty());
}

TEST_CASE("at 500 ns/div the window sits inside one 48 MSa/s block")
{
    auto sim = make_simulator();
    GeneratorSettings g;
    g.output = true;
    g.frequency_hz = 1e6;
    g.amplitude_vpp = 2;
    sim->set_generator(g);

    ScopeSettings s;
    s.channel[0].enabled = false;
    s.channel[1] = {true, 0.5, 0.0, Coupling::DC, 1.0};
    s.trigger.source = 1;
    s.trigger.level_v = 0;
    s.timebase = {500e-9, MemoryDepth::Small};
    Engine engine(*sim);
    for (int i = 0; i < 5; ++i) {
        const auto frame = engine.acquire(s);
        REQUIRE(frame);
        CHECK(frame->triggered);
        CHECK(frame->plan.sample_rate_hz == 48'000'000);
        CHECK(frame->volts[1].size() == frame->plan.window_samples + 1);
        const auto first = static_cast<std::size_t>(
            std::floor(frame->overview.window_begin * static_cast<double>(frame->plan.capture_samples) + 1e-9));
        CHECK(frame->plan.layout.run_begin(first) == frame->plan.layout.run_begin(first + frame->plan.window_samples));
        CHECK(frame->measurements[1].frequency == Approx(1e6).epsilon(0.02));
        // The margins for sin(x)/x come from the same block as the window.
        const std::size_t run = frame->plan.layout.run_begin(first);
        CHECK(frame->before[1].size() == std::min<std::size_t>(kSincTaps, first - run));
    }
}

TEST_CASE("at 1 us/div 48 MSa/s captures only the first run")
{
    auto sim = make_simulator();
    GeneratorSettings g;
    g.output = true;
    g.frequency_hz = 300e3;
    g.amplitude_vpp = 2;
    sim->set_generator(g);

    ScopeSettings s;
    s.channel[0].enabled = false;
    s.channel[1] = {true, 0.5, 0.0, Coupling::DC, 1.0};
    s.trigger.source = 1;
    s.trigger.level_v = 0;
    s.timebase = {1e-6, MemoryDepth::Small};
    Engine engine(*sim);
    for (int i = 0; i < 5; ++i) {
        const auto frame = engine.acquire(s);
        REQUIRE(frame);
        CHECK(frame->triggered);
        CHECK(frame->plan.sample_rate_hz == 48'000'000);
        CHECK(frame->plan.capture_samples == 1020);
        CHECK(frame->volts[1].size() == frame->plan.window_samples + 1);
        CHECK(frame->measurements[1].frequency == Approx(300e3).epsilon(0.02));
    }
}

TEST_CASE("a signal above half the sample rate is flagged as aliased")
{
    auto sim = make_simulator();
    GeneratorSettings g;
    g.output = true;
    g.frequency_hz = 10e6;  // above the 8 MHz a 16 MSa/s frame can show
    g.amplitude_vpp = 2;
    sim->set_generator(g);

    ScopeSettings s = defaults();
    s.channel[1] = {true, 0.5, 0.0, Coupling::DC, 1.0};
    s.timebase = {2e-6, MemoryDepth::Small};  // the fastest timebase below the top rate
    Engine engine(*sim);
    auto frame = engine.acquire(s, true);
    REQUIRE(frame);
    CHECK(frame->plan.sample_rate_hz == 16'000'000);
    CHECK(frame->aliased[1]);
    CHECK_FALSE(frame->aliased[0]);  // the 1 kHz square on CH1

    g.frequency_hz = 1e6;
    sim->set_generator(g);
    for (int i = 0; i < 10; ++i)
        frame = engine.acquire(s, true);
    CHECK_FALSE(frame->aliased[1]);

    s.timebase.seconds_per_div = 200e-9;  // the top rate itself: nothing to check
    frame = engine.acquire(s, true);
    CHECK(frame->plan.sample_rate_hz == 48'000'000);
    CHECK_FALSE(frame->aliased[1]);
}
