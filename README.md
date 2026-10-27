# bleaudiocall: LyraT Bluetooth speaker jo phone call bhi uthata hai

Board: ESP32-LyraT v4.3 (ESP32-D0WD-V3, 8 MB flash; wahi board jo `D:\Vscode\esp` ke kanchan speaker project mein hai). Framework: ESP-IDF v6.1 (shared tooling `D:\Vscode\esp\tooling`). Chip target: `esp32`. Bluetooth naam: **`blu2`** (`main/board_config.h` mein `BT_DEVICE_NAME`).

Status (2026-10-27): final build pass, release (-O2) config, 0 warnings in `main/`. Board par flash + phone test **pending** (dekho [04](docs/04-learning-process-hinglish.md) ka "pending" section).

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
