// SPDX-License-Identifier: GPL-3.0-or-later
// Generator settings, output model, sweep, and the simulator's generator on CH2.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "starosc/engine.hpp"
#include "starosc/generator.hpp"
#include "starosc/source.hpp"

using namespace starosc;
using Catch::Approx;

TEST_CASE("generator settings are kept inside the limits of the waveform")
{
    GeneratorSettings g;
    g.waveform = Waveform::Square;
    g.frequency_hz = 5e6;  // square waves stop at 2 MHz
    g.amplitude_vpp = 0.01;
    g.offset_v = -12;
    g.duty_percent = 0;
    g.sweep.stop_hz = 30e6;
    const GeneratorSettings c = constrained(g);
    CHECK(c.frequency_hz == 2e6);
    CHECK(c.amplitude_vpp == Approx(0.079));
    CHECK(c.offset_v == Approx(-4.79));
    CHECK(c.duty_percent == 1);
    CHECK(c.sweep.stop_hz == 2e6);

    g.waveform = Waveform::Sine;
    g.frequency_hz = 1234.4;
    CHECK(constrained(g).frequency_hz == 1234);
    g.frequency_hz = 5e6;
    CHECK(constrained(g).frequency_hz == 5e6);
}

TEST_CASE("generator levels follow the waveform and clip at the output limits")
{
    GeneratorSettings g;
    g.amplitude_vpp = 3;
    g.offset_v = 0.5;
    GeneratorLevels l = generator_levels(g);
    CHECK(l.max == Approx(2.0).margin(1e-4));
    CHECK(l.min == Approx(-1.0).margin(1e-4));
    CHECK(l.rms == Approx(std::sqrt(0.25 + 1.125)).epsilon(1e-4));
    CHECK_FALSE(l.clipped);

    g.waveform = Waveform::Square;
    g.duty_percent = 25;
    g.offset_v = 0;
    CHECK(generator_output(g, 0.1e-3) == Approx(1.5));   // high for the first quarter
    CHECK(generator_output(g, 0.5e-3) == Approx(-1.5));
    CHECK(generator_levels(g).rms == Approx(1.5));

    g.waveform = Waveform::Triangle;
    CHECK(generator_output(g, 0.25e-3) == Approx(1.5));
    CHECK(generator_output(g, 0.75e-3) == Approx(-1.5));
    CHECK(generator_levels(g).rms == Approx(1.5 / std::sqrt(3.0)).epsilon(1e-3));

    g.waveform = Waveform::RampUp;
    CHECK(generator_output(g, 0.0) == Approx(-1.5));
    g.waveform = Waveform::RampDown;
    CHECK(generator_output(g, 0.0) == Approx(1.5));

    g.waveform = Waveform::Sine;
    g.amplitude_vpp = 8;
    g.offset_v = 2;
    l = generator_levels(g);
    CHECK(l.clipped);
    CHECK(l.max == Approx(4.86));
}

TEST_CASE("sweep runs from start to stop and starts over")
{
    SweepSettings s{true, 100, 10'000, 2.0, true};
    CHECK(sweep_frequency(s, 0) == Approx(100));
    CHECK(sweep_frequency(s, 1.0) == Approx(1000));  // half way on a log scale
    CHECK(sweep_frequency(s, 2.5) == Approx(sweep_frequency(s, 0.5)));
    s.logarithmic = false;
    CHECK(sweep_frequency(s, 1.0) == Approx(5050));
}

TEST_CASE("the simulator shows the generator on CH2 while its output is on")
{
    auto sim = make_simulator();
    REQUIRE(sim->has_generator());
    GeneratorSettings g;
    g.output = true;
    g.waveform = Waveform::Square;
    g.frequency_hz = 2000;
    g.amplitude_vpp = 2;
    g.offset_v = 1;
    sim->set_generator(g);

    ScopeSettings s;
    s.channel[0].enabled = false;
    s.channel[1] = {true, 0.5, -2.0, Coupling::DC, 1.0};
    s.trigger.source = 1;
    s.trigger.level_v = 1.0;
    s.timebase = {500e-6, MemoryDepth::Small};
    Engine engine(*sim);
    auto frame = engine.acquire(s);
    REQUIRE(frame);
    CHECK(frame->measurements[1].frequency == Approx(2000).epsilon(0.01));
    CHECK(frame->measurements[1].max == Approx(2.0).margin(0.05));
    CHECK(frame->measurements[1].min == Approx(0.0).margin(0.05));

    sim->park_generator();
    frame = engine.acquire(s);
    REQUIRE(frame);
    CHECK(frame->measurements[1].frequency == Approx(1500).epsilon(0.01));  // its own sine again
}

TEST_CASE("the simulator's generator follows the model it acts as")
{
    const auto x = make_simulator();
    CHECK(x->has_generator());
    CHECK(x->has_generator_levels());
    const auto b = make_simulator("ISDS205B");
    CHECK(b->has_generator());
    CHECK_FALSE(b->has_generator_levels());  // a generator without amplitude and offset
    const auto plain = make_simulator("ISDS205");
    CHECK_FALSE(plain->has_generator());
    CHECK_FALSE(plain->has_generator_levels());
}
