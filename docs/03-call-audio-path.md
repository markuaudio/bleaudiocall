# 03: Call audio path (HFP voice <-> ES8388)

Date: 2026-10-27. Status: code; board test pending.

## Phone se speaker tak

```
phone (AG) --SCO/eSCO--> ESP32 controller --HCI--> Bluedroid HFP client
   CVSD -> PCM16 mono 8 kHz        mSBC -> PCM16 mono 16 kHz
   -> esp_hf_client_register_data_callback(incoming, outgoing)
   -> incoming(buf,len)  -> audio_engine_call_push -> call_rx_ring (byte ring)
   -> audio task (CALL): 10 ms block, mono -> L=R, gain = level/15, PA on
   -> audio_io_write -> I2S0 TX 32-bit slots -> ES8388 DAC -> LOUT1 -> NS4150 -> speaker
```

`sdkconfig.defaults`: `CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y` + `CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y` + `CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1`. Bina in teen ke SCO data PCM pins par chala jata hai aur speaker chup rehta hai. `CONFIG_BT_HFP_WBS_ENABLE=y` se mSBC (saaf awaaz) negotiate hoti hai; purane phone CVSD par aate hain, dono handle.

## Mic se phone tak

```
onboard mics -> ES8388 LINPUT1/RINPUT1 (reg 0x0a = 0x00, ADF "LINE1"), PGA reg 0x09 = 0x88 (+24 dB)
   -> ADC 32-bit I2S (reg 0x0c = 0x10) -> ASDOUT -> GPIO35 -> I2S0 RX
   -> audio_io_read (same 10 ms frame count) -> (L+R)/2 x MIC_DIGITAL_GAIN, clip
   -> call_tx_ring -> esp_hf_client_outgoing_data_ready()
   -> stack calls outgoing(buf,len) -> audio_engine_call_pull: poore `len` bytes de, warna 0
   -> CVSD/mSBC encode -> SCO -> phone
```

Stack partial reads discard karta hai, isliye pull tabhi deta hai jab poora chunk ho.

## I2S full duplex (audio_io.c)

- `i2s_new_channel(&cfg, &tx, &rx)` ek port par. TX pehle init (master), phir RX same pins/rate/32-bit -> driver ise duplex slave banata hai (log mein ek warning "switched from master to slave" normal hai).
- Original ESP32 par TX/RX **ek hi MCLK divider** share karte hain (hal: `i2s_ll_rx_set_mclk` = `i2s_ll_tx_set_mclk`). Isliye dono `I2S_MCLK_MULTIPLE_256` (ES8388 ratio registers 0x18/0x0d = 0x02). RX slave ko MCLK/BCLK >= 4 chahiye: 256 / (2 x 32) = 4, bilkul limit par, driver warning nahi.
- Rate switch: dono disable -> `i2s_channel_reconfig_std_clock` dono par -> TX silence preload -> TX enable -> RX enable sirf CALL mein.
- 8 kHz par MCLK 2.048 MHz, BCLK 512 kHz. 16 kHz par 4.096 / 1.024 MHz. 160 MHz PLL se exact divide.

## ES8388 record path (es8388.c)

| Reg | Value | Matlab |
|---|---|---|
| 0x00 | 0x36 | SameFs: ADC aur DAC ek Fs (kanchan setting) |
| 0x03 | 0xff -> 0x00 call mein | ADC + analog in + MICBIAS power (0xff = sab off) |
| 0x09 | 0x88 | mic PGA +24 dB L/R (`MIC_PGA_REG`) |
| 0x0a | 0x00 | LIN1/RIN1 input (onboard mics) |
| 0x0b | 0x02 | L data = L ADC, R data = R ADC |
| 0x0c | 0x10 | ADC 32-bit I2S |
| 0x0d | 0x02 | ADC MCLK ratio 256 |
| 0x10/0x11 | 0x00 | ADC digital 0 dB |

Playback registers kanchan jaise hi (LOUT1 0x1e, LOUT2 0x00, DAC profile P4 = -6 dB).

## Ring

Phone `+BSIR` se batata hai in-band ring hai ya nahi. Nahi hai to incoming par `audio_engine_ring(true)`: music rate par 425+480 Hz dual tone 1 s on / 2 s off music volume par. Voice link khulte hi ring band, CALL mode.

## Tuning / debugging

- `i` console: BT status + call status + codec/I2S readback.
- Har 5 s log: `CALL 8000 PA=1 out_peak mic_peak ...` aur `call rx_drops tx_drops underruns`.
  - `mic_peak` bolne par 5000+ hona chahiye; kam ho to `MIC_DIGITAL_GAIN` 3.0.
  - `rx_drops` badhe to `CALL_RX_BUFFER_BYTES` badhao; `underruns` badhe to `CALL_PREFILL_BYTES` badhao.
- Echo zyada: call volume `[` se kam, ya `CALL_BOOT_VOLUME` 8.
- `t`: bina phone ring tone test (speaker path check).
