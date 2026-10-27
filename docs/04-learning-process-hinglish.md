# 04: Learning process — zero se bleaudiocall tak (25–27 Oct 2026)

Yeh file poori yatra ek jagah rakhti hai: kya seekha, kya galat hua, kaise sahi hua. Detail wali docs link mein hain. Agar koi naya banda is project ko uthaye to yahi file pehle padhe.

## Timeline ek nazar mein

| Din | Kya hua | Seekh |
|---|---|---|
| 25 Oct | PC par ESP-IDF v6.1 setup, pehla "counter" firmware, board se connection fail → auto-download jumpers → pehla successful run | Editor aur compiler alag cheezein hain; COM port dikhna = sirf USB-UART, chip chal raha hai iska proof nahi |
| 25 Oct | kanchan Bluetooth speaker: A2DP + ES8388 + I2S + touch, pehli baar sirf **hiss** | Register 0 ka reference-enable bit, phir ESP32 ka FIFO packing rule (data width = slot width) |
| 26 Oct | Gaane chale, par awaaz kam. 10x, 100x, 20x, 60x digital gain try; 100x par board power-trip, reset loop | Digital gain se loudness nahi badhi: signal path samjhe bina gain badhana bekaar |
| 26 Oct | **Root cause**: LOUT2/ROUT2 register 0 dB par the → NS4150 ke differential input par signal cancel. Fix: -45 dB (ADF jaisa), DAC -6 dB, digital 1x | "Reference code loud tha" wala clue sabse keemti; A/B same hardware par |
| 27 Oct | Naya project **bleaudiocall**: kanchan ka verified audio + HFP calls + har-type pairing/reconnect. Build pass. Serial log mein reconnect loop bug mila | Link manager ka state machine kaagaz par pehle, code baad mein |
| 27 Oct | Final audit: loop bug fix, overlapping page attempts fix, ring-buffer wrap fix, -O2 release build, docs complete | "Build pass" aur "optimized" alag baatein hain |

## Phase 1: Toolchain (25 Oct) — [00](00-esp-idf-setup-hinglish.md), kanchan docs 02/03

1. VS Code tha, Git tha, Python tha; ESP-IDF nahi tha. Official Espressif Installation Manager (EIM) se v6.1 + esp32 target install kiya, `D:\Vscode\esp\tooling` mein. Shared tools `C:\Espressif\tools`.
2. Har terminal mein environment activate karna padta hai: `. .\tools\Enter-EspIdf.ps1`. VS Code terminal profile yeh khud karta hai.
3. Pehla firmware sirf ek counter log karta tha. Build pass = compile hua; run hua iska proof sirf serial log se milta hai.
4. Board se `No serial data received`. Wajah: LyraT ke auto-download jumpers JP23/24/25 nahi lage the. Lagaye → flash hua → counter chala. Upload ke baad jumpers hatao: GPIO0 I2S MCLK bhi hai.
5. Chip: ESP32-D0WD-V3, flash **8 MB** (esptool `flash-id` se measure kiya; config mein 8MB set kiya).

## Phase 2: kanchan speaker (25–26 Oct) — kanchan `docs/04` se `docs/13`

Kya banaya: apna ESP-IDF app (ESP-ADF pipeline nahi), A2DP sink + AVRCP, ES8388 register programming khud, I2S master, 32 KiB ring buffer, audio task core 1, touch pads.

Galtiyan jo hui, kram se:

1. **Sirf hiss.** ES8388 reg 0x00 = 0x12 tha: reference enable bit off. Datasheet section 6.1.1 padh kar 0x36 kiya. Phir bhi hiss.
2. **Silence + underruns.** 32-bit slots rakhe par 16-bit packed data bheja. ESP32 ka `i2s_ll.h` kehta hai data width aur slot width same honi chahiye (FIFO packing mode slot width se chunta hai). Fix: PCM16 → MSB-aligned PCM32 (`* 65536`), codec reg 0x17 = 0x20 (32-bit). Tab gaane chale.
3. **Awaaz kam.** Digital gain 10x → 100x → trip (PC USB port over-current, `phy_init` reset loop). Rollback 4x, 6x, 20x, 60x: 20x par out_peak 32767 (full scale) phir bhi kam. Yani digital raasta khatam, problem analog mein.
4. **Root cause (26 Oct 16:45):** LyraT v4.3 schematic: NS4150 PA ka IN+ ← LOUT1, IN- ← LOUT2. Hamne LOUT2/ROUT2 (reg 0x30/0x31) 0 dB par rakhe the = dono input par same signal = cancellation. ESP-ADF 0x00 (-45 dB) likhta hai isi wajah se. Fix ke baad "1x digital, DAC -6 dB" hi ADF-level loud hai.
5. **Research (14 agents):** NS4150 gain 20 dB; PA clean limit codec -11..-12 dB; ADF ka asli LyraT max DAC -6 dB hai, 0 dB nahi.

Seekh: gain badhane se pehle signal path ki wiring samjho. Ek register ki wajah se 4 experiment bekaar gaye.

## Phase 3: bleaudiocall (27 Oct) — [01](01-project-overview-hinglish.md), [02](02-har-type-bluetooth-connect.md), [03](03-call-audio-path.md)

Problem: speaker gaane bajata hai, call nahi uthata; ek purana phone connect hi nahi hota.

Design decisions (kyun aisa banaya):

| Decision | Kyun |
|---|---|
| Naya project, kanchan copy nahi | kanchan ka `audio_output.c` + `bluetooth_speaker.c` monolith tha; call ke liye mic path, rate switch 44.1k↔8k/16k, source arbitration chahiye tha. 8 chhote modules banaye, har ek ek kaam |
| Ek audio task bus ka maalik | ES8388 + I2S ek hi hardware; music, call, ring teen source. Ek task + `epoch` counter = race-free switch |
| Radio pehle, codec baad | RF calibration ko I2S noise se alag, zyada heap boot par (kanchan se seekha) |
| HFP voice HCI path par | `CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y` bina SCO data PCM pins par jata hai jo LyraT par wired nahi; firmware ko PCM milta hi nahi |
| I2S full duplex, dono 256×MCLK | Original ESP32 par TX/RX ek MCLK divider share karte hain; RX slave ko MCLK/BCLK ≥ 4 chahiye: 256/(2×32) = 4, bilkul limit par |
| Hamesha CONNECTABLE | kanchan A2DP connect par NON_CONNECTABLE ho jata tha → phone HFP alag se kholne aaye to radio jawab nahi deta |
| Class of Device = AV / Hands-free | Purane phone device list CoD se filter karte hain |
| SSP "Just Works" + fixed PIN 0000 | Naye aur purane dono phone |
| Link manager khud connect kare | Purane phone aur bonded phone expect karte hain ki accessory connection shuru kare |
| Naam `blu2` (kanchan nahi) | Phone ne "kanchan" ko A2DP-only yaad rakha hai (SDP cache). Naya naam = phone fresh service list padhta hai |

Build pehli baar mein pass, 0 warnings in `main/`. Board par flash karke serial log liya (`docs/serial-output.log`).

## Phase 4: Audit aur final (27 Oct) — [05](05-final-audit-aur-optimization.md)

Serial log padhne par bug dikha jo code review mein nahi dikha tha:

```
I (123215) bt: Reconnect attempt 1/6 to bonded phone ...
I (133215) bt: Reconnect attempt 2/6 ...
I (144215) bt: Reconnect attempt 1/6 ...   <-- counter reset!
```

Phone paas nahi tha (page timeout 0x104). Har fail par A2DP "DISCONNECTED" event aata hai; hamara code use "link toota" samajh kar `attempts = 0` kar deta tha. 6-attempt limit kabhi nahi lagti; board hamesha ke liye har 10 s page karta rehta (radio busy, battery/CPU waste). Saath mein har fail par poora audio path reclock ho raha tha, aur HFP ki page attempt A2DP ki page ke beech mein hi shuru ho jati thi.

Seekh: **log padho, sirf code mat padho.** Code "sahi" lag raha tha; 2 minute ka serial log 3 bug dikha gaya.

Baaki audit findings aur fixes [05](05-final-audit-aur-optimization.md) mein. Zero se dobara banane ka kram [06](06-complete-build-guide-hinglish.md) mein, board ka schematic/BOM [hardware/](hardware/README.md) mein.

## Kaam karne ka tareeka jo kaam aaya

1. Har change ke saath: backup, diff, build log, flash log, serial log `docs/` mein, date ke saath. Reset loop jaisi cheez sirf logs se pakdi gayi.
2. Serial console commands (`l r z b` tones, `] [` DAC steps, `i` status) = bina rebuild ke hardware experiment.
3. Har 5 s ek stats line (peaks, drops, underruns, i2s_errors): "awaaz nahi aa rahi" ko "data aa raha hai par analog mein kuch galat" mein badalne ka tareeka.
4. Research tab jab measurement ne raasta band kar diya (20x par full scale phir bhi kam → analog → schematic → cancellation).
5. Jo pata nahi, use "pending" likho, "ho gaya" nahi. Is project mein call path ab bhi hardware-verify pending hai.

## Abhi kya pending hai (honest status)

- Phone pair karke: music, incoming call ring, answer, dono taraf awaaz, hang up — **koi bhi board par verify nahi hua**. Serial log mein phone kabhi connect nahi hua (paas nahi tha / forget-repair nahi hua).
- Echo: speaker aur mic ek board par, koi AEC nahi. Phone ka echo canceller kaam karega, par call volume zyada rakhne par dusri taraf goonj aa sakti hai.
- `bt_core_connect_last` bond list ka aakhri entry "newest" maanta hai; Bluedroid order guarantee nahi karta. Ek phone ke saath farq nahi padta.
