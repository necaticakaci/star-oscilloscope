// SPDX-License-Identifier: GPL-3.0-or-later
// Generator encodings, checked against vdso traffic (trace/run1/usb.log) and its disassembly.
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "isds/generator.hpp"

using namespace isds;

TEST_CASE("waveform codes are one bit per waveform")
{
    CHECK(waveform_code(Waveform::Sine) == 0x01);
    CHECK(waveform_code(Waveform::Square) == 0x02);
    CHECK(waveform_code(Waveform::Triangle) == 0x04);
    CHECK(waveform_code(Waveform::RampUp) == 0x08);
    CHECK(waveform_code(Waveform::RampDown) == 0x10);
}

TEST_CASE("amplitude and offset codes follow vdso")
{
    // SetDDSZoomResistance(1000) sent 47 = 0x935, SetDDSBiasResistance(2048) sent 48 = 0x7ff.
    CHECK(amplitude_code(1000) == 0x935);
    CHECK(offset_code(2048) == 0x7ff);
    CHECK(amplitude_code(kZoomMin) == 0x596);
    CHECK(amplitude_code(kZoomMax) == 0xa6b);
    CHECK(amplitude_code(10) == amplitude_code(kZoomMin));
    CHECK(offset_code(kBiasMin) == 0xfff);
    CHECK(offset_code(kBiasMax) == 0);
    CHECK(duty_code(50) == 50);
    CHECK(duty_code(25) == 75);
    CHECK(duty_code(0) == 99);
}

TEST_CASE("generator frames")
{
    CHECK(to_hex(dds_value(Target::Ch1, DdsCommand::Amplitude, amplitude_code(1000))) == "0101470000093500");
    CHECK(to_hex(dds_value(Target::Ch1, DdsCommand::Offset, offset_code(2048))) == "010148000007ff00");
    CHECK(to_hex(sweep_frame(1000, 2000, 10, 5)) == "01014300000010" "000003e8" "000007d0" "0000000a" "00000005");
}

TEST_CASE("models with a generator")
{
    CHECK(has_generator("ISDS205X(1.1)"));
    CHECK(has_generator("ISDS205B"));
    CHECK_FALSE(has_generator("ISDS205A"));
    CHECK(has_level_control("ISDS205X(1.1)"));
    CHECK_FALSE(has_level_control("ISDS205B"));
}

TEST_CASE("volts map to level codes through the bench measurements")
{
    // docs/measurements.md §5: 0x800 = 1.06 Vpp, 0x900 = 2.55 Vpp; 0 V near offset code 0x7bd.
    CHECK(amplitude_code_for_vpp(1.06) == 0x800);
    CHECK(vpp_for_amplitude_code(0x880) == Catch::Approx(std::sqrt(1.06 * 2.55)));  // half way in dB
    CHECK(vpp_for_amplitude_code(0x900) == Catch::Approx(2.55));
    CHECK(amplitude_code_for_vpp(0.01) == 0x540);
    CHECK(amplitude_code_for_vpp(50) == 0xa20);
    CHECK(vpp_for_amplitude_code(0) == kAmplitudeFloorVpp);
    for (double vpp : {0.1, 0.5, 1.0, 3.0, 6.0})
        CHECK(vpp_for_amplitude_code(amplitude_code_for_vpp(vpp)) == Catch::Approx(vpp).epsilon(0.005));

    CHECK(offset_code_for_volts(0) == Catch::Approx(0x7bd).margin(3));
    CHECK(offset_code_for_volts(1.26) == 0x5ff);
    CHECK(offset_code_for_volts(-2.85) == 0xbff);
    CHECK(offset_code_for_volts(9) == 0);
    CHECK(offset_code_for_volts(-9) == 0xfff);
    CHECK(volts_for_offset_code(0x9ff) == Catch::Approx(-1.48));
    for (double v : {-4.0, -1.0, 0.0, 0.5, 2.0, 4.0})
        CHECK(volts_for_offset_code(offset_code_for_volts(v)) == Catch::Approx(v).margin(0.003));
}
