# Run from the VS Code PowerShell terminal:
# . .\tools\Enter-EspIdf.ps1
# Dot-sourcing keeps the activated environment in the current terminal.
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$activationScript = 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
if (-not (Test-Path -LiteralPath $activationScript)) {
    throw 'ESP-IDF activation script missing. See docs/00-esp-idf-setup-hinglish.md.'
}
. $activationScript
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw 'ESP-IDF activation did not expose idf.py. Check installation logs.'
}
