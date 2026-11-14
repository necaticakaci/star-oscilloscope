#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check an Instrustar scope through the vendor's vdso library (Windows + Linux).

Loads the library (vdso.dll / libvdso.so.1.0), waits for the device, lists its
capabilities and summarizes a single two-channel capture. With CH1 on the front-panel
1 kHz calibration output you should see roughly 2 Vpp at 1 kHz.

Usage:
    python tools/probe_vdso.py [--lib PATH] [--wait S] [--rate HZ] [--range MV] [--length KB]
"""
import argparse
import ctypes as C
import math
import os
import platform
import struct
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHARED = ROOT / "SDK-master" / "SharedLibrary"
WINDOWS = sys.platform == "win32"

# __stdcall on x86 Windows; x64 has a single calling convention on both platforms.
FUNCTYPE = C.WINFUNCTYPE if WINDOWS else C.CFUNCTYPE
NoticeCallBack = FUNCTYPE(None, C.c_void_p)

u, i, d, b, s8 = C.c_uint, C.c_int, C.c_double, C.c_bool, C.c_byte
PROTOTYPES = {
    "InitDll": (i, []),
    "FinishDll": (i, []),
    "GetOnlyId0": (u, []),
    "GetOnlyId1": (u, []),
    "IsDevAvailable": (i, []),
    "SetDevNoticeCallBack": (None, [C.c_void_p, NoticeCallBack, NoticeCallBack]),
    "SetOscChannelRange": (i, [i, i, i]),
    "GetOscSupportSampleNum": (i, []),
    "GetOscSupportSamples": (i, [C.POINTER(u), i]),
    "GetOscSample": (u, []),
    "SetOscSample": (u, [u]),
    "IsSupportHardTrigger": (i, []),
    "GetTriggerMode": (u, []),
    "GetTriggerStyle": (u, []),
    "GetTriggerSource": (u, []),
    "GetTriggerLevel": (i, []),
    "GetTriggerPulseWidthNsMin": (i, []),
    "GetTriggerPulseWidthNsMax": (i, []),
    "IsSupportTriggerSense": (i, []),
    "GetTriggerSenseDiv": (d, []),
    "IsSupportPreTriggerPercent": (b, []),
    "GetPreTriggerPercent": (i, []),
    "IsSupportTriggerForce": (i, []),
    "IsSupportAcDc": (i, []),
    "GetAcDc": (i, [u]),
    "IsSupportRollMode": (i, []),
    "GetMemoryLength": (u, []),
    "Capture": (i, [i, s8]),
    "IsDataReady": (i, []),
    "ReadVoltageDatas": (u, [s8, C.POINTER(d), u]),
    "IsVoltageDatasOutRange": (i, [s8]),
    "GetVoltageResolution": (d, [s8]),
    "IsSupportDDSDevice": (i, []),
    "GetDDSSupportBoxingStyle": (i, [C.POINTER(i)]),
    "IsDDSSupportSoftwareControlZoomBias": (i, []),
    "GetDDSBiasResistanceRangeMin": (i, []),
    "GetDDSBiasResistanceRangeMax": (i, []),
    "GetDDSZoomResistanceRangeMin": (i, []),
    "GetDDSZoomResistanceRangeMax": (i, []),
    "IsSupportIODevice": (i, []),
    "GetSupportIoNumber": (i, []),
    "ResetDevice": (i, []),
    "SetAcDc": (None, [u, i]),
    "SetTriggerMode": (None, [u]),
    "SetTriggerStyle": (None, [u]),
    "SetTriggerSource": (None, [u]),
    "SetTriggerLevel": (None, [i]),
    "SetTriggerSenseDiv": (None, [d]),
    "SetPreTriggerPercent": (None, [i]),
    "SetTriggerPulseWidthNs": (None, [i, i]),
    "TriggerForce": (None, []),
    "SetRollMode": (i, [u]),
    "SetDDSBoxingStyle": (None, [u]),
    "SetDDSPinlv": (None, [u]),
    "SetDDSDutyCycle": (None, [i]),
    "DDSOutputEnable": (None, [i]),
    "IsDDSOutputEnable": (i, []),
    "SetDDSBiasResistance": (None, [i]),
    "GetDDSBiasResistance": (i, []),
    "SetDDSZoomResistance": (None, [i]),
    "GetDDSZoomResistance": (i, []),
}

TRIGGER_STYLES = {
    0x000: "none", 0x001: "rising edge", 0x002: "falling edge", 0x004: "either edge",
    0x008: "+pulse >", 0x010: "+pulse <", 0x020: "+pulse <>",
    0x040: "-pulse >", 0x080: "-pulse <", 0x100: "-pulse <>",
}
DDS_STYLES = {0: "sine", 1: "square", 2: "triangle", 3: "rising sawtooth", 4: "falling sawtooth"}


def default_lib_path():
    if WINDOWS:
        if struct.calcsize("P") == 4:
            arch = "X86"
        else:
            arch = "AMR64" if platform.machine().upper() == "ARM64" else "X64"
        return SHARED / "Windows" / arch / "Release" / "vdso.dll"
    # libvdso.so in the SDK is a symlink that turns into a plain text file on Windows checkouts.
    return SHARED / "Linux" / "X64" / "Release" / "libvdso.so.1.0"


def load_library(path):
    if WINDOWS:
        os.add_dll_directory(str(path.parent))
        lib = C.WinDLL(str(path))
    else:
        lib = C.CDLL(str(path))
    for name, (restype, argtypes) in PROTOTYPES.items():
        fn = getattr(lib, name)
        fn.restype = restype
        fn.argtypes = argtypes
    return lib


def fmt_rate(v):
    for div, unit in ((1e6, "MSa/s"), (1e3, "kSa/s")):
        if v >= div:
            return f"{v / div:g} {unit}"
    return f"{v} Sa/s"


def estimate_frequency(data, rate):
    lo, hi = min(data), max(data)
    if hi - lo < 1e-3:
        return None
    mid, hyst = (lo + hi) / 2, (hi - lo) * 0.1
    high = data[0] > mid
    edges = []
    for idx, v in enumerate(data):
        if not high and v > mid + hyst:
            high = True
            edges.append(idx)
        elif high and v < mid - hyst:
            high = False
    if len(edges) < 2:
        return None
    return (len(edges) - 1) * rate / (edges[-1] - edges[0])


def wait_for_device(lib, added, seconds):
    deadline = time.monotonic() + seconds
    print(f"Waiting for device (up to {seconds:g} s)", end="", flush=True)
    while time.monotonic() < deadline:
        if added.is_set() or lib.IsDevAvailable():
            print(" found.")
            return True
        print(".", end="", flush=True)
        time.sleep(0.5)
    print(" not found.")
    return False


def print_not_found_hints():
    print("\nChecklist:")
    print("  - Is the rear switch in the 'Logic' (Multi VirAnalyzer) position?")
    print("    In the 'Saleae Logic' position the device shows up as 0925:3881, which this library ignores.")
    if WINDOWS:
        print("  - Is the vendor software closed? Only one application can use the device.")
        print("  - Does Device Manager list VID_D4A2 with the WinUSB driver?")
    else:
        print("  - Does 'lsusb -d d4a2:' list the device? (WSL: usbipd attach --wsl --busid ...)")
        print("  - Permissions: udev rule from docs/building.md §2, or run with sudo.")


def report_device(lib):
    uid = (lib.GetOnlyId1() << 32) | lib.GetOnlyId0()
    print(f"\nDevice id           : {uid:016X}")

    n = lib.GetOscSupportSampleNum()
    arr = (u * max(n, 1))()
    n = lib.GetOscSupportSamples(arr, n)
    rates = list(arr[:n])
    print(f"Sample rates        : {', '.join(fmt_rate(r) for r in rates)}")
    print(f"Current rate        : {fmt_rate(lib.GetOscSample())}")
    print(f"Memory depth        : {lib.GetMemoryLength()} KB")

    print("\nSupported (1=yes):")
    for label, fn in (
        ("Hardware trigger", "IsSupportHardTrigger"),
        ("Trigger sensitivity", "IsSupportTriggerSense"),
        ("Pre-trigger percent", "IsSupportPreTriggerPercent"),
        ("Force trigger", "IsSupportTriggerForce"),
        ("Roll mode", "IsSupportRollMode"),
        ("AC/DC (raw; inverted in the docs)", "IsSupportAcDc"),
        ("DDS", "IsSupportDDSDevice"),
        ("DDS amplitude/offset by software", "IsDDSSupportSoftwareControlZoomBias"),
        ("Digital IO", "IsSupportIODevice"),
    ):
        print(f"  {label:<34}: {int(getattr(lib, fn)())}")

    style = lib.GetTriggerStyle()
    print("\nTrigger defaults:")
    print(f"  mode={lib.GetTriggerMode()} (0=AUTO 1=continuous)  style=0x{style:03X} ({TRIGGER_STYLES.get(style, '?')})"
          f"  source=CH{lib.GetTriggerSource() + 1}  level={lib.GetTriggerLevel()} mV")
    print(f"  pulse width {lib.GetTriggerPulseWidthNsMin()}..{lib.GetTriggerPulseWidthNsMax()} ns"
          f"  sensitivity={lib.GetTriggerSenseDiv():g} div  pre-trigger={lib.GetPreTriggerPercent()}%")
    print(f"Coupling            : CH1={'AC' if lib.GetAcDc(0) else 'DC'}  CH2={'AC' if lib.GetAcDc(1) else 'DC'}")

    if lib.IsSupportDDSDevice():
        count = lib.GetDDSSupportBoxingStyle(None)
        styles = (i * max(count, 1))()
        count = lib.GetDDSSupportBoxingStyle(styles)
        print(f"DDS waveforms       : {', '.join(DDS_STYLES.get(v, str(v)) for v in styles[:count])}")
        print(f"DDS offset range    : {lib.GetDDSBiasResistanceRangeMin()}..{lib.GetDDSBiasResistanceRangeMax()}")
        print(f"DDS amplitude range : {lib.GetDDSZoomResistanceRangeMin()}..{lib.GetDDSZoomResistanceRangeMax()}")
    if lib.IsSupportIODevice():
        print(f"IO count            : {lib.GetSupportIoNumber()}")
    return rates


def capture_once(lib, rates, args):
    if not rates:
        print("\nEmpty sample rate list, skipping capture.")
        return False
    rate = min(rates, key=lambda r: abs(r - args.rate))
    lib.SetOscSample(rate)
    for ch in (0, 1):
        lib.SetOscChannelRange(ch, -args.range, args.range)
    time.sleep(0.2)

    real_kb = lib.Capture(args.length, 0)
    print(f"\nCapture: {fmt_rate(rate)}, ±{args.range} mV, requested {args.length} KB, device accepted {real_kb} KB")
    timeout = max(3.0, 2 * real_kb * 1024 / rate)
    deadline = time.monotonic() + timeout
    while not lib.IsDataReady():
        if time.monotonic() > deadline:
            print(f"  no data within {timeout:g} s.")
            return False
        time.sleep(0.01)

    size = max(real_kb, args.length) * 1024
    buf = (d * size)()
    for ch in (0, 1):
        got = lib.ReadVoltageDatas(ch, buf, size)
        if not got:
            print(f"  CH{ch + 1}: no data")
            continue
        data = buf[:got]
        mean = sum(data) / got
        rms = math.sqrt(sum((v - mean) ** 2 for v in data) / got)
        freq = estimate_frequency(data, rate)
        print(f"  CH{ch + 1}: {got} samples  min={min(data):+.3f} V  max={max(data):+.3f} V"
              f"  Vpp={max(data) - min(data):.3f} V  mean={mean:+.3f} V  AC rms={rms:.3f} V"
              f"  f≈{f'{freq:.1f} Hz' if freq else '-'}"
              f"  resolution={lib.GetVoltageResolution(ch) * 1000:.2f} mV"
              f"{'  [OUT OF RANGE]' if lib.IsVoltageDatasOutRange(ch) else ''}")
    return True


def finish_dll(lib, timeout=3.0):
    # On Linux FinishDll can busy-wait forever for an internal thread that is blocked in poll().
    worker = threading.Thread(target=lib.FinishDll, daemon=True)
    worker.start()
    worker.join(timeout)
    return not worker.is_alive()


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except AttributeError:
        pass
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--lib", type=Path, help="path to vdso.dll / libvdso.so.1.0")
    ap.add_argument("--wait", type=float, default=15, help="seconds to wait for the device (default 15)")
    ap.add_argument("--rate", type=int, default=1_000_000, help="requested sample rate in Hz (closest is used)")
    ap.add_argument("--range", type=int, default=5000, help="channel range ±mV (default 5000)")
    ap.add_argument("--length", type=int, default=16, help="capture length in KB (default 16)")
    args = ap.parse_args()

    path = args.lib or default_lib_path()
    print(f"Platform : {platform.system()} {platform.machine()}, Python {platform.python_version()}"
          f" ({struct.calcsize('P') * 8}-bit)")
    print(f"Library  : {path}")
    try:
        lib = load_library(path)
    except (OSError, AttributeError) as e:
        print(f"Cannot load: {e}")
        if WINDOWS:
            print("Is the VC++ 2015-2022 runtime (MSVCP140.dll) installed?")
        else:
            print("Is libusb-1.0 installed? (sudo apt install libusb-1.0-0)")
        return 1

    added = threading.Event()

    @NoticeCallBack
    def on_add(_):
        added.set()

    @NoticeCallBack
    def on_remove(_):
        added.clear()
        print("\n[device removed]")

    print(f"InitDll  : {lib.InitDll()}")
    lib.SetDevNoticeCallBack(None, on_add, on_remove)
    code = 1
    try:
        if not wait_for_device(lib, added, args.wait):
            print_not_found_hints()
            code = 2
        else:
            time.sleep(0.5)  # the SDK demo also waits half a second after the add notification
            rates = report_device(lib)
            code = 0 if capture_once(lib, rates, args) else 3
        return code
    finally:
        if not finish_dll(lib):
            print("\nWarning: FinishDll did not return within 3 s (vendor library bug), forcing exit.")
            sys.stdout.flush()
            os._exit(code)


if __name__ == "__main__":
    sys.exit(main())
