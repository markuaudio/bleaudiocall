# Build the client release package from the current build output.
# Usage (ESP-IDF environment active, after tools/Build.ps1):
#   powershell -ExecutionPolicy Bypass -File tools/Make-Release.ps1 -Version 1.0.0
param([Parameter(Mandatory = $true)][string]$Version)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$out = Join-Path $root "release\v$Version"
New-Item -ItemType Directory -Force $out | Out-Null
$full = Join-Path $out "blu2-v$Version-full-0x0.bin"
$toolsDir = if ($env:IDF_TOOLS_PATH) { $env:IDF_TOOLS_PATH } else { 'C:\Espressif\tools' }
$python = Join-Path $toolsDir 'python\v6.1\venv\Scripts\python.exe'
& $python -m esptool --chip esp32 merge-bin -o $full --flash-mode dio --flash-freq 40m --flash-size 8MB `
    0x1000 (Join-Path $build 'bootloader\bootloader.bin') `
    0x8000 (Join-Path $build 'partition_table\partition-table.bin') `
    0x10000 (Join-Path $build 'bleaudiocall.bin')
if ($LASTEXITCODE -ne 0) { throw 'merge-bin failed' }
Copy-Item (Join-Path $build 'bootloader\bootloader.bin') $out
Copy-Item (Join-Path $build 'partition_table\partition-table.bin') $out
Copy-Item (Join-Path $build 'bleaudiocall.bin') $out
$sums = Get-ChildItem $out -Filter *.bin | ForEach-Object { (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower() + '  ' + $_.Name }
Set-Content -Path (Join-Path $out 'SHA256SUMS.txt') -Value $sums -Encoding ascii
& $python -m esptool image-info (Join-Path $build 'bleaudiocall.bin') | Select-String -Pattern 'Project name|App version|Compile time|ELF file SHA256|ESP-IDF'
Write-Host "Release package written to $out"
Get-ChildItem $out | Format-Table Name, Length -AutoSize
