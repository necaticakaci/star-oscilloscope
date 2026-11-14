# SPDX-License-Identifier: GPL-3.0-or-later
"""ISDS205X reference driver: protocol, descriptor and calibration (no vendor library needed).

The protocol and the calibration model are documented in docs/protocol.md. This module is
both the specification and the test oracle for the C++ driver.

Calibration model (matches the vdso output with 0.0000 mV error):
    lsb_mV = 1000 * G / (255 * D)          G: gain code (command 06), D: level code (command 04)
    V_mV   = lsb_mV * (raw - 127.5) + cal   cal: s32 from the descriptor for (level, channel, G)
"""
import os
import struct
from dataclasses import dataclass, field

VID, PID = 0xD4A2, 0x5661
EP_OUT, EP_CTRL_IN, EP_DATA_IN = 0x01, 0x81, 0x82

SYS, DDS, OSC = 0x00, 0x01, 0x02
DEV, CH1, CH2 = 0xFF, 0x01, 0x02

SYNC = bytes.fromhex("ffffffffff1122334455")
HEARTBEAT = bytes.fromhex("00fffe10")
HEARTBEAT_REPLY = bytes.fromhex("0000fe0000000200")
CONFIRM = b"\xaa"

ADC_MID = 127.5
FULL_SCALE_MV = 1000.0
ADC_STEPS = 255
SETTLE_SAMPLES = 1028  # the first 1024 samples are FIFO leftovers, then ~4 samples of transition


# --------------------------------------------------------------------------- frames

def frame_value(kind, target, cmd, value=0):
    """8-byte value form: [kind][target][cmd][u32 BE][00]."""
    return struct.pack(">BBBIB", kind, target, cmd, value, 0)


def frame_payload(kind, target, cmd, payload):
    """Payload form: [kind][target][cmd][u32 BE length][payload]."""
    return struct.pack(">BBBI", kind, target, cmd, len(payload)) + payload


def frame_trigger(level_code, hysteresis_code):
    # Command 13 is the exception: the u32 field is 0, followed by a 4-byte payload.
    return struct.pack(">BBBIBBBB", OSC, DEV, 0x13, 0, 0, level_code, 0, hysteresis_code)


def handshake_reply(host_nonce, device_nonce):
    return host_nonce + device_nonce


def new_nonce():
    return os.urandom(5)


# --------------------------------------------------------------------------- descriptor

@dataclass
class Level:
    """Per-channel D code of one gain level (command 04) and per-G zero offset in mV."""
    code: tuple            # (CH1 D, CH2 D)
    cal: tuple             # ((CH1 G0, CH1 G1), (CH2 G0, CH2 G1))


@dataclass
class Descriptor:
    version: int
    vendor: str
    model: str
    device_id: int
    base_clock: int
    rates: list
    gain_codes: list
    levels: list = field(default_factory=list)
    raw: bytes = b""


def _cstr(b):
    return b.split(b"\0")[0].decode(errors="replace")


def parse_descriptor(d):
    """Decode the 334-byte handshake descriptor. Offsets verified for version 0x0a (ISDS205X 1.1)."""
    if len(d) < 0xA0 or d[0] != 0x0A:
        raise ValueError(f"unsupported descriptor (version {d[0]:#x}, {len(d)} bytes)")
    base_clock = struct.unpack_from(">I", d, 0x28)[0]
    n_rates = d[0x32]
    dividers = [d[0x34 + 2 * i] for i in range(n_rates)]
    n_gains = d[0x47]
    gains = list(struct.unpack_from(f">{n_gains}H", d, 0x48))
    pos = 0x48 + 2 * n_gains + 2
    n_levels = d[pos]
    pos += 1
    levels = []
    entry = struct.Struct(">H" + "i" * n_gains)
    for _ in range(n_levels):
        per_ch = []
        for _ch in range(2):
            code, *cal = entry.unpack_from(d, pos)
            per_ch.append((code, tuple(cal)))
            pos += entry.size
        levels.append(Level(code=tuple(c for c, _ in per_ch), cal=tuple(c for _, c in per_ch)))
    return Descriptor(
        version=d[0], vendor=_cstr(d[1:17]), model=_cstr(d[17:33]),
        device_id=int.from_bytes(d[-8:], "little"), base_clock=base_clock,
        rates=[base_clock // div for div in dividers], gain_codes=gains, levels=levels, raw=bytes(d),
    )


# --------------------------------------------------------------------------- calibration

@dataclass(frozen=True)
class Stage:
    """Hardware gain stage: index into the G codes + level index."""
    gain: int
    level: int


class Calibration:
    def __init__(self, desc):
        self.desc = desc

    def codes(self, stage, ch):
        """(G, D) codes sent with commands 06 and 04. ch: 1 or 2."""
        return self.desc.gain_codes[stage.gain], self.desc.levels[stage.level].code[ch - 1]

    def lsb_mv(self, stage, ch):
        g, d = self.codes(stage, ch)
        return FULL_SCALE_MV * g / (ADC_STEPS * d)

    def offset_mv(self, stage, ch):
        return self.desc.levels[stage.level].cal[ch - 1][stage.gain]

    def zero(self, stage, ch):
        """Fractional raw code that corresponds to 0 V."""
        return ADC_MID - self.offset_mv(stage, ch) / self.lsb_mv(stage, ch)

    def to_mv(self, raw, stage, ch):
        lsb, off = self.lsb_mv(stage, ch), self.offset_mv(stage, ch)
        return [lsb * (r - ADC_MID) + off for r in raw]

    def span_mv(self, stage, ch):
        lsb, off = self.lsb_mv(stage, ch), self.offset_mv(stage, ch)
        return lsb * (0 - ADC_MID) + off, lsb * (255 - ADC_MID) + off

    def stages(self):
        """Usable stages, finest resolution (smallest lsb) first.

        vdso never combines G1 (low gain) with the top level (062c); that combination is unverified.
        """
        top = max(range(len(self.desc.levels)), key=lambda i: self.desc.levels[i].code[0])
        out = [Stage(g, l) for g in range(len(self.desc.gain_codes)) for l in range(len(self.desc.levels))
               if not (g > 0 and l == top)]
        return sorted(out, key=lambda s: self.lsb_mv(s, 1))

    def select(self, ch, min_mv, max_mv, margin_codes=2):
        """Finest stage that covers the requested range with a margin; otherwise the widest stage."""
        candidates = self.stages()
        for stage in candidates:
            lo, hi = self.span_mv(stage, ch)
            pad = margin_codes * self.lsb_mv(stage, ch)
            if lo + pad <= min_mv and max_mv <= hi - pad:
                return stage
        return candidates[-1]

    def trigger_codes(self, stage, ch, level_mv, min_mv, max_mv, sense_div=0.4):
        """(level code, hysteresis code) for command 13."""
        lsb = self.lsb_mv(stage, ch)
        level = int(self.zero(stage, ch) + level_mv / lsb)
        hysteresis = round(sense_div * (max_mv - min_mv) / 10 / lsb)
        return max(0, min(255, level)), max(0, min(255, hysteresis))
