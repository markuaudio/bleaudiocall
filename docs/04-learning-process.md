# 04: Learning process — from zero to bleaudiocall (25–27 Oct 2026)

This file keeps the whole journey in one place: what was learned, what went wrong, how it was fixed. The detailed docs are linked. If someone new picks up this project, this is the file to read first.

## Timeline at a glance

| Day | What happened | Lesson |
|---|---|---|
| 25 Oct | ESP-IDF v6.1 set up on the PC, first "counter" firmware, board connection failed → auto-download jumpers → first successful run | Editor and compiler are separate things; a COM port showing up only proves the USB-UART bridge, not that the chip is running |
| 25 Oct | kanchan Bluetooth speaker: A2DP + ES8388 + I2S + touch, first result was **hiss only** | Register 0's reference-enable bit, then the ESP32 FIFO packing rule (data width = slot width) |
| 26 Oct | Songs played, but too quiet. Tried 10x, 100x, 20x, 60x digital gain; at 100x the board power-tripped into a reset loop | Digital gain did not raise loudness: raising gain without understanding the signal path is pointless |
| 26 Oct | **Root cause**: LOUT2/ROUT2 registers were at 0 dB → the signal cancelled at the NS4150 differential input. Fix: -45 dB (as in ADF), DAC -6 dB, digital 1x | The clue "the reference code was loud" was the most valuable one; A/B on the same hardware |
| 27 Oct | New project **bleaudiocall**: kanchan's verified audio + HFP calls + pairing/reconnect for every phone type. Build passed. Serial log revealed a reconnect-loop bug | Design the link manager's state machine on paper first, code second |
| 27 Oct | Final audit: loop bug fixed, overlapping page attempts fixed, ring-buffer wrap fixed, -O2 release build, docs completed | "Build passes" and "optimized" are different things |

## Phase 1: Toolchain (25 Oct) — [00](00-esp-idf-setup.md), kanchan project's docs 02/03

1. VS Code, Git and Python were there; ESP-IDF was not. Installed v6.1 + the esp32 target with the official Espressif Installation Manager (EIM), into <IDF folder> (the path must not contain spaces). Shared tools in `C:\Espressif\tools`.
2. The environment has to be activated in every terminal: `. .\tools\Enter-EspIdf.ps1`. The VS Code terminal profile does this automatically.
3. The first firmware only logged a counter. Build pass = it compiled; proof that it runs only comes from the serial log.
4. The board answered `No serial data received`. Cause: the LyraT auto-download jumpers JP23/24/25 were not fitted. Fitted them → flash worked → the counter ran. Remove the jumpers after upload: GPIO0 is also the I2S MCLK.
5. Chip: ESP32-D0WD-V3, flash **8 MB** (measured with esptool `flash-id`; config set to 8 MB).

## Phase 2: kanchan speaker (25–26 Oct) — the kanchan project's docs 04 to 13 (written in Hinglish)

What was built: our own ESP-IDF app (no ESP-ADF pipeline), A2DP sink + AVRCP, ES8388 register programming by hand, I2S master, 32 KiB ring buffer, audio task on core 1, touch pads.

Mistakes, in order:

1. **Hiss only.** ES8388 reg 0x00 was 0x12: reference-enable bit off. After reading datasheet section 6.1.1 it became 0x36. Still hiss.
2. **Silence + underruns.** 32-bit slots were configured but 16-bit packed data was sent. The ESP32 `i2s_ll.h` says data width and slot width must match (the FIFO packing mode is chosen from the slot width). Fix: PCM16 → MSB-aligned PCM32 (`* 65536`), codec reg 0x17 = 0x20 (32-bit). Then songs played.
3. **Too quiet.** Digital gain 10x → 100x → trip (PC USB port over-current, `phy_init` reset loop). Rolled back to 4x, 6x, 20x, 60x: at 20x out_peak hit 32767 (full scale) and it was still quiet. So the digital path was exhausted; the problem was analog.
4. **Root cause (26 Oct 16:45):** LyraT v4.3 schematic: the NS4150 PA has IN+ ← LOUT1, IN− ← LOUT2. We had LOUT2/ROUT2 (reg 0x30/0x31) at 0 dB = the same signal on both inputs = cancellation. ESP-ADF writes 0x00 (-45 dB) for exactly this reason. After the fix, "1x digital, DAC -6 dB" is already ADF-level loud.
5. **Research (14 agents):** NS4150 gain 20 dB; PA clean limit at codec -11..-12 dB; ADF's real LyraT maximum is DAC -6 dB, not 0 dB.

Lesson: understand the signal-path wiring before raising gain. One register wasted four experiments.

## Phase 3: bleaudiocall (27 Oct) — [01](01-project-overview.md), [02](02-connect-every-phone.md), [03](03-call-audio-path.md)

Problem: the speaker plays songs but cannot take a call; one old phone cannot connect at all.

Design decisions (why it was built this way):

| Decision | Why |
|---|---|
| New project, not a copy of kanchan | kanchan's `audio_output.c` + `bluetooth_speaker.c` were a monolith; calls need a mic path, a 44.1k↔8k/16k rate switch and source arbitration. Split into 8 small modules, one job each |
| One audio task owns the bus | ES8388 + I2S are one piece of hardware; music, call, ring are three sources. One task + an `epoch` counter = race-free switching |
| Radio first, codec second | Keeps RF calibration away from I2S noise, more heap at boot (learned in kanchan) |
| HFP voice over the HCI path | Without `CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y` the SCO data goes to the PCM pins, which are not wired on the LyraT; the firmware never sees PCM |
| I2S full duplex, both at 256×MCLK | On the original ESP32, TX and RX share one MCLK divider; the RX slave needs MCLK/BCLK ≥ 4: 256/(2×32) = 4, exactly at the limit |
| Always CONNECTABLE | kanchan went NON_CONNECTABLE on A2DP connect → when the phone came back to open HFP separately, the radio did not answer |
| Class of Device = AV / Hands-free | Old phones filter their device list by CoD |
| SSP "Just Works" + fixed PIN 0000 | Covers new and old phones |
| The link manager connects by itself | Old phones and already-bonded phones expect the accessory to start the connection |
| Name `blu2` (not kanchan) | The phone remembers "kanchan" as A2DP-only (SDP cache). A new name makes the phone read a fresh service list |

The build passed first time, 0 warnings in `main/`. Flashed and captured a serial log (`docs/serial-output.log`).

## Phase 4: Audit and final (27 Oct) — [05](05-final-audit-and-optimization.md)

Reading the serial log exposed a bug that code review had not:

```
I (123215) bt: Reconnect attempt 1/6 to bonded phone ...
I (133215) bt: Reconnect attempt 2/6 ...
I (144215) bt: Reconnect attempt 1/6 ...   <-- counter reset!
```

The phone was not nearby (page timeout 0x104). Every failure produces an A2DP "DISCONNECTED" event; our code read it as "link lost" and set `attempts = 0`. The 6-attempt limit never triggered; the board would page every 10 s forever (radio busy, battery/CPU wasted). On top of that, every failure reclocked the whole audio path, and the HFP page attempt started while the A2DP page was still in progress.

Lesson: **read the logs, not just the code.** The code "looked right"; two minutes of serial log showed three bugs.

The remaining audit findings and fixes are in [05](05-final-audit-and-optimization.md). The order for rebuilding from zero is in [06](06-complete-build-guide.md); the board schematic and BOM are in [hardware/](hardware/README.md).

## Working methods that paid off

1. With every change: backup, diff, build log, flash log, serial log in `docs/`, dated. Things like the reset loop were only caught through logs.
2. Serial console commands (`l r z b` tones, `] [` DAC steps, `i` status) = hardware experiments without a rebuild.
3. One stats line every 5 s (peaks, drops, underruns, i2s_errors): the way to turn "no sound" into "data is flowing but something is wrong in the analog path".
4. Research only once measurement closed the road (full scale at 20x yet still quiet → analog → schematic → cancellation).
5. Write "pending" for what is unknown, not "done". In this project the call path is still pending hardware verification.

## What is still pending (honest status)

- With a paired phone: music, incoming call ring, answer, audio in both directions, hang up — **none of it verified on the board**. In the serial log the phone never connected (not nearby / not forgotten and re-paired).
- Echo: speaker and mic on the same board, no AEC. The phone's echo canceller will help, but at high call volume the other side may hear an echo.
- `bt_core_connect_last` treats the last bond-list entry as the "newest"; Bluedroid does not guarantee the order. Makes no difference with a single phone.
