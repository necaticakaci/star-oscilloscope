// SPDX-License-Identifier: GPL-3.0-or-later
#include "starosc/gain_transfer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>
#include <stdexcept>
#include <thread>

namespace starosc {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr std::uint32_t kRate = 1'000'000;
constexpr std::size_t kSamples = 50'000;  // 50 periods
constexpr std::size_t kDroppedSamples = 4096;
constexpr double kFrequency = 1000;

constexpr double kFill = 0.7;     // of a stage's span, for the sine
constexpr double kRoom = 1.3;     // the tracking channel's span must hold this much more, as the output grows on G 0x109
constexpr double kShrink = 0.75;  // when the sine clips after all
constexpr int kAimSteps = 3;
constexpr int kFitAttempts = 6;
constexpr int kRepeats = 2;
constexpr int kLevelCaptures = 8;
constexpr double kLevelFraction = 0.53;  // of half the span, from its middle
constexpr double kLevelSineFill = 0.33;  // of the span: together about 85 % of the way to each end

constexpr auto kSettle = std::chrono::milliseconds(120);
constexpr auto kRelaySettle = std::chrono::milliseconds(150);

struct Fit {
    double a = 0, b = 0, c = 0;
    double phase() const { return std::atan2(-b, a); }
};

// a·cos + b·sin + c at frequency f, over samples [from, to).
Fit fit_at(const std::vector<float>& v, std::size_t from, std::size_t to, double f, double rate)
{
    double s[3][4] = {};
    for (std::size_t i = from; i < to; ++i) {
        const double ph = 2 * kPi * f * static_cast<double>(i) / rate;
        const double x[3] = {std::cos(ph), std::sin(ph), 1};
        for (int r = 0; r < 3; ++r) {
            s[r][3] += x[r] * v[i];
            for (int c = 0; c < 3; ++c)
                s[r][c] += x[r] * x[c];
        }
    }
    for (int k = 0; k < 3; ++k)
        for (int r = 0; r < 3; ++r)
            if (r != k) {
                const double m = s[r][k] / s[k][k];
                for (int c = k; c < 4; ++c)
                    s[r][c] -= m * s[k][c];
            }
    return {s[0][3] / s[0][0], s[1][3] / s[1][1], s[2][3] / s[2][2]};
}

// Reads the two inputs at given stages, waiting for what a change of stage or generator setting takes.
class Bench {
public:
    explicit Bench(Source& source) : source_(source), stages_(source.factory_calibration().stages) {}

    int stages() const { return static_cast<int>(stages_.size()); }
    const std::vector<StageCalibration>& list() const { return stages_; }
    const VoltageRange& span(int stage, int channel) const
    {
        return stages_[static_cast<std::size_t>(stage)].factory_span[static_cast<std::size_t>(channel)];
    }

    void setGenerator(const GeneratorSettings& settings)
    {
        source_.set_generator(settings);
        wait(kSettle);
    }

    std::array<SineReading, kChannels> read(const std::array<int, kChannels>& stage)
    {
        const AcquisitionRequest request = request_at_stages(stages_, stage, kRate, kSamples);
        AcquisitionRequest dropped = request;
        dropped.samples = kDroppedSamples;
        if (stage != current_) {
            for (std::size_t ch = 0; ch < kChannels; ++ch)
                if (current_[ch] < 0 || gainCode(stage[ch]) != gainCode(current_[ch])) {
                    // This sets the relay; give it time before anything is read.
                    source_.acquire(dropped);
                    wait(kRelaySettle);
                    break;
                }
            current_ = stage;
            source_.acquire(dropped);  // the first capture after a change of stage is dropped
        }
        const Acquisition a = source_.acquire(request);
        std::array<SineReading, kChannels> out;
        for (std::size_t ch = 0; ch < kChannels; ++ch)
            out[ch] = read_sine(a.channel[ch], kFrequency, static_cast<double>(a.sample_rate_hz));
        return out;
    }

    // The finest stage of `channel` that holds [low, high].
    int finestHolding(int channel, double low, double high) const
    {
        for (int s = 0; s < stages(); ++s)
            if (span(s, channel).min_v < low && span(s, channel).max_v > high)
                return s;
        return stages() - 1;
    }

private:
    std::uint16_t gainCode(int stage) const { return stages_[static_cast<std::size_t>(stage)].gain_code; }

    void wait(std::chrono::milliseconds time) const
    {
        if (!source_.simulated())
            std::this_thread::sleep_for(time);
    }

    Source& source_;
    std::vector<StageCalibration> stages_;
    std::array<int, kChannels> current_{-1, -1};
};

GeneratorSettings sine(double vpp, double offset)
{
    GeneratorSettings g;
    g.output = true;
    g.waveform = Waveform::Sine;
    g.frequency_hz = kFrequency;
    g.amplitude_vpp = vpp;
    g.offset_v = offset;
    return g;
}

// A sine of about `target` Vpp around `center`, which fits stage `b` of the channel measured and the stage of the
// other one; found with the channel on `a`, its wider neighbour, then corrected on `b`, where the output may differ.
GeneratorSettings aim(Bench& bench, int channel, int a, int b, int tracker, double target, double center)
{
    const auto c = static_cast<std::size_t>(channel);
    const std::size_t d = 1 - c;
    GeneratorSettings g = sine(target, center);
    std::array<int, kChannels> onA{}, onB{};
    onA[c] = a;
    onA[d] = tracker;
    onB[c] = b;
    onB[d] = tracker;
    bench.setGenerator(g);
    for (int step = 0; step < kAimSteps; ++step) {
        const SineReading r = bench.read(onA)[c];
        if (r.vpp <= 0)
            throw std::runtime_error(std::format("no signal on CH{}", channel + 1));
        g.amplitude_vpp *= target / r.vpp;
        g.offset_v += center - r.mean;
        bench.setGenerator(g);
    }
    for (int attempt = 0; attempt < kFitAttempts; ++attempt) {
        const auto r = bench.read(onB);
        if (r[c].clipped || r[d].clipped) {
            g.amplitude_vpp *= kShrink;
        } else {
            g.offset_v += center - r[c].mean;
            if (std::abs(center - r[c].mean) < target / 20)
                return g;
        }
        bench.setGenerator(g);
    }
    throw std::runtime_error(std::format("the signal does not fit stage {} of CH{}", b + 1, channel + 1));
}

}  // namespace

SineReading read_sine(const ChannelData& data, double nominal_hz, double rate_hz)
{
    const std::vector<float>& v = data.volts;
    SineReading out;
    out.clipped = data.clipped;
    if (v.size() < 4)
        return out;

    // The phase in the two halves gives the frequency the sine really has.
    const std::size_t half = v.size() / 2;
    const double first = fit_at(v, 0, half, nominal_hz, rate_hz).phase();
    const double second = fit_at(v, half, v.size(), nominal_hz, rate_hz).phase();
    const double seconds = static_cast<double>(half) / rate_hz;
    const double f = nominal_hz + std::remainder(second - first, 2 * kPi) / (2 * kPi * seconds);

    const Fit fit = fit_at(v, 0, v.size(), f, rate_hz);
    out.vpp = 2 * std::hypot(fit.a, fit.b);
    out.mean = fit.c;
    double sum = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const double ph = 2 * kPi * f * static_cast<double>(i) / rate_hz;
        const double e = v[i] - fit.a * std::cos(ph) - fit.b * std::sin(ph) - fit.c;
        sum += e * e;
    }
    out.residual_rms = std::sqrt(sum / static_cast<double>(v.size()));
    return out;
}

int reference_stage(const std::vector<StageCalibration>& stages) { return std::max(0, static_cast<int>(stages.size()) - 2); }

int check_stage(const std::vector<StageCalibration>& stages)
{
    const int reference = reference_stage(stages);
    for (int s = reference - 1; s >= 0; --s)
        if (stages[static_cast<std::size_t>(s)].gain_code != stages[static_cast<std::size_t>(reference)].gain_code)
            return s;
    return std::max(0, reference - 1);
}

LevelPlan level_plan(const std::vector<StageCalibration>& stages, int stage)
{
    const auto& spans = stages[static_cast<std::size_t>(stage)].factory_span;
    const double low = std::max(spans[0].min_v, spans[1].min_v);
    const double high = std::min(spans[0].max_v, spans[1].max_v);
    const double middle = (low + high) / 2;
    const double half = (high - low) / 2;
    return {stage, {middle + kLevelFraction * half, middle - kLevelFraction * half}, kLevelSineFill * (high - low)};
}

void check_generator_on_inputs(Source& source)
{
    constexpr double kVpp = 1.0;
    Bench bench(source);
    bench.setGenerator(sine(kVpp, 0));
    const int stage = reference_stage(bench.list());  // a volt reads well on it, and a tenth of it still does
    const auto r = bench.read({stage, stage});
    for (std::size_t ch = 0; ch < kChannels; ++ch) {
        const int number = static_cast<int>(ch) + 1;
        if (r[ch].vpp < kVpp * 0.04)
            throw std::runtime_error(std::format("no signal from the generator on CH{}", number));
        if (r[ch].vpp < kVpp * 0.2)
            throw std::runtime_error(std::format("CH{} reads about a tenth of the generator: is its probe on ×10?", number));
        if (r[ch].vpp < kVpp * 0.7 || r[ch].vpp > kVpp * 1.4 || r[ch].residual_rms > r[ch].vpp * 0.1)
            throw std::runtime_error(std::format("CH{} does not read the generator's sine: is something else connected?", number));
    }
}

GainTransfer transfer_gains(Source& source, const TransferProgress& progress)
{
    Bench bench(source);
    GainTransfer out;
    const int count = bench.stages();
    const int reference = reference_stage(bench.list());
    const int total = kChannels * (count - 1) + 1;
    int done = 0;
    if (progress)
        progress(done, total);
    for (int channel = 0; channel < kChannels; ++channel) {
        const auto c = static_cast<std::size_t>(channel);
        const std::size_t d = 1 - c;
        out.relative[c].assign(static_cast<std::size_t>(count), 1.0);
        for (int b = count - 2; b >= 0; --b) {
            const int a = b + 1;
            const VoltageRange& span = bench.span(b, channel);
            const double center = (span.min_v + span.max_v) / 2;
            const double target = kFill * (span.max_v - span.min_v);
            const int tracker = bench.finestHolding(static_cast<int>(d), center - kRoom * target / 2, center + kRoom * target / 2);
            bench.setGenerator(aim(bench, channel, a, b, tracker, target, center));

            std::array<int, kChannels> onA{}, onB{};
            onA[c] = a;
            onA[d] = tracker;
            onB[c] = b;
            onB[d] = tracker;
            const auto againstTracker = [&](const std::array<int, kChannels>& stages) {
                const auto r = bench.read(stages);
                if (r[c].clipped || r[d].clipped)
                    throw std::runtime_error(std::format("the signal clipped on stage {} of CH{}", stages[c] + 1, channel + 1));
                return r[c].vpp / r[d].vpp;
            };
            double sumA = 0, sumB = 0, firstA = 0, firstB = 0;
            for (int repeat = 0; repeat < kRepeats; ++repeat) {
                const double readA = againstTracker(onA);
                const double readB = againstTracker(onB);
                if (repeat == 0) {
                    firstA = readA;
                    firstB = readB;
                } else {
                    out.worst_spread = std::max({out.worst_spread, std::abs(readA / firstA - 1), std::abs(readB / firstB - 1)});
                }
                sumA += readA;
                sumB += readB;
            }
            out.relative[c][static_cast<std::size_t>(b)] = out.relative[c][static_cast<std::size_t>(a)] * sumB / sumA;
            if (progress)
                progress(++done, total);
        }
        const double atReference = out.relative[c][static_cast<std::size_t>(reference)];
        for (double& r : out.relative[c])
            r /= atReference;
    }

    // Without a multimeter, this is what makes the two channels read alike.
    const VoltageRange& span = bench.span(reference, 0);
    bench.setGenerator(sine(kFill * (span.max_v - span.min_v), (span.min_v + span.max_v) / 2));
    const auto r = bench.read({reference, reference});
    if (r[0].clipped || r[1].clipped)
        throw std::runtime_error("the signal clipped on the reference stage");
    out.channel_ratio = r[0].vpp / r[1].vpp;
    if (progress)
        progress(++done, total);
    return out;
}

std::array<double, kChannels> measure_level(Source& source, int stage, double volts, double sine_vpp)
{
    Bench bench(source);
    GeneratorSettings g = sine(sine_vpp, volts);
    bench.setGenerator(g);
    // The level depends on the coarse gain and on the amplitude, so it is aimed for while the inputs are as they will be.
    std::array<SineReading, kChannels> r{};
    for (int attempt = 0;; ++attempt) {
        r = bench.read({stage, stage});
        if (r[0].clipped || r[1].clipped) {
            g.amplitude_vpp *= kShrink;
        } else {
            const double error = volts - (r[0].mean + r[1].mean) / 2;
            if (std::abs(error) < sine_vpp / 10)
                break;
            g.offset_v += error;
        }
        if (attempt == kFitAttempts)
            throw std::runtime_error(std::format("the level does not fit stage {}", stage + 1));
        bench.setGenerator(g);
    }
    std::array<double, kChannels> out{};
    for (int k = 0; k < kLevelCaptures; ++k) {
        r = bench.read({stage, stage});
        for (std::size_t ch = 0; ch < kChannels; ++ch) {
            if (r[ch].clipped)
                throw std::runtime_error(std::format("the level clipped on CH{}", ch + 1));
            out[ch] += r[ch].mean / kLevelCaptures;
        }
    }
    return out;
}

AcquisitionRequest request_at_stages(const std::vector<StageCalibration>& stages, const std::array<int, kChannels>& stage,
                                     std::uint32_t rate_hz, std::size_t samples)
{
    AcquisitionRequest request;
    request.sample_rate_hz = rate_hz;
    request.samples = samples;
    for (std::size_t ch = 0; ch < kChannels; ++ch) {
        // Just inside its span, so that this stage is chosen and no wider one.
        const VoltageRange& s = stages[static_cast<std::size_t>(stage[ch])].factory_span[ch];
        const double inset = (s.max_v - s.min_v) * 0.02;
        request.channel[ch] = {true, {s.min_v + inset, s.max_v - inset}, Coupling::DC};
    }
    return request;
}

double gain_from_levels(std::array<double, 2> read, std::array<double, 2> actual)
{
    return (read[0] - read[1]) / (actual[0] - actual[1]);
}

void apply_gains(UserCalibration& calibration, const GainTransfer& transfer, const std::array<double, kChannels>& reference)
{
    for (std::size_t i = 0; i < calibration.stages.size(); ++i)
        for (std::size_t ch = 0; ch < kChannels; ++ch)
            calibration.stages[i].channel[ch].gain = reference[ch] * transfer.relative[ch][i];
}

}  // namespace starosc
