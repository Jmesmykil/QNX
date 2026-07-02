# Q OS uMenu/uSystem — Orphan & Gap Backlog (2026-06-20)

Source: full-battery audit (orphans/plans/dead-code sweep). Bugs/security sweep appended below when it lands.
Status legend: **IN-FLIGHT** (agent working) · **DO-NOW** (quick safe autonomous fix) · **GATED** (needs creator sign-off, do NOT auto-do) · **BIG** (large feature, schedule) · **TRACK** (low-pri, logged).

## ★ 2026-06-21 RE-AUDIT (read the ACTUAL code — most 2026-06-20 entries below are STALE/DONE) ★
- **DONE (verified in source, not intent):** cheats install+manage (`qd_CheatsInstaller`/`qd_CheatsManager`, full HTTPS+minizip) · overlays toggle (`qd_SettingsLayout` Overlays tab — items 1.3/1.4 are NOT disabled; built) · file manager (`qd_VaultLayout`, 12 sidebar roots incl NSP/NCA) · save backup/restore (`qd_SaveBackup`, P0 fixed) · **Pokémon viewer** party/trainer/bag/**boxes** for SwSh+BDSP, offsets HOST-VERIFIED (`qd_SwShSaveParser`/`qd_BDSPSaveParser`/`qd_SaveEditorLayout` — item 2.2 "offsets unconfirmed" is WRONG, they're confirmed; 2.3 viewer done) · reboot→hekate (`qd_Power`) · **DO-NOW data-safety C-3/M-1/M-2/H-4 + 5.6 dead-fn = all DONE (Wave-1, code-verified)** · 3.2 STATE.toml = CREATED.
- **SAFE-BUILDABLE REMAINING:** 2.4 QdAudio Phase-2 SFX wiring (IN-FLIGHT, build v3.7.54) · .nxtheme download + mod download (Tier-2 absorption, deferred) · Pokémon save WRITE-BACK editor (primitives `EncryptPB8`/`RecomputeChecksum`/`SwishEncrypt` ready but DORMANT/zero-callers; needs HW save-test to verify-without-corrupting).
- **CREATOR/HW-GATED (do NOT auto-do):** 2.1 NSP install (PD-11 NAND; `qd_NspInstaller`/`qd_EsIpc`/`qd_NsAmIpc` all `.parked`, not just a flag) · **1.1 HOME-from-game black = the am 2128-0035 limit → the Atmosphère am-IPS-patch (absorption phase, creator go-point)** · 3.1 de-upstream rename (functional path-literals, risky) · HW functional verify of cheats/overlay/restore/viewer/game-launch (needs device cold-cycle for rc=0x3257C + creator).
- **Test rig built:** `scripts/qos-test-rig.sh` → autonomous surface GREEN (10 pass / 0 fail / 3 blocked-needs-HW). The original 2026-06-20 list below is kept for history but treat THIS section as current.

## HIGH
- **1.1 HOME-from-app BLACK — Track B (resident uMenu) not built** — uSystem `main.cpp` relaunch path. **IN-FLIGHT** (Track B agent).
- **2.1 NSP installer write path fully stubbed** — `qd_NspInstaller.hpp` / `.cpp.parked` (PlaceContent/ImportTicket/Register/Rollback → NotImplemented). **GATED** — needs libnx NCA reader + explicit creator review; do NOT unlock gates autonomously.
- **4.1 `/observe` leaks serial + WiFi/BT MAC unauthenticated** — `qd_DebugObserve.cpp:174`, route `qd_DebugServer.cpp:209`. Dev-only acceptable; **DO-NOW**-able: gate behind debug.flag token or strip serial/MAC. Pre-release blocker.

## MED — actionable (DO-NOW / soon)
- **1.2 Self-heal watchdog — not built** — uSystem MainLoop + uMenu smi heartbeat. **IN-FLIGHT** (Track B agent does this too).
- **4.3 SEC-4: g_DebugServer.Stop() not called on focus-loss** — `qd_AppletLifecycle.cpp:~39` (RemoteShell+Nxlink stop, DebugServer doesn't). **DO-NOW** (1-line, consistency).
- **5.6 `CaptureScreenshot()` dead fn + 1 MiB static BSS** — `qd_DebugServer.cpp`. **DO-NOW** (delete dead fn).
- **4.2 `/reboot` `/press` `/touch` unauthenticated** — `qd_DebugServer.cpp:109`. Dev-only; token-gate for release. **TRACK** (pre-release).
- **4.4 debug server always-on; `UL_DEBUG_SERVER_DEV=0` for release** — **TRACK** (pre-release gate).
- **3.2 `STATE.toml` missing from fork** — create `tools/qos-ulaunch-fork/STATE.toml` (v3.7.45 reality). **DO-NOW**.

## MED — bigger / gated (schedule, don't auto-do)
- **1.3/1.4 Tesla overlay disabled (B2.1-B2.4)** — `main.cpp:2470` `UL_ENABLE_TESLA_OVERLAY 0`; needs summon-chord + hid-on-main-thread (historical AMS-fatal). **BIG + DEVICE-RISK** — serial B2.1→B2.2→B2.3→B2.4.
- **2.2 BDSP save parser offsets unconfirmed** — `qd_BDSPSaveParser.cpp` ~8 TODO(bdsp). Needs a real BDSP save vs PKHeX SAV8BS. **BIG**.
- **2.3 Save Editor: PartyBox/Inventory/Trainer = placeholder** — `qd_SaveEditorLayout.cpp:726`. **BIG**.
- **2.4 QdAudio Phase-2 module missing** — `qd_Audio.{cpp,hpp}` don't exist; ~40 silent events. **BIG** (plan in ABSORPTION_PROGRAM.md §AUDIO).
- **2.5 Z2 right-click: 5+ surfaces unwired, `qd_DockLayout.hpp` missing** — `qd_DesktopIcons.cpp:933`. **BIG**.
- **2.8 Runtime cheat toggle — `dmntcht` not linked** — gated on overlay (B2.x). **BIG**.
- **3.1 B4.1-B4.7 de-upstream rename (ul::→qos::)** — ~1111 uses, 45 files, 188 path literals + boot migrator. **BIG** (precedes AMS phase).
- **3.5 Public-release checklist ~90% incomplete** — `PUBLIC-RELEASE-CHECKLIST.md`. **GATED** (release).

## LOW — tracked
- 2.6 B1.2 argv always "" (`qd_DesktopIcons.cpp:6453`); 2.7 B1.3 desktop launch-mode toggle; 2.9 K+1..L designs (design-only); 2.10 telnet cmd-launch stub (intentional); 2.11/2.12 upstream TODOs; 2.13 NxlinkServer ReceiveOne HW-unverified; 1.5 SD-removal gate HW-unverify; 1.6 uSystem 7 upstream TODOs; 3.4 hbmenu boot-migrator (verify removed).
- ORPHAN FILES (cleanup, **GATED** delete — creator gates): 5.1 uDesigner+uScreen DEAD but in Makefile; 5.2 `agent_quarantine_sp4.12/` stale dupes; 5.3 `qd_NspInstaller.cpp.parked`; 5.4 3 stale `src/*.zip`; 5.5 USB-serial test-rig design never built.

## BUGS / SECURITY / STABILITY (audit 2026-06-20)
P0 save-corruption (RestoreSave commit-on-failure, CopyOneFile truncation, cross-user restore) = **CONFIRMED FIXED** in tree.
- **CRITICAL (all the dev debug-server :6010 — fine in dev, PRE-RELEASE blockers):** C-1 `UL_DEBUG_SERVER_DEV=1` hardcoded (`qd_DebugServer.cpp:68`) → make build-flag default 0; C-2 zero auth on all routes; C-4 `/crash` live unauth. **TRACK** (release: compile out / PIN-gate the whole server).
- **C-3 `/launchnro/<idx>` OOB — no upper-bound** (`qd_DebugServer.cpp:375`): add `idx < GetIconCount()`. **DO-NOW** (real safety bug even in dev).
- **H-4 `backup_folder` path traversal in RestoreSave** (`qd_SaveBackup.cpp:412`): `Sanitize()` not applied to backup_folder → `../../` escapes `sdmc:/JKSV/`. **DO-NOW** (real data-safety, not dev-only).
- H-1/H-2/H-3 (`/reboot` `/press` `/touch` `/screenshot` `/observe` unauth) — dev-only; **TRACK** (release gate).
- **M-1 NRO icon bounds** (`qd_NroAsset.cpp:263`): add `jpeg_abs_off+icon_size<=file_size` before malloc. **DO-NOW** (malformed NRO).
- **M-2 `ParseBagFromFile` return unchecked** (`qd_SaveEditorLayout.cpp:660`): bad save → empty bag → write-back overwrites valid data. **DO-NOW** (data-safety).
- M-3 render thread blocked by stale NACP `.join()` (`qd_WindowManager.cpp:653`) — perf; M-4 `log` cmd O(filesize) byte scan (`qd_ShellCommands.cpp:162`) — perf. **TRACK**.
- L-1 `kMaxOpenWindows=128` unvalidated (note: 104 windows tested-stable earlier; leave); L-2 occlusion approx >32 windows; L-3 debug header buffer. **TRACK** (low).
