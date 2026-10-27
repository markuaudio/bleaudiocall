$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$Host.UI.RawUI.WindowTitle = 'bleaudiocall - LIVE ESP32 call/music logs (COM6)'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    & 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe' (Join-Path $PSScriptRoot 'live_serial.py') --port COM6
}
finally { Pop-Location }
