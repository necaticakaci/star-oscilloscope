// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <array>

#include "starosc/timebase.hpp"

using namespace starosc;

namespace {
constexpr std::size_t kMax = 1'047'548;
// The ISDS205X: at 48 MSa/s, a first run of 1020 contiguous samples, then blocks of 256 with gaps between them;
// at 16 MSa/s captures longer than 64K now and then lose samples.
constexpr std::array<RateCapacity, 5> kRates{{{1'000'000, kMax, {}},
                                              {4'000'000, kMax, {}},
                                              {8'000'000, kMax, {}},
                                              {16'000'000, kMax, {}, 64 * 1024},
                                              {48'000'000, kMax, {1020, 256}}}};

AcquisitionPlan plan(double seconds_per_div, MemoryDepth depth = MemoryDepth::Small, double pre = 0.5)
{
    return plan_acquisition({seconds_per_div, depth}, pre, kRates);
}
}  // namespace

TEST_CASE("picks the fastest rate that fits the memory depth")
{
    const auto p = plan(500e-6);
    CHECK(p.sample_rate_hz == 1'000'000);
    CHECK(p.window_samples == 5000);
    CHECK(p.capture_samples == 16384);
    CHECK(p.pre_samples == 2500);
    CHECK(p.layout.contiguous());

    CHECK(plan(2e-6).sample_rate_hz == 16'000'000);
    CHECK(plan(2e-6).window_samples == 320);
    CHECK(plan(50e-6, MemoryDepth::Small).sample_rate_hz == 16'000'000);
}

TEST_CASE("48 MSa/s where the window fits inside one block")
{
    const auto p = plan(500e-9);
    CHECK(p.sample_rate_hz == 48'000'000);
    CHECK(p.window_samples == 240);
    CHECK(p.capture_samples == 16384);  // many blocks, each a chance for the trigger
    CHECK(p.pre_samples == 120);
    CHECK(p.layout.first_block == 1020);

    CHECK(plan(200e-9).sample_rate_hz == 48'000'000);
    CHECK(plan(200e-9).window_samples == 96);
    CHECK(plan(50e-6, MemoryDepth::Medium).sample_rate_hz == 16'000'000);

    std::array<RateCapacity, 5> contiguous = kRates;
    contiguous[4].layout = {};
    CHECK(plan_acquisition({50e-6, MemoryDepth::Medium}, 0.5, contiguous).sample_rate_hz == 48'000'000);
}

TEST_CASE("48 MSa/s in the first run when the window is longer than a block")
{
    // 1 µs/div: 480 samples a window, more than a block; two of them fit the first run.
    const auto p = plan(1e-6);
    CHECK(p.sample_rate_hz == 48'000'000);
    CHECK(p.window_samples == 480);
    CHECK(p.capture_samples == 1020);  // only the first run, which is contiguous
    CHECK(p.pre_samples == 240);
    CHECK(p.layout.run_end(0, p.capture_samples) == 1020);

    // 2 µs/div needs 960 samples a window at 48 MSa/s: two do not fit the first run.
    CHECK(plan(2e-6).sample_rate_hz == 16'000'000);

    // A source whose first run is as short as its blocks keeps the old limit.
    std::array<RateCapacity, 5> unprimed = kRates;
    unprimed[4].layout = {252, 256};
    CHECK(plan_acquisition({1e-6, MemoryDepth::Small}, 0.5, unprimed).sample_rate_hz == 16'000'000);
    CHECK(plan_acquisition({500e-9, MemoryDepth::Small}, 0.5, unprimed).sample_rate_hz == 48'000'000);
}

TEST_CASE("capture layout runs")
{
    const CaptureLayout l{1020, 256};
    CHECK(l.run_begin(0) == 0);
    CHECK(l.run_end(0, 16384) == 1020);
    CHECK(l.run_begin(1019) == 0);
    CHECK(l.run_begin(1020) == 1020);
    CHECK(l.run_end(1100, 16384) == 1276);
    CHECK(l.run_begin(16383) == 16380);
    CHECK(l.run_end(16383, 16384) == 16384);  // the last block is cut by the capture
    CHECK(l.run_end(500, 1020) == 1020);      // a capture of the first run alone is one run
    CHECK(l.shortest_run(16384) == 256);
    CHECK(l.shortest_run(1020) == 256);

    const CaptureLayout whole;
    CHECK(whole.run_begin(5000) == 0);
    CHECK(whole.run_end(5000, 16384) == 16384);
}

TEST_CASE("captures twice the window when memory allows")
{
    const auto p = plan(2e-3, MemoryDepth::Large);
    CHECK(p.sample_rate_hz == 8'000'000);
    CHECK(p.window_samples == 160'000);
    CHECK(p.capture_samples == 320'000);
}

TEST_CASE("16 MSa/s only for captures that rarely lose samples")
{
    const auto p = plan(200e-6, MemoryDepth::Medium);
    CHECK(p.sample_rate_hz == 16'000'000);
    CHECK(p.window_samples == 32'000);
    CHECK(p.capture_samples == 64'000);

    // Longer windows go to 8 MSa/s, where nothing is lost.
    CHECK(plan(500e-6, MemoryDepth::Large).sample_rate_hz == 8'000'000);
    CHECK(plan(2e-3, MemoryDepth::Large).sample_rate_hz == 8'000'000);

    // Without that limit 16 MSa/s takes them.
    std::array<RateCapacity, 5> unlimited = kRates;
    unlimited[3].reliable_samples = 0;
    const auto u = plan_acquisition({2e-3, MemoryDepth::Large}, 0.5, unlimited);
    CHECK(u.sample_rate_hz == 16'000'000);
    CHECK(u.capture_samples == 640'000);
}

TEST_CASE("slowest timebase is limited by memory")
{
    const auto p = plan(100e-3);
    CHECK(p.sample_rate_hz == 1'000'000);
    CHECK(p.window_samples == 1'000'000);
    CHECK(p.capture_samples == kMax);
}

TEST_CASE("pre-trigger fraction")
{
    CHECK(plan(500e-6, MemoryDepth::Small, 0.2).pre_samples == 1000);
    CHECK(plan(500e-6, MemoryDepth::Small, 1.5).pre_samples == 5000);
}
