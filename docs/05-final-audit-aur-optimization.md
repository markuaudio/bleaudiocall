# 05: Final audit — kya optimized nahi tha, kya fix kiya, kaise verify kiya

Date: 2026-10-27. Input: poora `main/` source, `sdkconfig`, build logs, aur board ka serial log `docs/serial-output.log`. Output: 5 code fixes, 2 config changes, final build `docs/2026-10-27-final-build.log`.

## Seedha jawab: "bilkul optimized hai kya?"

Nahi tha. Architecture achha tha (ek audio task, saaf modules, bounded ring buffers, BT callbacks kabhi block nahi karte), par:

| # | Finding | Kahan | Asar | Status |
|---|---|---|---|---|
| 1 | Compiler **-Og (debug)** par tha, release nahi | `sdkconfig` | Audio task per-sample float maths slow; binary bada | **fixed**: `-O2` |
| 2 | **Reconnect loop kabhi rukta nahi** jab phone paas na ho | `bt_core.c` + `bt_music.c` | Radio har 10 s page karta rehta, hamesha; CPU/battery waste, naye phone ke liye radio busy | **fixed** |
| 3 | HFP page attempt A2DP page ke **beech mein** shuru | `bt_core.c` link_task | Do overlapping page attempts ek hi phone par; purane phone confuse | **fixed** |
| 4 | Har fail attempt par **poora audio path reclock** (DAC profile, ADC off, I2S disable/enable) | `audio_engine.c` | Har 10 s bekaar I2S restart, log spam | **fixed**: idempotent |
| 5 | Byte ring **wrap par short read** → jhoothe underruns, 0-byte mic frames | `audio_engine.c` | Call mein har ~380 ms ek chhota gap dono direction mein (buffer size par wrap) | **fixed** |
| 6 | AVRCP **cover art** (OBEX/GOEP) compiled, kabhi use nahi | `sdkconfig` | RAM + flash waste | **fixed**: off |
| 7 | Docs mein naam "kanchan", code mein `blu2`; setup doc referenced par folder mein nahi | docs | Naya banda confuse | **fixed** |
| 8 | Echo canceller nahi (speaker + mic ek board par) | design | Call volume zyada par dusre ko goonj | **not fixed**: ESP-ADF DSP yahan nahi; phone ka AEC kaam karta hai |
| 9 | Bond list ka last entry "newest" maana | `bt_core.c` | Multi-phone par galat phone reconnect | **not fixed**: ek phone par farq nahi; doc mein note |

## Finding 2 detail: serial log ne pakda

`docs/serial-output.log` (phone paas nahi tha):

```
I (123215) bt: Reconnect attempt 1/6 to bonded phone 78:64:c0:96:94:bf
I (127215) bt: Opening calls (HFP) to 78:64:c0:96:94:bf ourselves     <- attempt 1 ki page abhi chal rahi (10 s timeout)
W (128445) BT_HCI: hcif conn complete: hdl 0xfff, st 0x4              <- page timeout
I (133215) bt: Reconnect attempt 2/6 ...
W (138725) BT_BTC: BTA_AV_OPEN_EVT::FAILED status: 2
I (138725) bt: Music (A2DP) disconnected from ...                      <- kabhi connected tha hi nahi
I (138725) es8388: DAC profile P4 ... ADC/microphones OFF ... Path: MUSIC  <- bekaar reclock
I (144215) bt: Reconnect attempt 1/6 ...                               <- counter reset = infinite loop
```

Chain: hamari A2DP attempt fail → Bluedroid `ESP_A2D_CONNECTION_STATE_DISCONNECTED` deta hai → `bt_music.c` ise `bt_core_profile_link(A2DP, false)` bhejta tha → `bt_core.c` "link toota" samajh kar `attempts = 0`, `next_attempt_at = +5 s`. `LINK_RECONNECT_ATTEMPTS 6` kabhi nahi pahunchta.

Fix (3 files):

- `bt_music.c`: DISCONNECTED par `was` (pehle connected tha?) check. `was` → `profile_link(false)` (asli link loss, reconnect cycle restart sahi hai). Nahi → naya `bt_core_profile_failed(PROFILE_A2DP)`.
- `bt_call.c`: SLC kabhi bana hi nahi → `bt_core_profile_failed(PROFILE_HFP)`.
- `bt_core.c`: `bt_core_profile_failed()` counter ko **chhoota nahi**. Sirf yeh decide karta hai ki doosri profile turant try karein ya nahi: tabhi jab (a) koi profile up nahi, (b) phone ne radio par jawab diya tha (`acl_ok`, `ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT` se), (c) doosri profile is link par abhi tak try nahi hui. Page timeout (0x104) = phone paas nahi → kuch nahi, next period tak wait.
- `bt_core.c` reconnect attempt aur follow-up A2DP attempt ab `followup_at = 0` set karte hain; timer tabhi re-arm hota hai jab outcome pata chale (`profile_link(up)` ya `profile_failed`). Isliye do page attempts overlap nahi karte (finding 3).
- Aakhri attempt par log: "Last reconnect attempt; afterwards waiting for the phone (or 'c' / reboot)".

Naya behaviour: boot +3 s, phir 10 s × 6, phir chup. Phone link tode → 5 s baad wahi cycle. User `x` → reboot/`c` tak band.

## Finding 4 detail

`audio_engine_music_stream(false)` har DISCONNECTED par call hoti thi aur bina check `++epoch` karti thi → audio task PA off, DAC profile re-write (I2C), ADC off (I2C), I2S disable/reclock/preload/enable, ring flush. Ab sirf state change par epoch badhta hai. `audio_engine_call_stop()` bhi: sirf agar source CALL tha.

## Finding 5 detail

FreeRTOS `RINGBUF_TYPE_BYTEBUF` mein `xRingbufferReceiveUpTo` wrap point par data **do tukdon** mein deta hai: pehla call sirf buffer-end tak ka hissa lauta sakta hai, chahe poora data maujood ho. Purana code:

- `audio_engine_call_pull` (mic → phone): `got < length` → item wapas, return 0 → Bluedroid ko us frame ke liye kuch nahi mila → outgoing voice mein gap.
- Audio task (phone → speaker): `have < frames` → underrun ginta, fade to silence → sunne mein click/gap.

6144-byte ring, 160 bytes/10 ms (8 kHz) → har ~380 ms ek wrap → har ~380 ms ek jhootha gap, dono direction. mSBC (16 kHz) par har ~190 ms.

Fix: `ring_read_exact(ring, capacity, dst, want)`: pehle `capacity - xRingbufferGetCurFreeSize()` se check ki `want` bytes hain; hain to loop mein do tukde jod kar poora `want` deta hai, nahi to 0 (kuch consume nahi, alignment bani rehti hai). Task mein ab local `voice[256]` buffer (512 B stack, task stack 8192 mein jagah hai).

## Config changes (`sdkconfig.defaults`)

```
CONFIG_COMPILER_OPTIMIZATION_PERF=y          # -O2; tha CONFIG_COMPILER_OPTIMIZATION_DEBUG (-Og)
CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED=n       # cover art + GOEP client off
```

Assertions **on rakhe** (`ASSERTION_LEVEL=2`): call path hardware par verify pending hai, panic message chahiye. Jab sab verify ho jaye to `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT=y` se thoda flash bachega.

`sdkconfig` ko defaults se regenerate kiya (purana `sdkconfig.old`), diff sirf yeh 8 lines (comments chhod kar):

```
< CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED=y
< CONFIG_BT_GOEPC_ENABLED=y                    (cover art ka dependent)
< CONFIG_COMPILER_OPTIMIZATION_DEBUG=y
< CONFIG_COMPILER_OPTIMIZATION_DEFAULT=y
< CONFIG_COMPILER_OPTIMIZATION_LEVEL_DEBUG=y
> CONFIG_COMPILER_OPTIMIZATION_PERF=y
< CONFIG_FREERTOS_TASK_FUNCTION_WRAPPER=y      (debug-only dependent)
< CONFIG_OPTIMIZATION_LEVEL_DEBUG=y
```

Matlab: purane `sdkconfig` mein koi hidden menuconfig change nahi tha; `sdkconfig.defaults` hi poora sach hai. Flash settings (8 MB, DIO, 40 MHz) kanchan se verified, nahi chhede.

## Verification (final build)

| Item | First build (`2026-10-27-first-build-rebuild.log`) | Final (`2026-10-27-final-build.log`) |
|---|---|---|
| Exit code | 0 | 0 |
| Warnings in `main/` | 0 | 0 |
| Compiler flag | `-Og` | `-O2` (compile_commands.json mein 1235 entries) |
| `bleaudiocall.bin` | 0xd1710 = 858,896 B | 0xc7d10 = 818,448 B (−40 KB, 74% partition free) |
| Bootloader | 0x6640 | 0x6640 (unchanged) |

CMake ki 5 warnings IDF ke apne `esp_wifi`/`wpa_supplicant` components ki hain (private include dirs), hamare code ki nahi; Wi-Fi link hi nahi hota.

## Jo abhi bhi pending hai (board par)

Yeh build **flash nahi** kiya gaya; board par abhi bhi pehla firmware hai. Test plan [01](01-project-overview-hinglish.md) ke "Test plan" section mein. Khaas dekhna:

1. Phone paas na ho: log mein "Reconnect attempt 6/6" ke baad "Last reconnect attempt" aur phir **koi** attempt nahi (finding 2 proof).
2. Phone paas ho, pair karke: "Music (A2DP) connected", 4 s mein "Calls (HFP) connected"; `Radio link ... status 0x0`.
3. Call mein 5-second stats: `call rx_drops=0 tx_drops=0 underruns=0` lagatar (finding 5 proof; pehle har 5 s mein ~13 underruns aate).
4. Call ke baad "CALL audio stop; back to music path" **ek baar** (finding 4 proof).

## Jo jaan-boojh kar nahi chheda

- `main.c` aur `audio_engine_init` dono PA GPIO configure karte hain: harmless, defensive.
- `bt_call.c` mein log statements `s.number`/`s.setup` lock ke bahar padhte hain: sab ek hi Bluedroid task se aate hain, race nahi.
- Console task `getchar()` + 20 ms delay: simple, kaafi.
- Bootloader log level INFO: boot par 1 s ka farq nahi, debugging mein kaam aata hai.
