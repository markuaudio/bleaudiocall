# bleaudiocall: LyraT Bluetooth speaker jo phone call bhi uthata hai

**English summary (for everyone else; the rest of this repo is written in Hinglish):**
Firmware for the **ESP32-LyraT V4.3** audio board, written directly on **ESP-IDF v6.1** (no ESP-ADF pipeline).
It turns the board into a Bluetooth speaker that also answers phone calls:

- **Music**: A2DP sink (SBC) + AVRCP play/pause/next and absolute volume.
- **Calls**: HFP hands-free unit, voice over HCI (CVSD 8 kHz / mSBC 16 kHz), ring, answer, hang-up, both onboard microphones back to the phone, I2S full duplex into the ES8388 codec.
- **Any phone**: Secure Simple Pairing (Just Works) for modern phones, fixed PIN 0000 for old ones, a link manager that opens A2DP/HFP itself and reconnects with a bounded retry loop.
- One audio task owns the codec/I2S bus and switches between music, call and ring sources.

What you will find here: the full source (`main/`), build/flash/monitor scripts for Windows (`tools/`), a step-by-step build guide, a learning log with every mistake and fix from the first "hiss only" bring-up to the LOUT2 amplifier-cancellation root cause, a code audit with the bugs found in serial logs, and the official Espressif schematic, component layout and a BOM extracted from it (`docs/hardware/`). Docs are Hinglish (Hindi in Latin script + English); the code comments, register tables and log lines are English, so the firmware itself is readable without Hindi.

Status: final release build flashed and verified on the board (reconnect logic, HFP service-level connection). Music playback and a live call with a phone are the remaining user tests.

---

Board: ESP32-LyraT v4.3 (ESP32-D0WD-V3, 8 MB flash; wahi board jo `D:\Vscode\esp` ke kanchan speaker project mein hai). Framework: ESP-IDF v6.1 (toolchain `D:\Vscode\esp-tooling`, 2026-10-27 se; path mein space nahi hona chahiye). Chip target: `esp32`. Bluetooth naam: **`blu2`** (`main/board_config.h` mein `BT_DEVICE_NAME`).

Status (2026-10-27 16:05): final build pass, release (-O2), 0 warnings, **board par flashed**; serial log se reconnect fixes proof (docs/05 verification section), ek phone se HFP connect hua. Music/call ka phone test **pending** (dekho [04](docs/04-learning-process-hinglish.md) ka "pending" section).

Kya karta hai:

- **Music**: phone se A2DP stereo (SBC), AVRCP se play/pause, next, phone ka volume slider.
- **Calls**: HFP Hands-Free unit. Incoming call par speaker khud ring karta hai, PLAY pad se call uthao, awaaz speaker se, tumhari awaaz board ke do onboard mics se phone tak. Hang up / reject bhi board se.
- **Har tarah ka phone**: naye phone (Secure Simple Pairing, "Just Works"), purane phone (PIN `0000`), aur jo phone khud profile nahi kholte unke liye speaker khud A2DP/HFP connect karta hai. Detail: [docs/02](docs/02-har-type-bluetooth-connect.md).

## Docs (is kram mein padho)

0. [ESP-IDF setup: toolchain, terminal, build commands](docs/00-esp-idf-setup-hinglish.md)
1. [Project overview, boot flow, file map, test plan](docs/01-project-overview-hinglish.md)
2. [Har type ka Bluetooth connect: purana phone kyun fail hua, ab kya alag hai](docs/02-har-type-bluetooth-connect.md)
3. [Call audio path: SCO -> I2S full duplex -> ES8388, mic wapas](docs/03-call-audio-path.md)
4. [**Learning process**: 25–27 Oct ki poori yatra, galtiyan, seekh](docs/04-learning-process-hinglish.md)
5. [**Final audit**: kya optimized nahi tha, kya fix kiya, kaise verify kiya](docs/05-final-audit-aur-optimization.md)
6. [**Complete build guide**: zero se project tak, koi bhi bana sake (hardware, PC, modules kram mein, test, troubleshooting)](docs/06-complete-build-guide-hinglish.md)
7. [Hardware mapping (LyraT v4.3)](docs/lyrat-v4.3-hardware.md)
8. [**Schematic, layout, BOM** (official Espressif PDFs + rendered pages + parts list)](docs/hardware/README.md), [shopping list + board BOM](docs/hardware/BOM-hinglish.md)

Saare changes ek jagah: [CHANGELOG.md](CHANGELOG.md). Build logs aur serial logs `docs/` mein date ke saath.

## Folder layout

```
main/            firmware source (8 modules, dekho docs/01)
sdkconfig.defaults  project ki saari non-default settings; sdkconfig isse generate hota hai
partitions.csv   nvs + phy + 3 MB factory app
tools/           Build.ps1, Flash.ps1, Monitor.ps1, Enter-EspIdf.ps1, live_serial.py, capture_serial.py
docs/            learning notes, audit, build guide, build/serial logs, ES8388 reference driver
docs/hardware/   official schematic + component layout (PDF + PNG), BOM
build/           generated (gitignored)
```

## Build / flash / monitor

VS Code mein yeh folder kholo (`D:\Vscode\bleaudiocall`). Naya terminal kholne par ESP-IDF v6.1 profile activate hota hai.

- Build: `Ctrl+Shift+B` (task "ESP32: Build bleaudiocall") ya `idf.py build`
- Flash: task "ESP32: Flash bleaudiocall (COM6)" ya `idf.py -p COM6 flash` (live monitor band karke; auto-download jumpers JP23/24/25 lage hon, upload ke baad hata do)
- Live logs: task "ESP32: Live call/music logs (COM6)"; monitor mein ek letter type karke Enter: `a` answer, `h` hangup, `m` redial, `c` connect, `x` disconnect, `v` bonds, `f` forget, `i` status, `t` ring test, `l r z b` tones, `] [ 0` DAC steps.

## Pehli baar pairing (zaroori)

Phone ne pehle "kanchan" ko sirf music speaker ke roop mein yaad rakha hai. Is firmware ka naam `blu2` hai taaki phone fresh service list (HFP ke saath) padhe. Phir bhi phone par purana "kanchan" **Forget device** kar do. Purana phone PIN maange to `0000`. Android mein `blu2` par "Media audio" aur "Phone calls" dono toggles dikhne chahiye.

## Touch pads

| Pad | Call nahi | Incoming call | Call chal rahi |
|---|---|---|---|
| PLAY | play / pause | **answer** | **hang up** |
| SET tap | next track | - | - |
| SET hold 2 s | Bluetooth disconnect | **reject** | hang up |
| VOL+ / VOL- | music volume (phone sync) | - | call volume 0..15 (phone sync) |

LED: steady = phone connected, slow blink = waiting / discoverable.

## Reconnect behaviour (final)

Boot ke 3 s baad bonded phone ko khud connect karta hai; fail ho to har 10 s, kul 6 baar, phir chup (phone khud aaye, ya console `c`, ya reboot). Phone link tode to 5 s baad wahi cycle. SET hold / `x` se user disconnect kare to reboot ya `c` tak koi auto-reconnect nahi.
