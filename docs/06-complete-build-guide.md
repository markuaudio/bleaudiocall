# 06: Complete build guide — from zero to bleaudiocall, so anyone can build it

This guide is for people who have only a LyraT V4.3 board, a PC and a phone. The full source is in this folder (`main/`), so there is no code to retype; the guide explains **in what order things were built, why, how each step was tested, and where you will get stuck**. Every section ends with a "Check": move on only when it passes.

Reading order: this file first, then follow the links wherever you need detail.

---

## 0. What you will end up with

Your own firmware on the ESP32-LyraT V4.3 (ESP-IDF v6.1, no ESP-ADF pipeline):

- Bluetooth **music** from the phone (A2DP sink, SBC), play/pause/next/volume (AVRCP).
- The phone's **calls** (HFP hands-free): ring, answer, audio in both directions (onboard mics), hang-up, volume.
- Pairing with **every phone**: new ones (SSP Just Works), old ones (PIN 0000); the speaker reconnects by itself.
- Control from 4 touch pads + a serial console; LED status.

Time: hardware setup 30 min, PC setup 1–2 hours (downloads), build 5 min, first test 15 min. Understanding the code: 1–2 days.

---

## 1. Hardware

Shopping list: [hardware/BOM.md](hardware/BOM.md) section A. Minimum: LyraT V4.3, 2× 4 Ω 3 W speakers (JST PH 2.0), 2× Micro-USB cables, 5 V ≥ 2 A charger, Windows PC, phone.

Schematic + layout + board BOM: [hardware/README.md](hardware/README.md). Pin mapping: [lyrat-v4.3-hardware.md](lyrat-v4.3-hardware.md).

### 1.1 Understand the board (5 minutes, open the schematic)

| Block | Parts | What it means for us |
|---|---|---|
| ESP32-WROVER-E (U6) | CPU + Bluetooth radio | the code runs here; flash 8 MB (measured) |
| ES8388 (U5) | codec: configured over I2C, audio over I2S | DAC → speaker/headphone, ADC ← mics |
| NS4150 ×2 (U11/U12) | class-D PA, IN+ ← LOUT1, IN− ← LOUT2, CTRL ← IO21 | PA enable GPIO21; LOUT2 must stay at -45 dB |
| CP2102N (U1) | USB-UART | COM port, logs, flashing |
| Q9/Q10/Q11 + JP23/24/25 | auto-download | no flashing without the jumper caps |
| J18 DIP ×8 | GPIO12-15 → SD or JTAG | **all OFF** (GPIO13 = Vol- touch is needed) |
| TP3-TP6 | touch: Play 33, Set 32, Vol- 13, Vol+ 27 | `board_config.h` |
| D17 + Q6 | green LED, IO22 | connected/discoverable |

### 1.2 Physical setup (power OFF)

1. DIP switch J18: **1–8 all OFF**.
2. Fit jumper caps on **JP23, JP24, JP25** (the two pins of each header). Needed for flashing.
3. Speakers: left speaker on the LEFT connector J15, right on RIGHT J16. Do **not** tie a speaker − to GND or to the other speaker (bridged outputs).
4. Plug nothing into AUX_IN (IO12 strap, can block boot).
5. Cable 1: PC → **USB-UART** port (J1, marked "UART" on the board). Cable 2: charger/PC → **USB POWER** (J17).
6. Power switch J3 ON. The red power LED (D8) and the green standby LED (D4) light up. The charging LED may blink without a battery; that is normal.

**Check:** Windows Device Manager shows "Silicon Labs CP210x USB to UART Bridge (COMx)". Ours: COM6.

---

## 2. PC software

Full detail: [00-esp-idf-setup.md](00-esp-idf-setup.md). Short version:

1. VS Code + extensions:
   ```powershell
   code --install-extension espressif.esp-idf-extension
   code --install-extension ms-vscode.cpptools
   ```
2. Espressif Installation Manager (EIM) CLI:
   ```powershell
   winget install --id Espressif.EIM-CLI --exact --source winget --accept-source-agreements --accept-package-agreements --silent
   ```
3. ESP-IDF **v6.1**, target **esp32** (the board's chip is an esp32, not an S3):
   ```powershell
   & "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Espressif.EIM-CLI_Microsoft.Winget.Source_8wekyb3d8bbwe\eim.exe" install -i v6.1 -t esp32 -p '<IDF folder>' --idf-features ide --skip-components-download true --do-not-track true
   & "...\eim.exe" select v6.1
   ```
   The IDF checkout goes to <IDF folder> (the ESP-IDF v6.1 checkout installed by EIM; **the path must not contain spaces**, ESP-IDF on Windows does not support them). Lesson from 2026-10-27: the toolchain folder was moved/renamed and the activation script's hard-coded path broke; the fix was moving it to a space-free folder and updating the EIM activation script, `eim_idf.json` and `.vscode/settings.json`. Tools go to `C:\Espressif\tools` (EIM's default; this is the one place where the C drive is required). Python venv `C:\Espressif\tools\python\v6.1\venv`.
4. Open this folder in VS Code. `.vscode/settings.json` sets the terminal profile "ESP-IDF v6.1 (bleaudiocall)", which runs `tools/Enter-EspIdf.ps1`. Open a new terminal:
   ```powershell
   idf.py --version      # ESP-IDF v6.1
   ```
   If EIM installed its tools somewhere other than the default, set the `IDF_TOOLS_PATH` environment variable (read by `tools/Enter-EspIdf.ps1` and `tools/Monitor.ps1`), and pick the installed v6.1 setup once via the ESP-IDF extension status bar ("ESP-IDF: Select Current ESP-IDF Version").

**Check:** `idf.py --version` prints v6.1. Extension status bar shows "ESP-IDF v6.1.0" (if not, click the status bar and select the setup; docs 00, section "InvalidSetup").

---

## 3. Project skeleton (the files that come first)

| File | Job | Content |
|---|---|---|
| `CMakeLists.txt` | loads the IDF build system, project name | 3 lines: `cmake_minimum_required`, `include($ENV{IDF_PATH}/tools/cmake/project.cmake)`, `project(bleaudiocall)` |
| `main/CMakeLists.txt` | source list + component dependencies | `idf_component_register(SRCS ... REQUIRES bt nvs_flash esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_touch_sens esp_timer esp_ringbuf)` |
| `sdkconfig.defaults` | all of the project's non-default settings | section 3.1 below |
| `partitions.csv` | flash layout | nvs 24 KB, phy 4 KB, factory app 3 MB |
| `.vscode/` | terminal profile, tasks (Ctrl+Shift+B), COM port | copy as-is |
| `tools/*.ps1`, `tools/*.py` | build/flash/monitor wrappers, serial capture | copy as-is; change the port if it is not COM6 |
| `.gitignore` | build/, sdkconfig.old, logs | |

Do not write `sdkconfig` by hand: `idf.py build` generates it from `sdkconfig.defaults`. New project = delete `sdkconfig` and build.

### 3.1 Why each group in sdkconfig.defaults

```
CONFIG_IDF_TARGET="esp32"
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y          # measured with esptool flash-id (docs 04)
CONFIG_PARTITION_TABLE_CUSTOM=y / FILENAME="partitions.csv"

CONFIG_BT_ENABLED=y, CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y, CONFIG_BT_BLE_ENABLED=n
CONFIG_BT_BLUEDROID_ENABLED=y, CONFIG_BT_CLASSIC_ENABLED=y
    # Classic BT only: A2DP/HFP are classic; BLE RAM released at boot (bt_core_radio_init)

CONFIG_BT_A2DP_ENABLE=y, CONFIG_BT_AVRCP_ENABLED=y, CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=n
    # music; SBC decoding inside the stack, we receive PCM (esp_a2dp_legacy_api.h)

CONFIG_BT_HFP_ENABLE=y, CONFIG_BT_HFP_CLIENT_ENABLE=y, CONFIG_BT_HFP_AG_ENABLE=n
CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y, CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y
CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1, CONFIG_BT_HFP_WBS_ENABLE=y
    # calls; SCO voice over HCI (the PCM pins are not wired on the LyraT); mSBC wide-band on

CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y, CONFIG_ESP_BROWNOUT_DET=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y        # -O2 release (docs 05)
CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED=n     # unused
```

**Check:** With only the skeleton + a `main.c` that does `ESP_LOGI`: `idf.py build` passes, `idf.py -p COM6 flash monitor` shows the log. This was kanchan's "lesson01" (docs 00 step 6, the kanchan project's docs 03). Leave the monitor with Ctrl+].

---

## 4. Modules, in the order they were written

The code for every step is in `main/`. Here: goal, design decision, pitfall, test. The order is the order in which this was learned; build in the same order and something will work at every step.

### Step 1: Boot, GPIO, serial console — `main.c`, `board_config.h`, `app_keys.h`

- `board_config.h`: **pins and numbers live only in this file**. New board = only this file.
- `app_main()`: LED + PA GPIO as outputs, low (PA quiet), NVS init (BT bonds live here), then the modules. **Order matters**: radio first, codec second (keeps RF calibration away from I2S noise, more heap).
- Console task: `getchar()` + switch; letters `a h m c x v f i t l r z b ] [ 0`. The way to experiment without rebuilding.
- `app_key()`: touch-pad routing: a call wins when there is one, otherwise music.

Pitfall: `getchar()` returns EOF non-blocking → `clearerr(stdin)` + a 20 ms delay, otherwise 100% CPU.

**Check:** boot log "bleaudiocall: music + calls; reset reason=1", "Initialized; free heap=...".

### Step 2: Codec + I2S playback — `es8388.c`, `audio_io.c`

Goal: a test tone from the speaker, without Bluetooth. This step took 2 days in kanchan (hiss, silence). The lessons, in order:

1. **I2C**: new driver (`driver/i2c_master.h`), 100 kHz, address **7-bit 0x10** (ADF's 0x20 is the 8-bit form). Confirm with `i2c_master_probe` first.
2. **ES8388 init sequence** (`es8388_init`, from the ADF reference driver in `docs/references/`):
   - 0x19=0x04 mute; 0x01=0x50, 0x02=0x00 power; 0x35/0x37/0x39 DLL; 0x08=0x00 **slave** (ESP32 is master).
   - **0x00=0x36**: reference enable + SameFs + DACMCLK. With 0x12 you get hiss only (the kanchan project's docs 06).
   - 0x17=0x20 DAC **32-bit** I2S; 0x18=0x02 MCLK ratio 256.
   - 0x27/0x2a=0x90 DAC → mixer; 0x2b=0x80 shared LRCK.
   - 0x2e/0x2f=0x1e LOUT1/ROUT1 0 dB; **0x30/0x31=0x00 LOUT2/ROUT2 -45 dB** (feeds NS4150 IN−; at 0 dB it cancels, the kanchan project's docs 12).
   - 0x1a/0x1b DAC volume: profile P4 = 0x0c = -6 dB (ADF's LyraT maximum). 0.5 dB per step, 1.5 dB per profile with `]`/`[`.
   - 0x04=0x3c DAC + outputs on; 0x02=0xf0 → 0x00 restart.
3. **I2S** (`driver/i2s_std.h`): master, MCLK GPIO0 = 256×Fs, BCLK 5, LRCK 25, DOUT 26, **32-bit data AND 32-bit slots**. The original ESP32 rule: data width = slot width (the FIFO packing mode is chosen from the slot width). 16-bit data + 32-bit slots = silence + underruns (the kanchan project's docs 06). That is why `audio_io_write` sends PCM16 as PCM32 via `* 65536`.
4. PA: DAC unmute → GPIO21 high; off: GPIO21 low → mute (`pa()` in audio_engine.c). Fewer pops.

**Check:** console `l` → left speaker 400 Hz, `r` → right 800 Hz, `z` silence (listen to the noise floor), `b` back. `i` readback: `ctrl=36 dacfmt=20 ratio=02 L1=1e R1=1e L2=00 R2=00 LDAC=0c RDAC=0c`.

### Step 3: Bluetooth music — `bt_core.c` (radio part), `bt_music.c`, `audio_engine.c` (MUSIC)

1. `bt_core_radio_init()`: `esp_bt_controller_mem_release(BLE)`, controller init/enable CLASSIC, Bluedroid init with `ssp_en = true`, enable. **Before the codec.**
2. `bt_core_start()`: device name, GAP callback, CoD, IO cap NONE, fixed PIN, page timeout, then `bt_music_init()`, `bt_call_init()`.
3. `bt_music.c`: AVRCP controller (send play/pause/next, play-status notifications) + AVRCP target (receive the phone's absolute volume, report our volume to the phone) + A2DP sink (`esp_a2d_sink_register_data_callback(audio_engine_music_push)`). The audio config event gives the rate (44.1/48 k) and channels.
4. `audio_engine.c` MUSIC path: the BT callback copies PCM as a `packet_t` (epoch, channels, length) into a 32 KiB NOSPLIT ring and **never blocks** (full = drop + counter). The audio task (core 1, priority 6): prefill 8 KiB or 150 ms, then pull packets, gain (volume² curve, 40 ms slew), peak limiter (overflow guard only, 1x gain), I2S write. Empty → fade from the last sample → rebuffer. Rate/format change → `epoch++` → the task reclocks, old packets are discarded.

Pitfalls: keep the digital volume at 1x (kanchan's 10x/20x/60x were all pointless; the real problem was LOUT2). Map phone absolute volume 0..127 → 0..100.

**Check:** pair the phone → "Music (A2DP) connected" → play a song → log `MUSIC 44100 Hz PA=1 in_peak=20000+ out_peak=... drops=0 underruns=0`. Confirm the channels with an L/R test track.

### Step 4: Touch pads — `touch_buttons.c`

ESP32 V1 touch: capacitance up → count **down**. 1 s baseline at boot (hands off), thresholds 80% press / 87% release (hysteresis), 60 ms debounce, slow baseline adaptation (0.1% per sample) only while idle. SET: tap = next, hold 2 s = disconnect/reject. VOL: tap, then a 200 ms repeat after 0.5 s.

**Check:** log "PLAY GPIO33 pressed (raw=...)"; music pauses/plays.

### Step 5: Calls — `bt_call.c`, `audio_engine.c` (CALL/RING), `audio_io.c` (RX), `es8388.c` (ADC)

Detail: [03-call-audio-path.md](03-call-audio-path.md).

1. **sdkconfig**: HFP client + HCI audio path (section 3.1). Without `BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI` the voice goes to the PCM pins = silent speaker.
2. `bt_call.c`: HFP client callback: SLC connected → `bt_core_profile_link(HFP, up)`; audio state CONNECTED/CONNECTED_MSBC → `esp_hf_client_register_data_callback(incoming, outgoing)` + `audio_engine_call_start(8000|16000)`; CIND call/call_setup events → state; BSIR → whether there is an in-band ring; VGS → volume. Answer `esp_hf_client_answer_call()`, hang up/reject `esp_hf_client_reject_call()` (AT+CHUP does both).
3. **I2S full duplex** (`audio_io.c`): `i2s_new_channel(&cfg, &tx, &rx)` on one port; TX init first (master), RX on the same pins + DIN GPIO35 (slave, the warning "switched from master to slave" is normal). On the original ESP32, TX/RX **share one MCLK divider** → both 256×; the RX slave needs MCLK/BCLK ≥ 4 → 256/(2×32) = 4, exactly at the limit. `audio_io_set_rate()`: disable both → reclock → preload silence → enable TX → enable RX only during a call.
4. **ES8388 record**: 0x03=0x00 (ADC + MICBIAS on, only during a call), 0x09=0x88 PGA +24 dB, 0x0a=0x00 LIN1/RIN1 (onboard mics), 0x0b=0x02, 0x0c=0x10 ADC 32-bit, 0x0d=0x02 ratio 256. Schematic note: RIN1's C50 is **NC**, so the right mic may be silent; the firmware takes (L+R)/2.
5. `audio_engine.c` CALL path: 10 ms blocks (80/160 frames); phone PCM from the byte ring with `ring_read_exact` (a whole block or nothing: a byte ring returns two pieces at the wrap, docs 05), mono → L=R, gain level/15; mic read with the same frame count (lock-step), (L+R)/2 × 2.0, byte ring → `esp_hf_client_outgoing_data_ready()` → the stack takes a whole chunk via `audio_engine_call_pull` (it discards partial ones).
6. **Ring**: if the phone provides an in-band ring it arrives over the voice link; otherwise `audio_engine_ring(true)`: 425+480 Hz, 1 s on / 2 s off, at the music rate.

Pitfall: there is no echo canceller; test at call volume 8–10. `MIC_DIGITAL_GAIN` 2.0 → 3.0 if the far side hears you too quietly.

**Check:** `t` → ring tone from the speaker (no phone needed). Call yourself from the phone → "Call setup: INCOMING" → PLAY → "Voice link connected (mSBC 16 kHz)" → `CALL 16000 PA=1 ... mic_peak=5000+ while speaking`, `rx_drops=0 tx_drops=0 underruns=0`.

### Step 6: Every phone connects — `bt_core.c` (link manager)

Detail: [02-connect-every-phone.md](02-connect-every-phone.md). In kanchan an old phone failed; causes and fixes:

| Fix | Code |
|---|---|
| Always CONNECTABLE, discoverable only while nothing is connected | `set_scan_mode()` from the link task |
| Class of Device AV / Hands-free / Audio+Rendering | `esp_bt_gap_set_cod` |
| SSP Just Works (CFM auto-accept) + legacy PIN 0000 (16 zeros if a 16-digit PIN is requested) | GAP events |
| 4 s after pairing/one profile is up, we open the other profile ourselves | `followup_at` |
| Boot +3 s: connect to the bonded phone ourselves; on failure 10 s × 6, then quiet | `link_task` reconnect |
| **A failed attempt ≠ a lost link** (otherwise the counter resets = infinite loop) | `bt_core_profile_failed()`, docs 05 |
| ACL status 0x104 (page timeout) = phone not nearby → do not try the other profile | `s.acl_ok` |
| New name `blu2` | bypasses the phone's old SDP cache ("kanchan" = music only) |

**Check:** phone not nearby: 6 attempts, "Last reconnect attempt", then quiet. Phone nearby: "Pairing successful", "Music connected", "Calls connected" within 4 s; `i` status.

### Step 7: Release — docs [05](05-final-audit-and-optimization.md)

`-O2`, cover art off, 3 bugs fixed from the serial log. Build: `tools/Build.ps1` → log `docs/2026-10-27-final-build.log`.

---

## 5. Build, flash, monitor

```powershell
# VS Code terminal (profile auto-activates) or: . .\tools\Enter-EspIdf.ps1
idf.py build                       # or Ctrl+Shift+B
idf.py -p COM6 flash               # or task "ESP32: Flash bleaudiocall (COM6)"; the monitor must be closed
idf.py -p COM6 monitor             # or task "ESP32: Live call/music logs (COM6)" (tools/live_serial.py)
```

After flashing (official procedure): power off → remove the JP23/24/25 caps (GPIO0 = MCLK, GPIO2 = SD D0) → power on. To flash again, refit the caps, or manually: hold BOOT, press and release RESET, release BOOT.

Expected boot log (in order): `Radio and host enabled` → `es8388: Ready` → `i2s: TX clocks ... MCLK=11289537 BCLK=2822400 Fs=44100` → `touch: ... baseline=` → `READY: music (A2DP) + calls (HFP). Select 'blu2' in the phone's Bluetooth list` → `Radio: connectable, DISCOVERABLE (visible in the phone's list)`.

Short capture without holding the port: `python tools\capture_serial.py --port COM6 --seconds 10 --output docs\serial-output.log` (ESP-IDF venv python).

---

## 6. Test checklist (on the board, in order)

| # | Test | Pass = |
|---|---|---|
| 1 | Boot | READY line, no reset loop (reset reason 1 = power-on) |
| 2 | `l` `r` `z` `b` | correct speaker, correct tone, no hiss in silence |
| 3 | Phone: forget the old "kanchan" → scan → `blu2` → pair (PIN 0000 if asked) | "Pairing successful", Android shows both the Media and Phone calls toggles |
| 4 | Music | "MUSIC 44100", drops/underruns 0, L/R correct, pads work, phone volume slider in sync |
| 5 | `t` ring test | ring from the speaker |
| 6 | Call yourself | INCOMING log, ring (in-band or local), PLAY = answer, audio both ways, mic_peak rises when you speak, PLAY = hang up, music returns |
| 7 | Reconnect | reboot the board → it joins the phone within 3 s; walk away with the phone → "Radio link closed ... 0x08" → come back → it reconnects |
| 8 | Old phone | PIN 0000, HFP connects; otherwise the log shows why (docs 02, "If it still fails") |

Our status (2026-10-27): 1–2 pass (from the kanchan hardware), 3–8 **pending**.

---

## 7. Troubleshooting (from the whole history)

| Symptom | Cause | Fix |
|---|---|---|
| `No serial data received` while flashing | JP23/24/25 caps missing | fit the caps, or manual BOOT+RESET |
| Unicode error in the build | Windows code page | `PYTHONUTF8=1` (set in Enter-EspIdf.ps1) |
| Connected but **hiss only** | ES8388 reg 0x00 reference bit off | 0x36 |
| Silence + rising underruns | 16-bit data, 32-bit slots | data = slots = 32-bit, PCM16 × 65536 |
| Very quiet, more gain does not help | LOUT2/ROUT2 at 0 dB → PA differential cancellation | reg 0x30/0x31 = 0x00 |
| Board resets on bass, `phy_init` loop | PC USB port current limit; the PA runs from the 5 V rail | 5 V 2 A charger; lower the DAC profile with `[` |
| Phone shows "connected (no media)" / HFP does not open | the speaker went NON_CONNECTABLE | always CONNECTABLE (bt_core) |
| Old phone does not list the speaker | CoD uncategorized | CoD AV/hands-free |
| Phone shows "kanchan" as music-only without calls | the phone's SDP cache | forget on the phone; new name `blu2` |
| "Reconnect attempt 1/6" every 10 s forever | failure treated as link lost, counter reset | docs 05 fix |
| Small gaps during a call, ~13 underruns every 5 s | byte-ring short read at the wrap | `ring_read_exact` |
| Speaker silent during a call, audio on the phone | SCO HCI path not configured | the 3 sdkconfig lines (section 3.1) |
| `HFP link attempt ended before SLC` + ACL 0x104 | phone not nearby / off | normal; if it happens with status 0x0 the phone has no HFP |
| Far side hears you too quietly | mic gain | `MIC_DIGITAL_GAIN` 3.0, then `MIC_PGA_REG` |
| Far side hears an echo | no AEC, speaker loud | call volume `[` / `CALL_BOOT_VOLUME 8` |
| I2S warning "switched from master to slave" | RX duplex slave | normal |
| Touch pad triggers by itself | a hand was on the pad at boot | reboot, hands off for 1 s |

---

## 8. Numbers you will tune (`main/board_config.h`)

`MUSIC_BOOT_VOLUME 70`, `AUDIO_BOOT_DAC_PROFILE 4` (-6 dB; P0 -12 … P8 0 dB), `CALL_BOOT_VOLUME 10`, `CALL_RX/TX_BUFFER_BYTES 6144`, `CALL_PREFILL_BYTES 480`, `MIC_PGA_REG 0x88`, `MIC_DIGITAL_GAIN 2.0f`, `CALL_RING_ON/OFF_MS 1000/2000`, `LINK_RECONNECT_ATTEMPTS 6`, `LINK_RECONNECT_PERIOD_S 10`, `LINK_PROFILE_FOLLOWUP_S 4`, `BT_DEVICE_NAME "blu2"`, `BT_LEGACY_PIN "0000"`.

---

## 9. File map (what is where)

| File | Lines (approx) | Responsibility |
|---|---|---|
| `main/main.c` | 95 | boot order, key routing, console |
| `main/board_config.h` | 50 | pins + tunables |
| `main/es8388.c/.h` | 140 | codec I2C, init table, DAC profiles, ADC power, readback |
| `main/audio_io.c/.h` | 125 | I2S duplex, rate switch, PCM16↔32 |
| `main/audio_engine.c/.h` | 410 | audio task: MUSIC / CALL / RING / test, rings, stats |
| `main/bt_core.c/.h` | 345 | radio, GAP, pairing, CoD, link manager, bonds |
| `main/bt_music.c/.h` | 180 | A2DP sink + AVRCP CT/TG |
| `main/bt_call.c/.h` | 240 | HFP client, call state, voice link, ring decision |
| `main/touch_buttons.c/.h` | 90 | touch V1, baseline, gestures |
| `tools/` | | Build/Flash/Monitor.ps1, Enter-EspIdf.ps1, live_serial.py, capture_serial.py |
| `docs/` | | 00 setup, 01 overview, 02 connect, 03 call path, 04 learning, 05 audit, 06 this guide, hardware/ |

The kanchan project's docs 04–13 (in the kanchan speaker project folder, Hinglish) hold the full audio bring-up story with logs; this folder's docs/04 summarizes it.
