// SPDX-License-Identifier: GPL-3.0-or-later
// DDS function generator of the ISDS205B/X (docs/protocol.md §9).
#pragma once

#include <cstdint>
#include <string_view>

#include "isds/protocol.hpp"

namespace isds {

enum class Waveform { Sine, Square, Triangle, RampUp, RampDown };
inline constexpr int kWaveformCount = 5;

// Command 40 value: one bit per waveform (vdso table BOXING_STYLE_FUNID).
std::uint32_t waveform_code(Waveform w);

// vdso's amplitude setting ("zoom resistance") is a linear gain of zoom / 100, i.e. -10..+30 dB.
// It is sent as the 12-bit control code of a 25 mV/dB VGA centred on 1.65 V (command 47).
inline constexpr int kZoomMin = 32;
inline constexpr int kZoomMax = 3162;
std::uint32_t amplitude_code(int zoom);

// vdso's offset setting ("bias resistance", 0..4095) is sent inverted (command 48).
inline constexpr int kBiasMin = 0;
inline constexpr int kBiasMax = 4095;
std::uint32_t offset_code(int bias);

// Duty cycle in percent (1..99); the device expects 100 - duty.
std::uint32_t duty_code(int percent);

// Output levels of the ISDS205X without a load, measured on one unit with a 1 kHz sine
// (docs/measurements.md §5). Typical values: other units, waveforms and frequencies may differ.
inline constexpr double kOutputHighV = 4.86;          // offset code 0x000
inline constexpr double kOutputLowV = -4.79;          // offset code 0xfff
inline constexpr double kAmplitudeFloorVpp = 0.079;   // amplitude codes up to 0x540
inline constexpr double kAmplitudeMaxVpp = 9.7;       // from 0xa20 on, clipped at the output limits
inline constexpr double kAmplitudeCleanVpp = 6.0;     // the wave distorts above (code 0x9e0)

// Linear in dB between the measured points (about 31 codes per dB); codes stay in 0x540..0xa20.
std::uint32_t amplitude_code_for_vpp(double vpp);
double vpp_for_amplitude_code(std::uint32_t code);

// Linear between the measured points: about −2.7 mV per code, up to ±70 mV off a straight line.
std::uint32_t offset_code_for_volts(double volts);
double volts_for_offset_code(std::uint32_t code);

// Command 43: hardware frequency sweep. Payload: four u32 BE.
Bytes sweep_frame(std::uint32_t start_hz, std::uint32_t stop_hz, std::uint32_t step_hz, std::uint32_t time);

// vdso creates a generator for ISDS205B and ISDS205X; only the X has software amplitude and offset.
bool has_generator(std::string_view model);
bool has_level_control(std::string_view model);

}  // namespace isds
