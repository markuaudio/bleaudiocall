# 08: Test report, 2026-10-27

Every test run on the board on 2026-10-27, in order, with the firmware it ran on, what was done, what the log showed, the verdict, and what changed because of it. All evidence files are in this repository; nothing here is from memory.

**Setup for all tests**

| Item | Value |
|---|---|
| Board | ESP32-LyraT V4.3, ESP32-D0WD-V3, 8 MB flash, two 4 Ω speakers on J15/J16, JP23/JP24/JP25 auto-download jumpers installed throughout |
| Power | 5 V wall adapter on the USB POWER port (J1) (rating not measured); PC on the USB-UART port (J17) for logs |
| Toolchain | ESP-IDF v6.1, release build (-O2), see `docs/2026-10-27-final-build.log` |
| Phones | A: `04:ec:d8:68:0e:79`, B: `2c:d0:66:b0:8c:74`, C: `50:49:b0:90:c5:33`, D (old bond, never present): `30:94:35:07:5a:b8` |
| Log tools | `tools/capture_serial.py` (short captures, `docs/2026-10-27-final-serial.log` holds the last one), `tools/live_serial.py` (continuous, `docs/test-data/2026-10-27-live-session.log`) |
| Time base | ESP32 uptime in ms from the log lines (`I (12345)`); wall-clock time was not logged. Resets restart the uptime. |
| Charts | `tools/plot_serial_log.py` draws SVG charts from the 5-second stats lines (no oscilloscope or meter measurements were made; all "measurements" are the firmware's own PCM peak counters and the ROM reset banners) |

Firmware identifiers as printed by `app_init: App version` (git describe, `-dirty` = uncommitted changes at build time):

| Id | Content |
|---|---|
| `831add3-dirty` | English log strings; reconnect fixes from the audit (docs/05) |
| `9efee2c-dirty` | reconnect to the last connected phone, RTC power-trip flag, ADC supply guard, always discoverable |
| `df773b5-dirty` | power self-test (radio-loaded), NVS power-trip flag, supply guard removed, discoverable only while free |
| final | self-test result margin of one step (`CAL_MARGIN_STEPS`), brownout detector 2.80 V and BROWNOUT-only trip rule, this commit |

---

## T01: Release build

- Firmware: audit fixes, `-O2`.
- Procedure: `tools/Build.ps1`, log `docs/2026-10-27-final-build.log`.
- Result: exit 0, 0 warnings in `main/`, `bleaudiocall.bin` 0xc7d10 = 818,448 bytes (first debug build was 0xd1710 = 858,896), compiler flag `-O2` confirmed in `build/compile_commands.json`.
- Verdict: **PASS**.

## T02: Reconnect loop on the original firmware (serial capture)

- Firmware: original (debug build, before the audit).
- Procedure: board running with the bonded phone D absent; 2 minute serial capture (`docs/serial-output.log`, kept as evidence).
- Observed:
  ```
  I (123215) bt: Reconnect attempt 1/6 to bonded phone 30:94:35:07:5a:b8
  I (133215) bt: Reconnect attempt 2/6 ...
  I (144215) bt: Reconnect attempt 1/6 ...      <- counter reset
  ```
  plus a full audio-path reclock (`DAC profile`, `ADC/microphones OFF`, `Path: MUSIC`) after every failed attempt and an HFP page starting 4 s into each A2DP page.
- Verdict: **FAIL** (defects D01, D02, D03 in docs/09).
- Action: fixes in `bt_core.c`, `bt_music.c`, `bt_call.c`, `audio_engine.c` (docs/05).

## T03: Reconnect fixes on the board

- Firmware: `-O2` build with the audit fixes.
- Procedure: flash (`docs/2026-10-27-final-flash.log` at that time), 75 s capture with phone D absent.
- Observed: `Reconnect attempt 1/6 ... 6/6`, `Last reconnect attempt; afterwards waiting for the phone`, then no further attempts; after every failure `Radio link ... status 0x104` + `Music (A2DP) attempt failed (phone not reachable)`, no reclock lines; at 69.7 s phone A connected HFP on its own: `Calls (HFP) connected to 04:ec:d8:68:0e:79`, features `0xf67`.
- Verdict: **PASS** for D01/D02/D03; first real HFP service-level connection.

## T04: English log strings

- Firmware: `831add3-dirty`.
- Procedure: flash, 12 s capture with reset.
- Observed: `READY: music (A2DP) + calls (HFP). Select 'blu2' in the phone's Bluetooth list`, `Radio: connectable, DISCOVERABLE (visible in the phone's list)`.
- Verdict: **PASS**.

## T05: Reconnect to the right phone

- Firmware: `9efee2c` (last connected phone in NVS, other bonds alternate, retries for ever).
- Procedure: flash, 40 s capture with reset; NVS had no "last peer" yet, phone A nearby, phone D absent.
- Observed:
  ```
  bt: 4 bonded phone(s); will try 30:94:35:07:5a:b8 (last connected) in 3 s, then the others in turn
  bt: Reconnect attempt 1 to last connected phone 30:94:35:07:5a:b8 (next in 10 s)
  bt: Radio link with 30:94:35:07:5a:b8: status 0x104 (page timeout)
  bt: Reconnect attempt 2 to other bonded phone 04:ec:d8:68:0e:79 (next in 10 s)
  bt: Radio link with 04:ec:d8:68:0e:79: status 0x0
  bt: Remembered 04:ec:d8:68:0e:79 as the phone to reconnect to
  bt: Music (A2DP) connected to 04:ec:d8:68:0e:79
  bt: Calls (HFP) connected to 04:ec:d8:68:0e:79
  ```
- Verdict: **PASS** (D06). A later boot (T07) shows `will try 04:ec:d8:68:0e:79 (last connected)` on attempt 1, connected within 1.5 s.

## T06: Supply guard idle baseline

- Firmware: `9efee2c-dirty` (ADC on GPIO36).
- Procedure: 32 s capture with reset, no music.
- Observed: `Guard: ... raw=4095`, stats every 5 s `supply raw=4095 quiet=4095 duck=1.00 sags=0`, no false sag.
- Verdict: PASS for "no false trigger", but see T09: the guard never detected a real collapse either. Removed later.

## T07: Always-discoverable and phone hand-over

- Firmware: `9efee2c-dirty` (always discoverable, newest phone wins, 5 s page timeout).
- Procedure: live monitor; phone B connected and streaming; phone C connected.
- Observed (`docs/test-data/2026-10-27-live-session.log`, lines 13-65):
  ```
  bt: Calls (HFP) connected to 2c:d0:66:b0:8c:74
  bt: Another phone 50:49:b0:90:c5:33 arrived while 2c:d0:66:b0:8c:74 is connected: switching to the new phone
  bt: Calls (HFP) disconnected from 2c:d0:66:b0:8c:74
  bt: Music (A2DP) disconnected from 2c:d0:66:b0:8c:74
  bt: Radio link closed by 2c:d0:66:b0:8c:74: reason 0x116
  bt: Calls (HFP) connected to 50:49:b0:90:c5:33      (1.4 s later)
  bt: Music (A2DP) connected to 50:49:b0:90:c5:33     (2.0 s later)
  ```
- Verdict: **PASS** for the hand-over mechanism. The "always discoverable" part was later reverted at the user's request (hidden while connected); the hand-over for already-paired phones stays.

## T08: Boot with the last connected phone present

- Firmware: `9efee2c-dirty`.
- Procedure: 45 s capture with reset, phone A present.
- Observed: `Radio: connectable, DISCOVERABLE` at 3.2 s, `Reconnect attempt 1 to last connected phone 04:ec:d8:68:0e:79`, ACL status 0x0 at 7.4 s, A2DP connected at 7.7 s, HFP at 12.5 s.
- Verdict: **PASS**.

## T09: High-volume music, supply-guard firmware (the failing case)

- Firmware: `9efee2c-dirty`.
- Procedure: live monitor running; the user played music from phone B/C and raised the phone volume to 90-100 % repeatedly.
- Observed: **eight EN-pin resets** (`rst:0x1 (POWERON_RESET)`, `reset reason=1 (power-on / EN pin)`) at live-log lines 109, 245, 359, 473, 586, 895, 1053, 1213. Pattern before each reset:
  ```
  I (7974) music: Music STARTED
  I (8164) audio: Music volume 90%
  ets Jul 29 2019 12:21:46          <- ROM banner = reset
  ```
  The last stats line before each reset still read `supply raw=4095 quiet=4095 duck=1.00 sags=0`; `out_peak` was 9,000-12,400 (about -11 to -8.4 dBFS) at P4 (-6 dB). No `POWER TRIP` line ever appeared after these resets (RTC flag lost). Chart: `docs/test-data/2026-10-27-live-session-peaks.svg`, resets 1-8 (dashed red lines at about 53, 72, 80, 88, 96, 290, 329, 387 s of continuous time).
- Verdict: **FAIL**. Root cause analysis in docs/07 (fourth iteration) and docs/09 D07-D09: the reset is the IMP809 supervisor, the supply guard cannot see the collapse in time, the RTC flag does not survive the reset.
- Action: supply guard removed; NVS flag; power self-test; music peak ceiling -6 dBFS.

## T10: Power self-test, first version (radio idle)

- Firmware: self-test build, tone -6 dBFS, 0.8 s per step, radio idle.
- Procedure: console `0` via `capture_serial.py --command 0`, 30 s capture. Phone B connected (not streaming).
- Observed: `self-test P0 ... P4` each 0.8 s apart, then `POWER SELF-TEST passed up to P4 (-6.0 dB)`. No reset.
- Verdict: test ran correctly, but **not representative** (see T12): it passed P4 while streaming at P4 still reset the board.

## T11: Power self-test, second version (radio busy)

- Firmware: `df773b5-dirty`, tone -3 dBFS, 1.5 s per step, Bluetooth inquiry running during the test.
- Procedure: console `0`, 40 s capture. Phone B connected; it even started streaming during the test (`Music STARTED` at 8.9 s, packets dropped, `Music SUSPENDED` at 14.2 s).
- Observed: steps P0..P4 at 7.25, 8.76, 10.26, 11.76, 13.26 s; `POWER SELF-TEST passed up to P4 (-6.0 dB)`; no reset.
- Verdict: ran correctly; still one step optimistic (T12).

## T12: High-volume music, self-test firmware, long run

- Firmware: `df773b5-dirty`, DAC profile P4 from T11.
- Procedure: live monitor restarted; the user played music from phone B at full phone volume.
- Observed (`docs/test-data/2026-10-27-live-session.log` from line 1556):
  1. One reset at P4 while streaming (live-log line 1556, continuous time about 574 s). Next boot:
     ```
     W (1043) audio: POWER TRIP: the board reset while the amplifier was on (reset reason 1). DAC profile lowered to P3 (-7.5 dB) and saved
     I (1043) audio: DAC profile P3 (-7.5 dB): limit found by the power self-test for this supply
     ```
  2. Phone B reconnected (A2DP 7.3 s, HFP 13.3 s), `Music STARTED` at 30.2 s.
  3. From 31 s to the end of the log (477 s uptime, **7.4 minutes** of continuous streaming, still running when the log was copied): every 5 s line `MUSIC 44100 Hz PA=1 in_peak≈18,300-20,800 (-5.1 to -3.9 dBFS) out_peak=16384 (-6.0 dBFS, the ceiling) limited=2,106-91,240 frames P3`, `drops=1 underruns=12`, `i2s_errors=0`. **No reset.**
- Charts: `docs/test-data/2026-10-27-live-session-peaks.svg` (the flat orange line at -6 dB after the last reset is the limiter ceiling; the blue line is the phone's PCM peak) and `docs/test-data/2026-10-27-live-session-limited.svg` (how many frames per 5 s the limiter held). Table: `docs/test-data/2026-10-27-live-session-table.csv`.
- User report: "ab theek hai" (fine for now).
- Verdict: **PASS** at P3 (-7.5 dB) with the -6 dBFS ceiling on this adapter: full phone volume, loud, no disconnect for the whole run. The fallback did what it was designed for (one reset, then stable), but the self-test should have landed on P3 directly.
- Action: `CAL_MARGIN_STEPS 1`: the self-test now saves (highest passed step - 1), i.e. P3 on this supply, so a fresh unit reaches the stable level without any reset in use.

## T13: Power self-test, final version (margin + brownout detector)

- Firmware: final (margin of one step, brownout detector at 2.80 V, only BROWNOUT counts as a trip). `build/config/sdkconfig.h`: `CONFIG_ESP_BROWNOUT_DET_LVL 7`.
- Procedure: console `0` (clears the saved level, runs the test), 40 s capture, record `docs/test-data/2026-10-27-T13-selftest-margin.log`. Phone C connected.
- Observed: `DAC profile reset to P2 and saved limit cleared; running the power self-test again`; steps P0..P4 at 12.9, 14.4, 15.9, 17.4, 18.9 s (1.5 s each); `POWER SELF-TEST passed up to P4 (-6.0 dB); saving P3 (-7.5 dB) with 1 step(s) of margin for real streaming.`; `DAC profile P3` applied. No reset.
- Verdict: **PASS**. A fresh unit on this supply now boots at P3, the level that T12 proved stable, without any reset in use. The brownout path itself (a collapse reading as `rst:0xf BROWNOUT`) has not been exercised yet because nothing tripped; it will show on the first real trip.

---

## Summary table

| Test | Firmware | Result | Led to |
|---|---|---|---|
| T01 release build | audit | PASS | - |
| T02 reconnect loop | original | FAIL | D01-D03 fixes |
| T03 reconnect fixes | audit | PASS | - |
| T04 English strings | 831add3 | PASS | - |
| T05 right phone | 9efee2c | PASS | - |
| T06 guard idle | 9efee2c-dirty | no false trigger | (guard later removed) |
| T07 hand-over | 9efee2c-dirty | PASS | discoverability changed to "only while free" on request |
| T08 boot reconnect | 9efee2c-dirty | PASS | - |
| T09 high volume | 9efee2c-dirty | FAIL, 8 resets | root cause, self-test design |
| T10 self-test v1 | self-test | ran, optimistic | radio load added |
| T11 self-test v2 | df773b5-dirty | ran, optimistic by 1 step | margin of 1 step |
| T12 long loud run | df773b5-dirty | PASS at P3, 7.4 min | margin of 1 step (D13), brownout rule (D16) |
| T13 self-test final | final | PASS, saves P3 directly | final configuration |

## Not tested yet

- A real phone call end to end (ring, answer, both directions, hang-up); the HFP service-level connection itself is proven (T03, T05, T07, T12).
- A real brownout being recognised (`rst:0xf`, `POWER TRIP` / self-test result) now that the detector sits above the supervisor; no trip has happened since the change.
- The self-test on a different adapter / cable, and on a unit with the auto-download jumpers removed (the official LyraT guide says to remove them after upload; with them installed the PC's DTR/RTS lines reach EN/IO0).
- Headphone output, AUX input, SD card: not used by this firmware.
