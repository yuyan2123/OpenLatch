$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    # Compilation only; deliberately no board discovery, serial port or upload.
    & ./.tools/arduino-cli/arduino-cli.exe compile --config-file arduino-cli.yaml `
        --fqbn 'esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app,FlashSize=4M' `
        --build-path build/esp32c3 firmware/DoorRelease
    if ($LASTEXITCODE -ne 0) { throw 'Firmware compilation failed.' }
} finally { Pop-Location }
