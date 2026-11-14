#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# CPU use of the app: runs it off-screen for a while and prints its share of one core.
#
#   tools/app_cpu.sh [SECONDS] [app arguments, e.g. --simulate]
#
# Uses a temporary settings directory, so the user's own settings are neither read nor written.
# Off-screen means no drawing cost on the display side (X server, WSLg); that comes on top.
set -euo pipefail

secs=${1:-10}
shift || true
root=$(cd "$(dirname "$0")/.." && pwd)
app=${STAROSC_APP:-$root/build/linux/app/star-oscilloscope}

config=$(mktemp -d)
trap 'rm -rf "$config"' EXIT
XDG_CONFIG_HOME=$config QT_QPA_PLATFORM=offscreen "$app" "$@" >/dev/null 2>&1 &
pid=$!
sleep 4  # opening the device and the first frames

ticks() { awk '{print $14 + $15}' "/proc/$pid/stat"; }
t1=$(ticks)
sleep "$secs"
t2=$(ticks)
kill "$pid"
wait "$pid" 2>/dev/null || true

awk -v t="$((t2 - t1))" -v hz="$(getconf CLK_TCK)" -v s="$secs" \
    'BEGIN { printf "app CPU: %.0f%% of one core over %d s\n", 100 * t / hz / s, s }'
