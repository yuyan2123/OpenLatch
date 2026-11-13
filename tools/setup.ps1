$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    python -m pip install --target .tools/python ziglang==0.14.1
    if ($LASTEXITCODE -ne 0) { throw 'Native compiler installation failed.' }
    $cli = Join-Path $projectRoot '.tools/arduino-cli/arduino-cli.exe'
    if (-not (Test-Path -LiteralPath $cli)) {
        New-Item -ItemType Directory -Force -Path .tools | Out-Null
        Invoke-WebRequest -Uri 'https://downloads.arduino.cc/arduino-cli/arduino-cli_1.6.0_Windows_64bit.zip' -OutFile .tools/arduino-cli.zip
        Expand-Archive -LiteralPath .tools/arduino-cli.zip -DestinationPath .tools/arduino-cli -Force
    }
    & $cli core update-index --config-file arduino-cli.yaml
    if ($LASTEXITCODE -ne 0) { throw 'Arduino index download failed.' }
    & $cli core install esp32:esp32@3.3.0 --config-file arduino-cli.yaml
    if ($LASTEXITCODE -ne 0) { throw 'ESP32 core installation failed.' }
    & $cli lib install HomeSpan@2.1.8 IRremote@4.4.1 --config-file arduino-cli.yaml
    if ($LASTEXITCODE -ne 0) { throw 'Arduino library installation failed.' }
} finally { Pop-Location }
