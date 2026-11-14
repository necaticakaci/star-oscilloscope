// SPDX-License-Identifier: GPL-3.0-or-later
// USB reliability check on the device; nothing needs to be connected to the inputs.
//
//   star-usb-stress [seconds]
//
// First commands at random intervals, counting the replies that go missing, then acquisition as the app runs it with
// pauses as Stop and Run would make. A reply lost on the way used to stop acquisition with "timed out waiting for the
// device" (docs/measurements.md §10). Run on Linux under usbipd and on a direct connection.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <random>
#include <thread>

#include "isds/device.hpp"
#include "starosc/engine.hpp"
#include "starosc/source.hpp"

using Clock = std::chrono::steady_clock;

namespace {

double since(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

void commands(double seconds)
{
    auto device = isds::Device::open();
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> wait_us(20'000, 130'000);
    long answered = 0, failed = 0;
    double slowest_ms = 0;
    bool fast = false;
    const auto start = Clock::now();
    while (since(start) < seconds) {
        std::this_thread::sleep_for(std::chrono::microseconds(wait_us(rng)));
        const auto t0 = Clock::now();
        try {
            device->set_sample_rate(fast ? 16'000'000 : 1'000'000);
            ++answered;
            slowest_ms = std::max(slowest_ms, since(t0) * 1e3);
        } catch (const std::exception& e) {
            ++failed;
            std::printf("  %.1f s: %s\n", since(start), e.what());
        }
        fast = !fast;
    }
    std::printf("commands: %ld answered, %ld failed, slowest answer %.1f ms\n", answered, failed, slowest_ms);
}

void frames(double seconds)
{
    auto source = starosc::open_device();
    starosc::Engine engine(*source);
    starosc::ScopeSettings s;
    s.channel[0].volts_per_div = s.channel[1].volts_per_div = 0.5;
    s.timebase = {200e-6, starosc::MemoryDepth::Small};
    long shown = 0;
    const auto start = Clock::now();
    for (int round = 0; since(start) < seconds; ++round) {
        for (int i = 0; i < 60 + round * 20; ++i) {
            try {
                (void)engine.acquire(s);
                ++shown;
            } catch (const std::exception& e) {
                std::printf("frames: failed after %ld frames, %.1f s: %s\n", shown, since(start), e.what());
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100 + round * 500));
    }
    std::printf("frames: no failure in %ld frames\n", shown);
}

}  // namespace

int main(int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof(argv[1]) : 120;
    commands(seconds);
    frames(seconds);
}
