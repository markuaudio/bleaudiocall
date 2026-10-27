# 02: Making every kind of phone connect over Bluetooth

Date: 2026-10-27. Status: design + code; verification with an old phone pending.

## What was wrong (kanchan firmware, `main/bluetooth_speaker.c` in the kanchan speaker project folder)

An old phone would not connect. Reading the code revealed these causes (one or more):

1. **NON_CONNECTABLE right after connecting**: on A2DP connect the firmware set `ESP_BT_NON_CONNECTABLE`. Many phones (especially old ones and iPhones) open the HFP/AVRCP link **separately**, a few seconds after A2DP. The radio stopped answering pages, the phone's second attempt failed, and some phones then drop the whole connection or show "connected (no media)".
2. **Music profile only**: the phone found no HFP record. Old feature phones (Nokia S40/Symbian, Sony Ericsson) look for hands-free first when connecting a "Bluetooth headset"; an A2DP-only device often does not even appear in their list, or they say "not supported".
3. **Class of Device unclassified**: Bluedroid's default CoD is `0x001f00` (uncategorized). Old phones filter their device list by CoD: an audio device needs major class Audio/Video + the Audio service bit, otherwise the icon is wrong or it is missing from the list.
4. **The firmware never connected by itself**: it only waited. Some phones (old ones, and ones that are already bonded) expect the accessory to initiate the connection (the way a headset joins the phone at power-on).
5. **The phone's cached pairing**: the phone remembered "kanchan" as A2DP-only (SDP cache). Even with new firmware the phone keeps using the old list until you forget + re-pair.

## What was done right (`main/bt_core.c`)

| Item | Now | Why |
|---|---|---|
| Scan mode | **always CONNECTABLE**; discoverable only while nobody is connected | profiles open at different times |
| Profiles | A2DP + AVRCP + **HFP HF** all in SDP | music-only phones, call-only phones, phones with both: everyone finds something |
| Class of Device | `major AV, minor Hands-free (0x02), service Audio + Rendering` via `esp_bt_gap_set_cod(..., ESP_BT_INIT_COD)` | old phones see a "headset/hands-free" |
| Pairing, new phones | SSP on, IO cap NONE ("Just Works"), `CFM_REQ` auto-accept, `KEY_NOTIF` logged | a speaker without a display; no number comparison |
| Pairing, old phones | fixed PIN `0000` (`esp_bt_gap_set_pin` FIXED) + `PIN_REQ` handler (sixteen zeros if a 16-digit PIN is requested) | Bluetooth 2.0 and older legacy pairing |
| Connect by ourselves (follow-up) | 4 s after pairing or after one profile comes up, we open the other profile ourselves (`LINK_PROFILE_FOLLOWUP_S`); each profile only once per link. If our attempt fails, the other profile is tried immediately **only** if the phone answered on the radio (ACL ok); on a page timeout we wait for the next reconnect period (`bt_core_profile_failed`) | if the phone did not open HFP we open it; if the phone has no HFP there is one warning and that is it; two page attempts never overlap |
| Reconnect by ourselves | 3 s after boot, A2DP connect to the newest bonded phone; on failure again after 10 s, 6 times (`LINK_RECONNECT_*`), then silence; if the phone drops the link, the same cycle after 5 s. Our own failed attempt does not count as "link lost" (previously the counter was reset = infinite loop, see [05](05-final-audit-and-optimization.md)) | headset-like behaviour; if the user stops it with `x`/SET-hold it stays off until reboot or `c` |
| Page timeout | 0x4000 = 10 s (default 5 s) | old phones in sleep answer late |
| Logs | ACL connect/disconnect status + reason, pairing failure status | if the next phone fails, the reason is in the log |
| Bond list | `v` list, `f` forget all, `c` connect last | forget on the phone + forget here = clean start |

## What to do on the phone

1. Settings > Bluetooth > old "kanchan" > **Forget / Unpair**. The new name is `blu2`.
2. Reboot the speaker (or `f` from the console). LED slow blink = discoverable.
3. Scan from the phone, select "blu2". If a PIN is requested: `0000`. A new phone joins without a PIN.
4. On the phone, "blu2" must show both the **Media audio** and **Phone calls** toggles (Android). iPhone: the device shows "Connected" in Settings.
5. Log check: `Music (A2DP) connected`, `Calls (HFP) connected`.

## If an old phone still fails

Look in the log:

- `Pairing failed, status N`: forget on the phone side, then `f`, then try again. Status 5/6 = key/auth mismatch (stale bond).
- `Radio link closed by ... reason 0x08`: range/timeout. 0x13 = the phone dropped it (no support on the phone side, or the user cancelled). 0x16 = we dropped it.
- `HFP link attempt ended before service level connection`: the phone has no hands-free (tablet/iPod), or the phone is not nearby (accompanied by `Radio link ... status 0x104` = page timeout). In the first case music will work, calls will not. Normal.
- HFP is tried even when the A2DP connect fails: a feature phone that only offers calls will connect too (no music, but calls).

If some phone treats the "hands-free" CoD as a car kit and misbehaves, try `cod.minor = 0x01` (wearable headset) or `0x05` (loudspeaker) in `bt_core.c`.
