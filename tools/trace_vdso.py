#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the vendor library through a scripted scenario and map its USB traffic to API calls (Linux only).

Runs under usbsniff.so. A marker is written to the log before every API call, so the log
shows which call sent which bytes. Captured voltages (ReadVoltageDatas) are saved as well,
to be lined up with the raw USB data by tools/fit_trace.py.

    tools/trace_vdso.sh [output_dir] [scenario]     scenario: basic (default) | calsweep
"""
import ctypes as C
import os
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import probe_vdso as vdso  # noqa: E402

OUT = Path(sys.argv[1] if len(sys.argv) > 1 else "trace")
SCENARIO = sys.argv[2] if len(sys.argv) > 2 else "basic"
OUT.mkdir(parents=True, exist_ok=True)

try:
    _mark = C.CDLL(None).usbsniff_mark
    _mark.argtypes = [C.c_char_p]
except AttributeError:
    _mark = None


def mark(text):
    print(f"--- {text}", flush=True)
    if _mark:
        _mark(text.encode())


def call(lib, name, *args, settle=0.3):
    mark(f"{name}({', '.join(map(str, args))})")
    result = getattr(lib, name)(*args)
    time.sleep(settle)
    return result


def capture(lib, tag, rate, ranges, length_kb=16, settle=0.3):
    """ranges: ((ch1_min, ch1_max), (ch2_min, ch2_max)) in mV. The markers are parsed by fit_trace.py."""
    (lo1, hi1), (lo2, hi2) = ranges
    mark(f"CAP-BEGIN {tag} rate={rate} ch1={lo1}..{hi1} ch2={lo2}..{hi2}")
    call(lib, "SetOscSample", rate, settle=settle)
    for ch, (lo, hi) in enumerate(ranges):
        call(lib, "SetOscChannelRange", ch, lo, hi, settle=settle)
    real_kb = call(lib, "Capture", length_kb, 0, settle=0)
    deadline = time.monotonic() + 5
    while not lib.IsDataReady():
        if time.monotonic() > deadline:
            mark(f"CAP-TIMEOUT {tag}")
            return
        time.sleep(0.005)
    mark(f"CAP-READY {tag} kb={real_kb}")
    size = max(real_kb, length_kb) * 1024
    buf = (C.c_double * size)()
    info = []
    for ch in (0, 1):
        got = lib.ReadVoltageDatas(ch, buf, size)
        (OUT / f"{tag}_ch{ch + 1}.f64").write_bytes(bytes(buf)[: got * 8])
        info.append(f"n{ch + 1}={got} res{ch + 1}={lib.GetVoltageResolution(ch) * 1000:.6f}"
                    f" out{ch + 1}={lib.IsVoltageDatasOutRange(ch)}")
    mark(f"CAP-END {tag} {' '.join(info)}")


def sample_rates(lib):
    n = lib.GetOscSupportSampleNum()
    arr = (C.c_uint * max(n, 1))()
    return list(arr[: lib.GetOscSupportSamples(arr, n)])


def scenario_basic(lib):
    rates = sample_rates(lib)
    mark(f"rates {rates}, memory {lib.GetMemoryLength()} KB")

    mark("idle 2 s (heartbeat?)")
    time.sleep(2)

    for rate in rates:
        call(lib, "SetOscSample", rate)
    for r in (100, 250, 500, 1000, 2500, 5000, 10000):
        call(lib, "SetOscChannelRange", 0, -r, r)
    call(lib, "SetOscChannelRange", 0, 0, 2000)
    for ac in (1, 0):
        call(lib, "SetAcDc", 0, ac)
    call(lib, "SetAcDc", 1, 1)
    call(lib, "SetAcDc", 1, 0)

    for name, args in (("SetTriggerSource", (1,)), ("SetTriggerSource", (0,)),
                       ("SetTriggerLevel", (500,)), ("SetTriggerLevel", (0,)),
                       ("SetTriggerStyle", (2,)), ("SetTriggerStyle", (4,)), ("SetTriggerStyle", (1,)),
                       ("SetPreTriggerPercent", (20,)), ("SetPreTriggerPercent", (50,)),
                       ("SetTriggerMode", (1,)), ("SetTriggerMode", (0,))):
        call(lib, name, *args)

    capture(lib, "c1_1M_5V", 1_000_000, ((-5000, 5000),) * 2)
    capture(lib, "c2_1M_1V", 1_000_000, ((-1000, 1000),) * 2)
    capture(lib, "c3_48M_5V", rates[-1], ((-5000, 5000),) * 2)
    capture(lib, "c4_4M_500mV", 4_000_000, ((-500, 500),) * 2)

    for name, args in (("SetDDSBoxingStyle", (0,)), ("SetDDSPinlv", (1000,)), ("SetDDSDutyCycle", (50,)),
                       ("SetDDSZoomResistance", (1000,)), ("SetDDSBiasResistance", (2048,)),
                       ("DDSOutputEnable", (1,)), ("SetDDSPinlv", (1_000_000,)), ("SetDDSBoxingStyle", (1,)),
                       ("SetDDSDutyCycle", (25,)), ("DDSOutputEnable", (0,))):
        call(lib, name, *args)


SWEEP_SYMMETRIC = (20, 50, 100, 150, 200, 250, 300, 400, 500, 600, 700, 800, 1000, 1200, 1300, 1500,
                   2000, 2500, 3000, 4000, 5000, 6000, 8000, 10000, 20000)
SWEEP_ASYMMETRIC = ((0, 1000), (0, 2000), (-2000, 0), (500, 1500), (-3000, 1000), (1000, 5000), (-200, 0))


def scenario_calsweep(lib):
    rates = sample_rates(lib)
    capture(lib, "warmup", 1_000_000, ((-5000, 5000),) * 2)  # vdso makes extra captures on the first one
    for r in SWEEP_SYMMETRIC:
        capture(lib, f"sym{r}", 1_000_000, ((-r, r),) * 2, settle=0.1)
    for lo, hi in SWEEP_ASYMMETRIC:
        capture(lib, f"asym{lo}_{hi}", 1_000_000, ((lo, hi),) * 2, settle=0.1)
    capture(lib, "mixed", 1_000_000, ((-1000, 1000), (-5000, 5000)), settle=0.1)
    for rate in rates:
        capture(lib, f"rate{rate // 1000}k", rate, ((-1000, 1000),) * 2, settle=0.1)
    for ac in (1, 0):
        call(lib, "SetAcDc", 0, ac, settle=0.1)
        call(lib, "SetAcDc", 1, ac, settle=0.1)
        capture(lib, f"acdc{ac}", 1_000_000, ((-1000, 1000),) * 2, settle=0.1)


SCENARIOS = {"basic": scenario_basic, "calsweep": scenario_calsweep}


def main():
    if SCENARIO not in SCENARIOS:
        sys.exit(f"unknown scenario: {SCENARIO} ({', '.join(SCENARIOS)})")
    lib = vdso.load_library(vdso.default_lib_path())
    added = threading.Event()

    @vdso.NoticeCallBack
    def on_add(_):
        mark("callback: device added")
        added.set()

    @vdso.NoticeCallBack
    def on_remove(_):
        mark("callback: device removed")

    mark("InitDll")
    lib.InitDll()
    lib.SetDevNoticeCallBack(None, on_add, on_remove)
    code = 1
    try:
        if vdso.wait_for_device(lib, added, 10):
            time.sleep(1)
            SCENARIOS[SCENARIO](lib)
            mark("scenario done")
            code = 0
    finally:
        mark("FinishDll")
        if not vdso.finish_dll(lib):
            mark("FinishDll did not return, exiting")
            sys.stdout.flush()
            os._exit(code)
    return code


if __name__ == "__main__":
    sys.exit(main())
