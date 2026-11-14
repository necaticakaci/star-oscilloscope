# ISDS205 USB protocol

How the Instrustar ISDS205 series talks over USB, as far as Star Oscilloscope uses it. It was learned from the
manufacturer's library (`libvdso.so` on Linux) and their Windows application:
- their USB traffic was recorded (`tools/trace_vdso.sh`, `tools/usb_capture.ps1`; [building.md](building.md) §4);
- every finding was then checked without their software (`tools/native_poc.py`, then the C++ driver in `driver/`).

The reference implementation in Python is `tools/isds205x.py`.

All tables of values come from one ISDS205X(1.1), "the measured unit" below. Other units carry their own calibration
in their descriptor (§7).

Marks: ✅ checked without the manufacturer's software · 🔎 seen in recordings, meaning inferred · ❓ open question

---

## 1. USB

| | |
|---|---|
| VID:PID | `D4A2:5661`, manufacturer "YiXingDianZiKeJi", product "YX-DSO" |
| Interface | 0, class 0xFF, no firmware upload |
| `EP 0x01` OUT bulk 64 B | commands |
| `EP 0x81` IN bulk 64 B | acknowledgements, handshake, heartbeat |
| `EP 0x82` IN bulk 512 B | sample data |

- Only bulk transfers are used.
- On Windows the device uses Microsoft's WinUSB driver, which the manufacturer's software binds to it with an INF file
  ([building.md](building.md) §1.2). On Linux libusb reaches it directly; a udev rule gives the user access.
- The rear switch must be on "Logic" (Multi VirAnalyzer). In its "Saleae Logic" position the device shows up as
  `0925:3881` instead, a logic analyser for sigrok/PulseView.
- A read without a timeout should always be pending on `EP 0x81`. A read cancelled by its timeout could lose a message
  arriving at that moment over usbip ([measurements.md](measurements.md) §10).

## 2. Frame format ✅

```
[type][target][command][u32 big-endian][...]
```

| Field | Values |
|---|---|
| type | `00` system (heartbeat) · `01` DDS · `02` oscilloscope |
| target | `ff` whole device · `01` CH1 · `02` CH2 |

- **Value form (8 bytes):** u32 = the value, then one padding byte `00`. Example: `02 01 19 00000001 00` = CH1 AC coupling.
- **Payload form:** u32 = payload length, then the payload. Example: `02 01 06 00000002 0109` = CH1 gain code 0x109.
- **Exception, command `13`:** u32 = 0, then a 4-byte payload: `02 ff 13 00000000 00 73 00 08`.

**Acknowledgement:** the device answers every command with 4 bytes on `EP 0x81`: `[type][target][command][status]`.
The status is usually `01`, `10` for command `13`. The capture command (`02`) is not acknowledged; the data simply come.

## 3. Opening a session ✅

```
host → 10 B  ff ff ff ff ff 11 22 33 44 55      sync; the device echoes it
host →  5 B  H (random)                         the library uses an unseeded rand(); any random bytes work
dev  →  5 B  D
host → 10 B  H ‖ D
dev  → 334 B descriptor (§7)
host →  1 B  aa                                 the device echoes aa
```

Then the library's opening commands, which `native_poc.py` repeats:
`12`(ff) → `01`(CH1, CH2) → `08` 48 MHz → `06`=0x52e (CH1, CH2) → `04`=0x62c (CH1, CH2) → `19`=DC (CH1, CH2).
The C++ driver then makes one priming capture of 2048 samples at 1 MSa/s and returns to 48 MHz (§6).

**The manufacturer's application opening the device** (USB recording):
- Its oscilloscope engine opens the device only while a channel is on. With both channels off it shakes hands, reads the
  descriptor and closes the connection half a second after `aa`, without capturing.
- Sequence: `12` → `01` (the enabled channel only) → `15` → `08` 4 MHz → `06`, `04` → `19` → captures straight away.
  Turning the other channel on sends its own `01`.
- H in the handshake is always `29 23 be 84 e1`: the low bytes of the first five values of an unseeded MSVC `rand()`
  (41, 18467, 6334, …).
- The generator panel shares the connection; DDS commands are sent only once Output is switched on.

**An interrupted capture ✅:**
- If the previous host closed in the middle of a capture (a crash, `kill`), the rest of that capture waits on `EP 0x82`.
- The device does not echo the sync until those data are read, so the handshake times out.
- The C++ driver and `native_poc.py` drain `EP 0x82` before the handshake.

## 4. Heartbeat ✅

The device sends `00 ff fe 10` once a second. The host answers `00 00 fe 00000002 00`.
❓ What happens without an answer has not been tried.
In the manufacturer's application the order looks reversed: the host sends `00 00 fe …` and the device answers
`00 ff fe 10` within 0.5 ms ❓.

## 5. Oscilloscope commands (type `02`)

| Command | Target | Form | Meaning |
|---|---|---|---|
| `12` | ff | value 0 | start the oscilloscope ✅ |
| `01` | ch | value 0 | channel on ✅ |
| `10` | ch | value 0 | channel off / stop ✅ |
| `08` | ff | payload u32 | sample rate, Hz ✅ |
| `06` | ch | payload u16 | **G**: coarse gain code, `0x109` (high gain) or `0x52e` (low gain); their ratio is about 5 ✅ |
| `04` | ch | payload u16 | **D**: fine gain code; the lsb is inversely proportional to D ✅ |
| `13` | ff | 4 B payload | trigger: `00 <level code> 00 <hysteresis code>` 🔎 |
| `19` | ch | value 0/1 | coupling: 0 = DC, 1 = AC ✅ |
| `02` | ff | value | capture; the value is the number of samples per channel ✅ |
| `15` | ff | value 0 | ❓ acknowledged as `02 ff 15 10` (status `10`, like `13`). The manufacturer's application sends it once when opening; it makes no difference to the 48 MSa/s continuity |

- **The hardware has no vertical offset (position).** Asymmetric ranges only change the gain stage; the position is
  applied in software.
- The library's `SetTriggerLevel/Style/Source/Mode` and `SetPreTriggerPercent` send nothing over USB. The trigger is
  searched in software (`IsSupportHardTrigger` = 0); the library still sends command `13`.
- `13` level code = `floor(zero + level_mV / lsb)`. Example: at ±1 V the zero is 115.49 → `0x73`.
  Hysteresis is about `sensitivity_div × (max − min) / 10 / lsb` (the rounding rule does not fit exactly ❓).
- `06` and `19` switch relays in the input. The driver waits 5 ms after either before capturing; without the wait the
  first frame after a change read low ([measurements.md](measurements.md) §10).

## 6. Data ✅

- `EP 0x82` delivers `samples × 2` bytes: unsigned 8 bit, **CH1 and CH2 interleaved** (`c1 c2 c1 c2 …`).
- **The first 1024 samples are what was left in the FIFO from before**, followed by about 4 samples of transition.
  The library drops the first 1028 samples (`isds205x.SETTLE_SAMPLES`). Our driver asks for `wanted + 1028` samples,
  rounded up to a multiple of 1024.
- **At 48 MSa/s the data are not continuous ✅** ([measurements.md](measurements.md) §10).
  - The device sends every sample asked for, but in blocks of 256.
  - Between the blocks there are gaps of varying length: sometimes none, sometimes a few µs. The jumps are at raw index
    1024 + 256·k.
  - After the 1028 samples dropped, 252 remain in the first block. The 1020 samples the library returns at this rate
    span four blocks, so they have gaps too.
  - The manufacturer's Windows application (Multi VirAnalyzer 3.13.2.0, `usbwave.dll`) does get 1020 contiguous
    samples at this rate. That led to the priming capture below.
  - **Priming capture ✅:** one capture below 16 MSa/s after opening makes every later 48 MSa/s capture deliver the
    1024 samples after the leftover (raw 1024–2047) without gaps; the gaps then start at raw 2048 + 256·k. Without it
    only raw 1024–1279 are contiguous. The effect lasts until the connection closes and does not depend on how the host
    reads. The manufacturer's application gets it by chance, because it opens on a slow timebase. Our driver captures
    2048 samples at 1 MSa/s when it opens.
  - At 4–16 MSa/s samples are lost too when the host does not keep up (below).
- **The device streams while it captures ✅:**
  - It has a buffer of about 1024 samples (2 KB). Samples nobody reads are lost; the 48 MSa/s blocks come from this.
  - The device keeps streaming as long as the host reads, without a new command; it stops only when the buffer is full.
    The 1024 samples at the start of every capture are this buffer, filled after the previous capture.
  - Reads must be queued **after** the capture command. Reads already pending take the stream and the device refuses
    the command.
  - The driver keeps 32 asynchronous reads of 8 KB queued (`isds::ReadStrategy`). One large read lost samples over
    usbip even at 8 MSa/s.
  - On Windows the data endpoint is read in WinUSB's RAW_IO mode. Without it WinUSB hands the queued reads to the
    controller one after another and the buffer overflowed between them at 4 MSa/s and faster. This needs libusb 1.0.28
    or later.
  - Driver: `Device::max_contiguous_samples(hz)` (1020 at 48 MSa/s; 252 before priming) and `block_samples(hz)` (256).
  - The application uses 48 MSa/s when the screen window fits in one block (≤ 500 ns/div; the trigger is searched inside
    the blocks) or when two windows fit in the contiguous start (1 µs/div; then only those 1020 samples are captured).
  - 16 MSa/s (32 MB/s) is more than a computer always keeps up with: in captures longer than 64K samples a sample is
    lost now and then. The application uses 8 MSa/s for longer windows (`reliable_samples`).

## 7. Descriptor (334 B, version 0x0a)

| Offset | Type | Content |
|---|---|---|
| 0x00 | u8 | version (`0a`) |
| 0x01 | 16 B | manufacturer "YiXingDianZi" |
| 0x11 | 16 B | model, e.g. "ISDS205X(1.1)" |
| 0x28 | u32 | base clock 48 000 000 Hz |
| 0x32 | u8 | number of rates (5) |
| 0x33.. | 2 B × n | `[01][divider]`: rate = clock / divider → 48, 12, 6, 3, 1 → 1, 4, 8, 16, 48 MSa/s |
| 0x3d | u8 | ADC bits (8) 🔎 |
| 0x3f / 0x43 | s32 | −500 / +500 ❓ |
| 0x47 | u8 | number of G codes (2) |
| 0x48.. | u16 × n | G codes: `0109`, `052e` |
| +2 B | | `0000` ❓ |
| then | u8 | number of levels (4) |
| then | level × channel × `[u16 D][s32 cal_G0][s32 cal_G1]` | **calibration table** |
| 0xa0.. | | ±6000 (input range?), DDS and other blocks ❓ |
| last 8 B | u64 LE | device serial number (`GetOnlyId0/1`) |

The table of the measured unit:

| Level | D (CH1/CH2) | cal mV CH1 (G0, G1) | cal mV CH2 (G0, G1) |
|---|---|---|---|
| 0 | 0x77 / 0x76 | −9, 2 | −76, −323 |
| 1 | 0xd3 / 0xd2 | 17, 105 | −19, −71 |
| 2 | 0x200 / 0x1fe | 29, 122 | 17, 65 |
| 3 | 0x62c / 0x62c | 33, 166 | 33, 166 (the same on both channels; possibly a factory default ❓. It is the stage with the most accurate gain: [measurements.md](measurements.md) §3) |

`tests/data/descriptor_isds205x_1.1.bin` is this descriptor with the serial number replaced by 1.

## 8. Calibration model ✅

```
lsb_mV = 1000 × G / (255 × D)
V_mV   = lsb_mV × (raw − 127.5) + cal[level][channel][G]
```

- `127.5` is a double constant in the library; everything else comes from the device's descriptor.
- C++: `driver/src/calibration.cpp`, tested against the golden data in `tests/data` to 1e-12 V.
- The model reproduces the library's `ReadVoltageDatas` output for 82 captures per channel with an error of
  **0.000000000 mV** (`tools/verify_cal.py`).
- With `native_poc.py --scan`, the means on all 7 stages and both channels differ from the library's by ≤ 0.36 lsb.

**Absolute accuracy:** the model gives the same result as the manufacturer's software, but the measured unit's table has
two kinds of error:
- a zero offset of 1–4 lsb;
- a gain about 6.5 % low, 2 % on the ±0.1 V stage.

The measurements and the user calibration that corrects them are in [measurements.md](measurements.md) §2–3.

The stages of the measured unit (the library never uses G1 with level 3, and neither do we):

| G | D (CH1/CH2) | lsb mV (CH1/CH2) | Range CH1 | Range CH2 |
|---|---|---|---|---|
| 0x109 | 0x62c | 0.658 / 0.658 | −51 … +117 | −51 … +117 |
| 0x109 | 0x200 / 0x1fe | 2.030 / 2.038 | −230 … +288 | −243 … +277 |
| 0x109 | 0xd3 / 0xd2 | 4.925 / 4.949 | −611 … +645 | −650 … +612 |
| 0x109 | 0x77 / 0x76 | 8.733 / 8.807 | −1122 … +1104 | −1199 … +1047 |
| 0x52e | 0x200 / 0x1fe | 10.156 / 10.196 | −1173 … +1417 | −1235 … +1365 |
| 0x52e | 0xd3 / 0xd2 | 24.645 / 24.762 | −3037 … +3247 | −3228 … +3086 |
| 0x52e | 0x77 / 0x76 | 43.697 / 44.068 | −5569 … +5573 | −5942 … +5296 |

**Choosing a stage:**
- The library uses roughly fixed thresholds (±50, ±250, ±600, ±800, ±1200, ±3000 mV) and ignores the asymmetric ranges.
  At ±1200 it picks, for CH1, a stage that ends at −1173 mV.
- Our rule (`Calibration::select`): the stage with the smallest lsb that covers the range asked for with a margin of two
  codes at both ends, chosen per channel. The position being digital, the range is the whole screen at the input.

## 9. DDS commands (type `01`)

Sources: the library's traffic and a disassembly of its `HsfUsbWaveISDSXBVer7` class. Checked with a cable from DDS OUT
to an input ([measurements.md](measurements.md) §5).

| Command | Target | Meaning | Example |
|---|---|---|---|
| `12` | ff | start / reset the DDS ✅ | `01ff1200 00000000` |
| `40` | 01 | waveform, `1 << style`: sine `01`, square `02`, triangle `04`, rising ramp `08`, falling ramp `10` ✅ | `01014000 00000100` |
| `42` | 01 | frequency, Hz ✅ | 1 kHz `000003e8`, 1 MHz `000f4240` |
| `44` | 01 | duty, **inverted**: 100 − duty ✅ | 50 % → `32`, 25 % → `4b` |
| `47` | 01 | **amplitude** code, 12 bit ✅ | `SetDDSZoomResistance(1000)` → `01014700 00093500` |
| `48` | 01 | **offset** code, 12 bit: 4095 − bias ✅ | `SetDDSBiasResistance(2048)` → `01014800 0007ff00` |
| `43` | 01 | sweep: 16 B payload, 4 × u32 BE: start Hz, stop Hz, step?, time? 🔎 | `01014300000010` `000003e8 000007d0 0000000a 00000005` |
| `45` | 01 | sweep waveform 🔎 | |
| `46` | 01 | sweep duty 🔎 | |
| `01` | ff | output on ✅ | `01ff0100 00000000` |
| `10` | ff | output off ✅ | `01ff1000 00000000` |

**Encodings:**
- **Amplitude:** `code = int(((20·log10(zoom/100) − 10) / 40 + 1.65) × 4095 / 3.3)`, zoom 32…3162, library default 1597.
  - The formula turns a VGA control voltage centred on 1.65 V at 25 mV/dB into the code of a 12-bit, 3.3 V DAC.
  - The Vpp each code gives was measured ([measurements.md](measurements.md) §5; 0x800 ≈ 1.06 Vpp).
- **Offset:** `code = 4095 − bias`, bias 0…4095. A larger code lowers the output: 0x000 ≈ +4.9 V, 0xfff ≈ −4.8 V.
- The library sends amplitude and offset only when the model string starts with `ISDS205X`. The ISDS205B has a
  generator without level control (§10).

**Order of sending:**
- Nothing about the DDS is sent until `DDSOutputEnable(1)`.
- Switching the output on sends `12`, `40`, `42`, `48`, `47`, `44`, `01`.
- While the output is on, only the setting that changed is sent.

**Behaviour:** switching the output off leaves its DC offset at the output. After power-up the output sits at about
+4.9 V DC ([measurements.md](measurements.md) §5). The amplitude and DC level also depend on the inputs' coarse gain
(`06`): with both inputs on the high-gain code the output is up to 20 % larger, and much more above 3 Vpp
([measurements.md](measurements.md) §5).

**Parking:** the way to bring the output to 0 V is to switch it on with the smallest amplitude and a 0 V offset and
switch it off again; level commands are sent only while the output is on. It settles near −10 mV
(`Source::park_generator`).

C++: `driver/include/isds/generator.hpp`, `Device::set_generator` / `set_generator_output`, `isdsctl gen`.

## 10. The model family 🔎

The USB transport, the handshake (`UsbDevVer5`) and the frame format are shared by all models. Only the acquisition
class is specific; the library picks it by a prefix of the device's identity string (`BasicDev::GetDesId`,
in `CreatSbqUsbWave`, `CreatHsfUsbWave`, `CreatJlyUsbWave`):

| Identity prefix | Oscilloscope class | DDS class | Recorder class |
|---|---|---|---|
| `ISDS205` (205, 205B, 205X…) | `SbqUsbWaveISDS205Ver7` | only `ISDS205B` and `ISDS205X`: `HsfUsbWaveISDSXBVer7` | `JlyUsbWaveISDS205` |
| `ISDS210` | `ISDS210Ver7` / `Ver11` (by version byte) | `ISDS210B` | — |
| `ISDS220` | `ISDS220Ver7` | `ISDS220B` | — |
| `ISDS2062` | `ISDS2062Ver10` / `Ver11` | `ISDS2062B` | — |
| `MSO20` | `MSO20Ver1` | `MSO20Ver1` | `MSO20Ver1` |

What follows:
- **The whole ISDS205 series** (A/B/C/X) runs the same oscilloscope code, so our driver and application should work on
  all of them unchanged. Only the ISDS205X(1.1) has been tried.
- Only B and X have the DDS. `ISDS205A`/`ISDS205C` appear nowhere in the library; those models probably report a plain
  `ISDS205`.
- The other families (210, 220, 2062, MSO20) would not need a whole new driver: a model-specific acquisition module on
  top of the shared layer (sampling commands, data handling, perhaps another descriptor layout). Someone with such a
  device could write it with the record/fit/verify workflow of [building.md](building.md) §4.
- The library looks for `D4A2:5661` and `D4A2:5662`; which model is 5662 is not known. The Windows DLL also lists 5640,
  5650 and 5660.

**In Star Oscilloscope:**
- The driver opens `D4A2:5661` only. If after the handshake the descriptor's model does not start with `ISDS205`, it
  sends the device nothing and says so ("ISDS210B is not supported: this program drives the ISDS205 series";
  `isds::supported_model`), so another family on the same USB IDs is not driven wrongly.
- The generator follows the model: on B and X (`has_generator`), amplitude and offset only on X (`has_level_control`):
  - **No generator** (plain ISDS205, probably A and C): the Generator tab, panel and strip are off; no gain wizard.
  - **ISDS205B:** Generator on; the amplitude and offset boxes are off and a note says why. The output is not parked at
    0 V, since parking without levels would mean switching the output on for a moment. No gain wizard, which sets its
    DC levels with the DDS offset.
  - **ISDS205X:** everything.
- The simulator stands in for a model with `--model` ([building.md](building.md) §1.1); the GUI tests check these cases
  that way. Not tried with a real A, B or C ❓.

## 11. Open questions

1. Not yet checked in hardware: the AC coupling's behaviour in detail, the hardware sweep (`43`). A difference of about
   110 ppm between the DDS and the sample clock remains; a frequency counter would tell which one drifts.
2. The rest of the descriptor (±500, ±6000, the blocks after 0xa0) and devices with a version other than 0x0a. The
   factory DDS calibration may be after 0xa0. The library's `UsbDevVer5::HandshakeOk` (the branch for version ≥ 10) and
   `HandshakeOk_Osc` could tell more.
3. The extra captures the library makes on its own at the first capture (1M samples at 16 MHz and 1 MHz): what for?
4. The device's behaviour when the heartbeat goes unanswered; timeouts on long (1M) captures.
5. Roll mode, the recorder (`Jly`), the 16-channel logic analyser and the sweep cannot be triggered through the SDK; their
   traffic has to be recorded from the manufacturer's Windows application (USBPcap).
