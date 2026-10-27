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

## Files touched

| File | Lines changed (approx.) |
|---|---|
| `main/bt_core.c` | +90: NVS helpers, `pick_preferred_target`, `pick_other_bond`, reconnect block, `connect_last`, `forget_all`, `bt_core_start` |
| `main/audio_engine.c` | +45: RTC flag, `load_boot_profile`, `dac_reset`, banner |
| `main/main.c` | +4: reset reason in words |
| `main/board_config.h` | +2 constants, comments |
| `README.md`, `docs/01`, `docs/02`, `docs/06`, `CHANGELOG.md` | behaviour and troubleshooting rows |

Commit: "Reconnect for ever to the last connected phone; power-trip protection".
