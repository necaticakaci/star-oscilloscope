#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Log the DC level of both channels on every stage, for gain/zero calibration against a DMM.

Each cycle sets every stage in turn (in alternating order, so the coarse-gain relay switches once
per cycle), captures 200k samples at 1 MSa/s and writes one JSON line:
    {"t": s, "cycle": n, "stages": [[[ch1_mean_mV, ch1_clipped], [ch2_mean_mV, ch2_clipped]], ...]}
The first line describes the stages. Analyse the result with dc_gain.py (docs/measurements.md).

    python3 dc_logger.py SECONDS OUT.jsonl
"""
import json
import signal
import sys
import time

import numpy as np

from isds205x import CH1, CH2
from native_poc import Scope

SAMPLES = 200_000


def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else 1800
    out = open(sys.argv[2] if len(sys.argv) > 2 else "dc_log.jsonl", "a")
    # Stop cleanly on kill as well, so a capture is never left half read.
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt))
    s = Scope()
    s.open()
    try:
        s.set_rate(1_000_000)
        stages = s.cal.stages()
        spans = [[s.cal.span_mv(st, ch) for ch in (CH1, CH2)] for st in stages]
        codes = [{"gain_code": s.cal.codes(st, CH1)[0], "level_code": [s.cal.codes(st, ch)[1] for ch in (CH1, CH2)]}
                 for st in stages]
        meta = {"model": s.cal.desc.model, "device_id": f"{s.cal.desc.device_id:016X}", "stages": codes,
                "stages_span_mv": spans, "samples": SAMPLES, "rate": 1_000_000}
        out.write(json.dumps({"meta": meta}) + "\n")
        t0 = time.time()
        cycle = 0
        while time.time() - t0 < duration:
            rec = {"t": round(time.time() - t0, 2), "cycle": cycle, "stages": [None] * len(stages)}
            order = range(len(stages)) if cycle % 2 == 0 else reversed(range(len(stages)))
            for i in order:
                for ch in (CH1, CH2):
                    s.set_stage(ch, stages[i])
                lo, hi = spans[i][0]
                s.set_trigger(0, lo, hi)
                s.capture(4096)  # first capture after a stage change is discarded
                a, b = s.capture(SAMPLES)
                row = []
                for ch, raw in ((CH1, a), (CH2, b)):
                    r = np.frombuffer(raw, np.uint8)
                    mean_mv = float(np.mean(s.cal.to_mv(raw, stages[i], ch)))
                    row.append([round(mean_mv, 3), bool(r.min() == 0 or r.max() == 255)])
                rec["stages"][i] = row
            out.write(json.dumps(rec) + "\n")
            out.flush()
            cycle += 1
    except KeyboardInterrupt:
        pass
    finally:
        s.stop()
        s.close()


if __name__ == "__main__":
    main()
