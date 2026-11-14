// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/timebase.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace starosc {
namespace {

// Small captures are cheap; capturing at least this much makes triggering on sparse events likelier.
constexpr std::size_t kMinCaptureSamples = 16 * 1024;
// In a capture of blocks, positions the trigger may take inside a block beyond the window.
constexpr std::size_t kMinBlockSlack = 4;

// How a capture at one rate holds the window with room to search for a trigger.
enum class Fit {
    None,
    Contiguous,  // a contiguous capture of at least two windows
    Blocks,      // a capture of blocks, the window inside one of them
    FirstRun,    // a capture of blocks cut to its first run, which holds two windows: then it is contiguous
};

// The longest capture to plan at a rate: its memory, or less where longer captures lose samples.
std::size_t capture_limit(const RateCapacity& r)
{
    return r.reliable_samples == 0 ? r.max_samples : std::min(r.reliable_samples, r.max_samples);
}

Fit fit(const RateCapacity& r, double window)
{
    if (r.layout.contiguous())
        return 2 * window <= static_cast<double>(capture_limit(r)) ? Fit::Contiguous : Fit::None;
    if (window + 1 + kMinBlockSlack <= static_cast<double>(r.layout.shortest_run(r.max_samples)))
        return Fit::Blocks;
    if (2 * window <= static_cast<double>(std::min(r.layout.first_block, r.max_samples)))
        return Fit::FirstRun;
    return Fit::None;
}

}  // namespace

std::size_t CaptureLayout::run_begin(std::size_t index) const
{
    if (contiguous() || index < first_block)
        return 0;
    if (block == 0)
        return first_block;
    return first_block + (index - first_block) / block * block;
}

std::size_t CaptureLayout::run_end(std::size_t index, std::size_t size) const
{
    if (contiguous())
        return size;
    if (index < first_block)
        return std::min(first_block, size);
    if (block == 0)
        return size;
    return std::min(run_begin(index) + block, size);
}

std::size_t CaptureLayout::shortest_run(std::size_t size) const
{
    if (contiguous())
        return size;
    return std::min({first_block, block == 0 ? first_block : block, size});
}

std::size_t max_window_samples(MemoryDepth depth)
{
    switch (depth) {
    case MemoryDepth::Small: return 8 * 1024;
    case MemoryDepth::Medium: return 64 * 1024;
    case MemoryDepth::Large: return 512 * 1024;
    }
    return 8 * 1024;
}

AcquisitionPlan plan_acquisition(const TimebaseSettings& timebase, double pre_trigger, std::span<const RateCapacity> rates)
{
    if (rates.empty() || std::ranges::any_of(rates, [](const RateCapacity& r) { return r.max_samples < 2; }))
        throw std::invalid_argument("no sample rates or capture memory");

    const double window_seconds = kDivisionsX * timebase.seconds_per_div;

    std::size_t chosen = 0;
    Fit how = Fit::None;
    for (std::size_t i = 0; i < rates.size(); ++i) {
        const RateCapacity& r = rates[i];
        const double window = window_seconds * r.rate_hz;
        const bool fits_memory =
            window <= static_cast<double>(std::min(max_window_samples(timebase.memory), r.max_samples));
        const Fit f = fit(r, window);
        if (fits_memory && f != Fit::None) {
            chosen = i;
            how = f;
        }
    }
    const RateCapacity& r = rates[chosen];
    const std::size_t first_run = std::min(r.layout.first_block, r.max_samples);
    std::size_t longest_window = capture_limit(r);
    if (how == Fit::FirstRun)
        longest_window = first_run / 2;
    else if (!r.layout.contiguous())
        longest_window = r.layout.shortest_run(r.max_samples) - 1;

    AcquisitionPlan plan;
    plan.sample_rate_hz = r.rate_hz;
    plan.layout = r.layout;
    const auto window = static_cast<std::size_t>(std::llround(window_seconds * plan.sample_rate_hz));
    plan.window_samples = std::clamp<std::size_t>(window, 2, longest_window);
    plan.capture_samples = how == Fit::FirstRun
                               ? first_run
                               : std::min(capture_limit(r), std::max(2 * plan.window_samples, kMinCaptureSamples));
    plan.pre_samples = static_cast<std::size_t>(
        std::llround(std::clamp(pre_trigger, 0.0, 1.0) * static_cast<double>(plan.window_samples)));
    return plan;
}

}  // namespace starosc
