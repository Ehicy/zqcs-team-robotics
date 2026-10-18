# ESP32-S3 application update only. Never invokes STM32 tools or erase_flash.
# Before an actual upload, disconnect servo power and support the mechanism.
[CmdletBinding()]
param(
    [string]$Port,
    [string]$Python,
    [string]$Esptool,
    [string]$Firmware,
    [ValidateRange(115200, 921600)][int]$Baud = 921600,
    [switch]$PlanOnly
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$workspace = Split-Path (Split-Path $repo -Parent) -Parent
if (!$Python) { $Python = Join-Path $workspace 'tools/ps2-build-venv/Scripts/python.exe' }
if (!$Esptool) { $Esptool = Join-Path $workspace 'tools/platformio-ps2-core/packages/tool-esptoolpy/esptool.py' }
if (!$Firmware) { $Firmware = Join-Path $repo 'output/servo-slew-20261018/esp32/firmware.bin' }
foreach ($file in @($Python, $Esptool, $Firmware)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "File not found: $file" }
}
$Firmware = (Resolve-Path -LiteralPath $Firmware).Path
$env:PYTHONUTF8 = '1'
# Vendor image parsing only: does not open a serial port, reset or flash a board.
$info = & $Python $Esptool --chip esp32s3 image_info $Firmware 2>&1
if ($LASTEXITCODE -ne 0) { throw "Invalid ESP32-S3 image: $($info -join [Environment]::NewLine)" }
Write-Host "Image ready: $Firmware"
if (!$Port) {
    # Match the previously identified ESP32's USB serial, never Bluetooth COMs.
    $detect = @'
from serial.tools import list_ports
matches = [p.device for p in list_ports.comports()
           if (p.serial_number or '').replace(':','').replace('-','').upper() == 'C04E302BD9A4']
print(matches[0] if len(matches) == 1 else '')
'@
    $Port = (& $Python -c $detect).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'USB port discovery failed' }
}
Write-Host 'Target: ESP32-S3 APP only, address 0x10000; STM32 is untouched.'
if ($PlanOnly) {
    $displayPort = if ($Port) { $Port } else { '<ESP32 USB not connected>' }
    Write-Host "Prepared: --chip esp32s3 --port $displayPort --baud $Baud write_flash 0x10000 firmware.bin"
    Write-Host 'PLAN ONLY: no device was opened, reset or written.'
    exit 0
}
if (!$Port) { throw 'ESP32 USB not detected. Connect the ESP32 data USB cable, or pass -Port COMn for the intended board.' }
if ($Port -notmatch '^COM[1-9][0-9]*$') { throw 'Expected a Windows serial port such as COM7' }
& $Python $Esptool --chip esp32s3 --port $Port --baud $Baud write_flash 0x10000 $Firmware
if ($LASTEXITCODE -ne 0) { throw 'ESP32 application upload failed; inspect the esptool error above.' }
Write-Host 'ESP32 APP written. Check INFO for crtc-lower-servo-slew-v4 before reconnecting servo power.'
