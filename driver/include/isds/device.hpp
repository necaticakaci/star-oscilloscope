// SPDX-License-Identifier: GPL-3.0-or-later
// USB connection to an ISDS205X oscilloscope (libusb; WinUSB on Windows).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "isds/calibration.hpp"
#include "isds/generator.hpp"

namespace isds {

enum class Coupling { DC = 0, AC = 1 };

// Generator settings in wire units (see isds/generator.hpp for the encodings).
struct GeneratorSettings {
    Waveform waveform = Waveform::Sine;
    std::uint32_t frequency_hz = 1000;
    int duty_percent = 50;
    std::uint32_t amplitude = amplitude_code(1597);  // command 47 code; vdso's default zoom
    std::uint32_t offset = offset_code(2048);        // command 48 code; mid-scale

    bool operator==(const GeneratorSettings&) const = default;
};

struct CaptureData {
    std::uint32_t sample_rate_hz = 0;
    std::array<Stage, 2> stage{};
    std::array<std::vector<std::uint8_t>, 2> raw;  // per channel, settle samples already removed
    // When each read of the data endpoint completed, in ms after the capture command was sent.
    std::vector<float> arrival_ms;
};

// How capture() reads the data endpoint: `in_flight` transfers of `chunk_bytes` each kept queued, or one
// transfer for the whole capture (`in_flight` 0).
//
// The device streams the samples while it acquires them, through a FIFO of about 1024 samples, and drops
// what the host does not take in time. Small reads kept queued take them in time; one large read lost
// samples every few hundred µs at 8 MSa/s over usbip (docs/measurements.md §10). The default was the best
// of the sizes and depths measured there. On Windows the driver picks its own below 16 MSa/s, unless one is set.
struct ReadStrategy {
    std::size_t chunk_bytes = 8192;
    int in_flight = 32;
};

class Device {
public:
    // Opens the first ISDS205 series oscilloscope found, performs the handshake and the vdso start-up sequence, then
    // one short capture at the lowest rate that makes the top rate's first kContiguousSamplesAtTopRate samples
    // contiguous. Throws isds::Error if no device is found, if it is another model, or if it does not answer as
    // expected.
    static std::unique_ptr<Device> open();

    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    const Calibration& calibration() const;
    const Descriptor& descriptor() const { return calibration().descriptor(); }

    // User correction (Calibration::set_correction). Applies to captures and to the next set_trigger.
    void set_correction(Stage stage, Channel ch, Correction correction);
    void clear_corrections();

    // Must be one of descriptor().sample_rates.
    void set_sample_rate(std::uint32_t hz);
    std::uint32_t sample_rate() const;

    void set_stage(Channel ch, Stage stage);
    Stage stage(Channel ch) const;

    void set_coupling(Channel ch, Coupling coupling);

    // Programs command 13. Triggering itself is done in software; vdso sends this as well.
    void set_trigger(Channel source, double level_mv, double min_mv, double max_mv);

    // Captures `samples` valid samples per channel (at most max_capture_samples()).
    CaptureData capture(std::size_t samples);
    std::size_t max_capture_samples() const;
    void set_read_strategy(const ReadStrategy& strategy);
    // Longest capture at `hz` whose samples are all contiguous: 1020 at the top rate, otherwise the whole memory.
    std::size_t max_contiguous_samples(std::uint32_t hz) const;
    // Longest capture at `hz` that rarely loses samples: 64K at 16 MSa/s, where the host has to keep up with 32 MB/s;
    // otherwise max_capture_samples().
    std::size_t reliable_samples(std::uint32_t hz) const;
    // Length of the blocks after the first run at `hz` (256 at the top rate), or 0 when captures are contiguous.
    std::size_t block_samples(std::uint32_t hz) const;

    bool has_generator() const;
    bool has_generator_level_control() const;
    // While the output is on, only the fields that changed are sent. Switching the output on sends
    // a reset and every setting first, as vdso does.
    void set_generator(const GeneratorSettings& settings);
    const GeneratorSettings& generator() const;
    void set_generator_output(bool on);
    bool generator_output() const;

    bool connected() const;

private:
    struct Impl;
    explicit Device(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace isds
