# 07: Reconnect and power-trip fixes (2026-10-27 evening)

Two problems reported after the final build was flashed:

1. `blu2` did not auto-connect to the phone.
2. At high volume the Bluetooth link dropped, even on a wall adapter. The user wants the speaker to stay connected until they disconnect it themselves.

This note records what was found, why it happened, what was changed, where, and how it was verified. Code references are to the files in `main/`.

---

## Problem 1: no auto-connect

### What the log showed

Boot log (`docs/2026-10-27-final-serial.log`, earlier capture):

```
bt: 4 bonded phone(s); will try 30:94:35:07:5a:b8 in 3 s
bt: Reconnect attempt 1/6 to bonded phone 30:94:35:07:5a:b8
bt: Radio link with 30:94:35:07:5a:b8: status 0x104 (page timeout)
...
bt: Reconnect attempt 6/6 ...
bt: Last reconnect attempt; afterwards waiting for the phone
```

Later in the same capture a different phone, `04:ec:d8:68:0e:79`, connected HFP on its own. That one is the user's phone.

### Why

- The firmware picked the reconnect target as the **last entry of the bond list** (`list[n-1]`). Bluedroid does not order that list by "most recent", so with 4 bonds it kept paging an old phone that was not even switched on.
- After 6 failed attempts it stopped for good (until reboot or console `c`). So even when the right phone came into range, the speaker was silent unless the phone initiated the connection.

### What was changed (`main/bt_core.c`, `main/board_config.h`)

| Change | Where | Why |
|---|---|---|
| Remember the last connected phone in NVS (`btlink/last`, 6-byte address) | `save_last_peer()` called from `bt_core_profile_link(up=true)`; `load_last_peer()`; `forget_last_peer()` from `bt_core_forget_all()` | Boot and `c` must try the phone the user actually uses, not an arbitrary bond. Written only when the address changes (no flash wear). |
| Boot target = last connected phone if still bonded, else newest bond | `pick_preferred_target()` used by `bt_core_start()` and `bt_core_connect_last()` | Replaces `list[n-1]`. |
| Alternate attempts: even attempts → preferred phone, odd attempts → one of the other bonds in round robin | `pick_other_bond()` + `link_task` reconnect block | A user who switched phones is still found; the preferred phone is still tried every second attempt. |
| Never give up: `LINK_RECONNECT_ATTEMPTS` (6) quick attempts every `LINK_RECONNECT_PERIOD_S` (10 s), then every `LINK_RECONNECT_SLOW_PERIOD_S` (30 s, new constant) for ever | `link_task`; constant in `board_config.h` | Headset behaviour: the speaker keeps looking until a phone answers. One page every 30 s costs nothing. Only SET-hold / `x` (`user_stopped`) stops it. |
| New log lines: `Reconnect attempt N to last connected|other bonded phone X (next in P s)`, `Phone not found after 6 quick attempts; keep trying every 30 s ...`, `Remembered X as the phone to reconnect to` | `link_task`, `save_last_peer()` | Makes the behaviour visible in the serial log. |

Unchanged on purpose: a failed attempt still does not count as "link lost" (`bt_core_profile_failed`, docs/05), the HFP follow-up only after the A2DP outcome, and page timeout 10 s.

### Verification (board, `docs/2026-10-27-final-serial.log`)

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

The first attempt still went to the stale "last connected" entry because NVS was empty on this boot; from now on `04:ec:d8:68:0e:79` is tried first.

---

## Problem 2: link drops at high volume

### Why (hardware, see `docs/hardware/BOM.md` section B5)

The two NS4150 class-D amplifiers run straight from the 5 V rail (`VBAT_For_Audio`). On bass peaks they pull well over 1 A. If the adapter or the cable cannot hold 5 V, the rail sags; the LD1117 LDO (≈1.1 V dropout) then loses regulation and the 3.3 V rail falls. Two things on the board reset the ESP32 when that happens:

- U10 **IMP809 supervisor** (2.63 V threshold) pulls EN low → `reset reason 1 (power-on / EN pin)`.
- The ESP32 **brownout detector** → `reset reason 9 (BROWNOUT)`.

A reset kills the Bluetooth link. The phone shows "disconnected"; the board reboots and reconnects a few seconds later. This is the same mechanism as the kanchan project's `phy_init` reset loop on a PC USB port (docs/04).

Firmware cannot measure the supply voltage on this board, but it can notice *afterwards* that it was reset while the amplifier was on, and it can control the only thing that sets the current: the output level.

### What was changed (`main/audio_engine.c`, `main/main.c`, `main/board_config.h`)

| Change | Where | Why |
|---|---|---|
| `rtc_pa_flag` in RTC memory (`RTC_NOINIT_ATTR`), set to a magic value when the PA is switched on and cleared when it is switched off | `pa()` helper | RTC memory survives EN-pin resets and brownouts (not a real power cycle), so the next boot knows the amplifier was on at the moment of the reset. |
| `load_boot_profile()` at the start of `audio_engine_init()`: read the saved DAC profile from NVS (`audio/dac`); if the flag was set **and** the reset reason is power-on/EN, brownout, power glitch or unknown → lower the profile by `POWER_TRIP_DAC_STEP` (1 step = 1.5 dB), save it, log `POWER TRIP: ... DAC profile lowered to Px` | `audio_engine.c` | Each trip makes the speaker 1.5 dB quieter at boot, permanently, until it no longer trips. The speaker settles at the loudest level the supply can hold and then stays connected. |
| `boot_profile` variable replaces the compile-time constant in the `]`/`[` log and the engine banner | `audio_engine.c` | The "reboot returns to Px" message is now true. |
| Console `0` resets the DAC to the compile-time default **and erases the learned limit** from NVS | `audio_engine_dac_reset()` | After upgrading the adapter/cable the user can try the full level again. |
| Reset reason logged in words at boot (`reset reason=9 (BROWNOUT)`) | `main.c` | Makes a power trip obvious in the log without looking up the enum. |
| New constant `POWER_TRIP_DAC_STEP 1` | `board_config.h` | Tunable. |

Why not simply ship a lower default? The clean limit of the NS4150 is around -11…-12 dB at the codec (docs/04); a fixed low default would make every unit quieter than its supply allows. The adaptive limit keeps full loudness on a good supply and only backs off on a weak one.

What this does **not** fix: a supply so weak that it trips even at P0 (-12 dB). The log then says `POWER TRIP at the lowest DAC profile: the supply or cable must be fixed`. A 5 V / 2 A adapter with a short, thick Micro-USB cable on the **POWER** port is the baseline.

### Verification

Build and flash passed (`docs/2026-10-27-final-flash.log`, 0 warnings in `main/`). Boot log shows `reset reason=1 (power-on / EN pin)` after the RTS reset from the flash tool and `Engine: ... DAC P4` with no `POWER TRIP` (the flag is not set on a tool reset because the PA was off). The high-volume trip itself has to be reproduced by the user with music playing; the expected log after the drop is:

```
boot: bleaudiocall: music + calls; reset reason=1 (power-on / EN pin)   <- or 9 (BROWNOUT)
audio: POWER TRIP: the board reset while the amplifier was on (reset reason 1). DAC profile lowered to P3 (-7.5 dB) and saved. ...
```

followed by an automatic reconnect to the phone within a few seconds.

---

## Second iteration: supply guard (find the safe level without any reset)

> **Outcome (same day): this did not work and was removed.** The live log showed the rail reading still at full scale right before every reset; see the fourth iteration below. Kept here as the record of what was tried.

The learned limit above only acts *after* a reset, and every reset means reboot, reconnect and the phone re-starting the stream. The user rightly called that "calibrating in front of the user". The second iteration measures the supply while playing and backs off before the supervisor can reset the chip.

### How the rail is measured on a board with no sense pin

The REC button line (GPIO36 / SENSOR_VP, ADC1 channel 0) is pulled up to the 3.3 V rail through R140 10 kΩ + R138 100 Ω (schematic, "Switch Keys"). The ESP32 ADC uses an internal bandgap reference, not VDD, so reading that pin tracks the 3.3 V rail itself. With 12 dB attenuation the ADC saturates near 3.1 V: a healthy rail reads near full scale (raw ≈ 4095), and the reading only starts to fall once the LDO has dropped out, which is exactly the region between "fine" and the IMP809 threshold (2.63 V). Pressing REC shorts the line to ground, which looks like a deep sag: a built-in way to test the ducking without a weak supply.

### What was changed

| Change | Where | Why |
|---|---|---|
| New module `supply.c/.h`: ADC1 oneshot on GPIO36, 12 dB attenuation, 4-sample average | `main/supply.c`, `main/CMakeLists.txt` (`supply.c`, `esp_adc`) | One call per audio block (about 15 us each) is cheap. |
| Quiet baseline: while the PA is off the task tracks the highest reading | audio task, `supply_base` | The threshold is relative, so it works whether the rail reads 4095 (saturated) or less. |
| Sag = reading more than `SUPPLY_SAG_COUNTS` (200) below the baseline | audio task | Roughly the first 0.1 to 0.2 V of rail drop, well before the 2.63 V reset point. |
| Duck: while a sag lasts, the output gain is multiplied by 0.87 per block (about 6 dB per 50 ms) down to `SUPPLY_DUCK_MIN` (0.25 = -12 dB); afterwards it recovers linearly over `SUPPLY_RECOVER_S` (3 s) | music `target`, call `level`, tones | Cuts the amplifier current within one or two blocks, so the rail recovers instead of collapsing. Recovery is slow enough not to pump. |
| Repeated sags (`SUPPLY_SAG_LIMIT` 4 within `SUPPLY_SAG_WINDOW_S` 30 s) lower the DAC profile one step **while playing** and save it (`save_dac_profile`) | audio task; `audio_engine.c` | The speaker converges on the loudest level the supply holds without a single disconnect. A brief reclock click is the only side effect. |
| Stats line every 5 s: `supply raw=... quiet=... duck=... sags=...` | audio task | Shows the rail reading and how often it sagged. |
| `POWER_TRIP_DAC_STEP` raised to 2 (3 dB) | `board_config.h` | The reset path is now the fallback only; if it ever triggers it should converge in one step. |

Expected behaviour on a weak supply: at a loud bass passage the log shows `Supply sag: rail raw 3850 (quiet 4095) with PA on; ducking output`, the volume dips for a moment, no reset, no disconnect. If that repeats four times in half a minute: `Supply sags repeatedly at P4: DAC profile lowered to P3 (-7.5 dB) and saved`. From then on the boot level is P3 and the sags stop (or the next step follows). `0` on the console clears the saved level after a better adapter is connected.

Bench check without a phone: start a tone (`l`), press REC: the tone ducks and the log reports the sag; release REC: it comes back within 3 s. Pressing REC four times within 30 s lowers the profile one step (restore with `0`).

---

## Third iteration: a second phone must connect without "refreshing", new phones must find the speaker

Reported: a second device only connected after refreshing the phone's Bluetooth list, and a brand-new Android phone (Redmi) could not connect at all.

### Why

- **Hidden while connected.** The speaker was discoverable only when no phone was connected (`hidden (already in use)` in the log). A second phone could not see it until the first one was gone.
- **The speaker's own paging starved the scans.** Reconnect attempts ran every 10 s with a 10 s page timeout, so the radio was paging almost all the time. While the controller pages, it skips inquiry-scan and page-scan windows, so a new phone's scan does not see `blu2` and its connection attempt times out. That matches "refresh" and "the new phone cannot connect".
- **No hand-over.** If a second phone did reach the speaker while the first was connected, the stack rejected its A2DP/HFP links (one phone at a time) and nothing in the firmware switched over.
- Checked and ruled out: the Bluedroid device-record limit (`BTM_SEC_MAX_DEVICE_RECORDS` = `BT_SMP_MAX_BONDS` 15 + `BT_ACL_CONNECTIONS` 4 = 19) is far above the 4 stored bonds.

### What was changed (`main/bt_core.c`, `main/board_config.h`)

| Change | Where | Why |
|---|---|---|
| Always **connectable + general discoverable**, also while a phone is connected | `set_scan_mode(true)` from `link_task`, log `DISCOVERABLE (always visible ...)` | A second phone sees `blu2` in its list at any time, no refresh. |
| **Newest phone wins**: when a radio link (ACL) from a different address succeeds while a phone is connected, the current phone's HFP and A2DP are disconnected, the newcomer becomes `peer`/`target`, and the follow-up opens our profiles to it 2 s later; reconnect paging is paused for 15 s | `ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT` handler | Whoever actively connects or pairs gets the speaker, like most consumer speakers. |
| A disconnect of a phone that is no longer the current peer does not clear the follow-up or restart reconnects | `bt_core_profile_link(up=false)` (`same_phone` check) | Otherwise the old phone's disconnect event would cancel the switch to the new one. |
| Lower paging duty: `LINK_RECONNECT_ATTEMPTS` 4, `LINK_RECONNECT_SLOW_PERIOD_S` 45, new `LINK_PAGE_TIMEOUT 0x2000` (5.12 s per attempt instead of 10 s) | `board_config.h`, `bt_core_start()` | Paging now occupies roughly 10 % of the time instead of nearly 100 %, so inquiry/page scans get through and new phones connect at the first try. |
| SET hold while nothing is connected = pairing mode | existing `bt_core_disconnect()` (`user_stopped`) | Stops the speaker's own paging completely until `c` or reboot; the speaker is then free for a new phone. |

### What to expect

- Phone A connected, phone B opens Bluetooth: `blu2` is listed immediately. Tapping it: log `Another phone B arrived while A is connected: switching to the new phone`, then `Music (A2DP) disconnected from A`, `Calls (HFP) disconnected from A`, and within a few seconds `Music (A2DP) connected to B`, `Calls (HFP) connected to B`. Phone A shows "disconnected".
- A bonded phone that reconnects on its own (for example an old phone coming home) also takes over. If that is unwanted, forget `blu2` on that phone.
- New phone pairing: pair from the phone as usual (Just Works, or PIN 0000 on old phones). No "refresh" needed; if it does not appear, hold SET for 2 s (pairing mode) and scan again.

---

## Fourth iteration: what the live log proved, and the final design (power self-test)

With the live monitor running, the user turned the phone up to 90-100 % five times in a row. `docs/live-serial.log` (local, not committed) showed the same sequence every time:

```
I (7974) music: Music STARTED
I (8164) audio: Music volume 90%
ets Jul 29 2019 12:21:46
rst:0x1 (POWERON_RESET),boot:0x1f (SPI_FAST_FLASH_BOOT)
...
I (524) boot: bleaudiocall: music + calls; reset reason=1 (power-on / EN pin)
```

Facts from those five resets:

1. **Every "disconnect" is an EN-pin reset by the IMP809 supervisor** (3.3 V rail below 2.63 V), 1-2 s after music starts at high phone volume. Never a Bluetooth-level disconnect.
2. **The supply guard never saw anything.** The last stats line before each reset still read `supply raw=4095 quiet=4095 duck=1.00 sags=0`. The 3.3 V rail stays at full scale until the 5 V input has fallen below the LDO dropout, and from there to 2.63 V takes well under a millisecond (about 110 uF on the rail, several hundred mA of load). Software sampling every 5-10 ms cannot react in time, and even a faster sampler would still be behind the 46 ms of audio already queued in DMA. The approach was wrong and was **removed** (`supply.c`, the ADC dependency and all `SUPPLY_*` constants are gone).
3. **The RTC-memory flag did not survive the reset**: no `POWER TRIP` line after any of the five resets. On this board the EN-pin reset is reported as `ESP_RST_POWERON` and the RTC slow memory contents were not usable. The flag now lives in **NVS** (`audio/paon`, written only when the amplifier state changes).
4. **The trips happened at a modest level.** The last stats before the resets showed `out_peak` around 12000 (about -8.7 dBFS) at P4 (-6 dB), i.e. a codec output near -15 dB, roughly 0.5 W per channel. That the supply still collapsed is explained by the board's own power path (schematic, "Power System"): USB POWER -> **D3 1N5819** (0.4-0.6 V drop at 1-2 A, rated 1 A) -> Q2 -> switch -> ferrites -> NS4150 PVDD and -> **LD1117** (dropout about 1.1 V). A 5.0 V adapter with a thin cable therefore delivers about 4.0-4.2 V to the board under amplifier load, the LDO drops out, the 3.3 V rail falls, and the supervisor resets the chip. The earlier research (kanchan docs 13) put the PA's clean limit at a codec level of -11 to -12 dB on such a rail; P4 is 5-6 dB above that.

Software cannot add voltage. What it can do is keep the output inside what *this* supply sustains, and find that limit once, deliberately, without a phone and without disconnects in normal use.

### Final design

| Change | Where | Why |
|---|---|---|
| **Power self-test** on the first boot after flashing (and after console `0` or `k`): a 60 Hz tone at **-3 dBFS** (`CAL_TONE_HZ`, `CAL_TONE_AMPLITUDE`) on both speakers, **1.5 s** (`CAL_STEP_MS`) at each DAC profile P0 -> P4 (`CAL_MAX_PROFILE`), **with the Bluetooth radio running an inquiry** the whole time (`bt_core_radio_load`). Before each step the step number is written to NVS (`audio/calstep`). | `power_self_test()` in `audio_engine.c`, run from the audio task once `bt_core_ready()` | 60 Hz bass is the worst case for amplifier current; the inquiry makes the ESP32 side draw about what it draws while streaming; 3 dB above the music ceiling is the margin. Verified the same day: a test with the radio idle at -6 dBFS passed P4 while streaming at P4 still reset the board, so the idle test was not representative. If the supply collapses, the supervisor resets the board during that step. |
| On the next boot, `calstep` present + a power-type reset reason -> the previous step is saved as the limit (`audio/dac`), the test is marked done (`audio/cal` = 1). If the test completes, P4 is saved. | `load_boot_profile()` | One deliberate reset at most, once, before any phone is involved. From then on the speaker boots at the loudest level this supply holds. |
| **Music peak ceiling -6 dBFS** (`MUSIC_PCM_PEAK_LIMIT 16384`) held by the existing limiter (instant attack, 150 ms release) | `board_config.h`, `scale_frame()` | 3 dB below the test tone. Any music is therefore easier on the supply than the test that passed, so a passed level should not trip in use. The headroom is given back by the higher DAC profile the test allows. |
| Uncalibrated default P2 (-9 dB) (`AUDIO_BOOT_DAC_PROFILE`), only used until the test has run | `board_config.h` | Conservative starting point, matches the measured clip point. |
| Fallback: reset while the amplifier was on (`audio/paon` = 1 in NVS) -> saved profile lowered by `POWER_TRIP_DAC_STEP` (1) | `load_boot_profile()`, `save_pa_flag()` | Covers a changed adapter or cable without re-running the test. |
| Console `0` = default profile + forget the saved limit + run the self-test now; `k` = run the self-test now | `audio_engine_dac_reset()`, `audio_engine_calibrate()`, `main.c` | After switching to a better adapter, `0` finds the new, higher limit. |
| Discoverable **only while no phone is connected** (user request); a bonded phone can still connect and takes over (newest phone wins) | `bt_core.c` `set_scan_mode` / `link_task` | Hidden while in use; visible to everyone as soon as it is free. |

### What the user sees

- First boot after flashing (as soon as Bluetooth is initialised, about 3 s): about 8 seconds of a low 60 Hz tone, rising in steps, from both speakers. If the supply is weak the board resets once during the tone and comes back quieter: `POWER SELF-TEST result: the board reset at P2, so P1 (-10.5 dB) is the loudest level this supply holds. Saved.` If it passes: `POWER SELF-TEST passed up to P4 (-6.0 dB)`.
- Afterwards: music at any phone volume stays connected. Loud passages are held at the -6 dBFS ceiling by the limiter.
- Better adapter later (5 V / 2 A, short thick cable, on the **POWER** port): type `0` in the monitor, the test runs again and the speaker gets louder if the supply allows it.
- Speakers must be connected during the test; with no load the test passes trivially.

### Verification

Build and flash passed. The live log after flashing shows the self-test running (`POWER SELF-TEST: 60 Hz tone ...`, `self-test P0 ... P4`) and its saved result; see the live-log excerpt in the CHANGELOG row for this change. The high-volume music test on the user's adapter is the final check.

---

## Files touched

| File | Lines changed (approx.) |
|---|---|
| `main/bt_core.c` | +90: NVS helpers, `pick_preferred_target`, `pick_other_bond`, reconnect block, `connect_last`, `forget_all`, `bt_core_start` |
| `main/audio_engine.c` | +45: RTC flag, `load_boot_profile`, `dac_reset`, banner |
| `main/main.c` | +4: reset reason in words |
| `main/board_config.h` | +8 constants, comments |
| `main/supply.c`, `main/supply.h` | added in iteration 2, **removed** in iteration 4 (did not work, see above) |
| `README.md`, `docs/01`, `docs/02`, `docs/06`, `CHANGELOG.md` | behaviour and troubleshooting rows |

Commit: "Reconnect for ever to the last connected phone; power-trip protection".
