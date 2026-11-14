// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "starosc/measure.hpp"

using namespace starosc;
using Catch::Approx;

TEST_CASE("sine wave")
{
    std::vector<float> v(10'000);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<float>(std::sin(2 * std::numbers::pi * 1000 * static_cast<double>(i) * 1e-6));
    const Measurements m = measure(v, 1e-6);
    CHECK(m.frequency == Approx(1000).epsilon(1e-3));
    CHECK(m.period == Approx(1e-3).epsilon(1e-3));
    CHECK(m.peak_to_peak == Approx(2.0).epsilon(1e-3));
    CHECK(m.mean == Approx(0).margin(1e-3));
    CHECK(m.rms == Approx(std::sqrt(0.5)).epsilon(1e-3));
    CHECK(m.duty == Approx(50).margin(0.5));
}

TEST_CASE("pulse train duty cycle")
{
    std::vector<float> v;
    for (int cycle = 0; cycle < 20; ++cycle)
        for (int i = 0; i < 100; ++i)
            v.push_back(i < 25 ? 3.3f : 0.0f);
    const Measurements m = measure(v, 1e-6);
    CHECK(m.frequency == Approx(10'000).epsilon(1e-3));
    CHECK(m.duty == Approx(25).margin(0.5));
    CHECK(m.max == Approx(3.3));
    CHECK(m.min == Approx(0));
}

TEST_CASE("timing needs a swing above the noise floor")
{
    std::vector<float> flat(1000, 0.5f);
    const Measurements m = measure(flat, 1e-6);
    CHECK(m.peak_to_peak == 0);
    CHECK(std::isnan(m.frequency));

    std::vector<float> noise;
    for (int i = 0; i < 1000; ++i)
        noise.push_back(i % 7 < 3 ? 0.01f : 0.0f);
    CHECK_FALSE(std::isnan(measure(noise, 1e-6).frequency));
    CHECK(std::isnan(measure(noise, 1e-6, 0.04).frequency));
}

namespace {

// A pulse train: `low` samples at base, `rise` samples on the way up, `high` at top, `fall` on the way down.
std::vector<float> trapezoid(int cycles, int rise, int high, int fall, int low, float base, float top)
{
    std::vector<float> v;
    for (int c = 0; c < cycles; ++c) {
        for (int i = 0; i < low; ++i)
            v.push_back(base);
        for (int i = 0; i < rise; ++i)
            v.push_back(base + (top - base) * static_cast<float>(i + 1) / static_cast<float>(rise + 1));
        for (int i = 0; i < high; ++i)
            v.push_back(top);
        for (int i = 0; i < fall; ++i)
            v.push_back(top + (base - top) * static_cast<float>(i + 1) / static_cast<float>(fall + 1));
    }
    return v;
}

std::vector<float> sine(std::size_t n, double period_samples, double amplitude, double phase = 0, double offset = 0)
{
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = static_cast<float>(offset + amplitude * std::sin(2 * std::numbers::pi * static_cast<double>(i) / period_samples + phase));
    return v;
}

}  // namespace

TEST_CASE("a sine has no flat levels: top and base are its peaks")
{
    const Measurements m = measure(sine(10'000, 100, 1.5, 0, 0.5), 1e-6);
    CHECK(m.top == Approx(m.max));
    CHECK(m.base == Approx(m.min));
    CHECK(m.amplitude == Approx(3.0).epsilon(1e-3));
    CHECK(m.overshoot == Approx(0));
    CHECK(m.undershoot == Approx(0));
    CHECK(m.rms == Approx(std::sqrt(0.25 + 1.5 * 1.5 / 2)).epsilon(1e-3));
    CHECK(m.ac_rms == Approx(1.5 / std::sqrt(2.0)).epsilon(1e-3));
    // Between 10 % and 90 % of a sine: arcsin(0.8) / π of the period.
    CHECK(m.rise_time == Approx(std::asin(0.8) / std::numbers::pi * 100e-6).epsilon(0.01));
    CHECK(m.fall_time == Approx(std::asin(0.8) / std::numbers::pi * 100e-6).epsilon(0.01));
    CHECK(m.positive_width == Approx(50e-6).epsilon(0.01));
    CHECK(m.negative_width == Approx(50e-6).epsilon(0.01));
}

TEST_CASE("a triangle has no flat levels either")
{
    std::vector<float> v;
    for (int c = 0; c < 20; ++c)
        for (int i = 0; i < 100; ++i)
            v.push_back(static_cast<float>(i < 50 ? i : 100 - i) / 50.0f);
    const Measurements m = measure(v, 1e-6);
    CHECK(m.top == Approx(1));
    CHECK(m.base == Approx(0));
    CHECK(m.rise_time == Approx(40e-6).epsilon(0.01));  // 80 % of a 50-sample slope
}

TEST_CASE("top and base of a pulse ignore overshoot and spikes")
{
    std::vector<float> v = trapezoid(20, 9, 300, 9, 300, 0.5f, 2.5f);
    // Ringing after each rising edge: 0.2 V above the top for 10 samples.
    for (std::size_t i = 310; i < v.size(); i += 618)
        for (std::size_t k = 0; k < 10 && i + k < v.size(); ++k)
            v[i + k] = 2.7f;
    // A dip below the base in each cycle.
    for (std::size_t i = 100; i < v.size(); i += 618)
        v[i] = 0.4f;
    const Measurements m = measure(v, 1e-6);
    CHECK(m.max == Approx(2.7));
    CHECK(m.min == Approx(0.4));
    CHECK(m.top == Approx(2.5).epsilon(0.002));
    CHECK(m.base == Approx(0.5).epsilon(0.002));
    CHECK(m.amplitude == Approx(2.0).epsilon(0.002));
    CHECK(m.overshoot == Approx(10).margin(0.2));
    CHECK(m.undershoot == Approx(5).margin(0.2));
}

TEST_CASE("edge times and pulse widths of a trapezoid")
{
    // Edges of 10 and 20 sample steps from base to top.
    const std::vector<float> v = trapezoid(30, 9, 200, 19, 400, 0.0f, 1.0f);
    const Measurements m = measure(v, 1e-6);
    CHECK(m.period == Approx(628e-6).epsilon(1e-3));
    CHECK(m.rise_time == Approx(8e-6).epsilon(0.03));
    CHECK(m.fall_time == Approx(16e-6).epsilon(0.03));
    CHECK_FALSE(m.rise_unresolved);
    CHECK_FALSE(m.fall_unresolved);
    CHECK(m.positive_width == Approx(214e-6).epsilon(0.002));
    CHECK(m.negative_width == Approx(414e-6).epsilon(0.002));
    CHECK(m.duty == Approx(100.0 * 214 / 628).margin(0.1));
}

TEST_CASE("thresholds move the reference levels")
{
    const std::vector<float> v = trapezoid(30, 99, 400, 99, 400, 0.0f, 1.0f);
    MeasureThresholds t;
    t.low = 20;
    t.high = 80;
    const Measurements narrow = measure(v, 1e-6, 0, t);
    const Measurements wide = measure(v, 1e-6);
    CHECK(narrow.rise_time == Approx(60e-6).epsilon(0.02));
    CHECK(wide.rise_time == Approx(80e-6).epsilon(0.02));

    t = {};
    t.mid = 25;
    const Measurements low_mid = measure(v, 1e-6, 0, t);
    // Crossing lower on both edges: 25 samples earlier on the way up, later on the way down.
    CHECK(low_mid.positive_width == Approx(wide.positive_width + 50e-6).epsilon(0.03));
    CHECK(low_mid.period == Approx(wide.period).epsilon(1e-3));
}

TEST_CASE("constrained thresholds stay in order")
{
    CHECK(constrained(MeasureThresholds{}) == MeasureThresholds{});
    const MeasureThresholds crossed = constrained(MeasureThresholds{70, 40, 20});
    CHECK(crossed.low == 70);
    CHECK(crossed.mid == 71);
    CHECK(crossed.high == 72);
    const MeasureThresholds extreme = constrained(MeasureThresholds{-5, 200, 300});
    CHECK(extreme.low == 1);
    CHECK(extreme.mid == 98);
    CHECK(extreme.high == 99);
}

TEST_CASE("an edge between two samples cannot be timed")
{
    const std::vector<float> v = trapezoid(30, 0, 100, 0, 100, 0.0f, 1.0f);
    const Measurements m = measure(v, 1e-6);
    CHECK(m.rise_unresolved);
    CHECK(m.fall_unresolved);
    CHECK(m.unresolved(Parameter::RiseTime));
    CHECK(m.unresolved(Parameter::FallTime));
    CHECK_FALSE(m.unresolved(Parameter::Period));
    CHECK(m.resolution_s == Approx(2e-6));
    CHECK(m.rise_time < m.resolution_s);
}

TEST_CASE("the period comes from falling edges when only those repeat")
{
    SECTION("two periods of a sine, starting on a rising edge")
    {
        const Measurements m = measure(sine(1000, 500, 1), 1e-6);
        CHECK(m.period == Approx(500e-6).epsilon(0.002));
        CHECK(m.duty == Approx(50).margin(1));
    }
    SECTION("the duty cycle stays that of the high part")
    {
        std::vector<float> v;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 100; ++i)
                v.push_back(i < 25 ? 1.0f : 0.0f);
        const Measurements m = measure(v, 1e-6);
        CHECK(m.period == Approx(100e-6));
        CHECK(m.duty == Approx(25));
    }
}

TEST_CASE("edges cut off by the ends of the samples do not count")
{
    const auto ramp = [](std::vector<float>& v, int from, int to) {
        for (int i = from; i != to; i += to > from ? 1 : -1)
            v.push_back(static_cast<float>(i) * 0.1f);
    };
    SECTION("the record starts in the middle of a rising edge")
    {
        std::vector<float> v;
        ramp(v, 5, 10);
        v.resize(v.size() + 100, 1.0f);
        ramp(v, 9, 0);
        v.resize(v.size() + 100, 0.0f);
        const Measurements m = measure(v, 1e-6);
        CHECK(std::isnan(m.rise_time));
        CHECK(m.fall_time == Approx(8e-6).epsilon(0.03));
        CHECK(std::isnan(m.frequency));
    }
    SECTION("the record ends in the middle of a rising edge")
    {
        std::vector<float> v(100, 1.0f);
        ramp(v, 9, 0);
        v.resize(v.size() + 100, 0.0f);
        ramp(v, 1, 8);
        const Measurements m = measure(v, 1e-6);
        CHECK(std::isnan(m.rise_time));
        CHECK(m.fall_time == Approx(8e-6).epsilon(0.03));
    }
}

TEST_CASE("one pulse has widths but no period")
{
    std::vector<float> v(200, 0.0f);
    std::fill(v.begin() + 50, v.begin() + 110, 1.0f);
    const Measurements m = measure(v, 1e-6);
    CHECK(std::isnan(m.period));
    CHECK(std::isnan(m.duty));
    CHECK(m.positive_width == Approx(60e-6).epsilon(0.01));
    CHECK(std::isnan(m.negative_width));
}

TEST_CASE("a small noisy pulse keeps its levels")
{
    // 40 lsb of swing with ±1 lsb of noise, as a trace on a coarse scale.
    const float lsb = 0.01f;
    std::vector<float> v;
    for (int c = 0; c < 40; ++c)
        for (int i = 0; i < 100; ++i) {
            const float level = i < 50 ? 40 * lsb : 0.0f;
            v.push_back(level + static_cast<float>(i % 3 - 1) * lsb);
        }
    const Measurements m = measure(v, 1e-6, 4 * lsb);
    CHECK(m.top == Approx(0.40).margin(0.005));
    CHECK(m.base == Approx(0.0).margin(0.005));
    CHECK(m.overshoot < 5);
}

TEST_CASE("delay and phase between two traces")
{
    struct Trace {
        std::vector<float> samples;
        Edges edges;
        Measurements m;
        explicit Trace(std::vector<float> v) : samples(std::move(v)) { m = measure(samples, 1e-6, 0, {}, &edges); }
        PairInput input() const { return {samples, m, edges}; }
    };
    const auto between = [](const Trace& a, const Trace& b) { return measure_pair(a.input(), b.input(), 1e-6); };

    SECTION("B follows A by a quarter period")
    {
        const PairMeasurements p = between(Trace(sine(10'000, 100, 1)), Trace(sine(10'000, 100, 1, -std::numbers::pi / 2)));
        CHECK(p.delay == Approx(25e-6).epsilon(0.001));
        CHECK(p.phase == Approx(90).margin(0.05));
        CHECK(p.value(Parameter::Delay) == p.delay);
        CHECK(p.value(Parameter::Phase) == p.phase);
    }
    SECTION("A has only one rising edge")
    {
        const PairMeasurements p = between(Trace(sine(1000, 500, 1)), Trace(sine(1000, 500, 1, -std::numbers::pi / 2)));
        CHECK(p.delay == Approx(125e-6).epsilon(0.002));
        CHECK(p.phase == Approx(90).margin(0.2));
    }
    SECTION("B leads A")
    {
        const PairMeasurements p = between(Trace(sine(10'000, 100, 1)), Trace(sine(10'000, 100, 2, std::numbers::pi / 3)));
        CHECK(p.phase == Approx(-60).margin(0.05));
        CHECK(p.delay == Approx(-100e-6 / 6).epsilon(0.001));
    }
    SECTION("half a period is half a period")
    {
        const PairMeasurements p = between(Trace(sine(10'000, 100, 1)), Trace(sine(10'000, 100, 1, std::numbers::pi)));
        CHECK(std::abs(p.phase) == Approx(180).margin(0.05));
        CHECK(std::abs(p.delay) == Approx(50e-6).epsilon(0.001));
    }
    SECTION("a slow sine in coarse ADC steps, with an offset between the channels")
    {
        // 25 steps from the middle to a peak, and one channel 0.3 steps higher. The same edge comes 2 samples earlier
        // on B than on A in steps like these, whatever the true delay is.
        const auto coarse = [](std::vector<float> v, float offset) {
            for (float& x : v)
                x = std::round((x + offset) * 25) / 25;
            return v;
        };
        const Trace a(coarse(sine(20'000, 1000, 1), 0));
        const Trace b(coarse(sine(20'000, 1000, 1), 0.012f));
        CHECK_FALSE(a.m.pulse);
        const PairMeasurements same = between(a, b);
        CHECK(std::abs(same.delay) < 0.1e-6);
        CHECK(std::abs(same.phase) < 0.04);

        const Trace later(coarse(sine(20'000, 1000, 1, -2 * std::numbers::pi * 0.02), 0.012f));
        CHECK(between(a, later).delay == Approx(20e-6).epsilon(0.01));
    }
    SECTION("pulses are timed by their edges, whatever else differs")
    {
        const std::vector<float> va = trapezoid(30, 3, 300, 3, 300, 0.0f, 1.0f);
        std::vector<float> vb(7, 0.0f);
        vb.insert(vb.end(), va.begin(), va.end() - 7);
        // A slow tail on the tops of B moves its fundamental, but not its edges.
        for (std::size_t i = 310; i < vb.size(); i += 606)
            for (std::size_t k = 0; k < 100 && i + k < vb.size(); ++k)
                vb[i + k] -= 0.1f * std::exp(-static_cast<float>(k) / 30);
        const Trace a(va), b(vb);
        CHECK(a.m.pulse);
        CHECK(b.m.pulse);
        const PairMeasurements p = between(a, b);
        CHECK(p.delay == Approx(7e-6).margin(0.2e-6));
        CHECK(p.phase == Approx(7.0 / 606 * 360).margin(0.15));
    }
    SECTION("an edge that one trace lacks at the end of the record is no delay")
    {
        std::vector<float> va = trapezoid(6, 3, 300, 3, 300, 0.0f, 1.0f);
        std::vector<float> vb = va;
        // B starts high, so it has no rising edge where A has its first.
        std::fill(vb.begin(), vb.begin() + 320, 1.0f);
        const Trace a(va), b(vb);
        REQUIRE(a.edges.rising.size() == b.edges.rising.size() + 1);
        const PairMeasurements p = between(a, b);
        CHECK(p.delay == Approx(0).margin(1e-9));
        CHECK(p.phase == Approx(0).margin(1e-3));
    }
    SECTION("a pulse against a sine of its frequency goes by the fundamentals")
    {
        const std::vector<float> square = trapezoid(30, 0, 50, 0, 50, -1.0f, 1.0f);
        // The first half of the square's period is low, so its fundamental is a sine turned over.
        const PairMeasurements p = between(Trace(square), Trace(sine(3000, 100, 1, std::numbers::pi)));
        CHECK(std::abs(p.phase) < 4);
    }
    SECTION("different frequencies have no delay")
    {
        const PairMeasurements p = between(Trace(sine(10'000, 100, 1)), Trace(sine(10'000, 90, 1)));
        CHECK(std::isnan(p.delay));
        CHECK(std::isnan(p.phase));
    }
    SECTION("single edges have a delay but no phase")
    {
        std::vector<float> va(1000, 0.0f), vb(1000, 0.0f);
        std::fill(va.begin() + 300, va.end(), 1.0f);
        std::fill(vb.begin() + 340, vb.end(), 1.0f);
        const PairMeasurements p = between(Trace(va), Trace(vb));
        CHECK(p.delay == Approx(40e-6));
        CHECK(std::isnan(p.phase));
    }
    SECTION("a trace without edges gives nothing")
    {
        const PairMeasurements p = between(Trace(std::vector<float>(1000, 0.5f)), Trace(sine(10'000, 100, 1)));
        CHECK(std::isnan(p.delay));
    }
}

TEST_CASE("statistics")
{
    Statistics s;
    CHECK(std::isnan(s.mean()));
    for (double x : {1.0, 2.0, 3.0, 4.0, std::nan("")})
        s.add(x);
    CHECK(s.count() == 4);
    CHECK(s.min() == 1);
    CHECK(s.max() == 4);
    CHECK(s.mean() == Approx(2.5));
    CHECK(s.stddev() == Approx(std::sqrt(5.0 / 3)));
    s.reset();
    CHECK(s.count() == 0);
}
