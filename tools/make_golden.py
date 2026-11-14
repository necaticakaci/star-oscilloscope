#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the golden test data for the C++ driver from a vdso trace and the Python reference model.

    python3 tools/make_golden.py trace/calsweep1 tests/data

Outputs:
  descriptor_isds205x_1.1.bin   handshake descriptor as received from the device, serial number replaced by 1
  vendor_captures.bin           raw samples + vdso ReadVoltageDatas output, one record per (G, D, channel)
  model_stages.csv              isds205x.Calibration per stage and channel (lsb, offset, zero, span)
  model_select.csv              isds205x.Calibration.select() and trigger_codes() for a set of ranges

vendor_captures.bin layout (little-endian):
  char[8] "ISDSGLD1", u32 record_count, then per record:
  u16 G, u16 D, u8 channel (1|2), u8 reserved, u16 n, u8 raw[n], f64 volts[n]
"""
import csv
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import isds205x as isds  # noqa: E402

SAMPLES = 256
DEVICE_ID_SIZE = 8
PLACEHOLDER_ID = 1
SELECT_RANGES = ([(-r, r) for r in (10, 20, 50, 100, 200, 250, 300, 500, 600, 700, 800, 1000, 1100, 1200,
                                    1300, 2000, 3000, 3100, 4000, 5000, 5300, 6000, 20000)]
                 + [(0, 100), (0, 1000), (0, 2000), (-2000, 0), (500, 1500), (-3000, 1000), (1000, 5000),
                    (-200, 0), (-40, 100), (-1150, 1000)])


def vendor_records(run):
    seen, records = set(), []
    for row in csv.DictReader(open(run / "cal.csv")):
        if row["offset"] in ("", "None") or row["rate"] != "1000000":
            continue
        g, d, ch = int(row["c06"], 16), int(row["c04"], 16), int(row["ch"])
        if (g, d, ch) in seen:
            continue
        seen.add((g, d, ch))
        k = int(row["offset"])
        raw = np.fromfile(run / "dumps" / row["dump"], dtype=np.uint8)[ch - 1::2][k:k + SAMPLES]
        volts = np.fromfile(run / f"{row['tag']}_ch{ch}.f64", dtype="<f8")[:SAMPLES]
        records.append((g, d, ch, raw, volts))
    return records


def main():
    run, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    desc_bytes = (run / "dumps" / "000000_ep81.bin").read_bytes()
    # The last eight bytes are the device's serial number: the published data carry a placeholder instead.
    desc_bytes = desc_bytes[:-DEVICE_ID_SIZE] + PLACEHOLDER_ID.to_bytes(DEVICE_ID_SIZE, "little")
    (out / "descriptor_isds205x_1.1.bin").write_bytes(desc_bytes)
    cal = isds.Calibration(isds.parse_descriptor(desc_bytes))

    records = vendor_records(run)
    with open(out / "vendor_captures.bin", "wb") as f:
        f.write(b"ISDSGLD1" + struct.pack("<I", len(records)))
        for g, d, ch, raw, volts in records:
            f.write(struct.pack("<HHBBH", g, d, ch, 0, len(raw)))
            f.write(raw.astype(np.uint8).tobytes())
            f.write(volts.astype("<f8").tobytes())

    with open(out / "model_stages.csv", "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["order", "gain_index", "level_index", "channel", "g", "d",
                    "lsb_mv", "offset_mv", "zero", "span_min_mv", "span_max_mv"])
        for order, stage in enumerate(cal.stages()):
            for ch in (1, 2):
                g, d = cal.codes(stage, ch)
                lo, hi = cal.span_mv(stage, ch)
                w.writerow([order, stage.gain, stage.level, ch, g, d, repr(cal.lsb_mv(stage, ch)),
                            cal.offset_mv(stage, ch), repr(cal.zero(stage, ch)), repr(lo), repr(hi)])

    with open(out / "model_select.csv", "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["channel", "min_mv", "max_mv", "g", "d", "trigger_level_code", "trigger_hysteresis_code"])
        for ch in (1, 2):
            for lo, hi in SELECT_RANGES:
                stage = cal.select(ch, lo, hi)
                g, d = cal.codes(stage, ch)
                level, hyst = cal.trigger_codes(stage, ch, 0, lo, hi)
                w.writerow([ch, lo, hi, g, d, level, hyst])

    print(f"{len(records)} vendor records, {len(cal.stages())} stages, {2 * len(SELECT_RANGES)} selections → {out}")


if __name__ == "__main__":
    main()
