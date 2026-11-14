// SPDX-License-Identifier: GPL-3.0-or-later
// Where samples come from: the ISDS205X over USB, or the built-in simulator.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "starosc/calibration.hpp"
#include "starosc/generator.hpp"
#include "starosc/settings.hpp"
#include "starosc/timebase.hpp"

namespace starosc {

struct ChannelRequest {
    bool enabled = true;
    VoltageRange range;  // input voltage range that must not clip (volts at the BNC)
    Coupling coupling = Coupling::DC;
};

struct AcquisitionRequest {
    std::uint32_t sample_rate_hz = 0;
    std::size_t samples = 0;  // per channel
    std::array<ChannelRequest, kChannels> channel{};
    int trigger_source = 0;
    double trigger_level_v = 0;  // at the BNC
};

struct ChannelData {
    std::vector<float> volts;  // at the BNC; empty for a disabled channel
    double lsb_v = 0;          // resolution of the selected gain stage
    VoltageRange span;         // voltage at the lowest and highest ADC code
    bool clipped = false;      // any sample at the ADC limits
    // The input is on the lower coarse gain, its most sensitive stages. That raises the generator's output
    // (docs/measurements.md §5). Known for a disabled input too: it keeps its last stage.
    bool sensitive = false;
};

struct Acquisition {
    std::uint32_t sample_rate_hz = 0;
    std::array<ChannelData, kChannels> channel{};
};

class Source {
public:
    virtual ~Source() = default;
    // The model, or "Simulator", as the status bar shows it; a device's serial number is in its calibration.
    virtual std::string name() const = 0;
    virtual bool simulated() const = 0;
    virtual std::vector<std::uint32_t> sample_rates() const = 0;  // ascending
    virtual std::size_t max_samples() const = 0;
    // How the samples of a capture at `rate` follow each other. At 48 MSa/s the ISDS205X delivers a first run of
    // 1020 contiguous samples, then 256-sample blocks with gaps between them.
    virtual CaptureLayout layout(std::uint32_t /*rate*/) const { return {}; }
    // Longest capture at `rate` whose samples are all contiguous.
    std::size_t max_contiguous_samples(std::uint32_t rate) const
    {
        const CaptureLayout l = layout(rate);
        return l.contiguous() ? max_samples() : l.first_block;
    }
    // Longest capture at `rate` that rarely loses samples. On the ISDS205X 64K at 16 MSa/s, where the host has to
    // keep up with 32 MB/s.
    virtual std::size_t reliable_samples(std::uint32_t /*rate*/) const { return max_samples(); }
    RateCapacity capacity(std::uint32_t rate) const { return {rate, max_samples(), layout(rate), reliable_samples(rate)}; }
    // Blocks until the capture is complete. Throws std::exception on device errors.
    virtual Acquisition acquire(const AcquisitionRequest& request) = 0;

    // The device's stages with factory corrections, identified by model and device ID.
    virtual UserCalibration factory_calibration() const = 0;
    // Applies the corrections for the stages it shares with factory_calibration(); other stages
    // keep the factory conversion. Returns how many stages matched.
    virtual int set_calibration(const UserCalibration& calibration) = 0;
    // Measures every stage of factory_calibration() in factory volts, inputs shorted, DC coupling.
    // The next acquire() restores the requested settings.
    virtual std::vector<StageZero> measure_zero() = 0;

    // The DDS generator; sources without one keep these defaults.
    virtual bool has_generator() const { return false; }
    // Whether the generator's amplitude and offset can be set. The ISDS205X has them; the ISDS205B's generator has
    // neither, and its output cannot be parked at 0 V.
    virtual bool has_generator_levels() const { return false; }
    // Applies `settings` except the sweep, which the caller runs by changing the frequency. Values are
    // constrained first. Switching the output off parks it, see park_generator().
    virtual void set_generator(const GeneratorSettings&) {}
    // Brings the output to about 0 V and switches it off. The device itself keeps the offset on the
    // output when switched off, and sits at about +4.9 V after power-up.
    virtual void park_generator() {}
};

// Signal simulator that quantizes through the calibration of a real ISDS205X. It acts as `model` for what depends on the
// model, the generator: an "ISDS205B" has one without levels, a plain "ISDS205" none.
inline constexpr std::string_view kSimulatedModel = "ISDS205X(1.1)";
std::unique_ptr<Source> make_simulator(std::string_view model = kSimulatedModel);

// The bench of a gain calibration: the generator on both inputs. The stages read off by known factors, and the output
// follows the inputs' coarse gain as on the real device (docs/measurements.md §5). No waiting, for tests.
struct SimulatedBench {
    std::vector<std::array<double, kChannels>> stage_gain;  // finest stage first
    double output_gain_per_fine_input = 1;                  // of the amplitude, for each input on the lower coarse gain
    double output_shift_per_fine_input = 0;                 // volts of its level, likewise
};
std::unique_ptr<Source> make_simulator(const SimulatedBench& bench);
// What a multimeter on DC reads on that bench now, as the last capture left the inputs. 0 for other sources.
double simulated_bench_output(const Source& source);

// Opens the first ISDS205 series oscilloscope found. Throws isds::Error if there is none, or if it is another model.
std::unique_ptr<Source> open_device();

}  // namespace starosc
