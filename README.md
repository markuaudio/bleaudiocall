# bleaudiocall: a LyraT Bluetooth speaker that also answers phone calls

Firmware for the **ESP32-LyraT V4.3** audio board, written directly on **ESP-IDF v6.1** (no ESP-ADF pipeline). It turns the board into a Bluetooth speaker that advertises as **`blu2`** and also takes phone calls:

- **Music**: A2DP sink (SBC) + AVRCP play/pause/next and absolute volume.
- **Calls**: HFP hands-free unit, voice over HCI (CVSD 8 kHz / mSBC 16 kHz), ring, answer, hang-up, both onboard microphones back to the phone, I2S full duplex into the ES8388 codec.
- **Any phone**: Secure Simple Pairing (Just Works) for modern phones, fixed PIN 0000 for old ones, a link manager that opens A2DP/HFP itself and reconnects with a bounded retry loop.
- One audio task owns the codec/I2S bus and switches between music, call and ring sources.

What you will find here: the full source (`main/`), build/flash/monitor scripts for Windows (`tools/`), a step-by-step build guide, a learning log with every mistake and fix from the first "hiss only" bring-up to the LOUT2 amplifier-cancellation root cause, a code audit with the bugs found in serial logs, and the official Espressif schematic, component layout and a BOM extracted from it (`docs/hardware/`).

Board: ESP32-LyraT V4.3 (ESP32-D0WD-V3, 8 MB flash; the same board used by the earlier kanchan speaker project, which is not part of this repo). Framework: ESP-IDF v6.1 installed by the Espressif Installation Manager (EIM) into `<IDF folder>`; ESP-IDF on Windows does not support paths containing spaces. Chip target: `esp32`. Bluetooth name: **`blu2`** (`BT_DEVICE_NAME` in `main/board_config.h`).

**Status (2026-10-27):** final release build (-O2, 0 warnings) flashed and verified on the board: the serial log proves the reconnect logic (no audio reclock on failed attempts; the retry loop now runs for ever, see below) and an HFP service-level connection with a real phone (see the verification section of [docs/05](docs/05-final-audit-and-optimization.md)). Music playback and a live call remain user tests (see the "pending" section of [docs/04](docs/04-learning-process.md)).

What it does:

- **Music**: A2DP stereo (SBC) from the phone; play/pause, next and the phone's volume slider over AVRCP.
- **Calls**: HFP hands-free unit. On an incoming call the speaker rings by itself, the PLAY pad answers, the caller is heard on the speaker and your voice goes to the phone through the board's two onboard microphones. Hang up / reject from the board as well.
- **Every kind of phone**: new phones (Secure Simple Pairing, "Just Works"), old phones (PIN `0000`), and for phones that do not open the profiles themselves the speaker connects A2DP/HFP on its own. Details: [docs/02](docs/02-connect-every-phone.md).

## Documentation (read in this order)

0. [ESP-IDF setup: toolchain, terminal, build commands](docs/00-esp-idf-setup.md)
1. [Project overview, boot flow, file map, test plan](docs/01-project-overview.md)
2. [Connecting every kind of phone: why the old phone failed, what is different now](docs/02-connect-every-phone.md)
3. [Call audio path: SCO -> I2S full duplex -> ES8388, microphones back](docs/03-call-audio-path.md)
4. [**Learning process**: the whole journey from 25 to 27 October, mistakes and lessons](docs/04-learning-process.md)
5. [**Final audit**: what was not optimized, what was fixed, how it was verified](docs/05-final-audit-and-optimization.md)
6. [**Complete build guide**: from zero to a working project so anyone can build it (hardware, PC, modules in order, tests, troubleshooting)](docs/06-complete-build-guide.md)
7. [Hardware mapping (LyraT V4.3)](docs/lyrat-v4.3-hardware.md)
8. [**Schematic, layout, BOM** (official Espressif PDFs + rendered pages + parts list)](docs/hardware/README.md), [shopping list + board BOM](docs/hardware/BOM.md)
9. [**Reconnect and power-trip fixes**: why the speaker did not auto-connect, why the link dropped at high volume, what changed and where, verification](docs/07-reconnect-and-power-trip-fixes.md)

All changes in one place: [CHANGELOG.md](CHANGELOG.md). Build logs and serial logs live in `docs/` with their dates.

## Folder layout

```
main/            firmware source (8 modules, see docs/01)
sdkconfig.defaults  every non-default project setting; sdkconfig is generated from it
partitions.csv   nvs + phy + 3 MB factory app
tools/           Build.ps1, Flash.ps1, Monitor.ps1, Enter-EspIdf.ps1, live_serial.py, capture_serial.py
docs/            learning notes, audit, build guide, build/serial logs, ES8388 reference driver
docs/hardware/   official schematic + component layout (PDF + PNG), BOM
build/           generated (gitignored)
```

## Build / flash / monitor

Open the project folder in VS Code. Opening a new terminal activates the ESP-IDF v6.1 profile (`tools/Enter-EspIdf.ps1`, which sources the EIM activation script in `C:\Espressif\tools`). `.vscode/settings.json` no longer pins an IDF path: after opening the folder, pick the installed v6.1 setup once via the ESP-IDF extension status bar ("ESP-IDF: Select Current ESP-IDF Version").

The serial port is COM6 in our setup; change it in `.vscode/settings.json`, `tools/Flash.ps1` and `tools/Monitor.ps1`.

- Build: `Ctrl+Shift+B` (task "ESP32: Build bleaudiocall") or `idf.py build`
- Flash: task "ESP32: Flash bleaudiocall (COM6)" or `idf.py -p COM6 flash` (close the live monitor first; the auto-download jumpers JP23/JP24/JP25 must be fitted, remove them after the upload)
- Live logs: task "ESP32: Live call/music logs (COM6)"; in the monitor type one letter and Enter: `a` answer, `h` hang up, `m` redial, `c` connect, `x` disconnect, `v` bonds, `f` forget, `i` status, `t` ring test, `l r z b` tones, `] [ 0` DAC steps.

## First pairing (required)

Your phone may still have the old device name "kanchan" cached as a music-only speaker. This firmware advertises as `blu2` so the phone reads a fresh service list (including HFP). Still, **Forget device** for the old "kanchan" entry on the phone. If an old phone asks for a PIN, enter `0000`. On Android, `blu2` should show both the "Media audio" and "Phone calls" toggles.

## Touch pads

| Pad | No call | Incoming call | Call in progress |
|---|---|---|---|
| PLAY | play / pause | **answer** | **hang up** |
| SET tap | next track | - | - |
| SET hold 2 s | Bluetooth disconnect | **reject** | hang up |
| VOL+ / VOL- | music volume (synced with the phone) | - | call volume 0..15 (synced with the phone) |

LED: steady = phone connected, slow blink = waiting. The speaker is **always visible** in a phone's Bluetooth list, even while another phone is connected; the newest phone that connects takes over (the previous one is disconnected). SET hold while nothing is connected = pairing mode (the speaker stops its own reconnect attempts until `c` or reboot).

## Reconnect behaviour (final)

The speaker never gives up on its own. 3 s after boot it connects to the **last connected phone** (remembered in NVS); if that fails it retries every 10 s for 4 attempts, then every 45 s for ever (short 5 s page timeout, so the radio stays free for new phones), alternating between the last connected phone and the other bonded phones, until a phone answers. If the phone drops the link, the same cycle starts after 5 s. Only a user disconnect (SET hold / `x`) stops it, until a reboot or `c`.

**Power-trip protection.** A disconnect "at high volume" is almost always the board resetting because the 5 V supply sags under the amplifier load (the IMP809 supervisor resets the ESP32 at 2.63 V on the 3.3 V rail; the brownout detector too). The firmware detects a reset that happened while the amplifier was on (a flag in RTC memory survives the reset), lowers the DAC profile by one step (1.5 dB), saves it in NVS and logs `POWER TRIP`. Each trip lowers it once more. On top of that a **supply guard** measures the 3.3 V rail live through the REC button line (GPIO36, pulled up to the rail; the ADC has its own reference): when the rail starts to sag under amplifier load, the output is ducked within milliseconds (down to -12 dB, recovering over 3 s), and if that happens four times in 30 s the DAC profile is lowered one step and saved **while playing**, with no reset and no disconnect. The speaker therefore finds the loudest level the supply holds by itself. Console `0` clears the learned limit. Details: [docs/07](docs/07-reconnect-and-power-trip-fixes.md). Use a 5 V / 2 A adapter with a short, thick cable; a weak adapter or thin cable trips at a lower level.
