#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Recover the vendor library's raw-to-volts conversion for every capture in a trace_vdso.py log.

For each capture, finds the range commands (06/04/13) most recently sent to the device and the
last EP 0x82 dump between CAP-BEGIN and CAP-READY, lines the raw data up with the
ReadVoltageDatas output and checks V = lsb * (raw - zero).

    python3 tools/fit_trace.py trace/<run>      → table + trace/<run>/cal.csv
"""
import csv
import re
import sys
from pathlib import Path

import numpy as np

TRANSFER = re.compile(r"T\d+\s+(IN|OUT)\s+([0-9a-f]{2})\s+len=\d+\s+rc=(-?\d+)\s+got=(\d+)\s*(.*)$")
PAYLOAD = re.compile(r"(?:\[([^\]]+)\]\s*)?([0-9a-f ]*)")
KV = re.compile(r"(\w+)=(\S+)")


def parse(run):
    """Return capture records: tag, parameters, command state at capture time, dump file."""
    state = {}            # (target, command) -> last payload (hex)
    captures, current = [], None
    for line in (run / "usb.log").read_text().splitlines():
        stamp, _, rest = line.strip().partition(" ")
        if rest.startswith("---- "):
            text = rest[5:]
            word, _, args = text.partition(" ")
            if word == "CAP-BEGIN":
                tag, _, params = args.partition(" ")
                current = {"tag": tag, **dict(KV.findall(params)), "dump": None}
            elif word == "CAP-READY" and current:
                current["state"] = dict(state)
                current["kb"] = int(dict(KV.findall(args))["kb"])
            elif word == "CAP-END" and current:
                current.update(KV.findall(args))
                captures.append(current)
                current = None
            continue
        m = TRANSFER.search(rest)
        if not m:
            continue
        direction, ep = m.group(1), m.group(2)
        dump, hexdata = PAYLOAD.match(m.group(5)).groups()
        data = bytes.fromhex(hexdata.replace(" ", ""))
        if direction == "OUT" and len(data) >= 8 and data[0] == 0x02:
            state[(data[1], data[2])] = data[7:].hex() if len(data) > 8 else data[3:7].hex()
        elif direction == "IN" and ep == "82" and dump and current and "state" not in current:
            current["dump"] = run / "dumps" / Path(dump).name
    return captures


def align(raw, volts, lsb):
    """Find where volts starts inside raw; returns (offset, zero, max_error_V, fitted_lsb)."""
    n, m = len(volts), len(raw)
    if m < n:
        return None
    candidates = [1028, 0, m - n]
    r, v = raw - raw.mean(), volts - volts.mean()
    size = 1 << int(np.ceil(np.log2(m + n)))
    corr = np.fft.irfft(np.fft.rfft(r, size) * np.conj(np.fft.rfft(v, size)), size)[: m - n + 1]
    candidates += [int(k) for k in np.argsort(corr)[::-1][:20]]
    best = None
    for k in candidates:
        if not 0 <= k <= m - n:
            continue
        seg = raw[k:k + n]
        zero = float(np.mean(seg - volts / lsb))
        err = float(np.max(np.abs(lsb * (seg - zero) - volts)))
        fitted = float(np.polyfit(seg, volts, 1)[0]) if np.ptp(seg) else float("nan")
        if best is None or err < best[2]:
            best = (k, zero, err, fitted)
        if err < 1e-9:
            break
    return best


def main():
    run = Path(sys.argv[1])
    rows = []
    for cap in parse(run):
        if not cap["dump"]:
            print(f"{cap['tag']}: no dump")
            continue
        raw_all = np.fromfile(cap["dump"], dtype=np.uint8).astype(float)
        state = cap["state"]
        for ch in (1, 2):
            volts = np.fromfile(run / f"{cap['tag']}_ch{ch}.f64", dtype="<f8")
            lsb = float(cap[f"res{ch}"]) / 1000
            lo, hi = map(int, cap[f"ch{ch}"].split(".."))
            fit = align(raw_all[ch - 1::2], volts, lsb) if len(volts) else None
            k, zero, err, fitted = fit if fit else (None, float("nan"), float("nan"), float("nan"))
            rows.append({
                "tag": cap["tag"], "rate": int(cap["rate"]), "ch": ch, "min_mv": lo, "max_mv": hi,
                "c06": state.get((ch, 0x06), ""), "c04": state.get((ch, 0x04), ""),
                "c13": state.get((0xFF, 0x13), ""), "c19": state.get((ch, 0x19), ""),
                "n": len(volts), "offset": k, "lsb_mv": round(lsb * 1000, 6),
                "lsb_fit_mv": round(fitted * 1000, 6), "zero": round(zero, 6),
                "err_mv": round(err * 1000, 6), "out": cap[f"out{ch}"], "dump": cap["dump"].name,
            })
    if not rows:
        sys.exit("no captures with CAP-BEGIN/CAP-END markers (record a new trace with trace_vdso.py)")
    with open(run / "cal.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    print(f"{'tag':<16}{'rate':>9}{'ch':>3}{'range mV':>15}{'06':>6}{'04':>6}{'13':>10}"
          f"{'n':>7}{'ofs':>6}{'lsb mV':>11}{'zero':>11}{'err mV':>9}")
    for r in rows:
        print(f"{r['tag']:<16}{r['rate']:>9}{r['ch']:>3}{r['min_mv']:>7}..{r['max_mv']:<6}{r['c06']:>6}{r['c04']:>6}"
              f"{r['c13']:>10}{r['n']:>7}{str(r['offset']):>6}{r['lsb_mv']:>11.4f}{r['zero']:>11.4f}{r['err_mv']:>9.4f}")
    print(f"\n{len(rows)} rows → {run / 'cal.csv'}")


if __name__ == "__main__":
    main()
