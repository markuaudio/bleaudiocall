# 09: Defect register, what was wrong and what was fixed

One entry per problem found on 2026-10-27, in the order they were found. Each entry says what the symptom was, how it was proven, why it happened, what was changed and where, how the fix was verified, and what is still open. Test ids refer to docs/08. Written so that someone building this hardware and firmware can follow every decision.

| Id | Problem | Status |
|---|---|---|
| D01 | Reconnect retry counter reset by our own failed attempt: infinite 10 s paging loop | fixed, verified T03 |
| D02 | HFP page attempt started while the A2DP page was still running | fixed, verified T03 |
| D03 | Whole audio path reclocked after every failed connection attempt | fixed, verified T03 |
| D04 | Byte ring buffers returned short reads at the wrap point: false underruns, 0-byte mic frames | fixed (code review), call test pending |
| D05 | Firmware built with debug optimisation (-Og) | fixed, verified T01 |
| D06 | Wrong reconnect target (last bond-list entry), and the speaker gave up after 6 attempts | fixed, verified T05, T08 |
| D07 | Board resets (EN pin) at high music volume: "Bluetooth disconnects" | mitigated to stable, verified T12; hardware limit documented |
| D08 | ADC "supply guard" could not detect the collapse | wrong approach, removed |
| D09 | RTC-memory flag did not survive the EN-pin reset: resets not recognised | fixed (NVS), verified T12 |
| D10 | Second phone needed a Bluetooth refresh; new phone could not connect | fixed (paging duty, hand-over), verified T07 |
| D11 | Toolchain path with spaces broke the ESP-IDF activation | fixed |
| D12 | Public repository contained Hinglish text and the author's local paths | fixed |
| D13 | Power self-test one step optimistic (passed P4, streaming at P4 reset once) | fixed (1-step margin), to be verified on the next fresh unit |
| D14 | Right onboard microphone coupling capacitor C50 is "NC" on the schematic | open, observation |
| D15 | `gpio: conflict found for GPIO[21]` warning at boot | open, cosmetic |
| D16 | Normal power-off or a flash-tool reset while playing was counted as a power trip (saved level lowered P3 -> P2) | fixed (brownout detector at 2.80 V, only BROWNOUT counts), verified T13 |

---

## D01: Reconnect counter reset by our own failed attempt

- **Symptom:** with the bonded phone absent, the log showed `Reconnect attempt 1/6 ... 2/6 ... 1/6 ...` for ever (T02).
- **Cause:** Bluedroid reports a failed outgoing A2DP connection as `ESP_A2D_CONNECTION_STATE_DISCONNECTED`, the same event as a lost link. `bt_music.c` forwarded both to `bt_core_profile_link(PROFILE_A2DP, false)`, which treated it as "the phone dropped us" and reset the attempt counter to 0.
- **Fix:** `bt_music.c` remembers whether the link had been up (`was`); a DISCONNECTED without a prior connection calls the new `bt_core_profile_failed(PROFILE_A2DP)` instead. `bt_call.c` does the same for HFP. `bt_core_profile_failed()` never touches the counter.
- **Files:** `main/bt_music.c`, `main/bt_call.c`, `main/bt_core.c`, `main/bt_core.h`.
- **Verified:** T03 (`1/6 ... 6/6`, then idle). Later superseded by D06 (retries for ever) but the counter logic is the same.

## D02: Overlapping page attempts

- **Symptom:** `Opening calls (HFP) ... ourselves` 4 s after `Opening music (A2DP) ... ourselves`, while the A2DP page (10 s timeout) was still running (T02).
- **Cause:** the reconnect code armed the HFP follow-up timer unconditionally 4 s after starting the A2DP attempt.
- **Fix:** the follow-up timer is cleared when an attempt starts and re-armed only by the outcome: `bt_core_profile_link(up)` (connected: open the other profile 4 s later) or `bt_core_profile_failed()` (failed: try the other profile at once only if the phone answered on the radio, `acl_ok`; on a page timeout, wait for the next period).
- **Files:** `main/bt_core.c`.
- **Verified:** T03, T05: after each `status 0x104` only `attempt failed (phone not reachable)`, no second page.

## D03: Audio path reclocked after every failed attempt

- **Symptom:** every 10 s the log showed `DAC profile P4`, `ADC/microphones OFF`, `TX clocks`, `Path: MUSIC` without any music (T02).
- **Cause:** `audio_engine_music_stream(false)` incremented the epoch unconditionally, and it was called from the A2DP DISCONNECTED event, including the failed-attempt case.
- **Fix:** `audio_engine_music_stream()` and `audio_engine_call_stop()` only bump the epoch when the state actually changes.
- **Files:** `main/audio_engine.c`.
- **Verified:** T03 (no reclock lines after failures).

## D04: Byte ring buffer short reads

- **Symptom (by analysis):** FreeRTOS `RINGBUF_TYPE_BYTEBUF` hands back data in two pieces at the wrap point; `xRingbufferReceiveUpTo` can return less than requested although enough data is queued. The call path treated that as an underrun (speaker side) or returned 0 bytes to the HFP stack (mic side, which discards partial reads). With 6144-byte rings and 160 bytes per 10 ms, a wrap every ~380 ms would have produced a gap every ~380 ms in both directions.
- **Fix:** `ring_read_exact()` checks the queued amount first and assembles the two pieces; callers get the whole block or nothing.
- **Files:** `main/audio_engine.c`.
- **Verified:** build only; the end-to-end call test is still pending (docs/08, "Not tested yet").

## D05: Debug optimisation

- **Symptom:** `CONFIG_COMPILER_OPTIMIZATION_DEBUG` (-Og) in `sdkconfig`; the audio task runs floating-point limiter maths per sample.
- **Fix:** `CONFIG_COMPILER_OPTIMIZATION_PERF=y` in `sdkconfig.defaults`, `sdkconfig` regenerated and the diff checked (only the intended lines changed). Also `CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED=n` (unused).
- **Verified:** T01 (`-O2` in compile_commands, binary 40 KB smaller).

## D06: Wrong reconnect target, gives up

- **Symptom:** the speaker paged `30:94:35:07:5a:b8` (an old bond, phone absent) six times after every boot and then stopped; the user's phone was never tried (T03 log, T05 first attempt).
- **Cause:** the target was the last entry of Bluedroid's bond list, which is not ordered by recency; and the retry loop had a hard limit of 6 attempts.
- **Fix:** the last connected phone is stored in NVS (`btlink/last`) and tried first; every other attempt goes to one of the other bonds in turn; attempts continue for ever (4 quick at 10 s, then every 45 s, 5 s page timeout) until a phone answers or the user disconnects.
- **Files:** `main/bt_core.c`, `main/board_config.h`.
- **Verified:** T05 (attempt 2 found the right phone, remembered it), T08 (attempt 1 connected in 1.5 s).

## D07: Board resets at high music volume

- **Symptom:** "Bluetooth disconnects at high volume, even on an adapter". Live log (T09): eight `rst:0x1 (POWERON_RESET)` banners, each 1-2 s after `Music STARTED` + `Music volume 90-100%`.
- **Proof of mechanism:** ROM reset code 0x1 = chip reset through the EN pin (brownout would print `rst:0xf`). The only things on EN are the IMP809 supervisor U10 (threshold 2.63 V on the 3.3 V rail) and the auto-download transistor Q9 (jumper JP23).
- **Why the rail collapses (schematic, "Power System"):** USB POWER J1 -> D3 1N5819 Schottky (1 A rated, 0.4-0.6 V drop at 1-2 A) -> Q2 -> switch J3 -> VCC_5V -> ferrites -> NS4150 PVDD (two amplifiers, up to 3 W each into 4 Ω) and -> L11 -> U3 LD1117 (dropout about 1.1 V) -> 3.3 V rail -> ESP32 + CP2102N + supervisor. With a 5.0 V adapter and an ordinary cable the board sees about 4.0-4.3 V under amplifier load; the LDO drops out, the 3.3 V rail follows the input down, and at 2.63 V the supervisor resets the chip. The kanchan research (its docs 13) had already placed the amplifier's clean limit at a codec level of -11 to -12 dB on such a rail; the firmware was running at -6 dB (P4).
- **What did not work:** D08 (ADC guard), the RTC flag (D09), and a self-test with the radio idle (T10) or busy (T11): both passed P4, yet streaming at P4 reset the board once more (T12). The difference between the test and real streaming is not fully explained (the test tone is louder than the music that tripped); candidates are the extra ESP32-side current while decoding and streaming, and the EN line being exposed to the CP2102N through the installed JP23 jumper.
- **What works (T12):** after the one remaining reset the NVS fallback lowered the DAC profile to P3 (-7.5 dB); with the music peak ceiling at -6 dBFS (`MUSIC_PCM_PEAK_LIMIT 16384`, held by the limiter) the board then streamed at full phone volume for 7.4 minutes without a reset.
- **Final firmware behaviour:** power self-test on the first boot (60 Hz, -3 dBFS, 1.5 s per step, radio busy, P0..P4), result saved with one step of margin (D13), so a new unit on this kind of supply boots at P3 without any reset in use; NVS power-trip fallback lowers one more step if a reset ever happens with the amplifier on; `0` re-runs the test after a supply change.
- **Files:** `main/audio_engine.c` (`power_self_test`, `load_boot_profile`, `save_pa_flag`), `main/bt_core.c` (`bt_core_radio_load`, `bt_core_ready`), `main/board_config.h` (`CAL_*`, `MUSIC_PCM_PEAK_LIMIT`, `POWER_TRIP_DAC_STEP`), `main/main.c` (reset reason in words, console `k`).
- **For hardware builders (not tested, suggestions):** the limit is the D3 + LD1117 chain, not the amplifiers. Options: (1) feed 5 V through the battery connector path or bridge D3 with a lower-drop Schottky (SS34 class) so VCC_5V stays near 5 V; (2) a 5 V / 2 A adapter with a short, thick cable on the POWER port; (3) more bulk capacitance on VCC_5V near the amplifiers; (4) remove JP23/JP24/JP25 in normal use as the official guide says. Any of these should let the self-test pass higher; re-run it with `0` afterwards.

## D08: ADC supply guard (removed)

- **Idea:** read the 3.3 V rail through the REC button line (GPIO36, pulled up to the rail) with ADC1, duck the output when it sags.
- **Why it failed (T09):** the reading stayed at full scale (4095) until the moment of reset. The rail does not sag gradually: it holds until the 5 V input is below the LDO dropout, then falls to the 2.63 V trip point in well under a millisecond (about 110 uF on the rail, several hundred mA load), far faster than the 5-10 ms audio block and the 46 ms of audio already queued in DMA.
- **Action:** `supply.c/.h`, the `esp_adc` dependency and the `SUPPLY_*` constants were removed; the attempt is recorded in docs/07 (second iteration).

## D09: RTC flag lost on EN-pin reset

- **Symptom:** no `POWER TRIP` line after any of the eight resets in T09 although the firmware set an `RTC_NOINIT_ATTR` flag whenever the amplifier was on.
- **Cause:** on this board the EN-pin reset is reported as `ESP_RST_POWERON` and the RTC slow-memory contents were not usable afterwards (whether cleared by the reset or by the rail collapse was not investigated further; the effect is the same).
- **Fix:** the flag is in NVS (`audio/paon`), written only when the amplifier state changes (`save_pa_flag`), cleared at boot after it has been read.
- **Verified:** T12 (`POWER TRIP: ... lowered to P3` after the reset at P4).

## D10: Second phone needed a refresh; new phone could not connect

- **Symptom:** a second phone only connected after refreshing its Bluetooth list; a new phone could not connect.
- **Causes:** (1) the speaker was non-discoverable while a phone was connected; (2) the speaker's own reconnect paging (10 s page timeout every 10 s) kept the radio busy, starving inquiry-scan and page-scan windows, so new phones neither saw nor reached it; (3) no hand-over: a second phone's A2DP/HFP was rejected.
- **Checked and ruled out:** the Bluedroid device-record limit (`BT_SMP_MAX_BONDS` 15 + `BT_ACL_CONNECTIONS` 4 = 19 records) with 4 bonds.
- **Fix:** page timeout 5 s, 4 quick attempts then every 45 s; newest phone wins (an ACL from a different phone while connected disconnects the current one and the follow-up opens our profiles to the newcomer); a disconnect of a phone that is no longer the current peer no longer cancels that follow-up. Discoverability: first "always", then, at the user's request, only while no phone is connected (an already-paired phone can still connect and take over; a new phone pairs when the speaker is free, or after SET hold = pairing mode).
- **Files:** `main/bt_core.c`, `main/board_config.h`.
- **Verified:** T07 (hand-over in 2 s).

## D11: Toolchain path with spaces

- **Symptom:** build failed with `CMake version file not found` after the user renamed the folder that held the ESP-IDF checkout; the new path contained spaces, which ESP-IDF on Windows does not support.
- **Fix:** the toolchain checkout was moved to a space-free folder; the EIM activation script, `eim_idf.json` and the project's VS Code settings were updated; `.vscode/settings.json` no longer pins an absolute path, and `tools/Enter-EspIdf.ps1` / `tools/Monitor.ps1` read `IDF_TOOLS_PATH`.
- **Verified:** T01 and every later build.

## D12: Hinglish and local paths in the public repository

- **Fix:** all documentation, README, CHANGELOG, BOM and five firmware log strings rewritten in English; the author's drive and user paths replaced with placeholders; Hinglish originals kept only in the gitignored local folder `docs-hinglish/`.
- **Verified:** repository-wide search for Hinglish words and personal paths is empty; broken-link check is empty.

## D13: Self-test one step optimistic

- **Symptom:** T11 passed P4 with a tone louder than the music, yet streaming at P4 reset the board once (T12); P3 then ran stable for 7.4 minutes.
- **Fix:** `CAL_MARGIN_STEPS 1`: the saved result is the highest passed step minus one; if the test itself trips at step N, the saved result is N - 1 - margin.
- **Verified:** build only; a fresh unit (or `0` on this one after a supply change) will show `saving P3 (-7.5 dB) with 1 step(s) of margin`.

## D14: Right microphone capacitor NC (observation)

- The schematic shows C50 (RIN1 coupling capacitor) as "NC". The firmware averages both microphones; if the right one is silent the call microphone level is half of what the code assumes. If `mic_peak` stays low in a call, raise `MIC_DIGITAL_GAIN` or use the left channel only. Not tested (no call test yet).

## D16: False power trip on a normal reset

- **Symptom:** after the margin build was flashed while the phone was streaming, the next boot printed `DAC profile P2` although T12 had settled on P3. The NVS flag `paon` was 1 (amplifier on at the moment of the flash tool's reset), the reset reason was `POWERON`, so `load_boot_profile()` treated it as a supply collapse and lowered the level. The same would happen to a user who switches the speaker off (or presses RST) while music plays: every such power cycle would make the speaker 1.5 dB quieter.
- **Cause:** on the ESP32 an EN-pin reset (supervisor), the power switch and the flash tool all produce the same reset reason, `POWERON`. The reason alone cannot identify a supply collapse.
- **Fix:** the ESP32's own brownout detector is set to its highest level, 2.80 V (`CONFIG_ESP_BROWNOUT_DET_LVL_SEL_7` in `sdkconfig.defaults`), which is **above** the IMP809 supervisor's 2.63 V. A collapsing rail now trips the brownout detector first, which resets the chip with reason `BROWNOUT` (ROM banner `rst:0xf`). `load_boot_profile()` counts only `ESP_RST_BROWNOUT` as a trip, for both the self-test (`calstep`) and the fallback (`paon`). A `POWERON` reset during the self-test simply re-runs the test; a `POWERON` with `paon` = 1 is ignored.
- **Files:** `sdkconfig.defaults`, `sdkconfig`, `main/audio_engine.c`, `main/board_config.h`.
- **Verified:** T13 (docs/08).

## D15: GPIO21 conflict warning (cosmetic)

- `main.c` and `audio_engine_init()` both configure the PA enable pin; the second call logs `gpio: conflict found for GPIO[21]`. Harmless; remove the second `gpio_config` if the warning bothers you.
