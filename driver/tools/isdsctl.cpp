// SPDX-License-Identifier: GPL-3.0-or-later
// isdsctl: command line access to an ISDS205X through the open driver.
//
//   isdsctl info
//   isdsctl capture [--range MV] [--rate HZ] [--samples N] [--csv FILE]
//   isdsctl scan
//   isdsctl gen [--wave NAME] [--freq HZ] [--duty PCT] [--zoom N] [--bias N] [--seconds S]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "isds/device.hpp"

using namespace isds;

namespace {

constexpr std::array kChannels{Channel::Ch1, Channel::Ch2};

void usage()
{
    std::puts("usage: isdsctl info\n"
              "       isdsctl capture [--range MV] [--rate HZ] [--samples N] [--csv FILE]\n"
              "       isdsctl scan\n"
              "       isdsctl gen [--wave sine|square|triangle|rampup|rampdown] [--freq HZ] [--duty PCT]\n"
              "                   [--zoom 32..3162] [--bias 0..4095] [--seconds S]");
}

std::map<std::string, std::string> parse_options(int argc, char** argv)
{
    std::map<std::string, std::string> options;
    for (int i = 2; i < argc; ++i) {
        const std::string key = argv[i];
        if (key.rfind("--", 0) != 0 || i + 1 >= argc)
            throw Error("bad option: " + key);
        options[key.substr(2)] = argv[++i];
    }
    return options;
}

struct Stats {
    double mean = 0, min = 0, max = 0;
};

Stats stats(const std::vector<double>& v)
{
    Stats s{0, v.front(), v.front()};
    for (double x : v) {
        s.mean += x;
        s.min = std::min(s.min, x);
        s.max = std::max(s.max, x);
    }
    s.mean /= static_cast<double>(v.size());
    return s;
}

std::vector<double> to_mv(const Calibration& cal, const CaptureData& data, Channel ch)
{
    std::vector<double> mv(data.raw[index(ch)].size());
    cal.to_mv(data.raw[index(ch)], mv, data.stage[index(ch)], ch);
    return mv;
}

void print_stage_line(const Calibration& cal, Stage s)
{
    std::printf("  G=%#06x D=%#06x/%#06x  lsb %8.4f/%8.4f mV  span %8.1f..%7.1f / %8.1f..%7.1f mV\n",
                cal.gain_code(s), cal.level_code(s, Channel::Ch1), cal.level_code(s, Channel::Ch2),
                cal.lsb_mv(s, Channel::Ch1), cal.lsb_mv(s, Channel::Ch2), cal.span(s, Channel::Ch1).min_mv,
                cal.span(s, Channel::Ch1).max_mv, cal.span(s, Channel::Ch2).min_mv, cal.span(s, Channel::Ch2).max_mv);
}

int cmd_info(Device& dev)
{
    const Descriptor& d = dev.descriptor();
    std::printf("device   : %s / %s\n", d.vendor.c_str(), d.model.c_str());
    std::printf("id       : %016llX\n", static_cast<unsigned long long>(d.device_id));
    std::printf("rates    :");
    for (auto r : d.sample_rates)
        std::printf(" %g MSa/s", r / 1e6);
    std::printf("\nmax      : %zu samples per channel\nstages (CH1 / CH2):\n", dev.max_capture_samples());
    for (Stage s : dev.calibration().stages())
        print_stage_line(dev.calibration(), s);
    return 0;
}

int cmd_capture(Device& dev, const std::map<std::string, std::string>& opt)
{
    const double range = opt.contains("range") ? std::stod(opt.at("range")) : 1000.0;
    const auto rate = static_cast<std::uint32_t>(opt.contains("rate") ? std::stoul(opt.at("rate")) : 1'000'000);
    const std::size_t samples = opt.contains("samples") ? std::stoul(opt.at("samples")) : 16384;
    const Calibration& cal = dev.calibration();

    dev.set_sample_rate(rate);
    for (Channel ch : kChannels) {
        const Stage s = cal.select(ch, -range, range);
        dev.set_stage(ch, s);
        std::printf("CH%d: ±%g mV -> G=%#06x D=%#06x, lsb %.4f mV, span %.0f..%.0f mV\n", static_cast<int>(ch), range,
                    cal.gain_code(s), cal.level_code(s, ch), cal.lsb_mv(s, ch), cal.span(s, ch).min_mv,
                    cal.span(s, ch).max_mv);
    }
    dev.set_trigger(Channel::Ch1, 0, -range, range);
    const CaptureData data = dev.capture(samples);

    std::array<std::vector<double>, 2> mv{to_mv(cal, data, Channel::Ch1), to_mv(cal, data, Channel::Ch2)};
    for (Channel ch : kChannels) {
        const Stats s = stats(mv[index(ch)]);
        std::printf("  CH%d: %zu samples  min=%+.2f mV  max=%+.2f mV  mean=%+.2f mV\n", static_cast<int>(ch),
                    mv[index(ch)].size(), s.min, s.max, s.mean);
    }

    if (opt.contains("csv")) {
        std::ofstream out(opt.at("csv"));
        out << "time_s,ch1_v,ch2_v\n";
        for (std::size_t i = 0; i < samples; ++i)
            out << static_cast<double>(i) / data.sample_rate_hz << ',' << mv[0][i] / 1000 << ',' << mv[1][i] / 1000
                << '\n';
        std::printf("wrote %s\n", opt.at("csv").c_str());
    }
    return 0;
}

int cmd_scan(Device& dev)
{
    const Calibration& cal = dev.calibration();
    dev.set_sample_rate(1'000'000);
    std::printf("%6s %15s %17s   %12s %12s\n", "G", "D (CH1/CH2)", "lsb mV", "CH1 mean mV", "CH2 mean mV");
    for (Stage s : cal.stages()) {
        for (Channel ch : kChannels)
            dev.set_stage(ch, s);
        const Span span = cal.span(s, Channel::Ch1);
        dev.set_trigger(Channel::Ch1, 0, span.min_mv, span.max_mv);
        dev.capture(4096);  // discard the first capture after a stage change
        const CaptureData data = dev.capture(16384);
        std::printf("%#06x %#06x/%#06x %8.3f/%8.3f   %12.2f %12.2f\n", cal.gain_code(s), cal.level_code(s, Channel::Ch1),
                    cal.level_code(s, Channel::Ch2), cal.lsb_mv(s, Channel::Ch1), cal.lsb_mv(s, Channel::Ch2),
                    stats(to_mv(cal, data, Channel::Ch1)).mean, stats(to_mv(cal, data, Channel::Ch2)).mean);
    }
    return 0;
}

int cmd_gen(Device& dev, const std::map<std::string, std::string>& opt)
{
    static const std::map<std::string, Waveform> waves{{"sine", Waveform::Sine},
                                                        {"square", Waveform::Square},
                                                        {"triangle", Waveform::Triangle},
                                                        {"rampup", Waveform::RampUp},
                                                        {"rampdown", Waveform::RampDown}};
    GeneratorSettings g;
    if (opt.contains("wave")) {
        if (!waves.contains(opt.at("wave")))
            throw Error("unknown waveform: " + opt.at("wave"));
        g.waveform = waves.at(opt.at("wave"));
    }
    if (opt.contains("freq"))
        g.frequency_hz = static_cast<std::uint32_t>(std::stoul(opt.at("freq")));
    if (opt.contains("duty"))
        g.duty_percent = std::stoi(opt.at("duty"));
    if (opt.contains("zoom"))
        g.amplitude = amplitude_code(std::stoi(opt.at("zoom")));
    if (opt.contains("bias"))
        g.offset = offset_code(std::stoi(opt.at("bias")));
    const double seconds = opt.contains("seconds") ? std::stod(opt.at("seconds")) : 10.0;

    dev.set_generator(g);
    dev.set_generator_output(true);
    std::printf("generator on: %u Hz, duty %d %%, amplitude code %#05x, offset code %#05x for %g s\n", g.frequency_hz,
                g.duty_percent, g.amplitude, g.offset, seconds);
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    dev.set_generator_output(false);
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string command = argv[1];
    try {
        const auto options = parse_options(argc, argv);
        auto dev = Device::open();
        if (command == "info")
            return cmd_info(*dev);
        if (command == "capture")
            return cmd_capture(*dev, options);
        if (command == "scan")
            return cmd_scan(*dev);
        if (command == "gen")
            return cmd_gen(*dev, options);
        usage();
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "isdsctl: %s\n", e.what());
        return 1;
    }
}
