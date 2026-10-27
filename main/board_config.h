#pragma once

// ESP32-LyraT v4.3 wiring. Same board as the earlier kanchan speaker project.
// Only this file describes the hardware; a new board needs a new mapping.
#define BOARD_I2C_SDA 18
#define BOARD_I2C_SCL 23
#define BOARD_CODEC_ADDR 0x10 // ES8388, 7-bit address
#define BOARD_I2S_MCLK 0
#define BOARD_I2S_BCLK 5
#define BOARD_I2S_LRCK 25
#define BOARD_I2S_DOUT 26 // ESP32 -> ES8388 DSDIN (playback)
#define BOARD_I2S_DIN 35  // ES8388 ASDOUT -> ESP32 (microphones; input-only pin)
#define BOARD_PA_ENABLE 21
#define BOARD_LED 22
#define BOARD_TOUCH_PLAY 8  // GPIO33
#define BOARD_TOUCH_SET 9   // GPIO32
#define BOARD_TOUCH_UP 7    // GPIO27
#define BOARD_TOUCH_DOWN 4  // GPIO13; DIP 1-7 OFF, no SD/JTAG.

// Bluetooth identity. Phones that still remember the old music-only "kanchan"
// must forget it once and pair again so they re-read the new service list (HFP).
#define BT_DEVICE_NAME "blu2"
#define BT_LEGACY_PIN "0000" // Old (pre-2.1) phones ask for this PIN.

// Music (A2DP) path.
#define MUSIC_BUFFER_BYTES (32 * 1024)
#define MUSIC_PREFILL_BYTES 8192
#define MUSIC_BOOT_VOLUME 70       // Phone absolute volume replaces this after connect.
#define MUSIC_PCM_PEAK_LIMIT 32767 // Limiter is only an overflow guard (digital gain 1x).

// ES8388 DAC digital volume profile (see dac_profiles[] in es8388.c): 4 = -6 dB,
// ESP-ADF's real LyraT maximum, the clean limit of the NS4150 PA (research 2026-10-26).
#define AUDIO_BOOT_DAC_PROFILE 4

// Call (HFP) path. Voice PCM arrives from the phone as 16-bit mono at 8 kHz
// (CVSD) or 16 kHz (mSBC); the I2S bus runs at that same rate during a call.
#define CALL_BOOT_VOLUME 10          // HFP speaker gain 0..15; phone sends +VGS to change it.
#define CALL_RX_BUFFER_BYTES 6144    // phone -> speaker PCM ring (8 kHz: ~380 ms)
#define CALL_TX_BUFFER_BYTES 6144    // mic -> phone PCM ring
#define CALL_PREFILL_BYTES 480       // 30 ms at 8 kHz before the speaker starts
#define MIC_PGA_REG 0x88             // ES8388 reg 0x09: +24 dB on both onboard mics
#define MIC_DIGITAL_GAIN 2.0f        // software gain on mic PCM before it goes to the phone
#define CALL_RING_ON_MS 1000         // local ring tone pattern when the phone sends no in-band ring
#define CALL_RING_OFF_MS 2000

// Link manager: how hard we try to connect by ourselves (older phones often
// wait for the accessory to open the profiles).
#define LINK_RECONNECT_ATTEMPTS 6
#define LINK_RECONNECT_PERIOD_S 10
#define LINK_PROFILE_FOLLOWUP_S 4 // after one profile (or pairing) is up, open the other one ourselves
