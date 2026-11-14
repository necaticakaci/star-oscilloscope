#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds usbsniff and runs the vendor-library scenario under it.
# Usage: tools/trace_vdso.sh [output dir] [scenario]   (default: trace/<date-time>, basic)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$root/trace/$(date +%Y%m%d-%H%M%S)}"
scenario="${2:-basic}"
mkdir -p "$out/dumps" "$root/build"
gcc -shared -fPIC -O2 -Wall -Wextra -o "$root/build/libusbsniff.so" "$root/tools/usbsniff/usbsniff.c" -ldl -lpthread
USBSNIFF_LOG="$out/usb.log" USBSNIFF_DIR="$out/dumps" LD_PRELOAD="$root/build/libusbsniff.so" \
    python3 "$root/tools/trace_vdso.py" "$out" "$scenario"
echo "log: $out/usb.log"
