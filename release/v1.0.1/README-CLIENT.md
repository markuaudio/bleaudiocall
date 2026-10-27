# blu2 firmware v1.0.1 for ESP32-LyraT V4.3: flashing instructions

This package contains only the compiled firmware. No source code is included or required.

## Files

| File | Flash address | What it is |
|---|---|---|
| `blu2-v1.0.1-full-0x0.bin` | `0x0` | **Everything in one file**: bootloader + partition table + application. Use this one for new boards. It also overwrites the settings area, so the speaker comes up as new: paired phones are forgotten and the power self-test runs again at the first boot (a **factory reset**). |
| `bootloader.bin` | `0x1000` | 2nd-stage bootloader (only needed if you flash the three parts separately) |
| `partition-table.bin` | `0x8000` | partition table |
| `bleaudiocall.bin` | `0x10000` | the application. Flashing only this file (or the three parts at their addresses) **keeps** the paired phones and the self-test result: use that for updates of boards already in use. |
| `SHA256SUMS.txt` | | checksums; verify the download before flashing |
| `Flash-Client.ps1` / `flash.bat` | | Windows helper scripts (need Python + esptool, see option B) |

Flash settings for every tool: chip **ESP32**, SPI mode **DIO**, SPI speed **40 MHz**, flash size **8 MB**, baud rate 460800 (use 115200 if the connection is unstable).

## What you need

- ESP32-LyraT V4.3 board, two 4 Ω / 3 W speakers on the LEFT and RIGHT connectors, a 5 V / 2 A adapter on the **USB POWER** port (short, thick cable).
- A PC with the Silicon Labs CP210x USB-UART driver (Windows usually installs it automatically). Connect the PC to the board's **USB-UART** port (the one marked UART). Note the COM port in Device Manager.
- The board in download mode (see next section).

## Putting the board into download mode

Either:

- **Automatic:** put jumper caps on **JP23, JP24 and JP25** (three 2-pin headers next to the Boot button). The flashing tool then resets the board by itself. Espressif recommends removing the caps again after flashing.
- **Manual (no jumpers):** hold the **Boot** button, press and release **RST**, then release **Boot**. The board now waits for the download. Press **RST** again after flashing.

## Option A: Espressif Flash Download Tool (Windows, no command line)

1. Download "Flash Download Tools" from https://www.espressif.com/en/support/download/other-tools and unzip it.
2. Start `flash_download_tool_*.exe`, choose **ChipType: ESP32**, **WorkMode: Develop**, **LoadMode: UART**.
3. In the first row tick the checkbox, click `...`, select `blu2-v1.0.1-full-0x0.bin`, and type `0x0` in the address field.
4. SPI SPEED: **40MHz**, SPI MODE: **DIO**, DoNotChgBin: ticked. COM: your port. BAUD: 460800.
5. For a **new board** click **ERASE** first (clears old settings), wait for FINISH.
6. Click **START**. Wait for **FINISH** (about 20 s). Remove the jumpers (if used), press RST or power-cycle.

## Option B: esptool (command line, Windows / Linux / macOS)

Install once: `pip install esptool` (needs Python 3). Then, with the board in download mode:

```
python -m esptool --chip esp32 --port COM6 --baud 460800 erase-flash
python -m esptool --chip esp32 --port COM6 --baud 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-freq 40m --flash-size 8MB 0x0 blu2-v1.0.1-full-0x0.bin
```

Replace `COM6` with your port (`/dev/ttyUSB0` on Linux). On Windows you can simply run `Flash-Client.ps1` (right-click, Run with PowerShell) or `flash.bat COM6`. Older esptool versions use `erase_flash` / `write_flash` with underscores.

## Option C: in the browser (Chrome / Edge, no install)

Open https://espressif.github.io/esptool-js/ , click **Connect**, choose the board's COM port, baud 460800. Add the file `blu2-v1.0.1-full-0x0.bin` with flash address `0x0`, click **Program**. Use **Erase** first on a new board.

## First power-on after flashing (important)

1. Speakers connected, adapter on the POWER port, power switch ON.
2. The green LED blinks slowly. About 3 s after power-on the speaker plays a low 60 Hz test tone for about 8 seconds, rising in steps. This is the **power self-test**: it measures how much the power supply can deliver and stores the loudest safe level. It runs only once (and again after an erase). If the supply is too weak the board may reset once during the tone; that is expected and the result is still stored.
3. On the phone: Bluetooth > scan > **blu2** > pair (old phones: PIN 0000). The LED turns steady when connected.

## Daily use

| Control | Function |
|---|---|
| PLAY pad | play / pause; answer an incoming call; hang up |
| SET pad | next track; hold 2 s: reject call / disconnect (the speaker then waits for a new phone) |
| VOL+ / VOL- pads | volume (synchronised with the phone) |
| Green LED | steady = phone connected, slow blink = waiting |

The speaker reconnects to the last phone by itself after power-on. It is visible to other phones only while no phone is connected; a phone that was paired before can connect at any time and takes over.

## Troubleshooting

| Problem | Check |
|---|---|
| "Failed to connect" / no serial data while flashing | board not in download mode (jumpers or Boot+RST), wrong COM port, UART cable in the POWER port |
| Flashing works but nothing happens | remove the JP23/24/25 caps, press RST; check the speakers are on LEFT/RIGHT, not on the headphone jack |
| Sound drops out / the board restarts at high volume | use a 5 V / 2 A adapter with a short, thick cable on the POWER port. To make the speaker measure the new supply, run the self-test again: erase + re-flash (Option A ERASE, or `Flash-Client.ps1` without `-NoErase`), or type `0` + Enter in a serial terminal (115200 baud) on the UART port |
| Phone cannot see blu2 | disconnect the phone that is currently connected (hold SET 2 s), or power-cycle the speaker |
