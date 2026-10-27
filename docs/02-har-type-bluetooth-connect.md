# 02: Har type ka Bluetooth connect ho sake

Date: 2026-10-27. Status: design + code; purane phone par verify pending.

## Galat kya tha (kanchan firmware, `d:/Vscode/esp/main/bluetooth_speaker.c`)

Purana phone connect nahi ho raha tha. Code padhne par yeh wajah mili (ek ya zyada):

1. **Connect hote hi NON_CONNECTABLE**: A2DP connect par firmware `ESP_BT_NON_CONNECTABLE` set karta tha. Bahut se phone (khaas purane aur iPhone) A2DP ke kuch second baad **alag se** HFP/AVRCP ki link kholte hain. Radio ne page answer karna band kar diya, phone ki doosri koshish fail, kuch phone phir poora connection tod dete hain ya "connected (no media)" dikhate hain.
2. **Sirf music profile**: phone ke paas HFP record nahi mila. Purane feature phones (Nokia S40/Symbian, Sony Ericsson) "Bluetooth headset" connect karte waqt pehle hands-free dhoondhte hain; A2DP-only device ko kai baar list mein hi nahi dikhate ya "not supported" bolte hain.
3. **Class of Device unclassified**: Bluedroid default CoD `0x001f00` (uncategorized). Purane phone device list ko CoD se filter karte hain: audio device chahiye to major class Audio/Video + Audio service bit, warna icon galat ya list se bahar.
4. **Firmware kabhi khud connect nahi karta tha**: sirf wait. Kuch phone (purane, aur jo pehle bonded hain) expect karte hain ki accessory connection shuru kare (jaise headset power-on par phone se jud jata hai).
5. **Phone ka cached pairing**: phone ne "kanchan" ko A2DP-only yaad rakha hai (SDP cache). Naya firmware aane par bhi phone purani list use karta hai jab tak forget + re-pair na ho.

## Sahi kya kiya (`main/bt_core.c`)

| Cheez | Ab | Kyun |
|---|---|---|
| Scan mode | **hamesha CONNECTABLE**; discoverable sirf jab koi connected nahi | profiles alag-alag waqt par khulti hain |
| Profiles | A2DP + AVRCP + **HFP HF** sab SDP mein | music-only phone, call-only phone, dono wale phone: sabka kuch milta hai |
| Class of Device | `major AV, minor Hands-free (0x02), service Audio + Rendering` via `esp_bt_gap_set_cod(..., ESP_BT_INIT_COD)` | purane phone ko "headset/hands-free" dikhe |
| Pairing naye phone | SSP on, IO cap NONE ("Just Works"), `CFM_REQ` auto-accept, `KEY_NOTIF` log | bina display ka speaker; koi number compare nahi |
| Pairing purane phone | fixed PIN `0000` (`esp_bt_gap_set_pin` FIXED) + `PIN_REQ` handler (16-digit maange to solah zero) | Bluetooth 2.0 aur purane legacy pairing |
| Khud connect (follow-up) | pairing ya ek profile up hone ke 4 s baad doosri profile hum khud kholte hain (`LINK_PROFILE_FOLLOWUP_S`); har profile ek hi baar per link. Hamari attempt fail ho to doosri profile **tabhi** turant try hoti hai jab phone ne radio par jawab diya tha (ACL ok); page timeout par next reconnect period tak wait (`bt_core_profile_failed`) | phone ne HFP nahi kholi to hum kholte hain; phone mein HFP nahi hai to ek warning aur bas; do page attempts kabhi overlap nahi |
| Khud reconnect | boot ke 3 s baad newest bonded phone ko A2DP connect; fail to 10 s baad phir, 6 baar (`LINK_RECONNECT_*`), phir chup; phone ke link todne par 5 s baad wahi cycle. Hamari fail attempt "link toota" nahi ginti (pehle counter reset ho jata tha = infinite loop, dekho [05](05-final-audit-aur-optimization.md)) | headset jaisa behaviour; user `x`/SET-hold se roke to reboot ya `c` tak band |
| Page timeout | 0x4000 = 10 s (default 5 s) | sleep mein pade purane phone late jawab dete hain |
| Logs | ACL connect/disconnect status + reason, pairing fail status | agla phone fail ho to wajah log mein |
| Bond list | `v` list, `f` forget all, `c` connect last | phone side forget + yahan forget = clean start |

## Phone par kya karna hai

1. Settings > Bluetooth > purana "kanchan" > **Forget / Unpair**. Naya naam `blu2` hai.
2. Speaker reboot (ya `f` console se). LED slow blink = discoverable.
3. Phone se scan, "blu2" select. PIN maange to `0000`. Naya phone bina PIN jud jayega.
4. Phone mein "blu2" par **Media audio** aur **Phone calls** dono toggles dikhne chahiye (Android). iPhone: Settings mein device "Connected".
5. Log check: `Music (A2DP) connected`, `Calls (HFP) connected`.

## Agar phir bhi purana phone fail ho

Log mein dekho:

- `Pairing failed, status N`: phone side forget, phir `f`, phir dobara. Status 5/6 = key/auth mismatch (stale bond).
- `Radio link closed by ... reason 0x08`: range/timeout. 0x13 = phone ne kaata (phone side support nahi ya user cancel). 0x16 = humne kaata.
- `HFP link attempt ended before service level connection`: phone mein hands-free nahi (tablet/iPod), ya phone paas nahi (saath mein `Radio link ... status 0x104` = page timeout). Pehle case mein music chalega, calls nahi. Normal.
- A2DP connect fail par bhi HFP try hota hai: feature phone jo sirf calls deta hai woh bhi jud jayega (gaane nahi, calls haan).

Agar koi phone CoD "hands-free" ko car-kit samajh kar galat behave kare to `bt_core.c` mein `cod.minor = 0x01` (wearable headset) ya `0x05` (loudspeaker) karke dekho.
