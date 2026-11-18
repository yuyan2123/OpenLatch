param([switch]$TestNfc)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = $projectRoot
$driveAlias = $null
if ($env:OS -eq 'Windows_NT') {
    foreach ($letter in 'Z','Y','X','W','V','U','T','S','R','Q','P') {
        $candidateDrive = "${letter}:"
        if (Test-Path -LiteralPath "${candidateDrive}\") { continue }
        & subst $candidateDrive $projectRoot
        if ($LASTEXITCODE -eq 0) { $driveAlias = $candidateDrive; $buildRoot = "${candidateDrive}\"; break }
    }
    if (-not $driveAlias) { throw 'No free temporary drive alias for the Windows compiler paths.' }
}
Push-Location $buildRoot
try {
    & python tools/build_setup_page.py
    if ($LASTEXITCODE -ne 0) { throw 'Setup page generation failed.' }
    if ($TestNfc) { & python tools/prepare_homekey.py --test-profile }
    else { & python tools/prepare_homekey.py }
    if ($LASTEXITCODE -ne 0) { throw 'HomeKey source preparation failed.' }
    # Compilation only; deliberately no board discovery, serial port or upload.
    $homeKeyFlags = "-DCONFIG_IDF_CMAKE=1 -DLOG_LOCAL_LEVEL=ESP_LOG_NONE -I${buildRoot}.tools/homekey-support"
    $outputPath = 'build/esp32c3'
    if ($TestNfc) {
        $homeKeyFlags += " -I${buildRoot}.tools/compile-fixture -DDOOR_TEST_NFC_BUILD=1"
        $outputPath = 'build/esp32c3-nfc-test'
    }
    & ./.tools/arduino-cli/arduino-cli.exe compile --config-file arduino-cli.yaml `
        --fqbn 'esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs,FlashSize=4M' `
        --libraries .tools/vendor-libraries `
        --build-property "compiler.cpp.extra_flags=$homeKeyFlags" `
        --build-path $outputPath firmware/OpenLatch
    if ($LASTEXITCODE -ne 0) { throw 'Firmware compilation failed.' }
    if ($TestNfc) { & python tools/verify_ota_image.py $outputPath --test-profile }
    else { & python tools/verify_ota_image.py $outputPath }
    if ($LASTEXITCODE -ne 0) { throw 'OTA image verification failed.' }
} finally {
    Pop-Location
    if ($driveAlias) { & subst $driveAlias /D }
}
