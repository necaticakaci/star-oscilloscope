# Building and development

How to build, test, run and package Star Oscilloscope, the tools in `tools/`, and how the protocol was worked out. The
protocol itself is in [protocol.md](protocol.md), the measurements in [measurements.md](measurements.md).

---

## 1. Environment

Linux is the first platform; Windows builds with MSYS2 (§1.2). Development has been done on Ubuntu 24.04 (also under
WSL2 with WSLg and usbipd, §2) and Windows 10.

| | Linux (Ubuntu 24.04) | Windows (MSYS2 UCRT64) |
|---|---|---|
| Compiler | GCC 13 or newer (C++20 with `<format>`) | GCC from MSYS2 |
| Build | CMake ≥ 3.20, Ninja | the same |
| USB | libusb-1.0 ≥ 1.0.27 | libusb ≥ 1.0.28 (RAW_IO, [protocol.md](protocol.md) §6) |
| GUI | Qt 6.2 or newer (tried with 6.4 and 6.12) | Qt 6 from MSYS2 |
| Tests | Catch2 3, Qt Test | the same |

Ubuntu/Debian packages:

```bash
sudo apt install build-essential cmake ninja-build pkg-config libusb-1.0-0-dev catch2 \
    qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev
# the Turkish interface and Qt's own translations (without them the program is built in English only):
sudo apt install qt6-l10n-tools qt6-tools-dev qt6-translations-l10n
# Python tools (tools/*.py):
sudo apt install python3-numpy python3-usb
```

## 1.1 Building and testing

```bash
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/linux
ctest --test-dir build/linux --output-on-failure      # no device needed
build/linux/driver/isdsctl info                       # needs the device
build/linux/driver/isdsctl capture --range 1000 --rate 1000000 --samples 16384 --csv capture.csv
build/linux/driver/isdsctl scan
```

Options: `-DSTAROSC_BUILD_TESTS=OFF` (no Catch2 or Qt Test needed), `-DSTAROSC_BUILD_APP=OFF` (driver and core only).

| Target | Content |
|---|---|
| `isds_core` | `driver/include/isds/{protocol,descriptor,calibration,generator}.hpp`. Plain C++20, no USB |
| `isds` | `device.hpp`: the libusb connection. A reader thread listens on the control endpoint, answers heartbeats and queues acknowledgements; capture data come through 32 queued asynchronous reads of 8 KB, since the device streams while it captures ([measurements.md](measurements.md) §10) |
| `isdsctl` | Command-line tool: `info`, `capture`, `scan`, `gen` |
| `isds_tests` | Catch2 tests of `isds_core` |
| `starosc_core` | `core/`: settings, timebase planning, zoom steps, software trigger, measurements and statistics, SI formatting, the simulator, the acquisition engine, FFT, windows and the spectrum engine, the gain wizard's transfer. No Qt, no USB |
| `starosc_device` | Puts the device behind the `Source` interface (uses `isds`) |
| `starosc_tests` | `core/tests`: trigger, measurements, zoom, timebase, formatting, end-to-end acquisition with the simulator, FFT, windows, harmonics |
| `starosc_gui` | `app/src`: all windows and widgets, as a static library the GUI tests link |
| `star-oscilloscope` | `app/`: the Qt 6 Widgets application (`main.cpp` + `starosc_gui`) |
| `starosc_gui_tests` | `app/tests`: QtTest interaction tests without a screen (drag, wheel, cursors, Autoset, X-Y, Math, references, generator, Spectrum, zoom, measurements, probe factor, colours, settings, calibration files, translations, …) |
| `star-loopback`, `star-spectrum-bench`, `star-usb-stress`, `star-icon` | Bench tools (§3) |

### Running the application

- Without a device the application opens with the simulator; the status bar says "Simulator (no device)" and, on hover,
  why the device could not be opened.
- `--simulate`: the simulator even with a device connected. It quantises to 8 bits with a real unit's calibration table.
- `--model NAME`: the simulator acts as that model, e.g. `ISDS205B` (generator without amplitude and offset) or
  `ISDS205` (no generator); default `ISDS205X(1.1)`. Only the generator depends on the model ([protocol.md](protocol.md) §10).
- `--tab spectrum`: open on the Spectrum tab (`oscilloscope`, `generator`).
- `--settings FOLDER`: keep the settings, calibration and references in FOLDER instead of the user's own.
- `--screenshot FILE.png --frames N`: save a picture of the window after N frames and quit. Works without a screen with
  `QT_QPA_PLATFORM=offscreen`; the window layout and measurement settings are not saved in this mode. Use a separate
  settings folder so as not to touch your own; a scenario (cursors, V/div…) is set up in its
  `StarOscilloscope/StarOscilloscope.conf`:
  ```bash
  T=$(mktemp -d); mkdir -p $T/StarOscilloscope; cp scenario.conf $T/StarOscilloscope/StarOscilloscope.conf
  XDG_CONFIG_HOME=$T QT_QPA_PLATFORM=offscreen build/linux/app/star-oscilloscope --simulate --screenshot shot.png --frames 8
  ```
- Settings: `~/.config/StarOscilloscope/StarOscilloscope.conf` on Linux, `%APPDATA%\StarOscilloscope\StarOscilloscope.ini`
  on Windows, with `calibration/` and `references/` beside it. Sections `[scope]`, `[cursors]`, `[measure]`,
  `[generator]`, `[spectrum]` (`app/src/settingsstore.cpp`), `[display]` (`app/src/displaystyle.cpp`).
- Look: View → Window Appearance offers every Qt style on the system and a classic grey palette; Fusion with the
  system's palette by default. View → Language picks the interface language ([translating.md](translating.md)).
- Shortcuts: F5 Run/Stop, F6 Single, F7 Force Trigger, F8 Autoset, Ctrl+E export CSV, Ctrl+T time cursors, Ctrl+U
  voltage cursors, Ctrl+0 zoom off.
- Help for a control shows in the status bar when the pointer is over it; the program has no tooltips.

### The acquisition flow

1. `Engine::acquire` plans from the timebase (`plan_acquisition`): the fastest rate that fits the memory setting and
   whose contiguous capture holds two screen windows.
   - At 48 MSa/s a capture starts with 1020 contiguous samples, then blocks of 256 ([measurements.md](measurements.md)
     §10). If two windows fit in the start (1 µs/div) only that is captured and the trigger searched in it; otherwise this
     rate is chosen when the window fits in a block (500 ns/div and below) and the trigger is searched only where the
     window stays inside one block. Sources describe their layout with `Source::layout(rate)` (`CaptureLayout`).
   - Long captures at 16 MSa/s lose a sample now and then: this rate is planned only up to 64K samples, longer windows
     use 8 MSa/s (`Source::reliable_samples`).
   - Twice the window, at least 16K samples, is captured so that the trigger can be searched; 8 samples on each side of
     the window are kept for sin(x)/x (`Frame::before/after`).
   - Below 48 MSa/s the engine takes a short 48 MSa/s capture when the rate changes and every 10 frames; if the signal
     is faster than half the rate, the frame is marked aliased.
2. Each channel's screen range (V/div × 8 divisions and position), divided by the probe factor, is turned into an input
   voltage range; the driver picks the most sensitive stage that covers it without clipping.
3. The trigger is searched in software (`find_trigger_near`): with hysteresis, at a fractional sample position, nearest
   the middle of the search range. The window is cut so that the trigger falls at the pre-trigger ratio; the fraction is
   used in drawing, so a triggered wave does not jitter.
4. Measurements run over the screen window (`measure_frame`); timing measurements only when the swing exceeds 4 lsb.
   Method: [measurements.md](measurements.md) §12. Zoom (`ZoomSettings`) changes only the drawing.
5. With Math on, the engine computes the trace sample by sample over the same window (`core/src/math.cpp`); a source
   channel is captured even when hidden. In a division, a divisor within 2 lsb of zero gives NaN, which drawing and
   measurements skip.
6. `AcquisitionWorker` runs this loop on its own thread and hands frames to the interface as `shared_ptr`. At most 60
   frames a second are shown; while waiting for a trigger captures go on without pause; if the interface falls two frames
   behind, acquisition waits, so frames never pile up.
7. With the Spectrum tab open the same thread runs `SpectrumEngine::acquire` (`core/src/spectrum.cpp`):
   - It captures at the spectrum's rate with the oscilloscope's ranges and coupling. At continuous rates one record is
     the FFT's length (at 1M points the last 1028 samples are zero-padded, the device's memory being slightly short). At
     48 MSa/s more than 256 points come from the contiguous start, zero-padded to 1024; fewer average the records of the
     start and of every block.
   - The FFT is our own (`core/src/fft.cpp`): radix-2 on powers of two, real input through one complex transform of half
     the length. About 3 ms at 64K points, 72 ms at 1M. The Dolph-Chebyshev window is computed with the FFT too and kept
     until the length changes.
   - Power is kept as Vrms² per bin; a sine's peak bin reads its RMS squared (with the window's coherent gain). The total
     power of tones and noise is the sum of bins over the window's equivalent noise bandwidth.
   - Averaging is linear over the first N spectra, then exponential over N; peak hold keeps the largest per bin. Both
     restart when the rate, length, window, range or traces change.
   - `analyze_spectrum`: the largest tone above DC is the fundamental; harmonics are searched with folding; the noise is
     the mean of the remaining bins, extended over the whole span.

### Performance

`tools/app_cpu.sh` runs the application without a screen and measures its CPU share; `tools/app_cpu_threads.sh` per
thread, at a timebase, memory depth and display scale; `tools/app_cpu.ps1` on Windows with the window visible, per thread
and with the window's answer time. `perf record -g -p <pid>` finds hot spots on Linux.

- **Lines must be one device pixel.** Qt's fast line drawing works only for pens of one device pixel or less. Traces drawn
  1.3 px wide took about 70 % of the time; a pen of one logical pixel on a display scaled to 125 % is 1.25 device pixels
  and fell into the same slow path (a whole core, half-second stalls at slow timebases). Every line on the oscilloscope,
  spectrum, overview strip and generator preview is drawn with `display_style::hairline()`: a cosmetic pen, one device
  pixel wide, two from 200 %. **Rule:** code drawing lines on these views uses `hairline()`, not `QPen(color, 1)`.
- Traces: per pixel column a faint min/max envelope, and over it the column's mean, antialiased and drawn twice (an
  antialiased line of one pixel spreads over two and looks dim). Sparse points are drawn with sin(x)/x or straight lines.
- Measured on Windows at 125 % (CPU as a share of one core):

  | Setting | Before | After |
  |---|---|---|
  | Simulator, 500 µs/div | 115 % (GUI 89 %); answer 12 ms | 19 % (GUI 10 %); answer 0.2 ms |
  | Simulator, 10 ms/div, large memory | 148 % (GUI 98 %); answer 430 ms | 51 % (GUI 2 %); answer 0.2 ms |
  | Simulator, 100 ms/div and 1 s/div | 550–600 ms stalls per frame | worst 1 ms |
  | Device, 500 µs/div | – | 30 %; answer 0.2 ms, worst 12 ms |
  | Device, 10 ms/div, large memory | – | 14 %; worst 4 ms |
  | Device, 1 s/div, large memory | – | 7 %; worst 5 ms |

- At slow timebases the simulator's acquisition thread spends 40–50 % making its own signals (noise, sines); the device
  does not cost that.

### Golden test data

The tests use golden data in `tests/data/`, made by `tools/make_golden.py <recording> tests/data` from a recording of
the manufacturer's library (§5) and the Python reference model:

| File | Content |
|---|---|
| `descriptor_isds205x_1.1.bin` | a unit's handshake descriptor, its serial number replaced by 1 |
| `vendor_captures.bin` | per stage and channel, 256 raw samples and the volts the library returned (format in `make_golden.py`) |
| `model_stages.csv` | the Python model's table of stages |
| `model_select.csv` | the Python model's stage choice and trigger codes for 33 ranges |

The tests check that frame bytes equal the library's traffic, that the calibration reproduces the library's volts to
1e-12 V, and that stage choice and trigger codes equal the Python model's.

## 1.2 Building on Windows (MSYS2 UCRT64)

Compiler, Qt 6, libusb and Catch2 come from MSYS2; the CMake files are the same as on Linux. Visual Studio is not needed.

**Setup** (once, about 2–3 GB):
1. `winget install --id MSYS2.MSYS2 -e --source winget` (into `C:\msys64`).
2. In an **MSYS2 UCRT64** window: `pacman -Syu`; if the window closes, open it again and `pacman -Su`.
3. In the same window:
   ```
   pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-tools mingw-w64-ucrt-x86_64-catch
   ```
   For the translations also (`lupdate` needs `Qt6Qml.dll`):
   ```
   pacman -S --needed mingw-w64-ucrt-x86_64-qt6-declarative mingw-w64-ucrt-x86_64-qt6-translations
   ```

UCRT64 makes native Windows programs with GCC and Windows 10's own C library; a program built in the MSYS environment
would depend on `msys-2.0.dll` and is not what we ship.

**Build** (in a UCRT64 window):
```
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/windows
QT_QPA_PLATFORM=offscreen ctest --test-dir build/windows
```

- The programs need the DLLs in `C:\msys64\ucrt64\bin`; the package puts them beside the program (§1.3).
- The executable's icon and version come from `app/resources/star-oscilloscope.rc.in`; CMake fills in the icon's path
  and the `project()` version. The `.ico` is made by `star-icon` (§3) and kept in the repository.

**What differs on Windows:**
- **Settings in a file,** not the registry: `%APPDATA%\StarOscilloscope\StarOscilloscope.ini` (`settings_store::useFiles`).
  `--settings FOLDER` replaces Linux's `XDG_CONFIG_HOME` trick, since Qt does not read `%APPDATA%` from the environment.
- **Numbers with a decimal point** in every interface language, as the on-screen readouts write them.
- **Monospaced readouts** in Consolas; Qt's default there (Courier New) was too thin on a dark background.
- **Styles:** Windows adds `windowsvista` and, on Windows 11 only, `windows11` (which ignores the palette on Windows 10).
  The classic Windows style draws its own raised frame around menus; their shadow is turned off in that style only.
- **Icons follow the palette:** toolbar and generator icons are drawn in the palette's text colour at every paint
  (`icons.cpp`, `QIconEngine`), so they stay visible on a dark palette.

**The device on Windows:** it needs Microsoft's WinUSB driver bound to it.
- **The manufacturer's "driver" is only that binding:** a signed INF file that tells Windows to use WinUSB for
  `VID_D4A2&PID_5661`; it carries no code of theirs. Device Manager then shows "YiXingDianZi USB DSO(v5) Driver" with
  the service WinUSB. Star Oscilloscope uses none of the manufacturer's DLLs; libusb talks to Windows's own WinUSB.
- **Why an INF is needed:** some devices ask for WinUSB themselves with a Microsoft OS descriptor (WCID), and Windows
  binds it without an INF. This one does not (`usbflags\D4A256610000`, `osvc = 00 00` in the registry).
- **Without the manufacturer's software:** Zadig (libwdi) makes the same binding with its own INF and certificate. Not
  tried yet ❓. After it, the manufacturer's application may no longer find the device, since it looks for the device
  interface its own INF registers ❓.
- The data endpoint is read in RAW_IO mode ([protocol.md](protocol.md) §6), which needs libusb 1.0.28 or later.
- If the device is attached to WSL with usbipd, give it back first: `usbipd unbind --busid <BUSID>` as administrator.

## 1.3 Packaging

The install rules and package settings are in `packaging/CMakeLists.txt`; CPack makes the packages. Only the program is
packaged, not the bench tools. Packages come from a separate Release build:

```bash
cmake -S . -B build/pkg -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTAROSC_BUILD_TESTS=OFF
cmake --build build/pkg --target star-oscilloscope
cpack --config build/pkg/CPackConfig.cmake -B build/pkg/packages   # under WSL: -B ~/packages (below)
```

The version comes from the `project(... VERSION ...)` line of the top `CMakeLists.txt`: the package names, Help → About,
`--version` and, on Windows, the executable's Properties. Versions follow MAJOR.MINOR.PATCH: PATCH for fixes, MINOR for
new features; 1.0 once the program has been used beyond the bench it was developed on.

**Windows: a portable zip.** `star-oscilloscope-<version>-windows-x64.zip` holds a folder of the same name: the
executable at its top, the DLLs beside it, where Windows looks for them. Installing runs `packaging/windows/deploy.cmake`:
1. `windeployqt6` puts Qt's libraries and plugins in place. Left out: image format plugins (screenshots are PNG, which
   Qt handles itself), touch input over the network (it brings Qt Network along), network information and TLS.
2. Qt's own translations for the program's languages only, as `translations/qtbase_<lang>.qm`; the languages are counted
   from `app/translations/*.ts`.
3. Every other DLL those need, found recursively with `objdump` (`file(GET_RUNTIME_DEPENDENCIES)`): libusb, the C++
   runtime, ICU, the font and image libraries. Only from Qt's and the compiler's folder; Windows's own DLLs stay out.
   A DLL not found stops the packaging.
4. **Licences:** MSYS2's `pacman` tells which package each shipped DLL belongs to; its licence goes to
   `licenses/<package>/`, and `licenses/PACKAGES.txt` lists the packages with their versions and where MSYS2 keeps their
   sources. LGPL libraries such as Qt and libusb ask for both.

About 33 MB zipped, 77 MB unpacked; more than half is ICU's data (`libicudt*.dll`), with which MSYS2's Qt is built.
Checked by unpacking into an empty folder and running with only Windows's folders on `PATH`: every DLL the running
program loaded came from the package's folder.

**Linux: a Debian package.** `star-oscilloscope_<version>_amd64.deb`, on the system's Qt:
- `/usr/bin/star-oscilloscope`, the menu entry `star-oscilloscope.desktop`, icons of 16–256 pixels under `hicolor`, the
  docs and `copyright`.
- A udev rule, `/usr/lib/udev/rules.d/60-star-oscilloscope.rules`, with `TAG+="uaccess"`: the logged-in user may open the
  device without root. `postinst` reloads the rules and re-triggers a device already plugged in.
- `dpkg-shlibdeps` works out the dependencies (Qt, libusb); Qt's Wayland plugin and translations are recommended.
  Without them the window opens through XWayland and Qt's own dialogs speak English.
- The maintainer field comes from `-DSTAROSC_PACKAGE_CONTACT="Name <e-mail>"`.
- Installed under another prefix (`cmake --install build/pkg --prefix ~/.local`), the rule lands where udev does not
  read; copy it to `/etc/udev/rules.d/` by hand.
- **Under WSL, stage on the Linux file system** (`cpack … -B ~/packages`): a package takes its files' modes from the
  staging folder, and on a Windows drive (`/mnt/d`) every file shows as 0777. `packaging/linux/check-modes.cmake` stops
  the packaging in that case.

**Linux: an AppImage.** `star-oscilloscope-<version>-x86_64.AppImage`, one file with Qt and its libraries inside:

```bash
packaging/linux/appimage.sh build/pkg build/pkg/packages
```

- It uses [linuxdeploy](https://github.com/linuxdeploy) and its Qt plugin, looked for in `~/tools/linuxdeploy` (or
  `LINUXDEPLOY_DIR`); they are not part of the repository. Versions in use:

  | File | Version | SHA-256 |
  |---|---|---|
  | `linuxdeploy-x86_64.AppImage` | 1-alpha-20251107-1 | `c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d` |
  | `linuxdeploy-plugin-qt-x86_64.AppImage` | 1-alpha-20250213-1 | `15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724` |
- The script installs the program into a temporary folder on the Linux file system and lets linuxdeploy collect the
  libraries and their `copyright` files; our full `LICENSE` goes into `usr/share/doc/star-oscilloscope/`.
- It adds what the Qt plugin leaves out:
  - Wayland's shell integration that opens the windows (`xdg-shell`) and the window frame for desktops that draw none
    (`bradient`, GNOME). Without them a Wayland session showed no window.
  - libusb, which linuxdeploy leaves to the system (its exclude list); without it the program would not start. The
    bundled copy wins over the system's: the program's `RUNPATH` is `$ORIGIN/../lib`, searched before the system's
    folders (only `LD_LIBRARY_PATH` would change that).
- glibc, the C++ library, X11, Wayland and OpenGL come from the system, so an AppImage runs on distributions no older
  than the one it was built on. Built on Ubuntu 24.04 it needs glibc 2.38 and GCC 13's C++ library (`GLIBCXX_3.4.31`):
  Debian 13, Ubuntu 24.04 and newer, current Fedora and Arch. Debian 12 is out: its GCC 12 lacks `std::format`, so the
  code cannot be built there either.
- An AppImage cannot install the udev rule; the README gives the command. Without the rule the program says so in the
  status bar.
- About 28 MB. Tried under WSLg with Wayland and X11, with Qt, ICU, libusb and libudev loaded from inside the image.

**Why a device does not open.** The driver looks for the device itself and tells apart: no device ("check the USB cable
and the rear switch"); a device the user may not open (the udev rule is missing); on Windows, a device without a WinUSB
driver. The message shows in the status bar and on hovering over "Simulator (no device)".

**Continuous integration (GitHub Actions).** `.github/workflows/ci.yml` runs the commands above; there is no separate packaging script. Each
Linux package is built in its target distribution's container, with that distribution's own Qt and libraries:

| Job | Environment | Product |
|---|---|---|
| Debian 13 | `debian:13` container (Qt 6.8) | `.deb`, build and tests |
| Ubuntu | `ubuntu:24.04` container (Qt 6.4) | `.deb`, build and tests |
| AppImage | `ubuntu:24.04` container | AppImage for Debian 13, Ubuntu 24.04 and newer |
| Windows | `windows-latest` with `msys2/setup-msys2`, UCRT64 (Qt 6.12) | zip, build and tests |

- The tests run in every environment, so the code is tried with three Qt versions.
- The two `.deb` files carry the distribution in their names (`-DSTAROSC_PACKAGE_DISTRO=debian13`).
- A tag such as `v0.7.0` builds every package from that tag's source and attaches them, with SHA-256 sums, to a draft
  GitHub release, which a maintainer reviews and publishes. Tests that need the device cannot run in CI; they are done on
  the bench before tagging.

## 2. The device under Linux and WSL

The rear switch must be on **"Logic" (Multi VirAnalyzer)**; the device then shows as `D4A2:5661 YX-DSO`. In "Saleae
Logic" it shows as `0925:3881`, a mode for sigrok/PulseView.

**Access without root** (the `.deb` installs this rule):

```bash
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="d4a2", ATTR{idProduct}=="5661", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/60-star-oscilloscope.rules
sudo udevadm control --reload-rules && sudo udevadm trigger --attr-match=idVendor=d4a2
```

`uaccess` gives the user of the local seat access. WSL has no seat; there use `MODE="0666"` in the rule instead.
Check with `lsusb -d d4a2:` and `ls -l /dev/bus/usb/<bus>/`.

**WSL with usbipd-win** (Windows side, PowerShell):

```powershell
winget install --id dorssel.usbipd-win          # once (asks for UAC)
usbipd list                                      # find the BUSID
usbipd bind --busid <BUSID>                      # once, as administrator
usbipd attach --wsl --busid <BUSID> --auto-attach
```

- `attach` needs the WSL virtual machine running; an open WSL terminal is enough (or `wsl -e sleep infinity` in the
  background).
- Back to Windows: `usbipd detach --busid <BUSID>`; to the Windows driver entirely: `usbipd unbind --busid <BUSID>`.
- Over usbip the link carries about 41 MB/s; a read cancelled by a timeout could lose a message, which is why the driver
  keeps a read without timeout on the control endpoint ([measurements.md](measurements.md) §10).

## 3. Tools (`tools/`)

| Tool | Platform | What it does |
|---|---|---|
| `isds205x.py` | any | **Reference driver** without USB: frames, descriptor, calibration, stage choice. The C++ driver's specification and test oracle |
| `native_poc.py` | Linux | Driver without the manufacturer's library (pyusb). `--range MV`, `--rate HZ`, `--scan --compare <recording>` |
| `probe_vdso.py` | Linux, Windows | Checks the device with the manufacturer's library: capabilities, one capture |
| `usbsniff/usbsniff.c` | Linux | `LD_PRELOAD` logger of `libusb_bulk_transfer` traffic, with marks |
| `trace_vdso.sh` + `trace_vdso.py` | Linux | Runs the manufacturer's library through a scenario and records its traffic. Scenarios: `basic`, `calsweep` |
| `fit_trace.py` | Linux | Fits raw → volts for every capture in a recording → `cal.csv` |
| `verify_cal.py` | Linux | Checks the calibration model of `isds205x.py` against the library's output |
| `make_golden.py` | any | Makes the golden data in `tests/data/` from a recording |
| `dc_logger.py` | Linux | Logs both channels' DC mean on all stages continuously ([measurements.md](measurements.md) §1) |
| `dc_gain.py` | any | Finds the steady steps in a `dc_logger.py` log and fits gain and zero per stage against multimeter values; `--json` writes a calibration file the application imports |
| `star-loopback` (`tools/loopback.cpp`) | Linux, Windows | With DDS OUT on CH2: the generator's parking, amplitude, offset, waveforms, duty, pulse measurements, frequency response and the continuity of captures at every rate. `--pulses`, `--delay` (both channels on one signal), `--external` (another source, generator untouched), `--step` (step response), `--grain [CH1-POS CH2-POS]` (noise against the ADC step, [measurements.md](measurements.md) §16), `--peaks`, `--sinc`, `--stages`, `--dds-state`, `--contiguity N`, `--gain-transfer`, `--gain-levels [--hold S]`, `--range V`, `--probe F` |
| `star-spectrum-bench` (`tools/spectrum_bench.cpp`) | Linux, Windows | With DDS OUT on CH2: sine, square, high frequency and noise floor through the Spectrum engine; `--at HZ RATE` one frequency at one rate; `--pair [V/div]` the Math trace against the channels with one signal on both ([measurements.md](measurements.md) §11, §15) |
| `star-usb-stress` (`tools/usb_stress.cpp`) | Linux, Windows | Counts lost answers under commands at random intervals, then runs the application's acquisition with pauses ([measurements.md](measurements.md) §10) |
| `star-icon` (`tools/make_icon.cpp`) | any | Writes the application icon's files from its drawing (`app/src/appicon.cpp`): the PNGs in `app/resources/icons/` and the `.ico`. A GUI test checks that the files match the drawing |
| `step_fit.py` | any | Fits two time constants to a step response from `star-loopback --step` or `vendor_csv.py --step` |
| `vendor_csv.py` | any | Reads the manufacturer's application's CSV export: stage, levels; `--sine HZ` a gap check, `--step` a square's step response |
| `usb_capture.ps1` | Windows | Records only the oscilloscope's USB traffic with USBPcap (other devices on the same hub stay out); administrator PowerShell, stops on Enter |
| `usbpcap_read.py` | any | Reads a USBPcap recording without Wireshark: commands and answers, heartbeats counted, reads folded; `--reads`, `--dump DIR` |
| `app_cpu.sh`, `app_cpu_threads.sh`, `app_cpu.ps1` | Linux, Windows | CPU of the application (§1.1, Performance) |

Examples (Linux, from the repository's root; the manufacturer's SDK in `SDK-master/`, §4):

```bash
python3 tools/probe_vdso.py
bash tools/trace_vdso.sh trace/new-recording calsweep
python3 tools/fit_trace.py trace/new-recording
python3 tools/verify_cal.py trace/new-recording
python3 tools/native_poc.py --scan --compare trace/new-recording
```

## 4. How the protocol was worked out

The manufacturer publishes an SDK with a closed library (github.com/instrustar-dev/SDK); its licence is unclear, so it
is not part of this repository. Download it into `SDK-master/` to use the tools that call it.

1. **Observe:** add a scenario or API call to `trace_vdso.py`; `mark()` writes a mark into the log before every call.
2. **Record:** `bash tools/trace_vdso.sh trace/<name> <scenario>`; `usb.log` shows under each mark the bytes that call sent.
3. **Fit:** for captures taken with `capture()`, `fit_trace.py` lines the raw data up with the library's volts.
4. **Model:** put the finding into `isds205x.py`; check it against the library with a verifier like `verify_cal.py`,
   aiming at zero error.
5. **Check independently:** do the same without the library with `native_poc.py` and compare.
6. **Document** in [protocol.md](protocol.md) with ✅ (checked without the library), 🔎 (inferred) or ❓ (open).

**The manufacturer's Windows application.** What it does and the SDK does not is learned from its own traffic; the
48 MSa/s priming capture was found this way ([measurements.md](measurements.md) §10).
1. Install Wireshark and USBPcap; restart once so that USBPcap's filter sits on the root hubs.
2. In an administrator PowerShell: `powershell -ExecutionPolicy Bypass -File tools\usb_capture.ps1`. It finds the
   oscilloscope on the hubs by name and records only its address. Start the application after the recording, so the
   handshake is included; Enter stops it.
3. `python tools/usbpcap_read.py recording.pcap` prints the command sequence as text; replaying it through our driver
   narrows down what matters.
4. USBPcapCMD's default snapshot length is 65 535: only the first 65 508 bytes of a longer read are recorded.

Notes: with both channels off the application never captures. Its trigger, rates and DDS settings follow the same
protocol; only the order and timing differ ([protocol.md](protocol.md) §3).

**Principles:**
- We do not copy the manufacturer's code; we observe its inputs and outputs and write our own.
- A disassembly (`objdump -d -C`; the Linux library is not stripped) is used only for hints.
- We do not distribute the manufacturer's binaries.
- Nothing is ever written to the device's calibration or firmware.

## 5. Recordings (`trace/`)

Recordings of the manufacturer's library are not part of the repository; `trace_vdso.sh` makes new ones. Layout:
- `usb.log`: time, thread, direction, endpoint, result, hex. Lines starting with `----` are marks.
- `dumps/NNNNNN_epXX.bin`: every IN payload larger than 64 bytes; `000000_ep81.bin` is the handshake descriptor.
- `<tag>_chN.f64`: the library's `ReadVoltageDatas` output (float64 LE, volts).
- `cal.csv`: output of `fit_trace.py`.

## 6. Quirks of the manufacturer's library

| Behaviour | Effect / workaround |
|---|---|
| On Linux `FinishDll()` never returns; the main thread spins at 100 % CPU (with or without the device) | A timeout and `os._exit` (`probe_vdso.finish_dll`) |
| Prints debugging lines to stdout (`InitDll....`, `DestoryOscDevice Success`) | Ignore |
| The repository's `libvdso.so` symlink becomes plain text on Windows | Load `libvdso.so.1.0` directly |
| Linux x64 only; the Windows DLL needs the VC++ 2015–2022 runtime | — |
| Makes extra 1M captures at 16 MHz and 1 MHz on its own at the first capture | Purpose unknown; scenarios start with a `warmup` capture. The 1 MHz one should have primed 48 MSa/s ([protocol.md](protocol.md) §6), yet its 1020 samples at 48 MSa/s had gaps ❓ |
| Drops the first 1028 and the last ~1024 samples of a capture | The first 1024 are the FIFO's leftover ([protocol.md](protocol.md) §6) |
| At 48 MSa/s `Capture(16)` → 1 KB returns 1020 samples | The device sends all the data |
| Chooses stages with fixed thresholds, ignoring the calibrated asymmetric ranges | Our own `Calibration::select` |
| Trigger settings never reach the USB; the trigger is software | — |
| DDS settings are not sent until `DDSOutputEnable(1)` | — |
| The handshake nonce is an unseeded `rand()` (`67 c6 69 73 51`) | The device accepts any random value |

## 7. What still needs the manufacturer's Windows application

To learn the protocol of what the SDK cannot trigger: the data recorder (high-speed streaming), the DDS sweep, the
16-channel logic analyser. Method: give the device back to Windows, install the application and its driver, record
each feature on its own with USBPcap (§4) and work through the recordings as above.
