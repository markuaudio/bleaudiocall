# Changelog

One line per change; details in the linked document. Format: date — what changed — result (good/bad).

| Date | Change | Result | Detail |
|---|---|---|---|
| 2026-10-27 | New project `bleaudiocall`: kanchan speaker (A2DP/AVRCP) + HFP calls + pairing/reconnect for every kind of phone; ES8388 record path, I2S full duplex, call/music/ring audio engine | build: see `docs/2026-10-27-first-build.log`; hardware test pending | [01](docs/01-project-overview.md), [02](docs/02-connect-every-phone.md), [03](docs/03-call-audio-path.md) |
| 2026-10-27 | Bug found in the serial log: with the phone out of range the reconnect counter reset on every failure (infinite 10 s loop), the HFP page attempt started in the middle of the A2DP page, and every failure reclocked the audio path. Fix: a failed attempt is not a lost link (`bt_core_profile_failed`), follow-up only after the outcome is known, idempotent stream/call stop | build: `docs/2026-10-27-final-build.log`; board test pending | [05](docs/05-final-audit-and-optimization.md) |
| 2026-10-27 | Short reads at the byte-ring wrap point caused false underruns / 0-byte microphone frames. Fix: `ring_read_exact` (whole chunk or nothing) | same build | [05](docs/05-final-audit-and-optimization.md) |
| 2026-10-27 | Release config: `-O2` (was `-Og` debug), AVRCP cover art off; `sdkconfig` regenerated from the defaults, diff verified | same build | [05](docs/05-final-audit-and-optimization.md) |
| 2026-10-27 | Docs: learning process (25–27 Oct), final audit, copy of the setup doc, README/name `blu2` synced | docs | [04](docs/04-learning-process.md) |
| 2026-10-27 | Complete build guide (docs/06), official LyraT V4.3 schematic + layout download (Rev 4.3, 2022-01-19) + rendered pages + BOM (shopping list, board ICs, audio path parts, power tree) | docs | [06](docs/06-complete-build-guide.md), [hardware](docs/hardware/README.md) |
| 2026-10-27 16:05 | Toolchain moved to a space-free folder (ESP-IDF on Windows rejects paths with spaces); activation script, EIM config and VS Code settings updated; fresh `build/`; **flashed**; 75 s serial capture: reconnect 6/6 then idle, no reclock on failures, HFP phone connected | flash + serial pass (`docs/2026-10-27-final-flash.log`, `docs/2026-10-27-final-serial.log`); music/call user test pending | [05](docs/05-final-audit-and-optimization.md) |
| 2026-10-27 | All documentation, README, BOM and firmware log messages rewritten in English for a public GitHub audience | docs + code | [README](README.md) |
