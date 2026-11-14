# SPDX-License-Identifier: GPL-3.0-or-later
"""Reads a USBPcap capture of the ISDS205X (from tools/usb_capture.ps1) without Wireshark.

    python tools/usbpcap_read.py capture.pcap                 # commands and replies, sample reads folded
    python tools/usbpcap_read.py capture.pcap --reads         # every read on EP 0x82 too
    python tools/usbpcap_read.py capture.pcap --dump DIR      # sample data of each capture command to DIR/NNN.bin

Only bulk transfers are shown; heartbeats (00 00 fe / 00 ff fe) are counted, not printed.
"""
import argparse
import os
import struct
import sys

LINKTYPE_USBPCAP = 249


def packets(path):
    """Yields (time, irp, status, endpoint, transfer, completion, length, data) for each packet.

    length is the transfer's real length; data can be shorter when the capture's snaplen cut it (USBPcapCMD's default
    snaplen is 65535, so a longer read keeps only its first 65508 bytes)."""
    with open(path, 'rb') as f:
        head = f.read(24)
        magic = struct.unpack('<I', head[:4])[0]
        if magic not in (0xa1b2c3d4, 0xa1b23c4d):
            sys.exit(f'{path}: not a little-endian pcap file')
        nano = magic == 0xa1b23c4d
        if struct.unpack('<I', head[20:24])[0] != LINKTYPE_USBPCAP:
            sys.exit(f'{path}: not a USBPcap capture')
        while True:
            rec = f.read(16)
            if len(rec) < 16:
                return
            sec, frac, incl, _ = struct.unpack('<IIII', rec)
            body = f.read(incl)
            t = sec + frac / (1e9 if nano else 1e6)
            hlen, irp, status, _func, info, _bus, _dev, ep, transfer, dlen = struct.unpack('<HQIHBHHBBI', body[:27])
            yield t, irp, status, ep, transfer, bool(info & 1), dlen, body[hlen:hlen + dlen]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('pcap')
    ap.add_argument('--reads', action='store_true', help='print every completed read on EP 0x82')
    ap.add_argument('--dump', metavar='DIR', help='write the sample data after each capture command to DIR')
    args = ap.parse_args()

    t0 = None
    heartbeats = 0
    pending = {}            # irp -> (time, endpoint, length requested) for reads submitted on 0x82
    run = None              # [first time, last time, reads, bytes, sizes] of consecutive 0x82 completions
    dump_index = 0
    dump_file = None

    def flush_run():
        nonlocal run
        if run and not args.reads:
            sizes = run[4]
            shown = ', '.join(f'{n}×{s}' for s, n in sorted(sizes.items()))
            print(f'{run[0]:10.6f}  ...   0x82 IN   {run[2]} reads, {run[3]} B in {1e3 * (run[1] - run[0]):.3f} ms ({shown})')
        run = None

    if args.dump:
        os.makedirs(args.dump, exist_ok=True)

    truncated = 0
    for t, irp, status, ep, transfer, done, length, data in packets(args.pcap):
        if transfer != 3:           # bulk only
            continue
        if t0 is None:
            t0 = t
        rel = t - t0
        if ep == 0x82:
            if not done:
                pending[irp] = rel
                continue
            if status != 0:
                flush_run()
                print(f'{rel:10.6f}  read  0x82 IN   status 0x{status:08x} (submitted {pending.pop(irp, float("nan")):.6f})')
                continue
            pending.pop(irp, None)
            if len(data) < length:
                truncated += 1
            if dump_file:
                dump_file.write(data)
            if args.reads:
                cut = f' (only {len(data)} captured)' if len(data) < length else ''
                print(f'{rel:10.6f}  read  0x82 IN   {length} B{cut}  {data[:16].hex()}')
            else:
                if run is None:
                    run = [rel, rel, 0, 0, {}]
                run[1] = rel
                run[2] += 1
                run[3] += length
                run[4][length] = run[4].get(length, 0) + 1
            continue
        if not data:
            continue
        hexed = data.hex()
        if hexed.startswith('0000fe') or hexed.startswith('00fffe'):
            heartbeats += 1
            continue
        flush_run()
        direction = 'OUT' if ep == 0x01 else 'IN '
        print(f'{rel:10.6f}  bulk  0x{ep:02x} {direction}  {len(data):4} B  {hexed[:96]}{"…" if len(hexed) > 96 else ""}')
        if args.dump and ep == 0x01 and len(data) >= 3 and data[0] == 0x02 and data[2] == 0x02:
            if dump_file:
                dump_file.close()
            dump_index += 1
            dump_file = open(os.path.join(args.dump, f'{dump_index:03}.bin'), 'wb')
    flush_run()
    if dump_file:
        dump_file.close()
    print(f'{heartbeats} heartbeat messages not shown')
    if truncated:
        print(f'{truncated} reads were cut by the snaplen; record with a larger one (USBPcapCMD -s) to keep all data')


if __name__ == '__main__':
    main()
