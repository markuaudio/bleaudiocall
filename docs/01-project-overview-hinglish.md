# 01: bleaudiocall project overview

Date: 2026-10-27. Status: final build `docs/2026-10-27-final-build.log` (release -O2), board par phone test pending. Audit aur fixes: [05](05-final-audit-aur-optimization.md).

## Problem

Kanchan speaker (`d:/Vscode/esp`) sirf gaane bajata tha. Chahiye tha: wahi board, par jo phone connect ho uski **call bhi receive ho** (ring, answer, baat, hang up), aur **purana phone jo connect nahi ho raha tha woh bhi connect ho**.

## Kya banaya

Ek naya ESP-IDF project, kanchan ke verified audio settings (ES8388 register sequence, LOUT2 -45 dB cancellation fix, DAC -6 dB, 32-bit I2S slots) ko reuse karke, upar HFP aur ek "link manager" joda.

```
main/
  main.c            boot order, touch-key routing (call > music), serial console
  board_config.h    LyraT pins + saare tunable numbers (volumes, mic gain, retry counts)
  es8388.c/.h       codec: playback path + record path (mics), DAC profiles, ADC power
  audio_io.c/.h     I2S0 full duplex (TX speaker, RX mics), rate switch 44.1k <-> 8k/16k
  audio_engine.c/.h ek task jo speaker/mic bus ka maalik hai: MUSIC / CALL / RING / test tones
  bt_core.c/.h      radio + host, GAP (naam, class of device, pairing har type), link manager
  bt_music.c/.h     A2DP sink + AVRCP (kanchan se liya)
  bt_call.c/.h      HFP client: call state, answer/hangup/redial, voice link, volume sync
  touch_buttons.c   LyraT touch pads -> app_key()
```

## Boot order (main.c)

1. LED/PA GPIO low, NVS init (bonds yahin store hote hain).
2. `bt_core_radio_init()`: controller (BR/EDR only) + Bluedroid with SSP. Radio pehle, codec baad mein (RF calibration alag, zyada heap).
3. `audio_engine_init()`: ES8388 init (ADC off), ring buffers, I2S duplex at 44.1 kHz, audio task core 1 priority 6.
4. `touch_buttons_init()`.
5. `bt_core_start()`: naam "blu2" (`BT_DEVICE_NAME`; purana "kanchan" naam phone ke SDP cache mein A2DP-only hai), CoD hands-free, IO cap NONE, fixed PIN 0000, page timeout 10 s, A2DP + AVRCP + HFP init, bonded phone ko 3 s baad khud connect, link task.
6. Console task.

## Audio engine: ek task, teen source

| Source | Kab | Bus rate | Kya hota hai |
|---|---|---|---|
| MUSIC | A2DP stream | 44.1/48 kHz stereo | kanchan jaisa: prefill, 1x gain, limiter sirf overflow guard, PA tabhi on jab data ho |
| CALL | HFP voice link (SCO) up | 8 kHz (CVSD) ya 16 kHz (mSBC) | phone PCM mono -> dono channel; mics (L+R)/2 x 2.0 -> phone; 10 ms blocks, dono direction lock-step |
| RING | incoming call, phone in-band ring nahi deta | music rate | 425+480 Hz dual tone, 1 s on / 2 s off, music volume par |

Source/rate/DAC-profile badle to `epoch` badhta hai: PA off, DAC mute, profile apply, ADC on/off, I2S dono channel disable -> reclock -> silence preload -> enable, stale buffers flush.

## Numbers jo tune kar sakte ho (board_config.h)

- `CALL_BOOT_VOLUME 10` (0..15), phone +VGS se badalta hai.
- `MIC_PGA_REG 0x88` (+24 dB analog), `MIC_DIGITAL_GAIN 2.0f`. Dusri taraf awaaz kam ho to pehle digital gain 3.0, phir PGA.
- `AUDIO_BOOT_DAC_PROFILE 4` (-6 dB, ADF max). `] [` se live test.
- `LINK_RECONNECT_ATTEMPTS 6`, `LINK_RECONNECT_PERIOD_S 10`, `LINK_PROFILE_FOLLOWUP_S 4`.

## Jaanne wali limitations

- **Echo**: speaker aur mic ek hi board par; koi acoustic echo canceller nahi (ESP-ADF ke DSP algorithms yahan nahi hain). Phone apna echo canceller rakhta hai (hum AT+NREC=0 nahi bhejte), isliye phone side thik rahega; call volume zyada rakhne par dusre ko apni awaaz goonj sakti hai. Pehla test call volume 8..10 par karo.
- Ek phone ek baar. Connected hone par speaker hidden (non-discoverable) par connectable rehta hai.
- SBC only (AAC off), HFP mSBC (wide band) on, CVSD fallback automatic.

## Test plan (board par)

1. Flash, monitor. "READY: music (A2DP) + calls (HFP)" dikhna chahiye.
2. Phone par purana "kanchan" forget karo, phir pair. Log: "Pairing successful", phir "Music (A2DP) connected", 4 s ke andar "Calls (HFP) connected" (phone ne khud kholi ya humne).
3. Gaana bajao: kanchan jaisa hi bajna chahiye (MUSIC 44100 logs).
4. Khud ko call karo: "Call setup: INCOMING", speaker ring (ya phone ka in-band ring). PLAY dabao: "Voice link connected", CALL 8000/16000 logs, baat karo. mic_peak log mein badhna chahiye jab bolo.
5. PLAY dobara: hang up. Music wapas (phone resume kare to).
6. Purana phone: pair karo (PIN 0000 agar maange). Log mein dekho kaunsa profile khula; `i` se status.
