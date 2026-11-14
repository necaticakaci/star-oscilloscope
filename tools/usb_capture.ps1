# SPDX-License-Identifier: GPL-3.0-or-later
# Records the USB traffic of the ISDS205X alone with USBPcap, on Windows, for the reverse engineering in docs/building.md.
#
#   powershell -ExecutionPolicy Bypass -File tools\usb_capture.ps1 [-Out file.pcap]
#
# Run it in a PowerShell started as administrator: USBPcap lets only administrators capture. After installing USBPcap
# the computer must be restarted once, so that its filter sits on the USB root hubs.
#
# Only the oscilloscope is recorded, by its address on its root hub: a keyboard on the same hub stays out of the file.
# Press Enter to stop. Open the file with Wireshark or read it with tshark.
param(
    [string]$Out = (Join-Path $env:USERPROFILE "Desktop\isds_usb.pcap")
)

$usbpcap = 'C:\Program Files\USBPcap\USBPcapCMD.exe'
if (-not (Test-Path $usbpcap)) {
    Write-Host "USBPcap is not installed ($usbpcap)."
    exit 1
}
if (Get-Process USBPcapCMD -ErrorAction SilentlyContinue) {
    Write-Host "A USBPcap capture is already running. Stop it first: Stop-Process -Name USBPcapCMD"
    exit 1
}

for ($i = 1; $i -le 16; $i++) {
    $hub = "\\.\USBPcap$i"
    # USBPcapCMD writes the device list to the error stream.
    $config = & $usbpcap --extcap-config --extcap-interface $hub 2>&1 | ForEach-Object { "$_" }
    if (-not $config) {
        continue
    }
    # Lines like: value {arg=99}{value=3}{display=[3] YiXingDianZi USB DSO(v5) Driver}{enabled=true}{parent=1}
    foreach ($line in $config) {
        if ($line -match '\{value=(\d+)\}\{display=\[\d+\] ([^}]*(YiXing|DSO|D4A2)[^}]*)\}') {
            $address = $Matches[1]
            Write-Host "Found '$($Matches[2])' on $hub at address $address."
            # USBPcapCMD hands the capture to a detached process of its own and returns at once, so Ctrl+C cannot
            # stop it. This script stops it instead.
            & $usbpcap -d $hub --devices $address --inject-descriptors -b 16777216 -o $Out
            Start-Sleep -Seconds 1
            if (-not (Get-Process USBPcapCMD -ErrorAction SilentlyContinue)) {
                Write-Host "USBPcap did not start recording. Is this PowerShell running as administrator?"
                exit 1
            }
            Write-Host "Recording to $Out. Press Enter to stop."
            [void](Read-Host)
            Stop-Process -Name USBPcapCMD
            Write-Host "Stopped. $((Get-Item $Out).Length) bytes in $Out."
            exit 0
        }
    }
}
Write-Host "The ISDS205X was not found on any USBPcap root hub."
Write-Host "Is this PowerShell running as administrator, was the computer restarted after installing USBPcap, and is the device connected?"
exit 1
