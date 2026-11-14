// SPDX-License-Identifier: GPL-3.0-or-later
// Math trace and reference waveforms.
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "starosc/engine.hpp"
#include "starosc/math.hpp"
#include "starosc/reference.hpp"

using namespace starosc;
using Catch::Approx;

namespace {
ScopeSettings with_math(MathOperation op)
{
    ScopeSettings s;
    s.channel[0] = {true, 1.0, 1.0, Coupling::DC, 1.0};
    s.channel[1] = {true, 0.5, -2.0, Coupling::DC, 1.0};
    s.timebase = {500e-6, MemoryDepth::Small};
    s.trigger.level_v = 1.0;
    s.math = {true, op, 0, 1, 1.0, 0.0};
    return s;
}
}  // namespace

TEST_CASE("math operations sample by sample")
{
    const std::vector<float> a{1, 2, 3, 4};
    const std::vector<float> b{2, 0.0005f, -1, 0.5f};
    CHECK(compute_math(MathOperation::Add, a, b, 0.001)[2] == Approx(2));
    CHECK(compute_math(MathOperation::Subtract, a, b, 0.001)[0] == Approx(-1));
    CHECK(compute_math(MathOperation::Multiply, a, b, 0.001)[3] == Approx(2));
    const auto ratio = compute_math(MathOperation::Divide, a, b, 0.001);
    CHECK(ratio[0] == Approx(0.5));
    CHECK(std::isnan(ratio[1]));  // the divisor is noise around zero
    CHECK(ratio[2] == Approx(-3));
    CHECK(compute_math(MathOperation::Add, a, std::vector<float>{1, 1}, 0).size() == 2);
    CHECK(math_unit(MathOperation::Multiply) == "V²");
    CHECK(math_unit(MathOperation::Divide) == "U");
}

TEST_CASE("the math trace follows the window of the channels")
{
    auto sim = make_simulator();  // CH1: 0..2 V square at 1 kHz; CH2: 0.7 V sine at 1.5 kHz
    Engine engine(*sim);
    auto frame = engine.acquire(with_math(MathOperation::Subtract));
    REQUIRE(frame);
    REQUIRE(frame->math.size() == frame->volts[0].size());
    for (std::size_t i = 0; i < frame->math.size(); i += 97)
        CHECK(frame->math[i] == Approx(frame->volts[0][i] - frame->volts[1][i]).margin(1e-5));
    CHECK(frame->math_measurements.max == Approx(2.0 + 0.7).margin(0.15));
    CHECK(frame->math_measurements.min == Approx(0.0 - 0.7).margin(0.15));

    // A hidden channel is still captured when the math needs it.
    ScopeSettings hidden = with_math(MathOperation::Add);
    hidden.channel[1].enabled = false;
    frame = engine.acquire(hidden);
    REQUIRE(frame);
    CHECK(frame->volts[1].empty());
    CHECK(frame->math.size() == frame->volts[0].size());
    CHECK(frame->math_measurements.peak_to_peak == Approx(2.0 + 1.4).margin(0.2));

    ScopeSettings off = with_math(MathOperation::Add);
    off.math.enabled = false;
    frame = engine.acquire(off);
    REQUIRE(frame);
    CHECK(frame->math.empty());
}

TEST_CASE("references keep a trace with its time, scale and position")
{
    auto sim = make_simulator();
    Engine engine(*sim);
    const auto frame = engine.acquire(with_math(MathOperation::Multiply));
    REQUIRE(frame);

    const auto ch2 = make_reference(*frame, 1);
    REQUIRE(ch2);
    CHECK(ch2->source == "CH2");
    CHECK(ch2->unit == "V");
    CHECK(ch2->units_per_div == 0.5);
    CHECK(ch2->position_div == -2.0);
    CHECK(ch2->samples == frame->volts[1]);
    // The trigger point sits at the pre-trigger fraction of the screen.
    CHECK(ch2->start_s == Approx(screen_start_s(frame->settings)).margin(2 * frame->sample_interval_s));

    const auto math = make_reference(*frame, kMathSource);
    REQUIRE(math);
    CHECK(math->source == "Math");
    CHECK(math->unit == "V²");

    ScopeSettings no_math = with_math(MathOperation::Add);
    no_math.math.enabled = false;
    const auto plain = engine.acquire(no_math);
    REQUIRE(plain);
    CHECK_FALSE(make_reference(*plain, kMathSource));
}

TEST_CASE("references round-trip through CSV")
{
    Reference ref;
    ref.source = "CH1";
    ref.unit = "V";
    ref.samples = {0.5f, -0.25f, std::nanf(""), 1.75f};
    ref.sample_interval_s = 1e-6;
    ref.start_s = -2e-6;
    ref.units_per_div = 0.2;
    ref.position_div = 1.5;
    std::string error;
    const auto back = reference_from_csv(reference_to_csv(ref), &error);
    REQUIRE(back);
    CHECK(back->source == "CH1");
    CHECK(back->units_per_div == Approx(0.2));
    CHECK(back->position_div == Approx(1.5));
    CHECK(back->sample_interval_s == Approx(1e-6));
    CHECK(back->start_s == Approx(-2e-6));
    REQUIRE(back->samples.size() == 4);
    CHECK(back->samples[1] == Approx(-0.25));
    CHECK(std::isnan(back->samples[2]));

    CHECK_FALSE(reference_from_csv("time_s,value\n0,1\n1e-6,2\n", &error));  // not one of ours
    CHECK(error == "not a Star Oscilloscope reference");
    CHECK_FALSE(reference_from_csv("# star-oscilloscope reference\n0,1\nx,2\n", &error));
    CHECK(error.starts_with("line 3"));
}
