#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Proof of concept: talk to the ISDS205X WITHOUT the vendor library (pyusb / libusb).

Protocol and calibration live in tools/isds205x.py (see docs/protocol.md). Performs the
handshake, reads the calibration from the descriptor, picks a stage for the requested
range and captures.

    python3 tools/native_poc.py [--range MV] [--rate HZ] [--out capture.bin]
    python3 tools/native_poc.py --scan [--compare trace/<run>]   try every stage, compare with vdso
"""
import argparse
import csv
import struct
import sys
from pathlib import Path

import usb.core
import usb.util

sys.path.insert(0, str(Path(__file__).resolve().parent))
import isds205x as isds  # noqa: E402
from isds205x import CH1, CH2, DEV, OSC  # noqa: E402


class Scope:
    def __init__(self):
        self.dev = usb.core.find(idVendor=isds.VID, idProduct=isds.PID)
        if self.dev is None:
            sys.exit("Device not found (D4A2:5661). Is the rear switch on 'Logic' and the device attached to WSL?")
        if self.dev.is_kernel_driver_active(0):
            self.dev.detach_kernel_driver(0)
        usb.util.claim_interface(self.dev, 0)
        self.cal = None
        self.stage = {}

    def close(self):
        usb.util.release_interface(self.dev, 0)
        usb.util.dispose_resources(self.dev)

    def write(self, data):
        self.dev.write(isds.EP_OUT, data, timeout=1000)

    def read(self, timeout=1000):
        """Read one message from the control endpoint, answering any heartbeats in between."""
        while True:
            data = bytes(self.dev.read(isds.EP_CTRL_IN, 1024, timeout=timeout))
            if data == isds.HEARTBEAT:
                self.write(isds.HEARTBEAT_REPLY)
                continue
            return data

    def drain(self):
        # Leftovers of a capture a previous host never read block the handshake.
        try:
            while True:
                self.dev.read(isds.EP_DATA_IN, 65536, timeout=100)
        except usb.core.USBTimeoutError:
            pass
        try:
            while True:
                self.read(timeout=100)
        except usb.core.USBTimeoutError:
            pass

    def send(self, frame, ack=True):
        self.write(frame)
        if ack:
            reply = self.read()
            if reply[:3] != frame[:3]:
                raise RuntimeError(f"unexpected reply {reply.hex()} (sent {frame.hex()})")

    def open(self):
        self.drain()
        self.write(isds.SYNC)
        if self.read() != isds.SYNC:
            raise RuntimeError("bad sync echo")
        host = isds.new_nonce()
        self.write(host)
        device = self.read()
        self.write(isds.handshake_reply(host, device))
        desc = self.read()
        while len(desc) < 0xA0:
            desc += self.read()
        self.write(isds.CONFIRM)
        self.read()
        self.cal = isds.Calibration(isds.parse_descriptor(desc))
        # Same start-up sequence as vdso (docs/protocol.md §3)
        g_init, d_init = self.cal.desc.gain_codes[-1], 0x062C
        self.send(isds.frame_value(OSC, DEV, 0x12))
        for ch in (CH1, CH2):
            self.send(isds.frame_value(OSC, ch, 0x01))
        self.set_rate(self.cal.desc.rates[-1])
        for cmd, value in ((0x06, g_init), (0x04, d_init)):
            for ch in (CH1, CH2):
                self.send(isds.frame_payload(OSC, ch, cmd, struct.pack(">H", value)))
        for ch in (CH1, CH2):
            self.send(isds.frame_value(OSC, ch, 0x19, 0))
        return self.cal.desc

    def set_rate(self, hz):
        self.send(isds.frame_payload(OSC, DEV, 0x08, struct.pack(">I", hz)))

    def set_stage(self, ch, stage):
        g, d = self.cal.codes(stage, ch)
        self.send(isds.frame_payload(OSC, ch, 0x06, struct.pack(">H", g)))
        self.send(isds.frame_payload(OSC, ch, 0x04, struct.pack(">H", d)))
        self.stage[ch] = stage

    def set_trigger(self, level_mv, min_mv, max_mv, source=CH1):
        level, hyst = self.cal.trigger_codes(self.stage[source], source, level_mv, min_mv, max_mv)
        self.send(isds.frame_trigger(level, hyst))

    def capture(self, samples):
        """Return `samples` valid raw samples per channel as (CH1 bytes, CH2 bytes)."""
        request = -(-(samples + isds.SETTLE_SAMPLES) // 1024) * 1024
        self.send(isds.frame_value(OSC, DEV, 0x02, request), ack=False)
        raw = bytes(self.dev.read(isds.EP_DATA_IN, request * 2, timeout=5000))
        skip = isds.SETTLE_SAMPLES
        return raw[0::2][skip:skip + samples], raw[1::2][skip:skip + samples]

    def stop(self):
        for ch in (CH2, CH1):
            self.send(isds.frame_value(OSC, ch, 0x10))


def stats(values):
    mean = sum(values) / len(values)
    return mean, min(values), max(values)


def vendor_means(run):
    """Mean voltage (mV) per (G, D, channel) from a vdso trace."""
    means = {}
    if not run:
        return means
    run = Path(run)
    for row in csv.DictReader(open(run / "cal.csv")):
        if row["rate"] != "1000000" or row["offset"] in ("", "None"):
            continue
        ch = int(row["ch"])
        data = (run / f"{row['tag']}_ch{ch}.f64").read_bytes()
        volts = struct.unpack(f"<{len(data) // 8}d", data)
        means.setdefault((int(row["c06"], 16), int(row["c04"], 16), ch), sum(volts) / len(volts) * 1000)
    return means


def scan(scope, compare):
    vendor = vendor_means(compare)
    cal = scope.cal
    scope.set_rate(1_000_000)
    print(f"\n{'G':>6} {'D (CH1/CH2)':>14} {'lsb mV':>16}   {'CH1 mean mV':>11} {'vdso':>8} {'diff/lsb':>8}"
          f"   {'CH2 mean mV':>11} {'vdso':>8} {'diff/lsb':>8}")
    for stage in cal.stages():
        for ch in (CH1, CH2):
            scope.set_stage(ch, stage)
        lo, hi = cal.span_mv(stage, CH1)
        scope.set_trigger(0, lo, hi)
        scope.capture(4096)  # discard the first capture after a stage change
        raws = scope.capture(16384)
        cells = []
        for ch, raw in zip((CH1, CH2), raws):
            mean, _, _ = stats(cal.to_mv(raw, stage, ch))
            g, d = cal.codes(stage, ch)
            ref = vendor.get((g, d, ch))
            diff = f"{(mean - ref) / cal.lsb_mv(stage, ch):+8.2f}" if ref is not None else f"{'-':>8}"
            cells.append(f"{mean:11.2f} {ref if ref is not None else float('nan'):8.2f} {diff}")
        g = cal.codes(stage, CH1)[0]
        d1, d2 = cal.codes(stage, CH1)[1], cal.codes(stage, CH2)[1]
        print(f"{g:#06x} {d1:#06x}/{d2:#06x} {cal.lsb_mv(stage, 1):7.3f}/{cal.lsb_mv(stage, 2):7.3f}   "
              + "   ".join(cells))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--range", type=int, default=1000, help="±mV (default 1000)")
    ap.add_argument("--rate", type=int, default=1_000_000, help="sample rate in Hz")
    ap.add_argument("--out", help="write the raw capture (CH1 then CH2) to this file")
    ap.add_argument("--scan", action="store_true", help="scan every gain stage")
    ap.add_argument("--compare", help="vdso trace to compare against in --scan (after fit_trace.py)")
    args = ap.parse_args()

    scope = Scope()
    try:
        desc = scope.open()
        print(f"device  : {desc.vendor} / {desc.model}  id {desc.device_id:016X}")
        print(f"rates   : {', '.join(f'{r / 1e6:g} MSa/s' for r in desc.rates)}")
        if args.scan:
            scan(scope, args.compare)
        else:
            scope.set_rate(args.rate)
            for ch in (CH1, CH2):
                stage = scope.cal.select(ch, -args.range, args.range)
                scope.set_stage(ch, stage)
                lo, hi = scope.cal.span_mv(stage, ch)
                print(f"CH{ch}     : ±{args.range} mV → G={scope.cal.codes(stage, ch)[0]:#06x} "
                      f"D={scope.cal.codes(stage, ch)[1]:#06x}, lsb {scope.cal.lsb_mv(stage, ch):.4f} mV, "
                      f"span {lo:.0f}..{hi:.0f} mV")
            scope.set_trigger(0, -args.range, args.range)
            raws = scope.capture(16384)
            for ch, raw in zip((CH1, CH2), raws):
                mean, lo, hi = stats(scope.cal.to_mv(raw, scope.stage[ch], ch))
                print(f"  CH{ch}: {len(raw)} samples  raw[{min(raw)}..{max(raw)}]  "
                      f"min={lo:+.2f} mV  max={hi:+.2f} mV  mean={mean:+.2f} mV")
            if args.out:
                Path(args.out).write_bytes(raws[0] + raws[1])
        scope.stop()
    finally:
        scope.close()


if __name__ == "__main__":
    main()
