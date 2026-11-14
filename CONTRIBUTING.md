# Contributing

Thank you for helping. Reports from other ISDS205 models, measurements, translations and code are all welcome.

## Reporting a problem

Open an issue with:
- the model and the output of `isdsctl info` (or, in the program, Acquire > Calibration…, which shows the model; the
  serial number is not needed);
- the operating system and how the program was installed (zip, `.deb`, AppImage, built from source);
- what you did, what you expected and what happened; the status bar's message if there was one.

**Data from other units** help a lot, even without a problem: the descriptor (`isdsctl info`) and, if you have a
multimeter, one DC comparison ([docs/measurements.md](docs/measurements.md) §8).

## Contributing code

1. Discuss larger changes in an issue first.
2. Build and run the tests ([docs/building.md](docs/building.md)); CI builds and tests on Debian, Ubuntu and Windows,
   and a pull request is merged only when it passes.
3. Changes that touch the device (driver, acquisition, generator) need a bench test: say in the pull request which
   model, which operating system and what you measured. Without a device such a pull request waits for someone who has
   one (`hardware-needed`).
4. Pull requests are squashed into one commit on `main`.

**Sign your commits** (Developer Certificate of Origin, <https://developercertificate.org>): `git commit -s` adds
`Signed-off-by: Your Name <you@example.org>`, by which you state that you have the right to submit the change under the
project's licence. There is no contributor licence agreement.

**Licence:** contributions come under GPL-3.0-or-later, like the rest. Every source file starts with
`SPDX-License-Identifier: GPL-3.0-or-later`.

## Conventions

- **C++20,** CMake, no compiler extensions; warnings on (`-Wall -Wextra -Wpedantic`) and none left. `core/` stays free of
  Qt and USB.
- **English** for code, comments, identifiers, the interface and commit messages. Comments say why, not what.
- **Tests** for what can be tested without the device: `core/tests` and `driver/tests` (Catch2), `app/tests` (Qt Test,
  run without a screen). The simulator stands in for the device.
- **The interface:**
  - every text in `tr()`, as whole sentences with `%1` ([docs/translating.md](docs/translating.md)); a new text needs its
    translations, or `update_translations` marks it unfinished;
  - no tooltips: help for a control goes to its status tip and shows in the status bar on hover;
  - lines on the oscilloscope, spectrum and similar views are drawn with `display_style::hairline()`; a pen of one
    logical pixel is slow on scaled displays ([docs/building.md](docs/building.md) §1.1, Performance).
- **Measurements and findings** go into the docs with how they were made, so that others can repeat them: protocol
  findings into [docs/protocol.md](docs/protocol.md), bench results into [docs/measurements.md](docs/measurements.md),
  bench tools into `tools/`.

## Working with the hardware

- **Never write to the device's calibration or firmware.** Star Oscilloscope keeps its calibration on the computer.
- **The input relays wear out:** coarse gain and coupling switch relays. Bench tests should switch them sparingly, not in
  fast loops.
- **DDS OUT** sits at about +4.9 V DC at power-up and changes with the inputs' scale ([docs/measurements.md](docs/measurements.md)
  §5): connect nothing sensitive to it while testing.
- Run code from others only after reading it, build scripts included.

## Translations

See [docs/translating.md](docs/translating.md): a new language is one `.ts` file and one line in `app/CMakeLists.txt`.
