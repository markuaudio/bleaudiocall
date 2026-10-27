# 06: Complete build guide — zero se bleaudiocall tak, koi bhi bana sake

Yeh guide un logon ke liye hai jinke paas sirf ek LyraT V4.3 board, ek PC aur ek phone hai. Poora source is folder mein hai (`main/`), isliye code dobara type nahi karna; yeh guide batati hai **kis kram mein kya banaya, kyun, kaise test kiya, aur kahan phasoge**. Har section ke end mein "Check" hai: aage tabhi badho jab check pass ho.

Padhne ka kram: pehle yeh file, phir jahan detail chahiye wahan link.

---

## 0. Kya banega

ESP32-LyraT V4.3 par apna firmware (ESP-IDF v6.1, bina ESP-ADF pipeline ke):

- Phone se Bluetooth **music** (A2DP sink, SBC), play/pause/next/volume (AVRCP).
- Phone ki **calls** (HFP hands-free): ring, answer, dono taraf awaaz (onboard mics), hang-up, volume.
- **Har phone** se pairing: naye (SSP Just Works), purane (PIN 0000); speaker khud reconnect karta hai.
- 4 touch pads + serial console se control; LED status.

Time: hardware setup 30 min, PC setup 1–2 ghante (download), build 5 min, pehla test 15 min. Code samajhna: 1–2 din.

---

## 1. Hardware

Shopping list: [hardware/BOM-hinglish.md](hardware/BOM-hinglish.md) section A. Minimum: LyraT V4.3, 2× 4 Ω 3 W speaker (JST PH 2.0), 2× Micro-USB cable, 5 V ≥ 2 A charger, Windows PC, phone.

Schematic + layout + board BOM: [hardware/README.md](hardware/README.md). Pin mapping: [lyrat-v4.3-hardware.md](lyrat-v4.3-hardware.md).

### 1.1 Board ko samjho (5 minute, schematic kholo)

| Block | Parts | Hamare liye matlab |
|---|---|---|
| ESP32-WROVER-E (U6) | CPU + Bluetooth radio | code yahan chalta hai; flash 8 MB (measure kiya) |
| ES8388 (U5) | codec: I2C se config, I2S se audio | DAC → speaker/headphone, ADC ← mics |
| NS4150 ×2 (U11/U12) | class-D PA, IN+ ← LOUT1, IN− ← LOUT2, CTRL ← IO21 | PA enable GPIO21; LOUT2 ko -45 dB rakhna zaroori |
| CP2102N (U1) | USB-UART | COM port, logs, flash |
| Q9/Q10/Q11 + JP23/24/25 | auto-download | jumper caps lage bina flash nahi hoga |
| J18 DIP ×8 | GPIO12-15 → SD ya JTAG | **sab OFF** (GPIO13 = Vol- touch chahiye) |
| TP3-TP6 | touch: Play 33, Set 32, Vol- 13, Vol+ 27 | `board_config.h` |
| D17 + Q6 | green LED, IO22 | connected/discoverable |

### 1.2 Physical setup (power OFF karke)

1. DIP switch J18: **1–8 sab OFF**.
2. Jumper caps **JP23, JP24, JP25** teeno par lagao (har header ke apne 2 pin). Yeh flash ke liye chahiye.
3. Speakers: LEFT connector J15 par left speaker, RIGHT J16 par right. Speaker − ko GND ya dusre speaker se **mat** jodo (bridge output).
4. AUX_IN mein kuch mat lagao (IO12 strap, boot rok sakta hai).
5. Cable 1: PC → **USB-UART** port (J1, board par "UART" likha). Cable 2: charger/PC → **USB POWER** (J17).
6. Power switch J3 ON. Red power LED (D8) jale, green standby (D4) jale. Charging LED bina battery ke blink kar sakti hai, normal.

**Check:** Windows Device Manager mein "Silicon Labs CP210x USB to UART Bridge (COMx)" dikhe. Hamara: COM6.

---

## 2. PC software

Poora detail: [00-esp-idf-setup-hinglish.md](00-esp-idf-setup-hinglish.md). Short version:

1. VS Code + extensions:
   ```powershell
   code --install-extension espressif.esp-idf-extension
   code --install-extension ms-vscode.cpptools
   ```
2. Espressif Installation Manager (EIM) CLI:
   ```powershell
   winget install --id Espressif.EIM-CLI --exact --source winget --accept-source-agreements --accept-package-agreements --silent
   ```
3. ESP-IDF **v6.1**, target **esp32** (board ka chip esp32 hai, S3 nahi):
   ```powershell
   & "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Espressif.EIM-CLI_Microsoft.Winget.Source_8wekyb3d8bbwe\eim.exe" install -i v6.1 -t esp32 -p 'D:\Vscode\esp\tooling\espressif' --idf-features ide --skip-components-download true --do-not-track true
   & "...\eim.exe" select v6.1
   ```
   Tools `C:\Espressif\tools` mein jate hain (EIM ka default; yeh ek jagah hai jahan C drive zaroori hai). Python venv `C:\Espressif\tools\python\v6.1\venv`.
4. Is folder ko VS Code mein kholo. `.vscode/settings.json` terminal profile "ESP-IDF v6.1 (bleaudiocall)" set karta hai jo `tools/Enter-EspIdf.ps1` chalata hai. Naya terminal kholo:
   ```powershell
   idf.py --version      # ESP-IDF v6.1
   ```
   Agar path alag hai to `tools/Enter-EspIdf.ps1` aur `.vscode/settings.json` mein `idf.currentSetup` badlo.

**Check:** `idf.py --version` v6.1 dikhaye. Extension status bar "ESP-IDF v6.1.0" (nahi to status bar click karke setup select karo, docs 00 section "InvalidSetup").

---

## 3. Project skeleton (jo files pehle banti hain)

| File | Kaam | Content |
|---|---|---|
| `CMakeLists.txt` | IDF build system load, project name | 3 lines: `cmake_minimum_required`, `include($ENV{IDF_PATH}/tools/cmake/project.cmake)`, `project(bleaudiocall)` |
| `main/CMakeLists.txt` | source list + component dependencies | `idf_component_register(SRCS ... REQUIRES bt nvs_flash esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_touch_sens esp_timer esp_ringbuf)` |
| `sdkconfig.defaults` | project ki saari non-default settings | neeche section 3.1 |
| `partitions.csv` | flash layout | nvs 24 KB, phy 4 KB, factory app 3 MB |
| `.vscode/` | terminal profile, tasks (Ctrl+Shift+B), COM port | copy as-is |
| `tools/*.ps1`, `tools/*.py` | build/flash/monitor wrappers, serial capture | copy as-is; port COM6 badlo agar alag |
| `.gitignore` | build/, sdkconfig.old, logs | |

`sdkconfig` khud mat likho: `idf.py build` ise `sdkconfig.defaults` se banata hai. Naya project = `sdkconfig` delete karke build.

### 3.1 sdkconfig.defaults ka har group kyun

```
CONFIG_IDF_TARGET="esp32"
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y          # esptool flash-id se measure kiya (docs 04)
CONFIG_PARTITION_TABLE_CUSTOM=y / FILENAME="partitions.csv"

CONFIG_BT_ENABLED=y, CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y, CONFIG_BT_BLE_ENABLED=n
CONFIG_BT_BLUEDROID_ENABLED=y, CONFIG_BT_CLASSIC_ENABLED=y
    # Classic BT only: A2DP/HFP classic hain; BLE ki RAM boot par release (bt_core_radio_init)

CONFIG_BT_A2DP_ENABLE=y, CONFIG_BT_AVRCP_ENABLED=y, CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=n
    # music; SBC decode stack ke andar, humein PCM milta hai (esp_a2dp_legacy_api.h)

CONFIG_BT_HFP_ENABLE=y, CONFIG_BT_HFP_CLIENT_ENABLE=y, CONFIG_BT_HFP_AG_ENABLE=n
CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y, CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y
CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1, CONFIG_BT_HFP_WBS_ENABLE=y
    # calls; SCO voice HCI par (PCM pins LyraT par wired nahi hain); mSBC wide-band on

CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y, CONFIG_ESP_BROWNOUT_DET=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y        # -O2 release (docs 05)
CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED=n     # unused
```

**Check:** Sirf skeleton + ek `main.c` jo `ESP_LOGI` karta ho: `idf.py build` pass, `idf.py -p COM6 flash monitor` mein log dikhe. Yeh kanchan ka "lesson01" tha (docs 00 step 6, kanchan docs 03). Monitor se bahar: Ctrl+].

---

## 4. Modules, likhne ke kram mein

Har step ka code `main/` mein hai. Yahan: goal, design decision, pitfall, test. Kram wahi hai jisme yeh seekha gaya; isi kram mein banao to har step par kuch kaam karta dikhega.

### Step 1: Boot, GPIO, serial console — `main.c`, `board_config.h`, `app_keys.h`

- `board_config.h`: **sirf isi file mein pins aur numbers**. Naya board = sirf yeh file.
- `app_main()`: LED + PA GPIO output low (PA chup), NVS init (BT bonds yahin), phir modules. **Order matter karta hai**: radio pehle, codec baad (RF calibration ko I2S noise se alag, zyada heap).
- Console task: `getchar()` + switch; letters `a h m c x v f i t l r z b ] [ 0`. Bina rebuild ke experiment ka tareeka.
- `app_key()`: touch pads ka routing: call ho to call jeet-ta hai, nahi to music.

Pitfall: `getchar()` non-blocking EOF deta hai → `clearerr(stdin)` + 20 ms delay, warna 100% CPU.

**Check:** boot log "bleaudiocall: music + calls; reset reason=1", "Initialized; free heap=...".

### Step 2: Codec + I2S playback — `es8388.c`, `audio_io.c`

Goal: test tone speaker se nikle, bina Bluetooth ke. Yahi step kanchan mein 2 din laga (hiss, silence). Seekh in order:

1. **I2C**: new driver (`driver/i2c_master.h`), 100 kHz, address **7-bit 0x10** (ADF ka 0x20 8-bit hai). `i2c_master_probe` se pehle confirm.
2. **ES8388 init sequence** (`es8388_init`, ADF reference driver se, `docs/references/`):
   - 0x19=0x04 mute; 0x01=0x50, 0x02=0x00 power; 0x35/0x37/0x39 DLL; 0x08=0x00 **slave** (ESP32 master).
   - **0x00=0x36**: reference enable + SameFs + DACMCLK. 0x12 rakhne par sirf hiss (kanchan docs 06).
   - 0x17=0x20 DAC **32-bit** I2S; 0x18=0x02 MCLK ratio 256.
   - 0x27/0x2a=0x90 DAC → mixer; 0x2b=0x80 shared LRCK.
   - 0x2e/0x2f=0x1e LOUT1/ROUT1 0 dB; **0x30/0x31=0x00 LOUT2/ROUT2 -45 dB** (NS4150 IN− par jata hai; 0 dB rakha to cancel, kanchan docs 12).
   - 0x1a/0x1b DAC volume: profile P4 = 0x0c = -6 dB (ADF ka LyraT max). 0.5 dB/step, 1.5 dB per profile `]`/`[`.
   - 0x04=0x3c DAC + outputs on; 0x02=0xf0 → 0x00 restart.
3. **I2S** (`driver/i2s_std.h`): master, MCLK GPIO0 = 256×Fs, BCLK 5, LRCK 25, DOUT 26, **32-bit data AND 32-bit slots**. Original ESP32 ka rule: data width = slot width (FIFO packing slot se chunta hai). 16-bit data + 32-bit slot = silence + underruns (kanchan docs 06). Isliye `audio_io_write` PCM16 ko `* 65536` karke PCM32 bhejta hai.
4. PA: DAC unmute → GPIO21 high; off: GPIO21 low → mute (`pa()` in audio_engine.c). Pop kam.

**Check:** console `l` → left speaker 400 Hz, `r` → right 800 Hz, `z` silence (noise floor suno), `b` wapas. `i` readback: `ctrl=36 dacfmt=20 ratio=02 L1=1e R1=1e L2=00 R2=00 LDAC=0c RDAC=0c`.

### Step 3: Bluetooth music — `bt_core.c` (radio part), `bt_music.c`, `audio_engine.c` (MUSIC)

1. `bt_core_radio_init()`: `esp_bt_controller_mem_release(BLE)`, controller init/enable CLASSIC, Bluedroid init with `ssp_en = true`, enable. **Codec se pehle.**
2. `bt_core_start()`: device name, GAP callback, CoD, IO cap NONE, fixed PIN, page timeout, phir `bt_music_init()`, `bt_call_init()`.
3. `bt_music.c`: AVRCP controller (play/pause/next bhejna, play-status notifications) + AVRCP target (phone ka absolute volume lena, apna volume phone ko batana) + A2DP sink (`esp_a2d_sink_register_data_callback(audio_engine_music_push)`). Audio config event se rate (44.1/48 k) aur channels.
4. `audio_engine.c` MUSIC path: BT callback PCM ko `packet_t` (epoch, channels, length) ke saath 32 KiB NOSPLIT ring mein copy karta hai, **kabhi block nahi** (full = drop + counter). Audio task (core 1, prio 6): prefill 8 KiB ya 150 ms, phir packets nikaal kar gain (volume² curve, 40 ms slew), peak limiter (sirf overflow guard, 1x gain), I2S write. Khaali → last sample fade → rebuffer. Rate/format badle → `epoch++` → task reclock karta hai, purane packets discard.

Pitfalls: volume digital 1x hi rakho (kanchan 10x/20x/60x sab bekaar the, asli problem LOUT2 thi). Phone absolute volume 0..127 → 0..100 map.

**Check:** phone pair → "Music (A2DP) connected" → gaana → log `MUSIC 44100 Hz PA=1 in_peak=20000+ out_peak=... drops=0 underruns=0`. L/R test track se channel confirm.

### Step 4: Touch pads — `touch_buttons.c`

ESP32 V1 touch: capacitance badhe → count **girta** hai. Boot par 1 s baseline (haath door), threshold 80% press / 87% release (hysteresis), 60 ms debounce, baseline slow adapt (0.1%/sample) sirf jab idle. SET: tap = next, hold 2 s = disconnect/reject. VOL: tap + 0.5 s baad 200 ms repeat.

**Check:** log "PLAY GPIO33 pressed (raw=...)"; music pause/play.

### Step 5: Calls — `bt_call.c`, `audio_engine.c` (CALL/RING), `audio_io.c` (RX), `es8388.c` (ADC)

Detail: [03-call-audio-path.md](03-call-audio-path.md).

1. **sdkconfig**: HFP client + HCI audio path (section 3.1). Bina `BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI` ke voice PCM pins par chala jata hai = speaker chup.
2. `bt_call.c`: HFP client callback: SLC connected → `bt_core_profile_link(HFP, up)`; audio state CONNECTED/CONNECTED_MSBC → `esp_hf_client_register_data_callback(incoming, outgoing)` + `audio_engine_call_start(8000|16000)`; CIND call/call_setup events → state; BSIR → in-band ring hai ya nahi; VGS → volume. Answer `esp_hf_client_answer_call()`, hang up/reject `esp_hf_client_reject_call()` (AT+CHUP dono karta hai).
3. **I2S full duplex** (`audio_io.c`): `i2s_new_channel(&cfg, &tx, &rx)` ek port par; TX init pehle (master), RX same pins + DIN GPIO35 (slave, warning "switched from master to slave" normal). Original ESP32 par TX/RX **ek MCLK divider** share karte hain → dono 256×; RX slave ko MCLK/BCLK ≥ 4 chahiye → 256/(2×32) = 4, bilkul limit par. `audio_io_set_rate()`: dono disable → reclock → silence preload → TX enable → RX enable sirf call mein.
4. **ES8388 record**: 0x03=0x00 (ADC + MICBIAS on, sirf call mein), 0x09=0x88 PGA +24 dB, 0x0a=0x00 LIN1/RIN1 (onboard mics), 0x0b=0x02, 0x0c=0x10 ADC 32-bit, 0x0d=0x02 ratio 256. Schematic note: RIN1 ka C50 **NC** hai, right mic shayad silent; firmware (L+R)/2 leta hai.
5. `audio_engine.c` CALL path: 10 ms blocks (80/160 frames); phone PCM byte-ring se `ring_read_exact` (poora block ya kuch nahi: byte ring wrap par do tukde deta hai, docs 05), mono → L=R, gain level/15; mic read same frames (lock-step), (L+R)/2 × 2.0, byte-ring → `esp_hf_client_outgoing_data_ready()` → stack `audio_engine_call_pull` se poora chunk leta hai (partial discard karta hai).
6. **Ring**: phone in-band ring de to voice link par aata hai; nahi to `audio_engine_ring(true)`: 425+480 Hz, 1 s on / 2 s off, music rate par.

Pitfall: echo canceller nahi hai; call volume 8–10 par test karo. `MIC_DIGITAL_GAIN` 2.0 → 3.0 agar dusri taraf awaaz kam.

**Check:** `t` → ring tone speaker se (bina phone). Phone se khud ko call → "Call setup: INCOMING" → PLAY → "Voice link connected (mSBC 16 kHz)" → `CALL 16000 PA=1 ... mic_peak=5000+ jab bolo`, `rx_drops=0 tx_drops=0 underruns=0`.

### Step 6: Har phone connect ho — `bt_core.c` (link manager)

Detail: [02-har-type-bluetooth-connect.md](02-har-type-bluetooth-connect.md). Kanchan mein purana phone fail hua tha; wajah aur fix:

| Fix | Code |
|---|---|
| Hamesha CONNECTABLE, discoverable sirf jab koi nahi juda | `set_scan_mode()` link task se |
| Class of Device AV / Hands-free / Audio+Rendering | `esp_bt_gap_set_cod` |
| SSP Just Works (CFM auto-accept) + legacy PIN 0000 (16-digit maange to 16 zero) | GAP events |
| Pairing/profile up ke 4 s baad doosri profile hum kholte hain | `followup_at` |
| Boot +3 s bonded phone ko khud connect; fail → 10 s × 6, phir chup | `link_task` reconnect |
| **Fail attempt ≠ link lost** (warna counter reset = infinite loop) | `bt_core_profile_failed()`, docs 05 |
| ACL status 0x104 (page timeout) = phone paas nahi → doosri profile try mat karo | `s.acl_ok` |
| Naya naam `blu2` | phone ka purana SDP cache ("kanchan" = music only) bypass |

**Check:** phone paas na ho: 6 attempts, "Last reconnect attempt", phir chup. Phone paas: "Pairing successful", "Music connected", 4 s mein "Calls connected"; `i` status.

### Step 7: Release — docs [05](05-final-audit-aur-optimization.md)

`-O2`, cover art off, serial log se 3 bug fix. Build: `tools/Build.ps1` → log `docs/2026-10-27-final-build.log`.

---

## 5. Build, flash, monitor

```powershell
# VS Code terminal (profile auto-activate) ya: . .\tools\Enter-EspIdf.ps1
idf.py build                       # ya Ctrl+Shift+B
idf.py -p COM6 flash               # ya task "ESP32: Flash bleaudiocall (COM6)"; monitor band ho
idf.py -p COM6 monitor             # ya task "ESP32: Live call/music logs (COM6)" (tools/live_serial.py)
```

Flash ke baad (official): power off → JP23/24/25 caps hatao (GPIO0 = MCLK, GPIO2 = SD D0) → power on. Dobara flash ke liye caps wapas, ya manual: BOOT dabaye rakho, RESET dabao-chhodo, BOOT chhodo.

Expected boot log (kram mein): `Radio and host enabled` → `es8388: Ready` → `i2s: TX clocks ... MCLK=11289537 BCLK=2822400 Fs=44100` → `touch: ... baseline=` → `READY: music (A2DP) + calls (HFP)` → `Radio: connectable, DISCOVERABLE`.

Short capture bina port pakde: `python tools\capture_serial.py --port COM6 --seconds 10 --output docs\serial-output.log` (ESP-IDF venv python).

---

## 6. Test checklist (board par, kram mein)

| # | Test | Pass = |
|---|---|---|
| 1 | Boot | READY line, koi reset loop nahi (reset reason 1 = power-on) |
| 2 | `l` `r` `z` `b` | sahi speaker, sahi tone, silence mein hiss nahi |
| 3 | Phone: purana "kanchan" forget → scan → `blu2` → pair (PIN 0000 agar maange) | "Pairing successful", Android mein Media + Phone calls dono toggles |
| 4 | Music | "MUSIC 44100", drops/underruns 0, L/R sahi, pads kaam karein, phone volume slider sync |
| 5 | `t` ring test | speaker se ring |
| 6 | Khud ko call | INCOMING log, ring (in-band ya local), PLAY = answer, dono taraf awaaz, mic_peak badhe bolne par, PLAY = hang up, music wapas |
| 7 | Reconnect | board reboot → 3 s mein khud phone se jude; phone door le jao → "Radio link closed ... 0x08" → wapas aao → jude |
| 8 | Purana phone | PIN 0000, HFP connect; nahi to log mein wajah (docs 02 "Agar phir bhi fail") |

Hamari status (2026-10-27): 1–2 pass (kanchan hardware se), 3–8 **pending**.

---

## 7. Troubleshooting (poori history se)

| Symptom | Wajah | Fix |
|---|---|---|
| `No serial data received` flash par | JP23/24/25 caps nahi | caps lagao, ya manual BOOT+RESET |
| Build mein Unicode error | Windows code page | `PYTHONUTF8=1` (Enter-EspIdf.ps1 mein hai) |
| Connected par sirf **hiss** | ES8388 reg 0x00 reference bit off | 0x36 |
| Silence + underruns badhte | 16-bit data, 32-bit slot | data = slot = 32-bit, PCM16 × 65536 |
| Awaaz bahut kam, gain badhane par bhi nahi | LOUT2/ROUT2 0 dB → PA differential cancel | reg 0x30/0x31 = 0x00 |
| Bass par board reset, `phy_init` loop | PC USB port current limit, PA 5 V rail se chalta hai | 5 V 2 A charger; DAC profile `[` se neeche |
| Phone "connected (no media)" / HFP nahi khulti | speaker NON_CONNECTABLE ho gaya tha | hamesha CONNECTABLE (bt_core) |
| Purana phone list mein nahi | CoD uncategorized | CoD AV/hands-free |
| Phone HFP bina call ke "kanchan" ko music-only dikhaye | phone SDP cache | phone par forget; naya naam `blu2` |
| Har 10 s "Reconnect attempt 1/6" hamesha | fail = link lost samjha, counter reset | docs 05 fix |
| Call mein chhote gaps, underruns har 5 s ~13 | byte ring wrap short read | `ring_read_exact` |
| Call mein speaker chup, phone par awaaz | SCO HCI path config nahi | 3 sdkconfig lines (section 3.1) |
| `HFP link attempt ended before SLC` + ACL 0x104 | phone paas nahi / off | normal; 0x0 status par aaye to phone mein HFP nahi |
| Dusri taraf awaaz kam | mic gain | `MIC_DIGITAL_GAIN` 3.0, phir `MIC_PGA_REG` |
| Dusri taraf goonj | no AEC, speaker loud | call volume `[` / `CALL_BOOT_VOLUME 8` |
| I2S warning "switched from master to slave" | RX duplex slave | normal |
| Touch pad khud dabta | boot par haath pad par tha | reboot, 1 s haath door |

---

## 8. Numbers jo tune karoge (`main/board_config.h`)

`MUSIC_BOOT_VOLUME 70`, `AUDIO_BOOT_DAC_PROFILE 4` (-6 dB; P0 -12 … P8 0 dB), `CALL_BOOT_VOLUME 10`, `CALL_RX/TX_BUFFER_BYTES 6144`, `CALL_PREFILL_BYTES 480`, `MIC_PGA_REG 0x88`, `MIC_DIGITAL_GAIN 2.0f`, `CALL_RING_ON/OFF_MS 1000/2000`, `LINK_RECONNECT_ATTEMPTS 6`, `LINK_RECONNECT_PERIOD_S 10`, `LINK_PROFILE_FOLLOWUP_S 4`, `BT_DEVICE_NAME "blu2"`, `BT_LEGACY_PIN "0000"`.

---

## 9. File map (kya kahan)

| File | Lines (approx) | Zimmedari |
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
| `docs/` | | 00 setup, 01 overview, 02 connect, 03 call path, 04 learning, 05 audit, 06 yeh guide, hardware/ |

Kanchan project (`D:\Vscode\esp`) ke docs 04–13 mein audio bring-up ki poori kahani logs ke saath hai; is folder ke docs/04 mein uska saar.
