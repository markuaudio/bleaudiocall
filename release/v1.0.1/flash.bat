@echo off
rem Flash blu2 firmware v1.0.1 onto an ESP32-LyraT V4.3.  Usage: flash.bat COM6
rem Needs Python 3 and esptool (pip install esptool). Board must be in download mode.
set PORT=%1
if "%PORT%"=="" set PORT=COM6
cd /d "%~dp0"
python -m esptool --chip esp32 --port %PORT% --baud 460800 erase-flash || goto :fail
python -m esptool --chip esp32 --port %PORT% --baud 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-freq 40m --flash-size 8MB 0x0 blu2-v1.0.1-full-0x0.bin || goto :fail
echo Done. Remove the JP23/JP24/JP25 caps if used, then press RST.
exit /b 0
:fail
echo Flashing failed. Check the COM port and download mode (JP23/24/25 caps or Boot+RST).
exit /b 1
