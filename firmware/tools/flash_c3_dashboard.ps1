# LuatOS ESP32-C3, classic CH343 board. No STM32 operations or eFuse changes.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Port,
    [switch]$PlanOnly
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$workspace=Split-Path (Split-Path $repo -Parent) -Parent
$python=Join-Path $workspace 'tools/ps2-build-venv/Scripts/python.exe'
$esptool=Join-Path $workspace 'tools/platformio-ps2-core/packages/tool-esptoolpy/esptool.py'
$bootApp=Join-Path $workspace 'tools/platformio-ps2-core/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
$image=Join-Path $repo 'output/c3-dashboard-20261022/esp32'
if($Port -notmatch '^COM[1-9][0-9]*$'){throw 'Pass the C3 USB serial port, such as COM9.'}
$files=@((Join-Path $image 'bootloader.bin'),(Join-Path $image 'partitions.bin'),$bootApp,(Join-Path $image 'firmware.bin'))
foreach($file in @($python,$esptool)+$files){if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing file: $file"}}
$arguments=@($esptool,'--chip','esp32c3','--port',$Port,'--baud','460800','--before','default_reset','--after','hard_reset','write_flash','--flash_mode','dio','--flash_freq','80m','--flash_size','4MB','0x0',$files[0],'0x8000',$files[1],'0xe000',$files[2],'0x10000',$files[3])
if($PlanOnly){Write-Output "Ready: ESP32-C3 on $Port; bootloader 0x0, partitions 0x8000, OTA initializer 0xe000, app 0x10000. No device opened.";exit 0}
& $python @arguments
if($LASTEXITCODE -ne 0){throw 'C3 upload failed; inspect esptool output.'}
Write-Output 'C3 written. Read INFO: crtc-c3-direct-pwm-v2. STM32 unchanged.'
