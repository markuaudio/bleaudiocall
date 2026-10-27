# 05: Final audit — what was not optimized, what was fixed, how it was verified

Date: 2026-10-27. Input: the whole `main/` source, `sdkconfig`, build logs, and the board's serial log `docs/serial-output.log`. Output: 5 code fixes, 2 config changes, final build `docs/2026-10-27-final-build.log`.

## Straight answer: "is it fully optimized?"

It was not. The architecture was good (one audio task, clean modules, bounded ring buffers, BT callbacks never block), but:

| # | Finding | Where | Effect | Status |
|---|---|---|---|---|
| 1 | Compiler at **-Og (debug)**, not release | `sdkconfig` | Per-sample float maths in the audio task slow; larger binary | **fixed**: `-O2` |
| 2 | **Reconnect loop never stops** when the phone is not nearby | `bt_core.c` + `bt_music.c` | Radio pages every 10 s, forever; CPU/battery wasted, radio busy for a new phone | **fixed** |
| 3 | HFP page attempt starts **in the middle of** the A2DP page | `bt_core.c` link_task | Two overlapping page attempts to the same phone; old phones get confused | **fixed** |
| 4 | **Whole audio path reclocked** on every failed attempt (DAC profile, ADC off, I2S disable/enable) | `audio_engine.c` | Pointless I2S restart every 10 s, log spam | **fixed**: idempotent |
| 5 | Byte ring **short read at the wrap point** → false underruns, 0-byte mic frames | `audio_engine.c` | A small gap in both directions every ~380 ms of a call (wrap at the buffer size) | **fixed** |
| 6 | AVRCP **cover art** (OBEX/GOEP) compiled in, never used | `sdkconfig` | RAM + flash wasted | **fixed**: off |
| 7 | Docs said "kanchan", code says `blu2`; setup doc referenced but missing from the folder | docs | Confuses a newcomer | **fixed** |
| 8 | No echo canceller (speaker + mic on one board) | design | Echo for the far side at high call volume | **not fixed**: ESP-ADF DSP is not here; the phone's AEC does the job |
| 9 | Last bond-list entry treated as "newest" | `bt_core.c` | Wrong phone reconnected with several phones | **not fixed**: no difference with one phone; noted in the docs |

## Finding 2 in detail: caught by the serial log

`docs/serial-output.log` (phone not nearby):

```
I (123215) bt: Reconnect attempt 1/6 to bonded phone 78:64:c0:96:94:bf
I (127215) bt: Opening calls (HFP) to 78:64:c0:96:94:bf ourselves     <- attempt 1's page still running (10 s timeout)
W (128445) BT_HCI: hcif conn complete: hdl 0xfff, st 0x4              <- page timeout
I (133215) bt: Reconnect attempt 2/6 ...
W (138725) BT_BTC: BTA_AV_OPEN_EVT::FAILED status: 2
I (138725) bt: Music (A2DP) disconnected from ...                      <- was never connected
I (138725) es8388: DAC profile P4 ... ADC/microphones OFF ... Path: MUSIC  <- pointless reclock
I (144215) bt: Reconnect attempt 1/6 ...                               <- counter reset = infinite loop
```

Chain: our A2DP attempt fails → Bluedroid delivers `ESP_A2D_CONNECTION_STATE_DISCONNECTED` → `bt_music.c` forwarded it as `bt_core_profile_link(A2DP, false)` → `bt_core.c` took it as "link lost" and set `attempts = 0`, `next_attempt_at = +5 s`. `LINK_RECONNECT_ATTEMPTS 6` is never reached.

Fix (3 files):

- `bt_music.c`: on DISCONNECTED, check `was` (was it connected before?). `was` → `profile_link(false)` (real link loss, restarting the reconnect cycle is correct). Otherwise → the new `bt_core_profile_failed(PROFILE_A2DP)`.
- `bt_call.c`: SLC never came up → `bt_core_profile_failed(PROFILE_HFP)`.
- `bt_core.c`: `bt_core_profile_failed()` **never touches** the counter. It only decides whether to try the other profile right away: only if (a) no profile is up, (b) the phone answered on the radio (`acl_ok`, from `ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT`), (c) the other profile has not been tried on this link yet. Page timeout (0x104) = phone not nearby → do nothing, wait for the next period.
- `bt_core.c` reconnect attempts and follow-up A2DP attempts now set `followup_at = 0`; the timer is re-armed only once the outcome is known (`profile_link(up)` or `profile_failed`). So two page attempts never overlap (finding 3).
- Log on the last attempt: "Last reconnect attempt; afterwards waiting for the phone (or 'c' / reboot)".

New behaviour: boot +3 s, then 10 s × 6, then quiet. Phone drops the link → the same cycle after 5 s. User `x` → off until reboot/`c`.

## Finding 4 in detail

`audio_engine_music_stream(false)` was called on every DISCONNECTED and did `++epoch` without checking → the audio task switched the PA off, rewrote the DAC profile (I2C), switched the ADC off (I2C), disabled/reclocked/preloaded/re-enabled I2S, flushed the rings. Now the epoch only advances on a state change. `audio_engine_call_stop()` likewise: only if the source was CALL.

## Finding 5 in detail

In a FreeRTOS `RINGBUF_TYPE_BYTEBUF`, `xRingbufferReceiveUpTo` hands data back in **two pieces** at the wrap point: the first call can return only the part up to the buffer end, even when the full amount is present. The old code:

- `audio_engine_call_pull` (mic → phone): `got < length` → return the item, return 0 → Bluedroid got nothing for that frame → gap in the outgoing voice.
- Audio task (phone → speaker): `have < frames` → counted an underrun, faded to silence → audible click/gap.

6144-byte ring, 160 bytes per 10 ms (8 kHz) → one wrap every ~380 ms → one false gap every ~380 ms, in both directions. With mSBC (16 kHz) every ~190 ms.

Fix: `ring_read_exact(ring, capacity, dst, want)`: first check with `capacity - xRingbufferGetCurFreeSize()` that `want` bytes are present; if so, join the two pieces in a loop and return the full `want`, otherwise 0 (nothing consumed, alignment preserved). The task now has a local `voice[256]` buffer (512 B of stack; the 8192-byte task stack has room).

## Config changes (`sdkconfig.defaults`)

```
CONFIG_COMPILER_OPTIMIZATION_PERF=y          # -O2; was CONFIG_COMPILER_OPTIMIZATION_DEBUG (-Og)
CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED=n       # cover art + GOEP client off
```

Assertions **kept on** (`ASSERTION_LEVEL=2`): the call path is still pending hardware verification and a panic message is needed. Once everything is verified, `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT=y` saves a little flash.

`sdkconfig` was regenerated from the defaults (old copy `sdkconfig.old`); the diff is only these 8 lines (comments excluded):

```
< CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED=y
< CONFIG_BT_GOEPC_ENABLED=y                    (dependent of cover art)
< CONFIG_COMPILER_OPTIMIZATION_DEBUG=y
< CONFIG_COMPILER_OPTIMIZATION_DEFAULT=y
< CONFIG_COMPILER_OPTIMIZATION_LEVEL_DEBUG=y
> CONFIG_COMPILER_OPTIMIZATION_PERF=y
< CONFIG_FREERTOS_TASK_FUNCTION_WRAPPER=y      (debug-only dependent)
< CONFIG_OPTIMIZATION_LEVEL_DEBUG=y
```

Meaning: the old `sdkconfig` contained no hidden menuconfig changes; `sdkconfig.defaults` is the whole truth. Flash settings (8 MB, DIO, 40 MHz) were verified in kanchan and were not touched.

## Verification (final build)

| Item | First build (`2026-10-27-first-build-rebuild.log`) | Final (`2026-10-27-final-build.log`) |
|---|---|---|
| Exit code | 0 | 0 |
| Warnings in `main/` | 0 | 0 |
| Compiler flag | `-Og` | `-O2` (1235 entries in compile_commands.json) |
| `bleaudiocall.bin` | 0xd1710 = 858,896 B | 0xc7d10 = 818,448 B (−40 KB, 74% of the partition free) |
| Bootloader | 0x6640 | 0x6640 (unchanged) |

The 5 CMake warnings come from IDF's own `esp_wifi`/`wpa_supplicant` components (private include dirs), not from our code; Wi-Fi is never linked.

## Verification on the board (2026-10-27 16:05, `docs/2026-10-27-final-flash.log`, `docs/2026-10-27-final-serial.log`)

Flash passed (hash verified). 75 s serial capture, bonded phone not nearby:

- Finding 2 **proof**: `Reconnect attempt 1/6 ... 6/6`, `Last reconnect attempt; afterwards waiting for the phone`, no attempt after that. The counter did not reset.
- Finding 3 **proof**: after every attempt `Radio link ... status 0x104` + `Music (A2DP) attempt failed (phone not reachable)`; no separate HFP page attempt.
- Finding 4 **proof**: no `DAC profile` / `ADC/microphones OFF` / `Path: MUSIC` reclock lines after a failure (previously they appeared on every failure).
- Bonus: a second bonded phone (04:ec:d8:68:0e:79) connected HFP on its own: `Calls (HFP) connected`, features 0xf67, `Phone network NOT available` (the phone had no network/SIM at the time). So the HFP SLC works with a real phone.
- Boot: free heap 71468 B, touch baselines 1278/1278/1152/838, I2S 44.1 kHz clocks correct, no reset.
- Minor warning `gpio: conflict found for GPIO[21]`: both `main.c` and `audio_engine_init` configure the PA pin. Harmless; remove the `gpio_config` in `audio_engine_init` if you like.

## Still pending (on the board)

Music playback, incoming call ring/answer/audio/hang-up: needs a user test with a phone. The test plan is in the "Test plan" section of [01](01-project-overview.md). In particular, watch for:

1. Phone not nearby: after "Reconnect attempt 6/6" the log shows "Last reconnect attempt" and then **no** further attempt (finding 2 proof).
2. Phone nearby, paired: "Music (A2DP) connected", "Calls (HFP) connected" within 4 s; `Radio link ... status 0x0`.
3. During a call the 5-second stats stay at `call rx_drops=0 tx_drops=0 underruns=0` (finding 5 proof; previously ~13 underruns every 5 s).
4. After the call, "CALL audio stop; back to music path" appears **once** (finding 4 proof).

## Deliberately left alone

- Both `main.c` and `audio_engine_init` configure the PA GPIO: harmless, defensive.
- Log statements in `bt_call.c` read `s.number`/`s.setup` outside the lock: everything comes from the same Bluedroid task, no race.
- Console task `getchar()` + 20 ms delay: simple, sufficient.
- Bootloader log level INFO: no visible boot-time difference, useful when debugging.
