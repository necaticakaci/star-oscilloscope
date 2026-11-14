#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# CPU of the app per thread, off-screen with the simulator, at a timebase, memory depth and screen scale.
#
#   tools/app_cpu_threads.sh SECONDS_PER_DIV [MEMORY 0|1|2] [SCALE, e.g. 1.25] [SECONDS]
#
# SCALE stands for the user's display scaling (QT_SCALE_FACTOR): drawing costs at 125 % were not seen at 100 %
# (docs/building.md §1.1, Performance). "star-oscillosco" is the GUI thread, "QThread" the acquisition. The simulator makes
# its signals itself, which costs the acquisition thread far more than the device's samples do at slow timebases.
set -uo pipefail

tdiv=$1
mem=${2:-0}
scale=${3:-1}
secs=${4:-8}
root=$(cd "$(dirname "$0")/.." && pwd)
app=${STAROSC_APP:-$root/build/linux/app/star-oscilloscope}

config=$(mktemp -d)
trap 'rm -rf "$config"' EXIT
mkdir -p "$config/StarOscilloscope"
printf '[scope]\ntimebase\\secondsPerDiv=%s\ntimebase\\memory=%s\n' "$tdiv" "$mem" > "$config/StarOscilloscope/StarOscilloscope.conf"
QT_SCALE_FACTOR=$scale XDG_CONFIG_HOME=$config QT_QPA_PLATFORM=offscreen "$app" --simulate >/dev/null 2>&1 &
pid=$!
sleep 5

declare -A before
for t in /proc/$pid/task/*; do before[$(basename "$t")]=$(awk '{print $14 + $15}' "$t/stat"); done
sleep "$secs"
hz=$(getconf CLK_TCK)
line=""
for t in /proc/$pid/task/*; do
    id=$(basename "$t")
    now=$(awk '{print $14 + $15}' "$t/stat")
    used=$(( now - ${before[$id]:-0} ))
    [ "$used" -gt 0 ] && line="$line $(cat "$t/comm"):$(( used * 100 / (hz * secs) ))%"
done
kill "$pid"
wait "$pid" 2>/dev/null
echo "$tdiv s/div, memory $mem, scale $scale:$line"
