# Flash blu2 firmware v1.0.1 onto an ESP32-LyraT V4.3 (Windows).
# Needs Python 3 and esptool:  pip install esptool
# Put the board into download mode first (JP23/JP24/JP25 caps, or hold Boot + press RST).
param(
    [string]$Port = 'COM6',
    [int]$Baud = 460800,
    [switch]$NoErase
)
$ErrorActionPreference = 'Stop'
$bin = Join-Path $PSScriptRoot 'blu2-v1.0.1-full-0x0.bin'
if (-not (Test-Path $bin)) { throw "Firmware file not found: $bin" }
$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) { throw 'Python 3 not found. Install it from python.org, then run: pip install esptool' }
python -m esptool version | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'esptool not found. Run: pip install esptool' }

if (-not $NoErase) {
    Write-Host "Erasing flash on $Port (clears old settings; the power self-test will run at the next boot)..."
    python -m esptool --chip esp32 --port $Port --baud $Baud erase-flash
    if ($LASTEXITCODE -ne 0) { throw 'Erase failed. Is the board in download mode and is the port right?' }
}
Write-Host "Writing $bin to $Port at 0x0..."
python -m esptool --chip esp32 --port $Port --baud $Baud --before default-reset --after hard-reset `
    write-flash --flash-mode dio --flash-freq 40m --flash-size 8MB 0x0 $bin
if ($LASTEXITCODE -ne 0) { throw 'Flashing failed.' }
Write-Host 'Done. Remove the JP23/JP24/JP25 caps if you used them, then press RST or power-cycle the board.'
