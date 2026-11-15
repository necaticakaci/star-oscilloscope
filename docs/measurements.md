# Measurements

What the ISDS205 series does, measured against known sources, and how Star Oscilloscope deals with it. The numbers come
from one ISDS205X(1.1), "the measured unit" (its descriptor is in [protocol.md](protocol.md) §7). Another unit will have
other zero and gain errors; whether the rest holds for every unit is open (§8). §14 sums up the hardware's limits.

The tools named here are in `tools/` ([building.md](building.md) §3); most results can be repeated with them.

Marks: ✅ measured · 🔎 a single or indirect measurement · ❓ open question

---

## 1. Setup and method

| Instrument | Use |
|---|---|
| Digital multimeter, DC V | voltage reference |
| A second oscilloscope (OWON HDS2102S) with its own probe at ×1 | frequency and amplitude comparison; its signal generator as an independent source (§13) |
| Adjustable analogue power supply (potentiometer) | low-impedance DC source, 0.018–2.97 V |
| The probes that come with the device | ×1 and ×10. Probe compensation affects only high frequencies at ×10, not DC |

**Connection:** supply and multimeter in parallel; the supply to CH1 directly with a BNC–croc cable, to CH2 through a
probe (×1, then ×10).

**Method:**
- `tools/dc_logger.py` measures all 7 stages on both channels in every cycle: the mean of 200 000 samples at 1 MSa/s.
  A cycle takes about 1.7 s.
- The order of the stages is reversed every cycle, so that the coarse gain relay switches once per cycle, not twice.
- The supply is raised step by step; each step is held 15–20 s and the multimeter reading noted.
- `tools/dc_gain.py` finds the steady steps by itself and fits `read = gain × true + zero` for every stage, leaving out a
  stage that clips on a step.

```bash
python3 tools/dc_logger.py 1200 dc.jsonl
python3 tools/dc_gain.py dc.jsonl                        # lists the steps
python3 tools/dc_gain.py dc.jsonl --dmm=-,0.0176,0.0888,0.2045,0.5464,0.9336,1.2422,2.506,2.971
python3 tools/dc_gain.py dc_x10.jsonl --dmm=0.0179,1.0263,2.532,- --probe 1,10
```

## 2. Zero offset ✅

Probes shorted to their own ground clips, DC coupling, the device's own calibration table.

| Stage | G / D (CH1) | lsb mV | CH1 mV | CH2 mV |
|---|---|---|---|---|
| ±0.1 V | 0x109 / 0x62c | 0.66 | −0.8 | −0.05 |
| ±0.25 V | 0x109 / 0x200 | 2.03 | −1.9 | −2.1 |
| ±0.6 V | 0x109 / 0xd3 | 4.9 | −10.7 | −13.9 |
| ±1.1 V | 0x109 / 0x77 | 8.7 | −31.5 | −36.4 |
| ±1.3 V | 0x52e / 0x200 | 10.2 | −31.8 | −32.3 |
| ±3 V | 0x52e / 0xd3 | 24.7 | −33.2 | −48.3 |
| ±5.5 V | 0x52e / 0x77 | 43.7 | −110.3 | −124.9 |

- The offset does not depend on the sample rate and repeats; two hours later the 0 V step gave the same values within
  ±2 lsb. It moves by about ±2 lsb over a few hours.
- It is 1–4 lsb and comes from the offsets in the device's table (`cal`). Our conversion gives the same result as the
  manufacturer's library ([protocol.md](protocol.md) §8), so their software shows the same offset.

## 3. Gain ✅

Eight steps from 0.018 to 2.97 V from the supply. The table gives `read / true`:

| Stage | CH1, no probe | CH2, ×1 probe | CH2, ×10 probe (÷10) 🔎 |
|---|---|---|---|
| ±0.1 V | 0.980 | 0.980 | 0.964 |
| ±0.25 V | 0.930 | 0.938 | 0.924 |
| ±0.6 V | 0.934 | 0.942 | 0.922 |
| ±1.1 V | 0.931 | 0.940 | 0.936 |
| ±1.3 V | 0.934 | 0.936 | 0.914 |
| ±3 V | 0.940 | 0.944 | 0.952 |
| ±5.5 V | 0.934 | 0.938 | 0.926 |

- **Linear:** the deviation from the fitted line is ≤ 1 lsb on the fine stages; the coarse ones show a few lsb of
  quantisation effect (the ADC's differential non-linearity).
- **Consistent zeros:** the fitted zeros agree with the shorted values of §2 within about 1 lsb.
- The ±0.1 V stage clips at +117 mV and was fitted from two steps only (18 and 89 mV); both channels giving 0.980
  supports it.
- **Not the probe or the cable:** CH1 (no probe) and CH2 (×1 probe) read low by the same ratio. The ×1 probe's conductor
  measured about 101 Ω, 0.01 % against the 1 MΩ input.
- **The ×10 probe reads about 1.5 % lower again:** its real ratio is about 10.15 (tolerances of the tip resistor and the
  input). The ×10 column comes from 3 steps only.

**Reading:**
- Our voltage conversion equals the manufacturer's library, so their software shows the same error: it is in the
  device's calibration table, not in the driver.
- The only stage with a right gain is ±0.1 V, whose entry is the same on both channels (level 3: D = 0x62c, cal 33/166)
  and looks like a factory default ([protocol.md](protocol.md) §7).
- The six stages calibrated per channel at the factory (levels 0–2) all read about 6.5 % low: the reference used there
  may have been about 7 % off.
- "The factory calibrated with a ×10 probe" does not hold: at ×10 the reading is lower still.

**Cross-checks:** with the gains of this table, the DDS output's DC read 2.656 V on ±3 V and 2.635 V on ±5.5 V against
2.647 V on the multimeter (+0.3 %, −0.4 %).

The application therefore has a user calibration: a zero and a gain per stage and channel, kept per device (§9). The
probe factor can be any number, e.g. 10.15.

## 4. Frequency 🔎

| Measurement | Result |
|---|---|
| Front-panel calibration output, against our sample clock | 1000.54 Hz |
| The same output on the second oscilloscope | 1.000 kHz, 1.001 kHz about every 10 s; consistent with ≈ 1000.5 Hz |
| DDS set to 1 000 Hz | 999.883 Hz (−117 ppm) |
| DDS set to 12 345 Hz | 12 343.684 Hz (−107 ppm) |
| DDS set to 100 000 Hz | 99 989.51 Hz (−105 ppm) |

- The DDS reads about 110 ppm low against our sample clock. An external generator read −100 ppm the same way (§15), so
  the sample clock is probably about 100 ppm fast. A counter of ≤ 10 ppm would settle it ❓.
- Method: 1M samples at 1 MSa/s; peak of a Hann-windowed FFT with parabolic interpolation.

## 5. DDS output ✅

1 kHz sine into CH2 with a ×1 probe, corrected with the gains of §3 (about ±1 %). Commands and encodings:
[protocol.md](protocol.md) §9.

### Offset (command `48`)

Measured at the smallest amplitude (`47` = 0x596).

| `48` code | library bias | Output |
|---|---|---|
| 0x000 | 4095 | +4.86 V (upper limit) |
| 0x1ff | 3584 | +4.07 V |
| 0x3ff | 3072 | +2.65 V |
| 0x5ff | 2560 | +1.26 V |
| 0x7ff | 2048 (library default) | −0.18 V |
| 0x9ff | 1536 | −1.48 V |
| 0xbff | 1024 | −2.85 V |
| 0xdff | 512 | −4.25 V |
| 0xfff | 0 | −4.79 V (lower limit) |

- About −2.70 mV per code; 0 V near 0x7bd. The deviation from a straight line reaches ±70 mV, so volts are turned into
  codes by interpolating this table (`isds/generator.hpp`). A per-unit table would be better.

### Amplitude (command `47`)

Measured with the offset near 0 V.

| `47` code | Vpp |
|---|---|
| ≤ 0x540 | ≈ 0.079 V (floor) |
| 0x600 | 0.152 V |
| 0x700 | 0.392 V |
| 0x800 | 1.06 V |
| 0x900 | 2.55 V |
| 0x9c0 | 5.61 V |
| 0x9e0 | distortion starts (asymmetric) |
| ≥ 0xa20 | ≈ 9.7 V, clipped at the output's limits |

- Linear in dB: about 30.8 codes/dB, matching the library's model (31.0 codes/dB). `Vpp ≈ 1.05 V × 10^((code − 0x800) / 617)`
  for 0x580–0x9c0.
- The DC level shifts slightly with the amplitude: about −80 mV from the smallest to the library's default amplitude.

### Behaviour

- Switching the output off (`10`) stops the wave but **leaves its DC offset at the output**.
- ⚠️ **At power-up the output sits at about +4.9 V DC**, as if `48` were 0. A circuit on DDS OUT sees this until the
  generator is set. The application parks the output at 0 V when it connects.
- An output switched off is not a usable reference: it read about 106 mV differently on the two coarse gains, explained
  below.

### The output depends on the inputs' coarse gain ✅

**Finding:** without any command to the generator, its output changes when an input's coarse gain changes (G 0x52e ↔
0x109): both the amplitude and the DC level. The second oscilloscope saw the same at the same point (5 Vpp set: 8.3–8.6 V
and 6.1 V), so the signal itself changes, not our reading.

`star-loopback --dds-state --probe 10`: 1 kHz sine, Vpp and mean at the probe tip as read on CH2 (CH1 alike):

| Set | Both 0x52e | CH1 0x109 | CH2 0x109 | Both 0x109 |
|---|---|---|---|---|
| 0.5 V | 0.49 V, +0.03 | 0.53 V, −0.02 | 0.54 V, −0.02 | 0.59 V, −0.08 |
| 1 V | 0.95 V, +0.04 | 1.05 V, −0.02 | 0.99 V, −0.03 | 1.07 V, −0.09 |
| 2 V | 2.02 V, +0.02 | 2.20 V, −0.05 | 2.18 V, −0.06 | 2.32 V, −0.12 |
| 3 V | 2.98 V, −0.01 | 3.22 V, −0.08 | 3.20 V, −0.09 | 3.57 V, −0.17 |
| 4 V | 4.00 V, −0.03 | 4.46 V, −0.11 | 4.45 V, −0.13 | 5.31 V, −0.27 |
| 5 V | 4.94 V, −0.05 | 5.93 V, −0.20 | 5.85 V, −0.22 | 8.24 V, −0.43 |
| 6 V | 5.87 V, −0.09 | 8.27 V, −0.38 | 8.16 V, −0.42 | 8.97 V, −0.29 (saturating) |
| 8 V | 8.51 V, −0.29 | 9.38 V, −0.19 | 9.32 V, −0.22 | 9.07 V, −0.29 (saturating) |

- **The amplitude table is right with both inputs on 0x52e.** Each input on 0x109 adds about 10 %, both together 20 %;
  above 3 V the effect grows fast.
- **The DC level moves too:** about −130 mV at 1 V with both on 0x109.
- Sending the settings again changes nothing: it is an analogue effect, perhaps the relay touching the line that feeds
  the DDS control voltage ❓.
- **When G 0x109 is chosen:** when the screen's range is within about ±1.1 V, i.e. about 250 mV/div at ×1 or 2.5 V/div
  at ×10 and below.
- The application warns about it on the Generator tab; there is no correction, which would need four measurements per
  unit. The gain wizard reads the DDS on the other channel because of it (§9).

### Checked from the application

DDS OUT into CH2 with a ×1 probe, user calibration, `star-loopback` (`tools/loopback.cpp`):
- **Parking:** at power-up +4.865 V; after switching the output on and off with the smallest amplitude and 0 V offset,
  −10 mV. The application does this when it connects, when it closes (an option) and at Output OFF.
- **Amplitude, 1 kHz sine:** set 0.2 / 0.5 / 1 / 2 / 3 / 5 Vpp, read 0.226 / 0.536 / 1.031 / 2.019 / 2.990 / 5.075 Vpp
  (peak-to-peak includes the noise's peaks, hence the excess on small amplitudes).
- **Offset, 0.1 Vpp:** set −3 / −1.5 / −0.5 / 0 / +0.5 / +1.5 / +3 V, mean −2.985 / −1.499 / −0.512 / −0.007 / +0.478 /
  +1.502 / +2.996 V; at most 22 mV off.
- **Waveforms, 2 Vpp:** square 2.046, triangle 2.019, rising ramp 1.993, falling ramp 1.967 Vpp.
- **Duty:** 25 % read 25.0 %.
- **Frequency response,** DDS, ×1 probe and input together, 1 Vpp sine, against 1 kHz:

| Frequency | 10 kHz | 100 kHz | 500 kHz | 1 MHz | 2 MHz | 5 MHz | 10 MHz | 20 MHz |
|---|---|---|---|---|---|---|---|---|
| Ratio | 0.976 | 0.872 | 0.844 | 0.812 | 0.708 | 0.445 | 0.267 | 0.168 |
| dB | −0.2 | −1.2 | −1.5 | −1.8 | −3.0 | −7.0 | −11.5 | −15.5 |

- The fall of about 13 % between 10 and 100 kHz is the input's, not the DDS's: another generator gives the same (§13).
- Above 2 MHz the ×1 probe's own bandwidth takes part. The DDS's edge (118–133 ns at 48 MSa/s) is slower than the other
  generator's (70 ns), so part of the −3 dB at 2 MHz may be the DDS's ❓.
- The application sets the amplitude for 1 kHz and warns above 20 kHz that the reading comes out low.

**Open:** the DDS's own response without a probe; the hardware sweep (`43`); a factory DDS calibration after 0xa0 in the
descriptor ❓; the output impedance (200 Ω by the manufacturer) was not measured.

## 6. Not measured yet

- Above 4.2 V on the ±5.5 V stage (negative voltages: §9).
- AC coupling in detail.
- Which clock drifts by about 100 ppm (§4).

## 7. How the first measurements went

1. The protocol and the calibration model had been learned from the manufacturer's library and reproduced its output
   with zero error ([protocol.md](protocol.md) §8) — which only meant "we show what their software shows".
2. The front-panel 1 kHz square read about 2.0 V against 2.155 V on the multimeter: the first sight of the ~7 % low gain.
3. The DDS's amplitude and offset encodings were taken from a disassembly of the library and swept (§5).
4. Shorted probes gave a repeatable 1–4 lsb offset on every stage and rate (§2). The logging loop was changed to reverse
   the order of the stages each cycle, so that the coarse gain relay switches once per cycle.
5. The DC left at the switched-off DDS output read 106 mV differently on the two coarse gains, so the gain was measured
   again with an independent, low-impedance source (§3); later the cause turned out to be the DDS itself (§5).
6. The supply and multimeter gave six stages 6–7 % low and the ±0.1 V stage 2 % low, linear and with consistent zeros.
7. The ×10 probe ruled out a factory calibration with ×10 probes.
8. Corrected with these gains, the DDS's DC agreed with the multimeter within 0.4 %.
9. A recording stopped in the middle of a capture left the device mute at the next connection: the rest of the capture
   was still waiting. The driver drains it now ([protocol.md](protocol.md) §3).
10. The error is in the factory table, not in the driver; hence the user calibration (§9).

## 8. Is it the same on every unit? ❓

All of this was measured on one unit. What hints there are:
- **Units really are calibrated one by one:** the D codes of the factory-set stages differ slightly between the
  channels (e.g. 0x200 / 0x1fe).
- **Both channels are off by the same ratio,** which points to a reference error common to both during calibration.
- **The stage left uncalibrated is right:** the ±0.1 V stage, which looks like a factory default, is within 2 %. The
  hardware is nominally right; the calibration step added the error.

Either a batch-specific error (a wrong reference on one day or one fixture) or an error in the procedure itself. Only
data from other units can tell. Mainstream oscilloscopes typically specify a DC gain accuracy of 1–3 %; 6.5 % is outside
that. It may have gone unnoticed because most users work with ×10 probes, whose tolerance hides such a difference, and
measure DC levels with a multimeter.

**Data from other units that would help** (please open an issue):
1. **The descriptor:** the output of `isdsctl info` (D codes and stage ranges); no instrument needed. Is level 3 always
   0x62c with 33/166? How much do the D codes of levels 0–2 vary?
2. **One DC comparison:** a voltage measured with a multimeter (a battery, a bench supply) and what the ±1.1 V and ±3 V
   stages read for it. `dc_logger.py` and `dc_gain.py` do it in detail.

The user calibration corrects the error whether it is one unit's or the whole production's.

## 9. User calibration in the application

**Menu:** Acquire > Calibration…. The status bar shows which calibration is in use: `Cal: default` (the device's own
table as it is) or `Cal: user`; hovering over it shows when the zero and the gains were measured.

**Correction:** `V = (V_default − zero) / gain`, per stage and channel (`isds::Calibration::set_correction`). The voltage
conversion, the stage ranges, the choice of stage and the trigger code all use the corrected values. A stage without an
entry keeps the default.

**Steps:**
1. **Find the gains:** the Gain Wizard (below). Otherwise Import… a file made by `dc_logger.py` + `dc_gain.py --json`
   (§1), or edit the cells (a gain between 0.5 and 2).
2. **Measure the zero** (Measure Zero…): probes shorted to their ground clips; every stage on both channels, 100 000
   samples at 1 MSa/s. Refused on clipping, on a zero above 12 lsb, or on noise above 4 lsb + 3 mV — signs of an input
   that is not shorted. Repeat now and then: the zero moves by ±2 lsb over hours.
3. **OK:** saved as `calibration/<serial number>.json` beside the settings (`~/.config/StarOscilloscope/` on Linux,
   `%APPDATA%\StarOscilloscope\` on Windows). A calibration equal to the defaults removes the file.

**Rules:** a file is applied only to the device with the same serial number; another device's file is refused both on
import and in the driver. Stages are matched by their G and D codes, not their order.

**File format** (JSON, version 1):

```json
{
    "format": "star-oscilloscope-calibration", "version": 1,
    "model": "ISDS205X(1.1)", "device_id": "<16 hex digits>",
    "zero_time": "2026-11-01T20:41:53", "gain_time": "2026-11-01T20:41:53",
    "stages": [
        {"gain_code": 265, "level_code": [512, 510], "factory_span_mv_ch1": [-229.8, 287.8],
         "ch1": {"zero_mv": -2.08, "gain": 0.9303}, "ch2": {"zero_mv": -1.96, "gain": 0.938}}
    ]
}
```

**On the bench:** 2.506 V (multimeter) on both channels, 1 V/div, the widest stage (±5.5 V, 1 lsb ≈ 47 mV):

| Calibration | CH1 | CH2 |
|---|---|---|
| Default | 2.208 V (−11.9 %) | 2.224 V (−11.3 %) |
| User | 2.501 V (−0.2 %) | 2.503 V (−0.1 %) |

- On this stage the default error adds a zero offset of −110…−127 mV (2.5–3 lsb) to the gain error.
- **DC on a coarse stage is limited to about half an lsb:** with noise below 1 lsb, the mean of a quantised DC level can
  be off by up to half an lsb. For precise DC choose a scale that puts the signal on a fine stage (e.g. 2.5 V: 0.5 V/div
  and −2 div selects ±3 V, 1 lsb ≈ 26 mV).

### Gain wizard ✅

**Menu:** Acquire > Calibration… > Gain Wizard…. Needs ×1 probes on DDS OUT and, for right volts, a multimeter. The
gains found go into the Calibration window's table; OK keeps them. ISDS205X only: it sets DC levels with the DDS offset.

**Method** (`core/src/gain_transfer.cpp`):
1. **Connection check:** both channels must read a 1 Vpp sine; a missing signal or a probe at ×10 is reported.
2. **Reference stage (±3 V):** the generator holds two levels and the user reads the multimeter. The gain comes from the
   difference of the two, so the zero plays no part.
3. **Transfer from stage to stage:** the amplitude of a 1 kHz sine is read on two neighbouring stages; their ratio is the
   ratio of the gains. About 16 s for both channels, 12 pairs.
4. **Check (optional):** two more levels on the ±1.1 V stage of the other coarse gain; the difference is shown.

Without a multimeter step 2 is skipped: stages and channels agree with each other, and the absolute value stays what the
reference stage says. The page says so. The zero is a separate job (Measure Zero).

**Why it works this way:**
- **The levels carry a sine:** a flat DC level is read up to half an lsb wrong when the noise is below 1 lsb; a sine
  crosses many ADC codes and the error averages out.
- **The transfer uses amplitude, not DC:** the amplitude does not depend on the stage's zero or on quantisation.
- **Every reading against the other channel:** the generator's output changes with the inputs' coarse gain (§5), so one
  setting gives different voltages on two stages. While one channel changes stage, the other stays and reads the same
  signal at the same time; the change cancels in the ratio. In the simulator, with the output growing 10 % and moving
  65 mV per channel, the result stays within 0.1 %.

**With a multimeter on the bench** (`star-loopback --gain-levels --hold 25`), default volts:

| Stage | Set, sine | Multimeter | CH1 | CH2 |
|---|---|---|---|---|
| ±5.5 V | +2.40 V, 3 Vpp | 2.351 | 2.06699 | 2.08638 |
| ±5.5 V | −2.40 V, 3 Vpp | −2.489 | −2.42690 | −2.44156 |
| ±5.5 V | +2.40 V, 0.4 Vpp | 2.397 | 2.10986 | 2.12524 |
| ±3 V | +1.60 V, 2 Vpp | 1.5722 | 1.43688 | 1.44054 |
| ±3 V | −1.60 V, 2 Vpp | −1.6594 | −1.57649 | −1.58735 |
| ±1.1 V | +0.45 V, 0.64 Vpp | 0.3638 | 0.30195 | 0.30604 |
| ±1.1 V | −0.45 V, 0.64 Vpp | −0.5285 | −0.52012 | −0.52812 |

- The sine does not upset the multimeter: between 3 and 0.4 Vpp the multimeter saw 46 mV and CH1 46.2 mV (a real shift
  of the DC with the amplitude, §5).
- Negative voltages agree with positive ones: the CH1 zero from the two ±5.5 V levels is −116 mV, −110 mV shorted.
- The transfer agrees with the multimeter: against the ±3 V stage, ±5.5 V 0.9962 / 0.9957 (CH1, transfer / multimeter),
  0.9960 / 0.9985 (CH2); ±1.1 V 0.9861 / 0.9880 and 0.9988 / 0.9977.
- The ±5.5 V stage is less linear (its ratio moves 0.4 % with the signal's size), which is why the reference is ±3 V.
- Repeats agree within 0.02 %; 1000, 300 and 100 Hz give the same within 0.2 % on the finest stage, 0.13 % elsewhere.

**In the application:** the check page showed CH1 −0.21 %, CH2 +0.19 % (the multimeter's own error included). Gains found:

| Stage | CH1 | CH2 |
|---|---|---|
| ±0.1 V | 0.9593 | 0.9755 |
| ±0.25 V | 0.9170 | 0.9328 |
| ±0.6 V | 0.9225 | 0.9391 |
| ±1.1 V | 0.9191 | 0.9352 |
| ±1.3 V | 0.9263 | 0.9303 |
| ±3 V | 0.9321 | 0.9367 |
| ±5.5 V | 0.9285 | 0.9330 |

The earlier DC-step values of §3 were 0.3–1 % high: they used positive steps only, without a sine, and so carried the
quantisation error. The two channels now differ by up to 1.6 % on some stages, which is the channels' real difference.

---

## 10. Data continuity and the USB link ✅

### 48 MSa/s comes in blocks

**Found when** a 10 kHz sine captured at 48 MSa/s measured 21 kHz: consecutive samples jumped by up to 0.9 V on a 2 Vpp sine.

- At this rate samples come in blocks of 256 with gaps of varying length between them; the jumps are at raw index
  1024 + 256·k ([protocol.md](protocol.md) §6).
- The manufacturer's library (vdso, in their SDK) returns 1020 samples at this rate that span four blocks, gaps included.
- **Effect before the fix:** timebases of 17 µs/div and below captured 16K samples at 48 MSa/s; the trigger was searched
  across gaps and the trace broke where a block boundary fell inside the window (1.6 V jumps on a 100 kHz sine).

**Fix:**
- The driver reports the contiguous length per rate (`max_contiguous_samples`).
- The trigger is searched only where the window stays inside one block, so 48 MSa/s is used up to 500 ns/div with 16K
  captures; no window shows a jump any more (100 kHz and 1 MHz sines, 20 frames each).
- Autoset uses three captures: 48 MSa/s for the highest frequencies, 16 MSa/s × 64K, and 0.3 s at 1 MSa/s. A capture's
  frequency counts only if it sees at least 80 % of the signal's swing; a short capture of a slow signal saw noise as edges.
  Autoset found 50 Hz, 1 kHz, 10 kHz, 200 kHz and 2 MHz on the device.

### Waveforms at high frequency

A sine started to look angular above 1 MHz and lost its shape at 10 MHz. Fitting a sine of the known frequency to the
captured samples (DDS 2 Vpp, ×1 probe):

| Frequency | 48 MSa/s, one block: points per period | residual | Vpp | 16 MSa/s: points per period |
|---|---|---|---|---|
| 1 MHz | 48 | 6 mV | 1.49 | 16 |
| 2 MHz | 24 | 6 mV | 1.25 | 8 |
| 5 MHz | 9.6 | 5 mV | 0.72 | 3.2 |
| 10 MHz | 4.8 | 5 mV | 0.39 | 1.6, aliasing |
| 20 MHz | 2.4 | 5 mV | 0.22 | 0.8, aliasing |

- **The signal is not distorted; the drawing was.** At 48 MSa/s the samples lie on a clean sine up to 20 MHz, the
  residual at the level of noise and quantisation. Joining 5 points per period with straight lines makes a zigzag.
  The manufacturer's software shows the same zigzag above 5 MHz. It has an interpolation option (Spline or Sin, from
  6 MHz), but with a 10 MHz sine at 100 ns/div neither made a visible difference.
- **Applied:** sin(x)/x interpolation (8-tap, Lanczos-windowed sinc) when more than 3 pixels fall to a sample (View →
  Sin(x)/x Interpolation turns it off); 48 MSa/s at 500 ns/div; an orange ALIAS badge when the signal is above half the
  sample rate (a 10 MHz sine at 1 µs/div and 16 MSa/s showed 6 MHz).
- The amplitude falls with frequency: a separate matter (§13, below).

### How exact sin(x)/x is ✅

`star-loopback --sinc --range 0.18`: the second oscilloscope's generator, 2 Vpp sine, ×10 probes, 48 MSa/s, 48 captures.
Every k-th sample is kept, the ones between are computed with sin(x)/x and with straight lines and compared with the
real samples, i.e. against samples the method did not see. CH1, mV rms (1 lsb = 2.18 mV):

| Signal | Kept | Samples per period | sin(x)/x error | Straight-line error | The signal's own noise |
|---|---|---|---|---|---|
| 1 MHz | 1/2 | 24 | 1.25 | 1.22 | 0.97 |
| 1 MHz | 1/6 | 8 | 1.29 | 4.00 | |
| 5 MHz | 1/2 | 4.8 | 1.27 | 12.27 | 0.99 |
| 5 MHz | 1/4 | 2.4 | 1.39 | 36.50 | |
| 5 MHz | 1/6 | 1.6 | 91.95 | 64.31 | |
| 10 MHz | 1/2 | 2.4 | 1.26 | 18.45 | 0.91 |
| 10 MHz | 1/3 | 1.6 | 43.21 | 30.15 | |

- **Down to 2.4 samples per period the error of sin(x)/x is at the noise level** (20 MHz at 48 MSa/s); straight lines
  are 15–26 times worse there. Below 2 samples per period there is no information (Nyquist).
- With 16–24 or more samples per period sin(x)/x gains nothing and smooths ADC steps and noise into ripples; the trace
  is drawn with straight lines above 20 samples per period (`sinc_helps`).

### At high frequency the amplitude depends on the stage 🔎

Each stage swept with the DDS (`star-loopback --stages --probe 10`), CH1, each row against its own 1 kHz reading:

| Range | 1 lsb | 100 kHz | 1 MHz | 2 MHz | 5 MHz | 10 MHz | 20 MHz |
|---|---|---|---|---|---|---|---|
| ±0.04 V (G 0x109) | 0.67 mV | 0.90 | 0.85 | 0.72 | 0.41 | 0.19 | 0.06 |
| ±0.2 V (G 0x109) | 2.18 mV | 0.90 | 0.83 | 0.70 | 0.40 | 0.22 | 0.11 |
| ±0.55 V (G 0x109) | 5.27 mV | 0.90 | 0.86 | 0.74 | 0.45 | 0.19 | 0.08 |
| ±1.05 V (G 0x109) | 9.38 mV | 0.90 | 0.87 | 0.79 | 0.55 | 0.26 | 0.12 |
| ±1.15 V (G 0x52e) | 10.9 mV | 0.89 | 0.82 | 0.69 | 0.40 | 0.20 | 0.09 |
| ±2.9 V (G 0x52e) | 26.2 mV | 0.86 | 0.81 | 0.72 | 0.44 | 0.26 | 0.09 |
| ±5 V (G 0x52e) | 46.8 mV | 0.89 | 0.85 | 0.76 | 0.56 | 0.35 | 0.15–0.18 |

- Up to 1 MHz the stages differ by about 5 %; from 2 MHz the difference grows, to nearly 2× at 10 MHz. Within each
  coarse gain, wider ranges respond better. A 10 MHz signal from another generator read 2.3× lower on the finest stage
  than on ±1.1 V.
- The source and the probe load lower the absolute values too, so only ratios within a row mean much.
- **For the right amplitude of fast signals use a wide scale.** A per-stage correction needs the source's own response
  separated first.

### Samples lost at 4–16 MSa/s ✅

**Found when** the Spectrum tab showed sidebands around a 100 kHz sine whose spacing followed the sample rate, not the
signal (16 MSa/s: ±5 kHz, −27 dBc; SNR 13 dB instead of 35 dB).

**Method:** a DDS sine whose period is 160 samples; the capture is cut into periods and each period's phase measured.
In a continuous record the phase stays put; a lost or extra sample makes it jump.

**Cause:** the device does not store a capture; it streams it while capturing, through a buffer of about 1024 samples
(64 µs at 16 MSa/s, 128 µs at 8 MSa/s, 1 ms at 1 MSa/s). If the host's reads pause longer, samples are lost. The driver
used to read a capture with one large read; over usbip that left such pauses. 8 MSa/s needs only 16 MB/s but lost the
most: the problem is short pauses in reading, not the average rate.

| 1M samples, one read | Elapsed | Collecting |
|---|---|---|
| 1 MSa/s | 1059 ms | 1049 ms |
| 16 MSa/s | 86 ms | 66 ms |
| 48 MSa/s | 57 ms | 22 ms (the link carries 36.5 MB/s) |

**Read strategies compared** (asynchronous, `depth` reads of `size` queued; captures with jumps):

| Reads | 8 MSa/s, 256K, 4 records | 16 MSa/s, 256K, 4 records | 16 MSa/s, 1M |
|---|---|---|---|
| one read | 4/4 (215 jumps) | 4/4 (113) | |
| 16 KB × 4 | 0/4 | 2/4 (3) | |
| 64 KB × 8 | 4/4 (86) | 3/4 (46) | |
| 4 KB × 32 | | | 2/6 (3) |
| 8 KB × 32 | | | 0/6, 1/10 (1) |
| 8 KB × 64 | | | 5/10 (8) |

- Reads must be small (≤ 16 KB) and enough must be queued (≥ 128 KB). **Chosen: 8 KB × 32** (`isds::ReadStrategy`); at
  48 MSa/s the link rose from 36.5 to 40.9 MB/s.
- Reads must be queued **after** the capture command: reads pending before it take the stream and the device refuses
  the command.
- After the fix (Linux over usbip, five 1M captures per rate): 1, 4 and 8 MSa/s without jumps, 16 MSa/s with an
  occasional one. The 100 kHz sine's SNR is 35 dB at 4 and 8 MSa/s (before: 33 and 19 dB), 34.5 dB at 16 MSa/s (13 dB).

**Windows, direct USB** (`star-loopback --contiguity N`, 1M captures):

| Reads | 1 MSa/s | 4 MSa/s | 8 MSa/s | 16 MSa/s |
|---|---|---|---|---|
| 8 KB × 32, 20 captures | 20/20 clean | 0/20, 1655 jumps | 0/20, 3021 | 0/20, 3615 |
| The same in RAW_IO mode | 20/20 | 5/20, 24 | 5/20, 49 | 15/20, 6 |
| RAW_IO; 4 KB × 64 below 16 MSa/s, 8 KB × 32 at 16 (the driver's choice), 30 captures | 30/30 | 30/30 | 30/30 | 22/30, 12 |

- WinUSB hands queued reads to the controller one after another and the buffer overflowed between them; in **RAW_IO**
  mode the reads wait together (libusb ≥ 1.0.28, `libusb_endpoint_set_raw_io`). Linux needs neither.
- 16 MSa/s (32 MB/s) is near the practical limit of USB 2.0 and is never entirely clean: about 0.3–0.4 jumps per 1M
  samples, on Windows as over usbip. Measured on one computer; another USB controller may want other sizes ❓.
- **Decision:** the timebase plan uses 16 MSa/s only for captures up to 64K samples (`reliable_samples`; a loss
  probability of 1–3 % per capture there); longer windows use 8 MSa/s. On the Spectrum tab the user chooses the rate, so
  a rare loss at 16 MSa/s can raise the noise floor slightly.

### Acquisition stopping by itself ✅

Shortly after Run the acquisition sometimes stopped with "timed out waiting for the device".
- **Cause:** the driver watched the control endpoint (EP 0x81) with reads of 100 ms timeout, cancelling each on timeout;
  an answer arriving at the moment of cancelling was lost over usbip. The command waiting for it failed after 1 s.
  It was frequent because the engine changes the sample rate twice every 10 frames for its ALIAS check, each change
  waiting for an acknowledgement.
- **Fix:** one read without a timeout always pending on the control endpoint, never cancelled until the device closes;
  heartbeats answered on the reader thread.

| Test (`star-usb-stress`) | Before | After |
|---|---|---|
| Commands 20–130 ms apart | 1 answer lost in 1194 (90 s) | 0 in 1558 (120 s) |
| Frames and pauses, 200 µs/div | an error after 240 frames in one run | 0 in 5000 frames |
| Slowest answer | 39.9 ms | 1.7 ms |

Windows, direct USB: all 712 commands answered in 120 s, the slowest in 16.9 ms; no error in 1260 frames. Unplugging the
device during a command gives `LIBUSB_ERROR_NO_DEVICE` and the program closes without hanging.

### 1020 contiguous samples at 48 MSa/s: the priming capture ✅

The manufacturer's Windows application (Multi VirAnalyzer 3.13.2.0) captured 1020 contiguous samples at 48 MSa/s, our
driver did not. Fitting one sine (1 MHz from the DDS) to the whole record, residual in 64-sample pieces:

| Record (48 MSa/s, Windows, direct USB) | Contiguous | Residual rms |
|---|---|---|
| Manufacturer's application, 1020 samples, CSV export | 12/12 | 0.38–0.44 lsb |
| Our driver, 252 samples (one block) | 20/20 | 0.37–0.42 lsb |
| Our driver, 1020 samples (four blocks) | 0/50 | ≈ 19 lsb |

The application's USB traffic (USBPcap, [building.md](building.md) §4) showed it asking for 1 048 576 samples per channel
and reading 2 MB at once; the 1020 samples it shows are raw 1028–2047, contiguous in 12 of 12 reads. Differences on the
reading side (one 2 MB read, delays, RAW_IO off, its command `02 ff 15`) made no difference. Replaying its command
sequence through our driver did, and narrowing it down found the cause:

| Sent before the 48 MSa/s captures | Contiguous |
|---|---|
| The application's whole sequence | 6/6 |
| The same without its low-rate captures | 0/6 |
| Only a switch to 1 MSa/s, no capture | 0/6 |
| 2048 samples at 1, 4 or 8 MSa/s | 6/6 |
| 512K samples at 16 MSa/s | 0/6 |
| 128K samples at 48 MSa/s | 0/6 |
| A capture at 1 MSa/s, then captures at 16 and 48 MSa/s | 6/6 (the effect lasts) |

- **One capture below 16 MSa/s after opening** makes every later 48 MSa/s capture start with 1024 contiguous samples
  after the leftover; without it only the first block of 256. It lasts until the connection is opened again. The
  application gets it unknowingly, opening on a slow timebase.
- **Fix:** the driver makes a priming capture of 2048 samples at 1 MSa/s when it opens (`Device::open`, `prime()`, 2 ms);
  `max_contiguous_samples(48 MSa/s)` rose from 252 to 1020. Checked: 50/50 contiguous on Windows over four read
  strategies; `star-loopback --contiguity` 20/20 on Windows, 10/10 over usbip.
- **In the application:** 1 µs/div now uses 48 MSa/s (the first 1020 samples only); 500 ns/div and below as before. The
  Spectrum tab makes 1024 points at 48 MSa/s from the contiguous start; Autoset's fastest capture is 1020 samples.
- Why ❓: the second 1024 samples are probably the device's FIFO filling at full rate after the command; why an unprimed
  device gives only one block is not known. The SDK's library also captures at 1 MHz first, yet its 1020 samples had gaps.

### Settling after a coarse gain change ✅

The first frame after a change of coarse gain could read a wrong Vpp. Two ×1 probes on a 10 kHz square, both channels
switched between ±3 V (G 0x52e) and ±1.1 V (G 0x109), captures right after the change or after a wait. The relays were
switched about a hundred times for this and only for this; they should be spared.

| After the change | 1 MSa/s, 16K samples | 48 MSa/s, 16K samples |
|---|---|---|
| No wait, to the fine stage (G 0x109) | first capture's DC 15 mV off, Vpp right | first capture 0.44 Vpp (right: 2.0) |
| No wait, to the wide stage (G 0x52e) | first capture full scale (6.3 Vpp) | first two captures full scale |
| 2, 5, 10, 20, 50 ms wait | right | right |

- The input reads wrong for about 2 ms after the relay switches; at 48 MSa/s that spans two captures. A change of the
  fine gain (D) is not a relay and showed nothing of the sort.
- **Fix:** the driver waits 5 ms after a change of coarse gain or coupling before the next capture (`kRelaySettle`),
  only when a relay actually switched. The coupling relay's settling was not measured; the same time is assumed ❓.

## 11. Spectrum measurements

`star-spectrum-bench` (`tools/spectrum_bench.cpp`) measures with the Spectrum tab's engine. DDS OUT into CH2 with a ×1
probe, user calibration; 0.5 V/div, 1 MSa/s, 64K points, average of 8 spectra unless said otherwise.

**1 kHz sine, 2 Vpp (0.707 Vrms = −3.01 dBV):**

| Window | Level | THD | SNR | SINAD | ENOB | SFDR |
|---|---|---|---|---|---|---|
| Hann | −3.06 dBV | −42.7 dB | 35.5 dB | 34.8 dB | 5.48 | 43.4 dB |
| Blackman-Harris | −3.06 dBV | −42.9 dB | 35.6 dB | 34.8 dB | 5.49 | 44.2 dB |
| Flat top | −3.06 dBV | −42.7 dB | 35.6 dB | 34.8 dB | 5.49 | 44.7 dB |
| Kaiser (β 9) | −3.06 dBV | −42.8 dB | 35.6 dB | 34.8 dB | 5.49 | 43.9 dB |
| Dolph-Chebyshev | −3.07 dBV | −42.9 dB | 35.5 dB | 34.8 dB | 5.49 | 44.2 dB |
| Blackman-Harris, 1M points | −3.07 dBV | −42.8 dB | 35.5 dB | 34.8 dB | 5.48 | 44.1 dB |

- The level is 0.05 dB low; the windows agree within 0.01 dB. Frequency 999.89 Hz (the −110 ppm of §4).
- The largest harmonic is H2 (−44.6 dBc); H3 −61.5, H4 −50.6, H5 −50.5 dBc. Small non-harmonic spurs (≈ −60 dBV at
  10–18 kHz; −48 dBc at 255/257 kHz) from the DDS or the input ❓.
- ENOB 5.5 bits with the signal over half the screen. An ideal 8-bit ADC gives about 7 bits at half scale; the rest is
  the input's noise and the ADC's non-linearity.

**6 Vpp, 1 V/div (75 % of the screen):** level +6.49 dBV (expected +6.53), THD −32.9 dB (H2 −34.5, H3 −38.5 dBc),
SNR 37.9 dB, SINAD 31.7 dB, ENOB 4.97. Distortion grows at this amplitude; DDS or input, not separated ❓.

**1 kHz square, 2 Vpp:** H3 −9.6, H5 −14.1, H7 −17.0 dBc (theory −9.54, −13.98, −16.90); even harmonics below −58 dBc,
so the duty is very close to 50 %. THD 41.2 % (theory to the 7th harmonic: 41.4 %).

**High frequencies, 2 Vpp sine, Blackman-Harris:**

| Frequency | Rate, points | Level | THD | SNR | SFDR |
|---|---|---|---|---|---|
| 100 kHz | 1 MSa/s, 64K | −4.31 dBV | −44.0 dB | 35.0 dB | 47.2 dB |
| 300 kHz | 1 MSa/s, 64K | −4.43 dBV | −45.3 dB | 34.9 dB | 49.3 dB |
| 2 MHz | 48 MSa/s, 256 | −6.06 dBV | −36.4 dB | 34.2 dB | 44.9 dB |
| 5 MHz | 48 MSa/s, 256 | −9.94 dBV | −32.3 dB | 29.9 dB | 40.7 dB |
| 10 MHz | 48 MSa/s, 256 | −14.63 dBV | −28.4 dB | 25.1 dB | 37.5 dB |
| 20 MHz | 48 MSa/s, 256 | −18.92 dBV | −23.9 dB | 22.4 dB | 32.4 dB |

- The fall in level follows the combined response of §5. The 48 MSa/s rows were measured with one block (256 points,
  32 blocks averaged), where the noise per bin stays high; the tab now uses 1024 points from the contiguous start (§10).

**Noise floor,** generator parked, inputs on DDS OUT, 500 kHz band:

| Scale | Noise | Per bin (RBW 30.6 Hz) |
|---|---|---|
| 50 mV/div | 2.05 mVrms | −95.9 dBV |
| 500 mV/div | 5.78 mVrms | −86.9 dBV |
| 1 V/div | 16.1 mVrms | −78.0 dBV |

**In the simulator** (`core/tests/test_spectrum.cpp`): the FFT against a direct DFT; the windows' equivalent noise
bandwidths (Hann 1.5, Blackman-Harris 2.004, flat top 3.77 bins); a sine's level on a bin and between two; THD, SNR,
SINAD and SFDR within 0.3–0.5 dB on signals with −40/−50 dBc harmonics and known noise; folded harmonics; averaging and
peak hold; the 48 MSa/s block average; the spectrum of the Math trace.

---

## 12. Pulse measurements

`star-loopback --pulses`, user calibration: DDS OUT into CH2 with a ×1 probe, 2 Vpp, mean of 16 captures. Times in µs;
`<`: the edge is too fast for the sampling.

| Signal | Rate | Top | Base | Amplitude | Overshoot % | Preshoot % | Rise | Fall | + width | − width | Duty % | Frequency |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Square 1 kHz | 1 MSa/s | 1.031 | −0.963 | 1.993 | 0.5 | 1.1 | 3.195 | 2.642 | 500.06 | 500.06 | 50.0 | 999.9 |
| Square 1 kHz | 16 MSa/s | 1.031 | −0.965 | 1.996 | 1.5 | 2.3 | 2.905 | 2.026 | 500.06 | 499.91 | 50.0 | 1000.0 |
| Square 10 kHz, 25 % | 16 MSa/s | 0.985 | −0.959 | 1.945 | 2.5 | 1.3 | 0.772 | 1.351 | 25.004 | 75.007 | 25.0 | 9998.9 |
| Square 100 kHz | 16 MSa/s | 0.903 | −0.843 | 1.746 | 2.5 | 2.2 | 0.156 | 0.153 | 5.000 | 5.001 | 50.0 | 99990 |
| Square 1 MHz | 48 MSa/s | 0.886 | −0.820 | 1.706 | 1.0 | 2.0 | 0.123 | 0.118 | 0.500 | 0.501 | 50.0 | 999831 |
| Sine 1 kHz | 1 MSa/s | 1.034 | −0.985 | 2.019 | 0 | 0 | 305.3 | 307.4 | 497.5 | 502.7 | 49.7 | 999.9 |
| Triangle 1 kHz | 1 MSa/s | 1.034 | −0.980 | 2.014 | 0 | 0 | 406.2 | 407.0 | 502.6 | 497.5 | 50.3 | 1000.0 |

- Widths and duty follow the settings; frequencies show the ≈ 105 ppm of §4.
- A sine's 10–90 % time should be arcsin(0.8)/π of the period (295 µs), a triangle's 400 µs; the noise on the peaks
  moves the thresholds outward and explains the 305 and 406 µs.

**A square edge has two parts:** a fast step to about 85 % in a few hundred ns, then a slow settling (§13). At 1 kHz the
90 % level falls into the slow part, so the rise time reads 2.9 µs — the edge's real shape, not an error. At higher
frequencies the signal has no time to settle, the top stays where the fast part got, and the rise time drops to the
fast part (0.12 µs at 1 MHz). The same is why 100 kHz reads 87 % of 1 kHz.

**Method** (`core/src/measure.cpp`):
- **Top and base:** from the histogram. A wave counts as two-level when fewer than 25 % of the samples lie between 25 and
  75 % of the swing; a level is a band holding at least 40 % of its half's samples, otherwise the peak is used.
- **Edges:** at the middle threshold with a hysteresis of 10 % of the amplitude; from the last sample below the low
  threshold to the first above the high one, interpolated linearly. An edge cut by the record's end does not count.
- **Resolution limit:** an edge shorter than 2 samples shows as `<` and stays out of the statistics; below that the
  10–90 % time measures the sampling, not the signal.
- **Delay and phase:** no result if the frequencies differ by more than 1 %. Two pulse trains: CH1's rising edges against
  CH2's matching ones, the pairing chosen within one period and kept. Other periodic signals: the phase of the
  fundamental over a whole number of periods. Not periodic: the nearest edges, no phase.
- **Frequency:** from the rising edges, or the falling ones if only those repeat.

**Speed:** `measure` takes 4–5 ms on 512K samples, 0.07 ms on 8K.

### Delay and phase between the channels

`star-loopback --delay`: both channels on DDS OUT with ×1 probes, 2 Vpp, 16 captures; ranges from repeated runs.

| Signal | Rate | Method | Delay | Std. dev. | Phase |
|---|---|---|---|---|---|
| Square 1 kHz | 16 MSa/s | edge | 1.0–1.1 ns | 0.3–0.4 ns | 0.000° |
| Square 100 kHz | 16 MSa/s | edge | 0–0.1 ns | 0.2–0.4 ns | 0.00° |
| Square 1 MHz | 48 MSa/s | edge | 0.3–0.4 ns | 0.6–0.8 ns | 0.10–0.14° |
| Sine 1 kHz | 1 MSa/s | fundamental | 160–165 ns | 20–25 ns | 0.06° |
| Sine 10 kHz | 16 MSa/s | fundamental | 99–100 ns | 2 ns | 0.36° |
| Sine 100 kHz | 16 MSa/s | fundamental | −0.9 ns | 0.6–1.0 ns | −0.03° |
| Sine 1 MHz | 48 MSa/s | fundamental | 0.6–0.8 ns | 0.3–0.4 ns | 0.21–0.28° |
| Sine 5 MHz | 48 MSa/s | fundamental | 0.8–0.9 ns | 0.1–0.2 ns | 1.4–1.5° |

- **Both channels sample at the same moment:** on fast edges and fast sines the difference is below 1 ns, a twentieth of
  the sample interval at 48 MSa/s, probes and inputs included.
- **The 0.06–0.36° at 1–10 kHz is real:** the edges arrive together while the fundamental moves by about 100 ns, because
  the slow settling differs a little between the channels (§13); by 100 kHz it is gone. Probes or inputs: not separated ❓.
- **Why sines use the fundamental:** an edge's time is limited by the voltage resolution over the slope, not by the
  sample interval. On a 1 kHz sine on the ±1.8 V stage 1 lsb is 3.9 µs, and edge-based delays came out −0.4 to −3.8 µs;
  the fundamental's phase gave +0.17 µs with 0.01–0.02 µs spread. On pulses the fundamental does not give the edges'
  delay (a 10 kHz square: edges 1 ns apart, fundamental 134 ns), so pulses use edges.

---

## 13. The input's response, with an independent source ✅

**Question:** is the fall of §5 and the slow settling of §12 the DDS's, or the probe's and input's?
**Answer:** the input's. The second oscilloscope's own generator as the source, two ×1 probes and its own probe on the
same point, the DDS disconnected; `star-loopback --external` and `--step`.

**Sine, 2 Vpp:**

| Reading | 1 kHz | 100 kHz | Ratio |
|---|---|---|---|
| The second oscilloscope's own display | 2.04 Vpp | 2.08–2.10 Vpp | about 1.02 |
| Into CH1, sine fit | 1.987 Vpp | 1.747–1.748 Vpp | **0.880** |
| Into CH2, sine fit | 1.983–1.984 Vpp | 1.730 Vpp | **0.872** |
| DDS into CH2 (§5) | | | **0.872** |

**The input reads 12–14 % low at 100 kHz with a ×1 probe.** The gain calibration, made at DC and 1 kHz, does not change it.

**Square 10 kHz, step response** (about 1600 rising edges averaged at 16 MSa/s, as a share of the step):

| t (µs) | 0.25 | 0.5 | 1 | 2 | 3 | 5 | 7.5 | 10 | 15 | 20 | 30 | 40 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CH1 | 0.860 | 0.875 | 0.885 | 0.906 | 0.916 | 0.934 | 0.959 | 0.967 | 0.983 | 0.990 | 0.999 | 1.000 |
| CH2 | 0.849 | 0.865 | 0.878 | 0.894 | 0.903 | 0.921 | 0.947 | 0.958 | 0.975 | 0.988 | 0.998 | 1.000 |

`1 − a·exp(−t/τ)` fitted to the slow part: **CH1 a = 12.3 %, τ = 7.7 µs; CH2 a = 14.2 %, τ = 8.3 µs.** This single time
constant predicts the sine response within 1 % up to 100 kHz (CH1 0.882 against 0.880 measured, CH2 0.864 against 0.872)
and the sign and size of the channels' phase difference at 1–10 kHz.

- The fast part: rise and fall 70–71 ns at 48 MSa/s (3.4 samples) on both channels, against 118–133 ns from the DDS; the
  input is at least that fast (0.35 / 70 ns ≈ 5 MHz).

### Possible cause ❓ (not verified)

The shape fits an input divider that is not compensated: at high frequency the signal is divided by the capacitors'
ratio, at low frequency by the resistors'. If the two ratios differ by 12–14 %, an edge first goes that far and the rest
follows with the RC time constant; τ ≈ 8 µs means about 8 pF on 1 MΩ. The probe's trimmer does not reach it (below), so
such a divider would be inside the device. A first-order digital correction per channel could undo it, with `a` and `τ`
measured per unit with `--step` (and different for ×1 and ×10). It is not implemented; whether to is an open decision.

### Compared with the manufacturer's software ✅

The other generator, 10 kHz square, 2 Vpp, two ×1 probes; Windows, direct USB, both programs in turn, the same stage.
Ours: `star-loopback --step`, default calibration. Theirs: one record exported as CSV (1 046 524 samples, ≈ 650 rising
edges), analysed the same way with `tools/vendor_csv.py --step`.

| t (µs) | 0.25 | 0.5 | 1 | 2 | 5 | 10 | 20 | 30 | 40 |
|---|---|---|---|---|---|---|---|---|---|
| CH1, ours | 0.860 | 0.875 | 0.883 | 0.905 | 0.933 | 0.967 | 0.990 | 1.000 | 1.001 |
| CH1, manufacturer's | 0.860 | 0.875 | 0.884 | 0.905 | 0.933 | 0.967 | 0.990 | 0.999 | 1.000 |
| CH2, ours | 0.849 | 0.864 | 0.877 | 0.893 | 0.921 | 0.958 | 0.988 | 0.999 | 1.000 |
| CH2, manufacturer's | 0.850 | 0.865 | 0.877 | 0.894 | 0.921 | 0.958 | 0.988 | 0.999 | 1.000 |

| | Slow part, one τ (ours / theirs) | Two τ, `tools/step_fit.py` (ours / theirs) |
|---|---|---|
| CH1 | 12.3 %, 7.9 µs / 12.2 %, 7.9 µs | 13.6 %, 6.5 µs / 13.5 %, 6.5 µs |
| CH2 | 14.5 %, 8.1 µs / 14.3 %, 8.2 µs | 14.7 %, 7.8 µs / 14.6 %, 7.8 µs |

- **The shelf is the same in both programs, within 0.001:** it comes from the input, not from how our driver sets the
  device up. The manufacturer's software does not correct it.
- Their Vmin/Vmax (−991.682 / +930.592 mV) equal what we read with the default calibration from the same kind of
  capture: their conversion is our default model.

### ×10 probes and the trimmer

**10 kHz square:** turning the trimmer changes the amplitude, hardly the settling. **1 kHz square,** trimmers at their
ends, fitted with two time constants (`tools/step_fit.py`):

| | Fast part | The trimmer's part |
|---|---|---|
| CH1 (lowest) | 11.0 %, 16.8 µs | slow rise 9.8 %, 184 µs |
| CH2 (highest) | 16.9 %, 15.2 µs | overshoot and fall 20.8 %, 112 µs |

- The trimmer works as expected, with a time constant of about 100 µs; at 10 kHz that is longer than half a period, so
  it shows in the amplitude, not in the edge's shape.
- **The fast part does not depend on the trimmer:** it is there at both ends, about the same share as at ×1 (11–17 %),
  with about twice the time constant (15–17 µs). The probe neither makes nor removes it. At ms/div, where the trimmer is
  set with a 1 kHz square, 8–17 µs is too narrow to see.
- After setting the trimmers at 1 kHz: CH1 12.3 %, 15.0 µs; CH2 14.9 %, 14.2 µs — the shares of ×1, twice the time.
- Edge at 48 MSa/s: 50–58 ns at ×10 (70 ns at ×1).

### Probe ratio

1 kHz square, 2 Vpp, trimmers set; amplitudes from the top and base levels, 16 captures.

| | CH1 | CH2 |
|---|---|---|
| ×1, at the BNC | 1.980 V | 1.983 V |
| ×10, at the BNC | 0.2000 V | 0.1966 V |
| Ratio | **9.90** | **10.09** |
| In the application with these factors, 500 mV/div | 2.007 V | 2.006 V |

At ×1 the channels agree within 0.2 %, at ×10 they differ by 1.7 %: the probes' ratios. With these factors (the probe
box takes any number) both channels read the same within 0.05 %. The remaining 1.3 % against ×1 came from the stages:
after the first DC-step calibration they still differed by up to 1.4 % on this square; the gain wizard's calibration
(§9) was not measured this way again ❓.

---

## 14. Known hardware limits: summary

The findings of the sections above in one place; method and detail in the section named. All measured on one unit;
which belong to that unit and which to the model is not known (§8). Analogue errors arise before any software, so the
manufacturer's software shows them too; where compared, it did (§13).

### Errors that can be corrected

They repeat, so software can correct them.

| Subject | Measured | Status | § |
|---|---|---|---|
| Default gain | About 6.5 % low in general, 2 % on the ±0.1 V stage; −11.9 % on the widest stage before calibration | The gain wizard corrects it (check within 0.2 %). Mainstream instruments: 1–3 % | §3, §8, §9 |
| Zero offset | 1–4 lsb, moving ±2 lsb over hours | Measure Zero; repeat now and then | §2, §9 |
| Probe ratio | 9.90 and 10.09 for the probes measured, about ±1 % | Free probe factor per channel | §13 |
| Input shelf | Slow settling after an edge: at ×1 CH1 12.3 % and 7.7 µs, CH2 14.2 % and 8.3 µs; at ×10 the same share, about twice the time. 100 kHz reads 0.88 and 0.87 | The probe trimmer does not remove it. A digital filter looks feasible; not done, decision open ❓ | §13 |
| DDS amplitude and offset | Offset ±70 mV from a straight line; amplitude set for 1 kHz | Would need a per-unit table; not done | §5 |

### Physical limits

Software can improve these only in part, or at a cost.

| Subject | Measured | Consequence | § |
|---|---|---|---|
| Speed | Edge 70 ns at 48 MSa/s (×1), 50–58 ns (×10); about 5–7 MHz, the source's edge included | Amplitudes in the MHz range read low, and depend on the scale | §10, §13 |
| Noise | 2.05 mVrms at 50 mV/div, 5.78 at 500 mV/div, 16.1 at 1 V/div. SNR about 35 dB, ENOB 5.5 bits (signal over half the screen) | Quantisation dominates on wide stages; small signals want a fine scale | §11 |
| 48 MSa/s | USB 2.0 cannot carry it: every capture starts with 1020 contiguous samples (after the priming capture), then blocks of 256 with gaps | Reliable up to 1020 samples (21 µs); the fastest continuous rate for long records is 16 MSa/s | §10 |
| 16 MSa/s | 32 MB/s is near what USB 2.0 carries in practice; occasional losses in captures over 64K samples | Long windows use 8 MSa/s | §10 |
| Device buffer | About 1024 samples, 64 µs at 16 MSa/s | If the USB link pauses longer, data are lost | §10 |

### Unexpected behaviour

| Subject | Measured | Status | § |
|---|---|---|---|
| The generator depends on the inputs | Changes with the inputs' coarse gain without any command: 5 V set gives 4.94 V → 5.93 V (one input) → 8.24 V (both); the DC moves −130 mV at 1 V | A warning on the Generator tab. Correcting it needs four measurements per unit | §5 |
| Output at power-up | About +4.9 V DC until the generator is set | The application parks it at 0 V when it connects | §5 |
| Output switched off | The wave stops, the DC offset stays | Do not take a switched-off output for 0 V | §5 |
| Amplitude limit | Distortion starts a little above 5.6 Vpp; clipped at about 9.7 Vpp | About 5.6 Vpp for a clean signal | §5 |

### Against the manufacturer's specifications

| Specification | Measurement | Verdict |
|---|---|---|
| 20 MHz bandwidth | The edges point to 5–7 MHz, the sources' own limits mixed in; not measured with a flat source | Not verified ❓ |
| 48 MSa/s | Available; 1020 contiguous samples per capture, in the manufacturer's software as in ours | Met, for short records |
| 1 MB buffer per channel | The device's buffer is about 1024 samples; long records are kept on the computer at rates USB keeps up with | Partly |
| DDS at least 9 Vpp | Reaches about 9.7 V, clipped; clean up to about 5.6 V | Partly |
| DDS sine to 20 MHz | The frequency is there; the amplitude at 20 MHz is −15.9 dB through a ×1 probe, the DDS's own share not separated ❓ | Partly |
| DDS offset ±2.5 V | Codes span +4.86 V to −4.79 V; ±70 mV from linear | Met |
| 1 MΩ‖25 pF input, 200 Ω output | Not measured | Unknown |

### What is good

- **Channel matching:** the two channels sample within 1 ns; phase differences at low frequency are small (0.06° at 1 kHz,
  0.36° at 10 kHz, §12); at ×1 they read the same signal within 0.2 % (§13); CH1 − CH2 suppresses a common signal by
  28–50 dB up to 200 kHz (§15).
- **Linearity:** within 1 lsb at DC on the fine stages (§3).
- **Repeatability:** the errors repeat, which is why calibration works (§2, §9).
- **Time:** widths and duty follow the settings; the DDS and the sample clock differ by about 105 ppm (§4, §12).
- **Inside the 48 MSa/s blocks** the samples are clean; sin(x)/x is right to the noise level down to 2.4 samples per
  period (§10).

### What it can be trusted for

- **Trustworthy:** timing, frequency, duty, waveform shape, comparing two channels; after calibration DC and amplitudes up
  to about 1 kHz.
- **Read with care:** AC amplitudes above 10 kHz (the shelf; 12–13 % low at 100 kHz); amplitudes in the MHz range (lower,
  and dependent on the scale).
- **Not a reference:** the internal generator's set amplitude, especially with the inputs on fine stages and above
  3 Vpp. If a known amplitude matters, measure the output.

### Open

- Data from other units (§8).
- Bandwidth with a flat source; input and output impedance.
- AC coupling.
- How the coarse gain affects the DDS: only the effect is measured, not the circuit.
- A 400 kHz spur in opposite phase on the two channels (−57 to −62 dBV at 0.5 V/div) (§15); whether it is there with
  nothing connected and on other stages.
- Whether the sample clock is about 100 ppm fast: needs a source of known accuracy (§4, §15).

---

## 15. Math in the spectrum: one signal on both channels ✅

**Question:** is the Math trace on the Spectrum tab (the Oscilloscope tab's Math: CH1 + CH2, CH1 − CH2, CH1 × CH2) right?

**Setup:** an external generator's 10 kHz, 2 Vpp sinc pulse, split with a T to both channels; the internal generator
off. Default calibration, both channels 0.5 V/div DC, position 0 (±3 V stage, 1 lsb ≈ 24.7 mV). 1 MSa/s, 64K points,
Blackman-Harris, 8 spectra averaged. `star-spectrum-bench --pair`; the application's own display showed the same.

**The signal:** a sinc pulse has a rectangular spectrum: a flat comb of lines 10 kHz apart up to about 200 kHz, about
−26 to −27 dBV per line. DC is about −0.6 V (the pulse's peak is up, its long tail below zero).

| Frequency | CH1 | CH2 | CH1 + CH2 | Expected in phase | CH1 − CH2 | Difference / CH1 | CH2 − CH1 |
|---|---|---|---|---|---|---|---|
| 10 kHz | −26.1 | −25.8 | −19.9 | −19.9 | −54.4 | −28.3 dB | +0.33 dB |
| 20 kHz | −26.6 | −26.9 | −20.7 | −20.7 | −57.2 | −30.6 dB | −0.27 dB |
| 40 kHz | −26.6 | −26.7 | −20.6 | −20.6 | −75.8 | −49.2 dB | −0.05 dB |
| 100 kHz | −26.9 | −26.9 | −20.9 | −20.9 | −66.0 | −39.1 dB | −0.10 dB |
| 150 kHz | −27.3 | −27.3 | −21.3 | −21.3 | −73.1 | −45.9 dB | −0.01 dB |
| 190 kHz | −26.9 | −26.7 | −20.7 | −20.7 | −59.5 | −32.7 dB | +0.20 dB |

Levels in dBV; all 20 lines of the comb were measured, the table is a selection.

- **The sum is right:** on all 20 lines within 0.1 dB of the in-phase sum (+6 dB). The noise floor rises about 3 dB (the
  channels' noise is uncorrelated), so the sum is 3 dB cleaner.
- **The difference is right:** the common signal is suppressed by 28–50 dB, and what remains is explained line by line by
  the channels' level difference (0.33 dB alone leaves −28.2 dB; measured −28.3 dB). The timing's share is too small to
  measure: even at 200 kHz the suppression is 39 dB, which means less than 10 ns between the channels (§12: within 1 ns).
- **The channels differ by ±0.4 dB line by line, with the sign changing from line to line.** A real gain or frequency
  response difference would not jump like that. The lines are only 2–3 lsb high (peak ≈ 65 mV) and the signal is
  periodic; probably each channel's own quantisation and non-linearity errors, which fall on the harmonics of a periodic
  signal ❓.
- **The product is right:** CH1 × CH2 = x², DC 0.407 V² (expected ≈ 0.41), lines up to 400 kHz, the shape a squared signal
  with DC gives (x = d + s: x² = d² + 2d·s + s²).
- **Units:** the product reads in dB(V²) or V², the ratio (÷) in dB or without a unit; both channels −12.8 dBm, the
  product −29.7 dB(V²).
- **A 400 kHz spur in opposite phase:** CH1 −62.2, CH2 −57.4 dBV; larger in the difference (−53.4 dBV), smaller in the sum
  (−64.6 dBV). From the input it would cancel in the difference, so it comes from inside the oscilloscope. Not measured
  with nothing connected ❓.
- **Clock:** the external source read 9999.0 Hz (−100 ppm), the internal DDS about −105 ppm (§4): two different sources,
  the same deviation, so the sample clock is probably about 100 ppm fast ❓.

**What it is good for:**
- **CH1 − CH2:** the spectrum of a voltage between two points neither of which is ground (a shunt, a balanced line), with
  the common signal suppressed by about 30–50 dB. The limit is the channels' level difference, not timing; a gain
  calibration and a stage close to the signal should help ❓. Neither channel may clip.
- **CH1 + CH2:** the common-mode noise of a balanced line; with one signal on both channels a spectrum 3 dB cleaner, in
  which internal spurs in opposite phase (400 kHz) shrink.
- **CH1 × CH2:** the instantaneous product: with CH1 a voltage and CH2 a shunt's current (probe factor 1/R), power — its DC
  the mean power, its lines the power's ripple. With one signal on both channels, squaring: the envelope of an AM carrier
  comes down to low frequency. Two different frequencies give their sum and difference (a mixer).

---

## 16. Why one channel can look more stepped ✅

**Observation:** the same signal on both channels (10 kHz sinc, 2 Vpp), both 500 mV/div, 16 MSa/s; CH1's trace clearly
stepped, CH2's smoother, though both are drawn the same way.

**Measurement:** `star-loopback --grain [CH1-POS CH2-POS]` leaves the generator alone and measures each channel's ADC
step and noise in steps; the noise from the second difference, to which a slow signal hardly contributes.

| Position (CH1 / CH2) | CH1 step | CH2 step | CH1 noise | CH2 noise |
|---|---|---|---|---|
| 0 / 0 div | 24.6 mV (±3 V stage) | 24.8 mV | 0.35 step (8.6 mV) | 0.36 step (9.0 mV) |
| +1.9 / −1.76 div | 24.6 mV | 24.8 mV | 0.35 step | 0.36 step |
| +2.0 / −1.76 div | **43.7 mV (±5.5 V stage)** | 24.8 mV | 0.39 step (16.9 mV) | 0.36 step |
| +2.2 / −2.0 div | **43.7 mV** | 24.8 mV | 0.39 step (16.8 mV) | 0.36 step |

- **It is the stage:** the position is a digital shift on the screen, with no analogue offset at the input, so the stage
  must take the whole voltage the screen covers without clipping (the driver keeps a margin of 2 codes at both ends).
  At 500 mV/div and +2.2 div the bottom of the screen is −3.1 V at the BNC. CH1's ±3 V stage reaches −3.04…+3.25 V with
  its factory offset, −2.99 V with the margin: not enough, so the driver picks ±5.5 V and the step grows 1.8× (43.7 mV,
  5 pixels at 59 px/div). CH2 at −1.76 div needs +2.88 V, within its ±3 V stage.
- **The threshold differs per channel and direction:** the stages' ranges are shifted by the factory offsets (CH1
  −3.04…+3.25, CH2 −3.23…+3.09 V). On the measured unit CH1 stays on the fine stage up to +1.9 div at 500 mV/div.
- **Not the noise:** about a third of a step on both channels; it hides the steps on neither.
- **Not the probe:** the same probe and signal change step when the position changes.
- The two stages share the coarse gain and differ in the fine gain: no relay switches.
