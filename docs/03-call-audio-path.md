# 03: Call audio path (HFP voice <-> ES8388)

Date: 2026-10-27. Status: code; board test pending.

## From the phone to the speaker

```
phone (AG) --SCO/eSCO--> ESP32 controller --HCI--> Bluedroid HFP client
   CVSD -> PCM16 mono 8 kHz        mSBC -> PCM16 mono 16 kHz
   -> esp_hf_client_register_data_callback(incoming, outgoing)
   -> incoming(buf,len)  -> audio_engine_call_push -> call_rx_ring (byte ring)
   -> audio task (CALL): 10 ms block, mono -> L=R, gain = level/15, PA on
   -> audio_io_write -> I2S0 TX 32-bit slots -> ES8388 DAC -> LOUT1 -> NS4150 -> speaker
```

`sdkconfig.defaults`: `CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y` + `CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y` + `CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1`. Without these three the SCO data goes to the PCM pins and the speaker stays silent. `CONFIG_BT_HFP_WBS_ENABLE=y` negotiates mSBC (clean voice); old phones come in on CVSD, both are handled.

## From the microphones to the phone

```
onboard mics -> ES8388 LINPUT1/RINPUT1 (reg 0x0a = 0x00, ADF "LINE1"), PGA reg 0x09 = 0x88 (+24 dB)
   -> ADC 32-bit I2S (reg 0x0c = 0x10) -> ASDOUT -> GPIO35 -> I2S0 RX
   -> audio_io_read (same 10 ms frame count) -> (L+R)/2 x MIC_DIGITAL_GAIN, clip
   -> call_tx_ring -> esp_hf_client_outgoing_data_ready()
   -> stack calls outgoing(buf,len) -> audio_engine_call_pull: hand over the full `len` bytes, otherwise 0
   -> CVSD/mSBC encode -> SCO -> phone
```

The stack discards partial reads, so the pull only delivers when a complete chunk is available.

## I2S full duplex (audio_io.c)

- `i2s_new_channel(&cfg, &tx, &rx)` on one port. TX is initialised first (master), then RX on the same pins/rate/32-bit -> the driver makes it the duplex slave (one warning "switched from master to slave" in the log is normal).
- On the original ESP32, TX and RX **share one MCLK divider** (hal: `i2s_ll_rx_set_mclk` = `i2s_ll_tx_set_mclk`). So both use `I2S_MCLK_MULTIPLE_256` (ES8388 ratio registers 0x18/0x0d = 0x02). The RX slave needs MCLK/BCLK >= 4: 256 / (2 x 32) = 4, exactly at the limit, no driver warning.
- Rate switch: disable both -> `i2s_channel_reconfig_std_clock` on both -> TX silence preload -> TX enable -> RX enable only in CALL.
- At 8 kHz MCLK is 2.048 MHz, BCLK 512 kHz. At 16 kHz 4.096 / 1.024 MHz. Exact division from the 160 MHz PLL.

## ES8388 record path (es8388.c)

| Reg | Value | Meaning |
|---|---|---|
| 0x00 | 0x36 | SameFs: ADC and DAC share one Fs (kanchan setting) |
| 0x03 | 0xff -> 0x00 during a call | ADC + analog input + MICBIAS power (0xff = everything off) |
| 0x09 | 0x88 | mic PGA +24 dB L/R (`MIC_PGA_REG`) |
| 0x0a | 0x00 | LIN1/RIN1 input (onboard mics) |
| 0x0b | 0x02 | L data = L ADC, R data = R ADC |
| 0x0c | 0x10 | ADC 32-bit I2S |
| 0x0d | 0x02 | ADC MCLK ratio 256 |
| 0x10/0x11 | 0x00 | ADC digital 0 dB |

Playback registers are the same as kanchan (LOUT1 0x1e, LOUT2 0x00, DAC profile P4 = -6 dB).

## Ring

The phone tells us via `+BSIR` whether it provides an in-band ring. If not, on an incoming call `audio_engine_ring(true)`: a 425+480 Hz dual tone at the music rate, 1 s on / 2 s off, at music volume. As soon as the voice link opens the ring stops and CALL mode takes over.

## Tuning / debugging

- `i` on the console: BT status + call status + codec/I2S readback.
- Every 5 s in the log: `CALL 8000 PA=1 out_peak mic_peak ...` and `call rx_drops tx_drops underruns`.
  - `mic_peak` should be 5000+ while you speak; if lower, `MIC_DIGITAL_GAIN` 3.0.
  - If `rx_drops` grows, raise `CALL_RX_BUFFER_BYTES`; if `underruns` grows, raise `CALL_PREFILL_BYTES`.
- Too much echo: lower the call volume with `[`, or `CALL_BOOT_VOLUME` 8.
- `t`: ring tone test without a phone (speaker path check).
