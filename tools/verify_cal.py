#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the isds205x.py calibration model against the vendor library's output.

For every capture in cal.csv (written by fit_trace.py), converts the raw EP 0x82 dump to volts
using only constants computed from the device descriptor, and compares the result with
ReadVoltageDatas.

    python3 tools/verify_cal.py trace/<run>
"""
import csv
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import isds205x as isds  # noqa: E402


def find_stage(cal, ch, g_hex, d_hex):
    for stage in [isds.Stage(g, l) for g in range(len(cal.desc.gain_codes)) for l in range(len(cal.desc.levels))]:
        g, d = cal.codes(stage, ch)
        if f"{g:04x}" == g_hex and f"{d:04x}" == d_hex:
            return stage
    return None


def main():
    run = Path(sys.argv[1])
    desc_file = run / "dumps" / "000000_ep81.bin"
    desc = isds.parse_descriptor(desc_file.read_bytes())
    cal = isds.Calibration(desc)
    print(f"{desc.vendor} / {desc.model}  id {desc.device_id:016X}  clock {desc.base_clock} Hz")
    print(f"rates {desc.rates}  gain codes {[hex(g) for g in desc.gain_codes]}")
    for i, lvl in enumerate(desc.levels):
        print(f"  level {i}: D={[hex(c) for c in lvl.code]}  cal(mV)={lvl.cal}")
    print("\nStages (CH1 / CH2):")
    for s in cal.stages():
        spans = [cal.span_mv(s, ch) for ch in (1, 2)]
        print(f"  G={cal.codes(s, 1)[0]:#06x} D={cal.codes(s, 1)[1]:#06x}/{cal.codes(s, 2)[1]:#06x}  "
              f"lsb {cal.lsb_mv(s, 1):8.4f}/{cal.lsb_mv(s, 2):8.4f} mV  "
              f"span {spans[0][0]:8.1f}..{spans[0][1]:7.1f} / {spans[1][0]:8.1f}..{spans[1][1]:7.1f} mV")

    worst, checked, failures = 0.0, 0, []
    with open(run / "cal.csv") as f:
        for row in csv.DictReader(f):
            if row["offset"] in ("", "None"):
                continue
            ch, k, n = int(row["ch"]), int(row["offset"]), int(row["n"])
            stage = find_stage(cal, ch, row["c06"], row["c04"])
            if stage is None:
                failures.append(f"{row['tag']} CH{ch}: no stage for {row['c06']}/{row['c04']}")
                continue
            raw = np.fromfile(run / "dumps" / row["dump"], dtype=np.uint8)[ch - 1::2][k:k + n].astype(float)
            vendor = np.fromfile(run / f"{row['tag']}_ch{ch}.f64", dtype="<f8") * 1000
            ours = cal.lsb_mv(stage, ch) * (raw - isds.ADC_MID) + cal.offset_mv(stage, ch)
            err = float(np.max(np.abs(ours - vendor)))
            worst, checked = max(worst, err), checked + 1
            if err > 1e-6:
                failures.append(f"{row['tag']} CH{ch}: max error {err:.6f} mV")
    print(f"\n{checked} capture/channel pairs compared, worst error {worst:.9f} mV")
    for line in failures:
        print("  FAIL:", line)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
