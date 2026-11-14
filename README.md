# Star Oscilloscope

Open-source oscilloscope software for the Instrustar ISDS205 series USB oscilloscopes, working through its own USB
driver, on Linux and Windows. It has a sober, classic instrument look, and its measurements have been checked against
known sources ([docs/measurements.md](docs/measurements.md)).

The manufacturer's software runs on Windows only, and sigrok does not support the oscilloscope mode of these devices.
This project is not affiliated with Instrustar.

## Supported devices

The whole ISDS205 series (ISDS205, 205A, 205B, 205C, 205X) uses the same acquisition, so Star Oscilloscope should drive
all of them ([docs/protocol.md](docs/protocol.md) §10). **It has been tested only with the ISDS205X.** Reports from
other models are very welcome.

- The signal generator is used on the ISDS205B and ISDS205X; amplitude and offset are set on the ISDS205X only.
- The switch on the back must be on "Logic" (Multi VirAnalyzer), not "Saleae Logic".

## Features

- **Oscilloscope:** two channels, 1 to 48 MSa/s, timebases from 100 ns/div to seconds, software trigger (Auto, Normal,
  Single; edges with hysteresis), Autoset, horizontal zoom with an overview of the record, sin(x)/x interpolation,
  CLIP and ALIAS warnings.
- **Measurements:** a selectable list with statistics: frequency, period, duty, amplitudes, top and base, RMS, rise and
  fall times, pulse widths, overshoot, delay and phase between the channels; time and voltage cursors.
- **Math and references:** CH1 ± CH2, CH1 × CH2, CH1 ÷ CH2; two reference waveforms; X-Y mode.
- **Spectrum:** FFT of 8 to 1M points, 18 windows, V, dBV, dBmV, dBu and dBm, averaging, peak hold, a logarithmic axis,
  THD, SNR, SINAD, ENOB, SFDR and harmonics; the spectrum of the Math trace.
- **Signal generator:** sine, square, triangle and ramps, frequency, duty, a software sweep; on the ISDS205X also
  amplitude and offset in volts, and the output is parked at 0 V when the program connects.
- **Calibration:** the device's own calibration reads about 6.5 % low on the unit we measured. A user calibration per
  device corrects zero and gain; the gain wizard finds the gains with the built-in generator and, optionally, a
  multimeter.
- Any probe factor (e.g. 10.15), selectable trace colours, a print theme, a front panel with knobs as an alternative to
  the form, CSV export and screenshots, a built-in simulator.
- Interface in English and Turkish ([docs/translating.md](docs/translating.md)).

## Installing

Packages are on the project's Releases page.

**Windows 10 and 11, 64-bit:** unpack `star-oscilloscope-<version>-windows-x64.zip` into a folder and run
`star-oscilloscope.exe`. Nothing is installed; the folder can be moved. Settings are kept in `%APPDATA%\StarOscilloscope`.
- The program reaches the device through Windows's WinUSB driver. The manufacturer's software binds it to the device,
  and Device Manager then shows "YiXingDianZi USB DSO(v5) Driver". If the manufacturer's software has never been
  installed on the computer, install its driver once ([docs/building.md](docs/building.md) §1.2 explains what it is).

**Debian 13, Ubuntu 24.04 and newer:**
```bash
sudo apt install ./star-oscilloscope_<version>_amd64.deb
```
- The package uses the system's Qt 6 and installs a udev rule, so the logged-in user can open the device without root;
  a device already plugged in need not be replugged.
- The program appears in the applications menu as "Star Oscilloscope".

**Other Linux distributions (AppImage):** runs on Debian 13, Ubuntu 24.04 and newer, current Fedora and Arch. Nothing to
install; make the file executable and run it:
```bash
chmod +x star-oscilloscope-<version>-x86_64.AppImage
./star-oscilloscope-<version>-x86_64.AppImage
```
- To open the device without root, a udev rule is needed once (the `.deb` installs it by itself):
  ```bash
  echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="d4a2", ATTR{idProduct}=="5661", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/60-star-oscilloscope.rules
  sudo udevadm control --reload-rules && sudo udevadm trigger --attr-match=idVendor=d4a2
  ```
  Without the rule the program starts with the simulator and says in the status bar that the rule is missing.
- Without FUSE on the system, start the AppImage with `--appimage-extract-and-run`.

**From source:** see [docs/building.md](docs/building.md).

## Using it

- Without a device the program opens with a simulator; `--simulate` uses it even with a device connected.
- The settings, calibration and reference waveforms are kept per user; `--settings FOLDER` keeps them in a folder of
  your choice instead (a portable copy, or trying things without touching your own).
- Help for a control appears in the status bar when the pointer is over it.
- Shortcuts: F5 Run/Stop, F6 Single, F7 Force Trigger, F8 Autoset, Ctrl+E export CSV, Ctrl+T and Ctrl+U cursors,
  Ctrl+0 zoom off.

## Known hardware limits

Measured on one ISDS205X; details and the comparison with the manufacturer's specifications are in
[docs/measurements.md](docs/measurements.md) §14.

- **Gain:** with the device's own calibration readings are about 6.5 % low. The gain wizard brings them within 0.2 % of a
  multimeter.
- **Amplitude at higher frequencies:** readings start to fall above 10 kHz and are 12–13 % low at 100 kHz, because of
  the input itself (the manufacturer's software shows the same). In the MHz range the amplitude also depends on the
  chosen scale.
- **Generator:** its amplitude and DC level change with the inputs' scale (5 Vpp set can become 8.3 Vpp). If an exact
  amplitude matters, measure the output.
- **At power-up** the generator's output sits at about +4.9 V DC until it is set.
- **48 MSa/s:** USB 2.0 cannot carry this rate. Every capture starts with 1020 contiguous samples (21 µs), then comes in
  blocks of 256 with gaps; the program uses it only where that is enough.
- **16 MSa/s:** a computer cannot always keep up with 32 MB/s; in captures longer than 64K samples a sample is lost now
  and then, so longer windows use 8 MSa/s.
- **The good side:** timing, frequency and the match between the two channels (within 1 ns) are solid, and the errors
  repeat, which is why calibration works.

## Documentation

| Document | Content |
|---|---|
| [docs/building.md](docs/building.md) | Building, testing, packaging, the tools, how the protocol was worked out |
| [docs/protocol.md](docs/protocol.md) | The USB protocol and the calibration model |
| [docs/measurements.md](docs/measurements.md) | Measurements against known sources; the hardware's limits (§14) |
| [docs/translating.md](docs/translating.md) | Translating the interface |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Reporting problems, contributing code, measurements and translations |
| [CHANGELOG.md](CHANGELOG.md) | What changed in each version |

## Repository layout

| Path | Content |
|---|---|
| `driver/` | The USB driver, C++20 on libusb: `isds_core` (protocol, descriptor, calibration), `isds` (the device), `isdsctl` |
| `core/` | `starosc_core`: timebase planning, software trigger, measurements, FFT and spectrum, the simulator, the acquisition engine (no Qt); `starosc_device` |
| `app/` | The Qt 6 Widgets application `star-oscilloscope` |
| `packaging/` | Install rules, the Windows zip, the Debian package and the AppImage |
| `tests/data/` | Golden data for the C++ tests |
| `tools/` | Bench, recording and analysis tools; the Python reference driver `isds205x.py` |

## Versions

Versions are MAJOR.MINOR.PATCH: PATCH releases fix, MINOR releases add. 0.7.0 is the first public release; 1.0 will
follow once the program has been used beyond the bench it was developed on.

## Licence

Star Oscilloscope is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
version: **GPL-3.0-or-later**. The full text is in [LICENSE](LICENSE); every source file carries an SPDX identifier.
The program comes WITHOUT ANY WARRANTY.

Copyright © 2026 Necati Çakacı.

The packages also carry Qt, libusb and other libraries under their own licences, listed in the packages. The
manufacturer's SDK is not part of this project. Instrustar and the model names are trademarks of their owners.
