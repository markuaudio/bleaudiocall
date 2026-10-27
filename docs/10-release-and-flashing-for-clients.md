# 10: Delivering the firmware to a client without the source code

How to hand a client a flashable firmware image only, how the client flashes it onto their own ESP32-LyraT V4.3 boards, what to put in the package, how to protect the image, and what to keep in mind for commercial use. The ready-made client package for v1.0.1 is in `release/v1.0.1/` (its `README-CLIENT.md` is written for the client; this document is written for you).

## 1. What a "burn file" is on the ESP32

An ESP32 boots from three pieces in external flash, each at a fixed address:

| Address | File (from `build/`) | Size | Role |
|---|---|---|---|
| `0x1000` | `bootloader/bootloader.bin` | about 26 KB | second-stage bootloader |
| `0x8000` | `partition_table/partition-table.bin` | 3 KB | partition table (`partitions.csv`: nvs, phy_init, factory app) |
| `0x10000` | `bleaudiocall.bin` | about 818 KB | the application |

The client never needs the source: these binaries contain everything. For convenience they are merged into **one file flashed at `0x0`** (`esptool merge-bin`), which is what the release package ships as `blu2-v1.0.1-full-0x0.bin`. The NVS area (`0x9000`, 24 KB) holds the Bluetooth bonds and the power self-test result. The **merged image overwrites it with 0xFF** (merge-bin pads the gaps), so flashing the full image is a factory reset: bonds gone, self-test at the next boot. Verified in T14 (docs/08): after writing the full image the boot log said `not yet calibrated, the power self-test runs now` although no erase was done. For an **update that must keep** the client's pairings and the calibrated level, flash only `bleaudiocall.bin` at `0x10000` (or the three parts at their own addresses).

The binary carries a version string (`app_init: App version: 1.0.1`, set by `CONFIG_APP_PROJECT_VER` in `sdkconfig.defaults`) and a SHA-256 of the ELF in its app descriptor; `esptool image-info bleaudiocall.bin` prints both, which is how you identify what a client has on a board.

## 2. Building a release package (what you do)

1. Bump `CONFIG_APP_PROJECT_VER` in `sdkconfig.defaults` (and the row in `CHANGELOG.md`), commit, build with `tools/Build.ps1` (release `-O2` config, 0 warnings).
2. Merge and checksum (from the project folder, ESP-IDF environment active; the commands used for v1.0.1 are in `tools/Make-Release.ps1`):
   ```
   python -m esptool --chip esp32 merge-bin -o release/v1.0.1/blu2-v1.0.1-full-0x0.bin --flash-mode dio --flash-freq 40m --flash-size 8MB 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/bleaudiocall.bin
   ```
   Copy the three separate binaries as well, write `SHA256SUMS.txt`, add `README-CLIENT.md`, `Flash-Client.ps1`, `flash.bat`.
3. Flash the package onto your own board with the client's procedure (not with `idf.py`) and check the boot log: `App version: 1.0.1`, the self-test, pairing. Keep that log with the release (`docs/test-data/`).
4. Zip `release/v1.0.1/` and send it. Nothing in it reveals the source. (The `.map` and `.elf` files in `build/` do contain symbol names: never ship those.)

## 3. How the client flashes it (what they do)

Three ways, all described step by step in `release/v1.0.1/README-CLIENT.md`:

| Way | Needs | Best for |
|---|---|---|
| A. Espressif Flash Download Tool (Windows GUI) | the tool from espressif.com, CP210x driver | non-technical client, production line |
| B. esptool (`pip install esptool`) + `Flash-Client.ps1` / `flash.bat` | Python 3 | anyone comfortable with a terminal; scriptable |
| C. esptool-js in Chrome/Edge (https://espressif.github.io/esptool-js/) | nothing to install | one-off flashing |

Settings in every tool: ESP32, DIO, 40 MHz, 8 MB, the full image at `0x0` (new boards, factory reset) or `bleaudiocall.bin` at `0x10000` (update, keeps pairings and calibration). Erase first on new boards.

Download mode on the LyraT: jumper caps on JP23/JP24/JP25 (automatic reset by the tool) or hold Boot, press RST, release Boot. After flashing remove the caps and press RST. The PC connects to the **USB-UART** port; power comes from the **USB POWER** port.

First boot: the power self-test plays a 60 Hz tone for about 8 s and stores the loudest safe level for that unit's supply. Tell the client that the speakers must be connected and that one reset during the tone is normal on a weak supply (docs/07).

## 4. Protecting the image (optional)

A plain image can be read back from the flash (`esptool read-flash`) and copied to other boards. If that matters commercially:

- **Flash encryption** (ESP32 rev 3 supports it): the flash content is encrypted with a key in eFuse; a read-back is useless. Enable `CONFIG_SECURE_FLASH_ENC_ENABLED` in "Release" mode; the first boot burns the eFuses and encrypts in place. **Irreversible** per chip; a wrongly configured unit is bricked for further updates unless you keep the key. Test the whole procedure on a spare board first.
- **Secure boot v2**: the bootloader only runs images signed with your key; combined with flash encryption this stops both copying and modification. Also irreversible.
- Simpler alternatives: a per-unit licence check is not possible without a server; a unique serial in NVS is only cosmetic. If you do not need copy protection, ship the plain image: it is what this package is.

Neither is enabled in v1.0.1. Enabling them changes the flashing procedure (encrypted write, signed images), so do it before the first client delivery or plan a documented update path.

## 5. Updating in the field

Re-flashing over UART with the same package is the update path for v1.0.1 (the partition table has one factory app, no OTA slots). If you later want updates without a cable, add two OTA partitions (`partitions.csv`) and an update mechanism; that is a firmware change, not a packaging one. Bluetooth bonds and the self-test result survive a re-flash unless you erase.

## 6. Commercial use: things to check

- **The source is public.** The repository https://github.com/markuaudio/bleaudiocall is public; anyone can rebuild the same binary. If you want "binary only" to mean something, make the repository private (`gh repo edit markuaudio/bleaudiocall --visibility private`) or keep the public repo as the open-source edition and develop the client version privately.
- **Licences of what is inside the binary:** ESP-IDF, Bluedroid and the SBC codec in it are Apache-2.0 (commercial use allowed; keep the licence notice and copyright attributions in your product documentation); the ES8388 register sequence follows Espressif's reference driver under the Espressif MIT licence (`docs/references/ES8388-LICENSE.txt`, free for use on Espressif products, notice must be kept). Your own code in `main/` has no licence file yet: add one (MIT/Apache for open release, or a proprietary notice) so the client knows what they may do.
- **Bluetooth:** using the Bluetooth name/logo on a product requires Bluetooth SIG listing (the ESP32-WROVER-E module is a qualified component, the end product still needs a declaration). Profiles used: A2DP sink, AVRCP, HFP hands-free unit.
- **Radio/EMC:** the module carries FCC/CE/SRRC modular certifications; the final product with enclosure, speakers and supply still needs its own EMC/safety assessment in the target market.
- **Hardware limits to put in the product spec:** 5 V / 2 A adapter, short thick cable, speakers 4 Ω / 3 W, the first-boot self-test, no acoustic echo canceller (calls are fine at moderate volume), one phone at a time.
- **Support data:** ask the client for the boot log (any serial terminal at 115200 baud on the UART port) when something goes wrong; the log contains the version, the reset reason, the self-test result and every Bluetooth event.
