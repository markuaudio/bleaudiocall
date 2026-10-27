# Changelog

Har change yahan ek line me; detail linked doc me. Format: date — kya badla — result (sahi/galat).

| Date | Change | Result | Detail |
|---|---|---|---|
| 2026-10-27 | Naya project `bleaudiocall`: kanchan speaker (A2DP/AVRCP) + HFP calls + har-type pairing/reconnect; ES8388 record path, I2S full duplex, call/music/ring audio engine | build: dekho `docs/2026-10-27-first-build.log`; hardware test pending | [01](docs/01-project-overview-hinglish.md), [02](docs/02-har-type-bluetooth-connect.md), [03](docs/03-call-audio-path.md) |
| 2026-10-27 | Serial log se bug: phone paas na ho to reconnect counter har fail par reset (infinite 10 s loop), HFP page A2DP page ke beech, har fail par audio path reclock. Fix: fail ≠ link lost (`bt_core_profile_failed`), follow-up outcome ke baad hi, idempotent stream/call stop | build: `docs/2026-10-27-final-build.log`; board test pending | [05](docs/05-final-audit-aur-optimization.md) |
| 2026-10-27 | Byte ring wrap par short read → jhoothe underruns / 0-byte mic frames. Fix: `ring_read_exact` (poora chunk ya kuch nahi) | same build | [05](docs/05-final-audit-aur-optimization.md) |
| 2026-10-27 | Release config: `-O2` (tha `-Og` debug), AVRCP cover art off; `sdkconfig` defaults se regenerate, diff verified | same build | [05](docs/05-final-audit-aur-optimization.md) |
| 2026-10-27 | Docs: learning process (25–27 Oct), final audit, setup doc copy, README/naam `blu2` sync | docs | [04](docs/04-learning-process-hinglish.md) |
| 2026-10-27 | Complete build guide (docs/06), official LyraT V4.3 schematic + layout download (Rev 4.3, 2022-01-19) + rendered pages + BOM (shopping list, board ICs, audio path parts, power tree) | docs | [06](docs/06-complete-build-guide-hinglish.md), [hardware](docs/hardware/README.md) |
| 2026-10-27 16:05 | Toolchain `D:\Vscode\esp-tooling` par move (purane folder rename se path toota, space wala path IDF nahi leta); activation/eim/VS Code settings update; `build/` fresh; **flash**; 75 s serial: reconnect 6/6 phir chup, fail par koi reclock nahi, HFP phone connect | flash + serial pass (`docs/2026-10-27-final-flash.log`, `docs/2026-10-27-final-serial.log`); music/call user test pending | [05](docs/05-final-audit-aur-optimization.md) |
