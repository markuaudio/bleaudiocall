# 01: bleaudiocall project overview

Date: 2026-10-27. Status: final build `docs/2026-10-27-final-build.log` (release -O2), phone test on the board pending. Audit and fixes: [05](05-final-audit-and-optimization.md).

## Problem

The kanchan speaker (the earlier speaker project, its old Bluetooth name was "kanchan") only played music. What was needed: the same board, but whichever phone connects should also **have its calls answered** (ring, answer, talk, hang up), and **an old phone that would not connect should connect too**.

## What was built

A new ESP-IDF project that reuses kanchan's verified audio settings (ES8388 register sequence, LOUT2 -45 dB cancellation fix, DAC -6 dB, 32-bit I2S slots) and adds HFP plus a "link manager" on top.

```
main/
  main.c            boot order, touch-key routing (call > music), serial console
  board_config.h    LyraT pins + every tunable number (volumes, mic gain, retry counts)
  es8388.c/.h       codec: playback path + record path (mics), DAC profiles, ADC power
  audio_io.c/.h     I2S0 full duplex (TX speaker, RX mics), rate switch 44.1k <-> 8k/16k
  audio_engine.c/.h one task that owns the speaker/mic bus: MUSIC / CALL / RING / test tones
  bt_core.c/.h      radio + host, GAP (name, class of device, pairing of every kind), link manager
  bt_music.c/.h     A2DP sink + AVRCP (taken from kanchan)
  bt_call.c/.h      HFP client: call state, answer/hangup/redial, voice link, volume sync
  touch_buttons.c   LyraT touch pads -> app_key()
```

## Boot order (main.c)

1. LED/PA GPIO low, NVS init (bonds are stored here).
2. `bt_core_radio_init()`: controller (BR/EDR only) + Bluedroid with SSP. Radio first, codec later (RF calibration isolated, more heap).
3. `audio_engine_init()`: ES8388 init (ADC off), ring buffers, I2S duplex at 44.1 kHz, audio task on core 1 at priority 6.
4. `touch_buttons_init()`.
5. `bt_core_start()`: name "blu2" (`BT_DEVICE_NAME`; the old "kanchan" name is cached as A2DP-only in the phone's SDP cache), CoD hands-free, IO cap NONE, fixed PIN 0000, page timeout 10 s, A2DP + AVRCP + HFP init, connect to the bonded phone by ourselves after 3 s, link task.
6. Console task.

## Audio engine: one task, three sources

| Source | When | Bus rate | What happens |
|---|---|---|---|
| MUSIC | A2DP stream | 44.1/48 kHz stereo | like kanchan: prefill, 1x gain, limiter only as overflow guard, PA on only when there is data |
| CALL | HFP voice link (SCO) up | 8 kHz (CVSD) or 16 kHz (mSBC) | phone PCM mono -> both channels; mics (L+R)/2 x 2.0 -> phone; 10 ms blocks, both directions in lock step |
| RING | incoming call, phone sends no in-band ring | music rate | 425+480 Hz dual tone, 1 s on / 2 s off, at music volume |

When the source/rate/DAC profile changes, `epoch` increments: PA off, DAC mute, profile applied, ADC on/off, both I2S channels disabled -> reclocked -> silence preloaded -> enabled, stale buffers flushed.

## Numbers you can tune (board_config.h)

- `CALL_BOOT_VOLUME 10` (0..15), changed by the phone via +VGS.
- `MIC_PGA_REG 0x88` (+24 dB analog), `MIC_DIGITAL_GAIN 2.0f`. If the other side hears you too quietly, raise the digital gain to 3.0 first, then the PGA.
- `AUDIO_BOOT_DAC_PROFILE 4` (-6 dB, ADF maximum). Test live with `] [`.
- `LINK_RECONNECT_ATTEMPTS 6`, `LINK_RECONNECT_PERIOD_S 10`, `LINK_PROFILE_FOLLOWUP_S 4`.

## Limitations worth knowing

- **Echo**: speaker and microphones are on the same board; there is no acoustic echo canceller (ESP-ADF's DSP algorithms are not present here). The phone keeps its own echo canceller (we do not send AT+NREC=0), so the phone side will be fine; at high call volume the other party may hear their own voice echoed. Do the first test at call volume 8..10.
- One phone at a time. While connected the speaker stays hidden (non-discoverable) but connectable.
- SBC only (AAC off), HFP mSBC (wide band) on, CVSD fallback automatic.

## Test plan (on the board)

1. Flash, monitor. "READY: music (A2DP) + calls (HFP)" must appear.
2. Forget the old "kanchan" on the phone, then pair. Log: "Pairing successful", then "Music (A2DP) connected", within 4 s "Calls (HFP) connected" (opened by the phone itself or by us).
3. Play a song: it must sound exactly like kanchan (MUSIC 44100 logs).
4. Call yourself: "Call setup: INCOMING", the speaker rings (or the phone's in-band ring). Press PLAY: "Voice link connected", CALL 8000/16000 logs, talk. mic_peak in the log must rise when you speak.
5. PLAY again: hang up. Music comes back (if the phone resumes it).
6. Old phone: pair (PIN 0000 if asked). Check in the log which profile opened; `i` for status.
