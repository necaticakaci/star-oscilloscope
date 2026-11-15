# Changelog

Versions are MAJOR.MINOR.PATCH: PATCH releases fix, MINOR releases add.

## 0.7.0 — 2026-11-16, first public release

Oscilloscope software for the Instrustar ISDS205 series with its own USB driver, for Linux and Windows. Tested with the
ISDS205X.

- **Driver:** the USB protocol and the device's calibration model, learned from the manufacturer's library and checked
  without it; continuous captures at 1–16 MSa/s; 1020 contiguous samples at 48 MSa/s after a priming capture; clear
  messages when the device is missing, not accessible (udev) or has no WinUSB driver.
- **Oscilloscope:** two channels, software trigger (Auto, Normal, Single), Autoset, measurements with statistics,
  cursors, horizontal zoom with an overview strip, Math (CH1 ± CH2, ×, ÷), two references, X-Y, sin(x)/x, CLIP and
  ALIAS warnings, any probe factor, CSV export and screenshots.
- **Spectrum:** FFT of 8 to 1M points, 18 windows, V and dB units, averaging, peak hold, THD, SNR, SINAD, ENOB, SFDR,
  harmonics, the spectrum of the Math trace.
- **Generator** (ISDS205B and ISDS205X): waveforms, frequency, duty, software sweep; amplitude and offset in volts and
  parking at 0 V on the ISDS205X.
- **Calibration:** a user calibration per device (zero and gain per stage and channel), Measure Zero, the gain wizard.
- **Interface:** form and front panel controls, trace colours, print theme, any Qt style; English and Turkish.
- **Packages:** a portable zip for Windows, a `.deb` and an AppImage for Linux.
