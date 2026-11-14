# SPDX-License-Identifier: GPL-3.0-or-later
# CPU of the app per thread, and how fast its window answers (the GUI thread), while it runs visibly on Windows.
#
#   tools\app_cpu.ps1 -Settings FOLDER [-AppArgs @("--simulate")] [-Seconds 10] [-Label TEXT]
#
# FOLDER is a settings folder for --settings (FOLDER\StarOscilloscope\StarOscilloscope.ini), so the user's own settings
# are neither read nor written; put the timebase and memory to measure in it. The window comes to the front: keys typed
# meanwhile go to it. The window's answer is the time a WM_NULL message takes, sampled ten times a second.
# Results for one machine: docs/building.md §1.1, Performance.
param([Parameter(Mandatory = $true)][string]$Settings, [string[]]$AppArgs = @("--simulate"), [int]$Seconds = 10,
      [string]$Label = "")

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class AppCpuWin {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
}
"@
$root = Split-Path -Parent $PSScriptRoot
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$p = Start-Process -FilePath "$root\build\windows\app\star-oscilloscope.exe" -ArgumentList ($AppArgs + @("--settings", $Settings)) -PassThru
Start-Sleep -Seconds 6  # the device or simulator opens, the first frames come
$p.Refresh()
$hwnd = $p.MainWindowHandle
$before = @{}
foreach ($t in $p.Threads) { $before[$t.Id] = $t.TotalProcessorTime.TotalMilliseconds }
$cpu0 = $p.TotalProcessorTime.TotalMilliseconds
$clock = [Diagnostics.Stopwatch]::StartNew()
$answers = @()
while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $result = [IntPtr]::Zero
    [void][AppCpuWin]::SendMessageTimeout($hwnd, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 10000, [ref]$result)
    $answers += $sw.Elapsed.TotalMilliseconds
    Start-Sleep -Milliseconds 100
}
$p.Refresh()
$elapsed = $clock.Elapsed.TotalMilliseconds
$cpu1 = $p.TotalProcessorTime.TotalMilliseconds
$threads = foreach ($t in $p.Threads) {
    $from = if ($before.ContainsKey($t.Id)) { $before[$t.Id] } else { 0 }
    100 * ($t.TotalProcessorTime.TotalMilliseconds - $from) / $elapsed
}
$sorted = $answers | Sort-Object
"{0}: app {1:N0} % of one core; busiest threads {2}; window answers in median {3:N1} ms, worst {4:N0} ms" -f $Label,
    (100 * ($cpu1 - $cpu0) / $elapsed),
    (($threads | Sort-Object -Descending | Select-Object -First 3 | ForEach-Object { "{0:N0}%" -f $_ }) -join " "),
    $sorted[[int]($sorted.Count / 2)], $sorted[-1]
$p.CloseMainWindow() | Out-Null
Start-Sleep -Seconds 2
if (!$p.HasExited) { Stop-Process -Id $p.Id -Force }
