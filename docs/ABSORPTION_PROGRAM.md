# Q OS Userland-Absorption Program — SSOT (2026-06-19)

Source: 6-agent absorption audit (workflow `wf_0175acd3-22b`), source-verified. Drives the autonomous absorption run until userland is fully absorbed → Atmosphère phase.

## CRITICAL PREMISE CORRECTION
"Absorb hbmenu to free a memory pool to host our code" is **factually wrong**. hbloader's big heap (~440 MB applet / near-full-RAM app, `uLoader/.../loader_Target.cpp:112-162`) belongs to the **child NRO process** and is **kernel-reclaimed on NRO exit** — uMenu/uSystem cannot retain it. Absorbing hbmenu frees only: (a) the **PhotoViewer title slot** `0x010000000000100D` (a launch *hook*, already ours via ECS), (b) **~6-17 MB SD storage**.
**The real host for resident Q OS code = the already-built uSystem `vi:m` overlay (Option A)**, later a net-new sysmodule (Option B, additive — not from hbmenu).

## MAJOR DISCOVERY: the overlay already exists (~60%, HW-GREEN v3.0.2)
The Tesla-style overlay the creator remembered IS built and proven, sitting behind one `#define`:
- `UL_ENABLE_TESLA_OVERLAY 0` at `uSystem/source/main.cpp:1916` (init :1918, finalize :1969).
- Engine: `uSystem/.../overlay/overlay_TestLayer.cpp` (557 LOC, full `vi:m` max-Z managed-layer lifecycle :381-526, render loop :190-318), `overlay_Renderer.cpp` (RGBA_4444), `overlay_FontCache.cpp` (stbtt, no GPL taint), `overlay_Actions.cpp` (atomic bridge). HW-GREEN commit `f01d2fed`.
- It composites over any game/NRO with zero game cooperation (NOT the gated `viGetIndirectLayerImageMap` windowed-NRO path — that's dead). uSystem NPDM has `service_access:["*"]`, no patch needed.
- Disabled 2026-05-19 for: (1) chrome bled into launch transitions, (2) render thread called `hidGetTouchScreenStates()` but uSystem never `hidInitialize()` → AMS fatal.

## BACKLOG (ordered; S=<1d M=~1wk L=multi-wk; self-test via /icons /launchnro/<idx> cmd_home /screenshot /state FTP)

### PHASE 0 — hygiene
- B0.1 Update stale STATE.toml (describes parked Rust µkernel; reality v3.7.x) · CODE · S
- B0.2 Delete legacy XorTroll About dialog `ui_Common.cpp:400` (renders uLaunch URL); keep `qd_AboutLayout.cpp:112` · CODE · S
- B0.3 Strip dead weight: uDesigner, uScreen, agent_quarantine_sp4.12, stale zips/themes (~150MB) · CODE · S · (deletions = gated/confirm)
- B0.4 W16-TELNET-HARDEN: DevConsole telnet INADDR_ANY+no-auth (`ROADMAP.md:58`) — PIN+localhost or remove before public · CODE · M

### PHASE 1 — finish hbmenu absorption (launcher mechanics already EXCEED hbmenu)
- **B1.1 NACP name/author display** (BIGGEST gap — tiles show filename stem only, `qd_DesktopIcons.cpp:2299-2306`, `qd_VaultLayout.cpp:312-319`). Add `ExtractNroNacp` to `qd_NroAsset.cpp` (ASET NACP section hdr 0x18/sz 0x20), reuse `qd_CheatTitleResolver.cpp:143 PickNacpName`. Write into existing `name[64]` (do NOT grow NroEntry, `static_assert(sizeof==1632)`). · CODE · M · self-test /icons
- B1.2 Interactive argv entry (ABI plumbed `TargetInput.nro_argv`; sites pass ""). swkbd ctx-menu. · CODE · S
- B1.3 Desktop launch-mode toggle + sort (mirror Vault) · CODE · S
- **B1.4 Decommission hbmenu fallback + migrator** (remove `HbmenuPath` seed `ul_Include.hpp:34`/`menu_Entries.cpp:160`, dead `ChooseHomebrew`; boot-migrator deletes `sdmc:/hbmenu.nro`+`switch/hbmenu/` per `docs/45_HBMenu_Replacement_Design.md` §4). · CODE+DEVICE · M
- **B1.1 + B1.4 are the only two blocking "uMenu fully replaces+strips hbmenu".**

### PHASE 2 — re-light the overlay (the resident host)  [dependency: B2.1→B2.2+B2.3→B2.4]
- B2.1 Re-enable overlay (flip define) + fix transition-flash via `std::atomic<bool> g_OverlayVisible` (resident, draws transparent unless summoned) · CODE+DEVICE · M
- B2.2 Summon gesture (0% — none wired). Q-distinct chord (e.g. L+R+DPad-Up, NOT Tesla's) in uSystem main-loop input scan ~`:660-680`. · CODE+DEVICE · M
- **B2.3 Safe input (THE historical crash)** — do ALL input on uSystem main thread (owns hid for HOME), render thread reads atomics only. NO `hidsysEnableAppletToGetInput` (banned). Re-enable dispatch `main.cpp:1496-1501`. **HIGHEST-RISK item; gates runtime-mods.** · CODE+DEVICE · M
- B2.4 `IOverlayModule` interface + RenderLoop refactor (`overlay_TestLayer.cpp:190-318`) · CODE · M

### PHASE 3 — game-mods (launcher-side B3.1-3 independent; B3.4 overlay-gated)
Existing: full launcher-side cheat subsystem (`qd_CheatsManager/Layout/Installer/TitleResolver`); toggles apply next-launch only.
- B3.1 LayeredFS mod manager (`qd_ModsManager/Layout`, `.disabled`-rename like `qd_SettingsLayout.cpp:884/931`) · CODE · M
- B3.2 Mod install/import (generalize `qd_CheatsInstaller` ssl+minizip) · CODE+DEVICE · S-M
- B3.3 Per-title mod profiles (sidecar TOML) · CODE · S
- B3.4 **Runtime cheat toggle via overlay** — link vendored `dmnt:cht` (`dmntcht.h`, not linked anywhere) into the overlay; only process live with a game. Gated on B2.x. · CODE+DEVICE · M

### PHASE 4 — de-upstream/rebrand (widest blast radius — its own gated release LAST)
- B4.1 `ul::`→`qos::` (~1111 uses) · L · B4.2 `<ul/...>` include+dir rename · L · B4.3 libs/uCommon + arc `UL`→`QOS` · M · B4.4 45 residual `ui_*/am_*` renames · M · B4.5 `sdmc:/ulaunch/`→`sdmc:/qos/` MIGRATION (188 literals + boot-migrator) · L · B4.6 macros/logging (KEEP GPLv2/ISC attribution) · S · B4.7 uManager decision · S-M

## READY-FOR-ATMOSPHÈRE (minimum bar to start AMS phase)
A. hbmenu replaced: B1.1 (names) + B1.4 (fallback gone + migrator). B. Host works: B2.1-2.4 (overlay re-enabled, summon, safe-input HW-verified, ≥1 module + can load external `.ovl`). C. Runtime mods: B3.4 (live cheat toggle HW-verified). [D de-upstream + E hygiene required for public release; B4.1 rename ideally before deep AMS coupling.]
Highest device-risk gate: **B2.3 hid coexistence** (uSystem/uMenu).

## DEAD — do not resume
BackgroundIndirect windowed-NRO (`viGetIndirectLayerImageMap` 2114-0011 gate, TERMINATED) · uDesigner/uScreen/agent_quarantine · legacy `ChooseHomebrew`/`hbmenu.nro` fallback.

Full audit: `tasks/w4thuk6au.output`.

## CURRENT STATE (autonomous run, 2026-06-19)
- **DONE+VERIFIED:** B1.1 NACP names (live proof: haze.nro→"USB File Transfer", retroarch_switch.nro→"RetroArch", daybreak.nro→"Daybreak"). Control surface live in uMenu v3.7.34: `/icons` (render-thread snapshot, deadlock fixed), `/launchnro/<idx>`, uSystem `cmd_home` HOME-trigger (g_DevMode-gated), `home_trace.log` diagnostic.
- **HOME-over-NRO bug:** fix A (sys::SetForeground before Terminate) INSUFFICIENT — HW self-test confirmed it STILL HANGS (cmd_home consumed → HandleHomeButton ran → blocking `la::Terminate` on the serial MainLoop never returned; uMenu never relaunched, no fatal). Fix **B/C/D built** (uSystem.nsp 596685B): non-blocking terminate (`RequestExitNonBlocking` + `g_TerminatingNro` deadline polled in MainLoop), 500ms timeout, no UL_RC_ASSERT (degrade to LaunchMenu), + `home_trace.log` step markers. ARMED in watcher to auto-deploy+self-test on device return.
- **DEVICE STUCK:** the fix-A self-test hung uSystem's MainLoop inside HandleHomeButton → uMenu can't relaunch → needs a physical POWER-CYCLE (no remote reboot: :6010 dead, :5000 dead). Watcher `bhcy0m2qa` waits 30min for the device then stages fix B/C/D + self-tests HOME (launch sphaira → cmd_home → return? + reads trace).
- **DEPLOY OPS (learned):** uMenu = `/ulaunch/bin/uMenu/main` (.nso, hot-swap). uSystem (qlaunch) = `/atmosphere/contents/0100000000001000/exefs.nsp` — BOOT-CRITICAL; backup `exefs.nsp.bak-pre-trigger-20260619`; recover via Hekate VOL- + FTP-restore. **FTP RNFR/RNTO renames are FLAKY under load → use DIRECT OVERWRITE (`curl -T` over the target) + cmp-verify + retry.** Spine-gate: declare a task via `/Users/astral/.claude/scripts/astral-task set <id>` before any mutating Bash.
- **NEXT — code-only (no device):** B3.1 LayeredFS mod manager (IN PROGRESS) → B0.2 About-dialog + B0.x hygiene → B2.4 IOverlayModule refactor. **DEVICE-GATED (await power-cycle + HOME verdict):** B2.1 overlay re-enable → B2.2 summon → B2.3 hid-coexistence (highest risk) → B3.4 runtime mod toggle → NRO compat-sweep.

## UPDATE — 2026-06-19 (no-sleep fixed; v3.7.36 live; FULL-mode launch hazard)
- **DEVICE NOW STABLE & AWAKE.** uMenu **v3.7.36** (B1.1 names + /icons-fix + B3.1 mods + B0.2 About) deployed + verified (real names live, desktop renders clean). uSystem = **no-sleep + fix B/C/D** (596569B).
- **NO-SLEEP FIX (done):** uSystem asserts `appletSetAutoSleepDisabled(true)` at dev-init (`main.cpp:1959`, g_DevMode) AND re-asserts it every ~30 main-loop iters (`:1553`). uMenu's own no-sleep only held while uMenu was foreground → it slept the instant uMenu Finalized; uSystem is always resident so it now holds across NRO/transition/black states. Verified: device held awake through 2 reboots. `/tmp/keepawake.sh` poker (touch 640,100 q25s while :6010 up) = backup.
- **⚠️ CRITICAL HAZARD — FULL-mode NRO launch blacks the device:** `/launchnro/<idx>` (and desktop tap) auto-classified sphaira as **FULL/APPLICATION mode** (`LaunchHomebrewApplication`, donor-takeover) not applet. HOME-return then relaunched uMenu in **MenuApplicationSuspended (start mode 3)** which rendered BLACK + uMenu down. fix B/C/D only fixes the `la::` APPLET terminate path; the `app::` FULL-mode path is UNFIXED. **DO NOT auto-launch NROs / auto-test HOME until:** (a) `/launchnro` forced to APPLET mode, (b) FULL-mode HOME-return (mode 3) fixed OR a `cmd_resetmenu`/`cmd_reboot` recovery trigger added to uSystem, (c) confirm sphaira-class NROs launch applet-mode. The no-sleep means a future black stays FTP-reachable (:5000) instead of sleeping → recoverable.
- **RECOVERY DISCIPLINE (creator directive):** when an action blacks the screen, bring it back immediately (don't get stuck). Sleep-black → was unrecoverable remotely (network off); now prevented by no-sleep. Hang-black → :5000 stays up → deploy fix / (future) cmd_reboot trigger.
- **NEXT (safe, code-only / hot-swap):** B3.2 mod importer, B3.3 mod profiles, B2.4 IOverlayModule refactor, B0.1 STATE.toml. **THEN gated behind launch-safety + recovery-trigger:** the FULL-mode→applet fix, a uSystem cmd_reboot/cmd_resetmenu, then B2.1-2.3 overlay (hid-coexistence = highest device risk), B3.4 runtime toggle, compat-sweep.

## UPDATE 2 — SELF-SUFFICIENT RECOVERY PROVEN + window-crash orphan (2026-06-19)
- **RECOVERY IS NOW PROVEN (self-sufficient, no power-cycle).** uSystem (599201B, deployed) has THREE remote recovery levers, all FTP-file-triggered + g_DevMode-gated, polled in MainLoop ~q300ms: `cmd_home` (relaunch uMenu), `cmd_resetmenu` (force-terminate any app/applet + LaunchMenu NORMAL mode), `cmd_crash` (fatalThrow 0xCAFE → Atmosphère fatal_auto_reboot 10s → clean CFW boot). **`cmd_crash` HW-VERIFIED: drop sdmc:/ulaunch/cmd_crash → recovered in ~50s.** Foundation = the no-sleep fix (device stays awake/reachable on :5000 even when uMenu is black). HOME-from-app also fixed: app:: branch terminates the app (non-blocking) + LaunchMenu NORMAL (mode 2) — no more MenuApplicationSuspended (mode-3) black.
- **HONESTY FAILURE LOGGED:** the perf work was reported "parked at a fill-rate wall, ~40fps@8 windows" but the actual 100-window GOAL was NEVER tested — extrapolated from 8. Creator opened ~25 windows → **CRASHED Atmosphère** (a real resource bug, almost certainly VRAM/heap exhaustion from per-window textures: WIN-2 content-bake + WIN-A shadow-bake + nine-patch ≈ MBs/window × 25). The "fill-rate wall" conclusion was premature. ORPHAN: window system crashes AMS at ~25 windows; the 100-window goal is UNMET + UNTESTED. Now testing for real (open until crash → fix → retest to 100); recovery net makes it safe.
- DISCIPLINE (RSI): test the ACTUAL target with real device evidence; never extrapolate-and-park. See memory `project_qos_umenu_remote_recovery.md`.

## UPDATE 3 — 100-WINDOW GOAL MET (DONE-VERIFIED, 2026-06-19)
- **v3.7.38: opened 104 windows, ZERO crash, ft FLAT ~25,500µs (~39fps) from 8→104.** Screenshot confirms clean render; temps healthy (PCB 44 / SoC 46°C). The honesty-failure orphan is RESOLVED with real device evidence (was crashing at ~8/25).
- FIX (qd_Window/qd_Frame/qd_WindowManager): (1) null-check ALL SDL_CreateTexture/SetRenderTarget → degrade to live-render on failure (was the crash path: unchecked SetRenderTarget=-1 under VRAM pressure → NVN corruption → reboot); (2) evict content-bake + shadow-bake textures for all but the top-`kMaxBakedWindows=8` visible windows → bake VRAM capped ~97MB regardless of N; (3) `kMaxOpenWindows=128` graceful cap. The flat ft proves the eviction + `kMaxWins=32`-bounded occlusion cap per-frame cost.
- REMAINING window polish (non-blocking): files/saves windows do a synchronous content build on the render thread → ~1s freeze on open (should be async/placeholder-then-swap). Logged, not yet done.
- NEXT ORPHANS (the rest of "complete all orphans"): UI alignment, theming + icon-persistence, sound — from audit wk89ull51, via sonnet swarms, each DONE-VERIFIED on the now-recoverable device.

## UPDATE 4 — HOME-relaunch-black UNRESOLVED (2 attempts); icon-persistence fixed; swarming (2026-06-19)
- **v3.7.39 deployed** (uMenu: HOME foreground-retry + icon-persistence; uSystem: drain+settle). HOME-from-NRO retest: **STILL BLACK** — trace confirms terminate→150ms-settle→LaunchMenu all execute, but uMenu never renders (:6010 never starts, no fatal). TWO fixes failed: (1) terminate-flow, (2) drain+settle+foreground-retry. INSIGHT: the NRO launches as a LIBRARY APPLET (la:: branch), so the "ApplicationExited" drain was MOOT — only the 150ms settle applied + failed. Likely STRUCTURAL applet-display-handoff (a /crash reboot relaunches uMenu fine = clean kernel layer teardown; in-system relaunch blacks). **MITIGATED by cmd_crash recovery (~50s), NOT fixed.** Deeper investigation in progress (uMenu vi-layer recreation / upstream-uLaunch NRO-return comparison / longer settle for la::).
- **icon-persistence FIXED (v3.7.39, needs reboot-verify):** proactive blob SaveToDisk after prewarm (qd_DesktopIcons ~3851) + OnExitRequest hook (qd_AppletLifecycle) + boot-load (already in ctor). Was: SaveToDisk only via atexit/dtor = skipped on Switch relaunch → never persisted.
- **SWARMING NOW (disjoint files):** alignment (#1 callout — shared layout-constants + chrome/dock/hot-corners/tooltips + saves/cheats/vault/settings menus), sound (latency 4096→1024 + 16-24 ch, wire dead SFX), relaunch-deep. Theming next (qd_Frame conflicts with alignment).
- ⚠️ **202 files uncommitted** — creator commits.

## AUDIO — Phase 1 done (v3.7.40); Phase 2 QdAudio QUEUED
- **Phase 1 (DONE, in v3.7.40, needs HW-verify):** Mix_OpenAudio buffer 4096→1024 (~93ms→~23ms) + Mix_AllocateChannels(24); wired 4 dead SFX (PageTurn/Error/FavoriteOn/Off) in qd_Launchpad; boot-chime PreloadSfx (off the hot path). Files: Plutonium audio_Audio/Sfx + qd_Launchpad/qd_LaunchpadHostLayout.
- **Phase 2 QUEUED — full QdAudio desktop SFX module (~40 silent events):** create source/ul/menu/qdesktop/qd_Audio.cpp + include/.../qd_Audio.hpp; ONE `PlayDesktopSfx(DesktopSfxEvent)` entry point + loaded-SFX table (every subsystem calls the coordinator, not its own handles); wire QdAudio::Initialize/Finalize into MenuApplication::LoadBgmSfxForCreatedMenus / DisposeAllSfx. Events: desktop icon nav, app-launch confirm, folder open/close, ctx-menu open/close, hot-corner, Vault open/close + file ops, Monitor/Settings open/close + item change, lockscreen unlock ok/fail, toast, power-menu/sleep/shutdown, theme-change, login BGM fade-in, dialog open/close/nav/confirm/cancel. DO NOT touch main.cpp. GATED on Phase-1 HW-verify; significant (not a quick patch). Run AFTER the HOME-from-game fix is verified.

## RELAUNCH-BLACK — BREAKTHROUGH (v3.7.41 boot-trace, 2026-06-19)
- 3 fixes FAILED (terminate-flow; drain+settle+fg-retry; latch-removal+500settle). All targeted foreground/display. WRONG layer.
- Instrumented uMenu startup (BtWrite→/ulaunch/umenu_boot_trace.log, flushed per step) + viInitialize probe. EVIDENCE:
  - **uMenu#1 (clean boot) trace COMPLETE**: __appInit ENTRY (sm+fsdev OK) → vi probe rc=0 → __nx_win_init RETURNED OK → Renderer → Load() → FIRST OnRender frame. uMenu boots fully; display/__nx_win_init is FINE; vi probe is BENIGN (not the earlier "no boot" — that was a transient sleep).
  - **uMenu#2 (relaunch after sphaira via HOME) trace EMPTY** — never reaches the FIRST checkpoint (right after smInitialize+fsdevMountSdmc). BtWrite proven working by uMenu#1.
- **CONCLUSION: relaunched uMenu#2 dies BEFORE its first SD write — earliest process startup (crt0/__libnx_init/smInitialize/fsInitialize/fsdevMountSdmc) OR the process never runs. NOT the display init.** Only difference vs uMenu#1 = the RELAUNCH context (uSystem terminates NRO then immediately LaunchMenu).
- **PRIME SUSPECT + FIX DIRECTION**: uSystem doesn't WAIT for the prior applet's process to be truly dead/reaped before LaunchMenu (fixed 500ms sleep ≠ real wait) → uMenu#2's early sm/fs init blocks on the dying app's fs/sm/applet-slot. Fix = wait-for-prev-process-death (appletHolderWaitInteractiveOut / holder exit event / pm poll) before LaunchMenu. Investigate vs upstream uLaunch menu-relaunch loop.
- Recovery confirmed: cmd_crash = SAFE CFW reboot, desktop back @50s, NO strand. Creator OK'd safe-reboot-recovery for tests; "don't crash it" = don't gratuitously break the working device / never OFW reboot.

## NIGHT SESSION — autonomous until 05:30 (creator directive 2026-06-19 ~22:17)
Work autonomously optimizing the ENTIRE system until READY to absorb Atmosphère, then STOP + wait for creator.
- **HARD STOP**: do NOT begin absorbing Atmosphère autonomously. Harden uMenu+uSystem first; when solid + ready for the Atmosphère step, STOP and wait for the creator.
- **ORDER**: (1) HOME-relaunch black — verify the v3.7.42 sm-readiness fix; iterate sm→fs until FIXED. (2) Red-team the whole system (qos-redteam-audit workflow): stress+pentest+pnytail+premortem → fix every confirmed issue → re-audit until clean (this is the commit gate). (3) Orphans: theming, QdAudio Phase-2. (4) Perf optimize. (5) Solid + ready → STOP + wait.
- **DEVICE SAFETY**: cmd_crash = SAFE CFW reboot, OK for recovery (creator-approved); NEVER OFW/bpcRebootSystem; never gratuitously crash/strand the working device; patient recovery polls; ONE device test at a time.
- **HARD RULES**: NEVER eMMC/partition/autoRCM/fuse. Online OK but NEVER touch Nintendo servers. Auto-start can't ship. NEVER commit (202+ dirty) until red-team clean + explicit creator ask; never AI co-author; Jmesmykil sole author.

## RELAUNCH-BLACK — TRACK A FAILED, root cause is ARCHITECTURAL (v3.7.43, 2026-06-19)
- v3.7.43 home_trace: `sm port accepted @1 poll (~10ms)` AND `FS probe OK @1 poll (~10ms)` — BOTH sm+fsp-srv ready immediately. `LaunchMenu rc=0`. `uMenu#2 IsActive=YES` all 20 polls (alive 2s). Boot-trace STILL EMPTY. STILL BLACK.
- **5 fixes now FAILED**: settle(v41), latch(v41), drain, sm-probe(v42), fs-probe(v43). Pre-LaunchMenu timing (settle/probes) AND post-LaunchMenu behavior (pump/poll) BOTH fail. Services CONFIRMED ready. uMenu#2 launches + stays alive but hangs before its 1st SD write REGARDLESS.
- **CONCLUSION: NOT a timing or service-readiness race.** It's the in-session 2nd-library-applet relaunch MECHANISM (ecs::RegisterLaunchAsApplet, terminate-uMenu-then-recreate after the NRO). The probe approach is dead.
- **FIX = TRACK B (architectural): keep uMenu RESIDENT — do NOT terminate+relaunch.** Real uLaunch keeps the menu in LibAppletMode_AllForeground and auto-refocuses when the foreground app exits. GAME case (creator priority "HOME from a game") ~4-line change: don't LaunchMenu, let the still-alive uMenu#1 refocus + push HomeRequest — BUT HW-unverified (AM may need appletRequestToGetForeground) + must verify uMenu#1's holder survives a game launch. NRO case needs a 2nd appletHolder (la:: only tracks one, clobbered by the NRO launch).
- STOP timing fixes. Develop Track B carefully + ONE consolidated device test. Recovery (HTTP /crash) reliably restores desktop ~40-50s; device never permanently stranded. v3.7.43 instrumentation (IsActive poll, sm/fs probes) = harmless, remove when Track B lands.

## RESILIENCE AUDIT — make uSystem never-need-injection (v3.7.44, 2026-06-20)
ROOT CAUSE of the "had to RCM re-inject" failures: the device SLEPT → WiFi off → :5000+:6010 unreachable → only physical recovery left.
- `HandleSleep()` (main.cpp:490 → `appletStartSleepSequence`) is reached from THREE triggers: Unk_Sleep general-channel msg (:806), DetectShortPressingPowerButton (:944), and **AppletMessage::AutoPowerDown = idle auto-sleep (:955)**. `appletSetAutoSleepDisabled(true)` (dev, :1614/:2352) only suppresses the idle TIMER and is NOT honored across applet transitions / when uMenu isn't foreground — so AutoPowerDown still fired and slept the unit.
- **FIX (v3.7.44, dev-gated backstops, never ship):** `HandleSleep()` returns early when g_DevMode (closes all 3 sleep paths); `Unk_Shutdown` (:810) suppressed when g_DevMode (shutdown = power-off = Erista RCM re-inject). Net: in dev the console NEVER sleeps/powers-off → always reachable → ALWAYS remotely recoverable (cmd_crash / HTTP /crash) → NO injection.
- VERIFIED-SAFE: reboot path = `appletStartRebootSequence` (:814) → hekate autoboot → CFW. NO `bpcRebootSystem` anywhere (that was the old OFW landmine — absent now).
- REMAINING hardening (TODO, documented): (1) gate `SdCardRemoved→appletStartShutdownSequence` (:969) in dev — an SD glitch currently powers off→inject; sys-ftpd runs from RAM so staying up keeps it recoverable. (2) SELF-HEAL WATCHDOG — uMenu heartbeat (via smi IPC last-UpdateStatus tick) → uSystem auto force-relaunch a hung/black uMenu, escalating to fatalThrow(CFW reboot) after K fails → device self-heals with no operator. (3) boot-stall robustness (if uSystem doesn't fully boot, no in-process backstop can run). Note: with no-sleep alone the device is always reachable, so the watchdog is a bonus, not required to avoid injection.
- Validation: v3.7.44 + 24min hands-off idle-watch (deploy→reboot→confirm→leave idle→must stay reachable). RESULT: stayed AWAKE 18min idle (frame 7563→65364 @60fps) = no-sleep VALIDATED.

### v3.7.45 — inject-risk sweep complete (4 gates live)
Sonnet sweep of ALL uSystem strand paths. Full disposition:
1. HandleSleep (sleep) — GATED (g_DevMode) ✓  2. Unk_Shutdown — GATED ✓  3. Unk_Reboot=appletStartRebootSequence — SAFE (→hekate→CFW) ✓  4. SdCardRemoved→shutdown (main.cpp ~:986) — GATED this pass ✓  5. cmd_crash fatalThrow — intentional recovery ✓  6. ams::Exit abort — unreachable/auto-reboots ✓
- **CRITICAL OFW LANDMINE FIXED:** uMenu `power::Reboot()` (qd_Power.cpp:39) did a bare `bpcRebootSystem()` → STOCK OFW (the exact strand from earlier this session), and the power-menu "Reboot" button (ui_StartupMenuLayout.cpp:181) called it. FIX: `Reboot()` now delegates to `RebootToHekate()` (payload-staged → CFW) — fixed at source so every caller is safe. uMenu `Shutdown()` (bpcShutdownSystem) left as-is = deliberate user action.
- RESIDUAL (documented, not blocking): (a) boot-stall before MainLoop (UL_RC_ASSERT in Initialize) — no in-process backstop possible; (b) RebootToHekate's own fallback is bare reboot→OFW only if reboot_payload.bin missing (payload IS present on device); (c) SELF-HEAL WATCHDOG planned but NOT built — needs a uMenu-side heartbeat sender (smi `SystemMessage::Heartbeat` every ~30s) + uSystem receiver/checker in MainLoop → force-relaunch→escalate to fatalThrow(CFW). Full both-sides plan in this session's a7eccd95 agent output. Bonus only: with no-sleep the device is always reachable, so manual cmd_crash already recovers any hang.
- NET: in dev the device never sleeps/shuts-down/OFW-reboots → always remotely recoverable → NO physical RCM inject needed.

## v3.7.46-48 — resident-uMenu fix + watchdog regression (2026-06-20)
- **v3.7.47 RESIDENT FIX (the real HOME-black fix):** dual appletHolder in la:: — `g_MenuLibraryAppletHolder` for uMenu (NEVER clobbered by NRO launches) + `g_LibraryAppletHolder` for NROs. `HolderFor(id)`, `IsMenuActive()`, `TerminateMenu()`. main.cpp HOME paths (g_TerminatingNro + g_TerminatingApp) now REFOCUS the resident uMenu (`appletRequestToGetForeground()` + HomeRequest) instead of LaunchMenu. GAME case: app::Start uses a SEPARATE AppletApplication holder → uMenu survives → refocus should work. NRO case: dual-library-applet coexistence HW-UNVERIFIED (fallback to LaunchMenu if menu holder dead). Files: la_LibraryApplet.cpp/.hpp, ecs_ExternalContent, main.cpp.
- **WATCHDOG REGRESSION → DISABLED (v3.7.48):** the v3.7.46 watchdog's uMenu heartbeat (`SendHeartbeat`, SYNC SMI on the render thread, every 1800 frames) BLOCKS FOREVER → uMenu froze at frame 1800 (~30s idle), :6010 dead, /icons ERR. DISABLED at qd_AppletLifecycle.cpp:189. **VERIFIED FIXED** (frame ran past 2231, icons stable). uSystem watchdog dormant (gates on heartbeat tick that never arrives). Re-enable ONLY with a non-blocking heartbeat.
- **Wave 1 (uMenu, in v3.7.48):** save data-safety (H-4 path traversal + M-2 unchecked parse), debug-server hardening (C-3 OOB + dead-code + /observe serial/MAC strip), M-1 NRO bounds, QdAudio coordinator module (qd_Audio.cpp/.hpp — coordinator only, per-layout SFX wiring deferred). All compile GREEN.
- **OPEN REGRESSION (v3.7.47+, INVESTIGATE):** slow boot-record/icon load (~30s; was instant on v46) + slow game-launch via /launchnro (~4s → >60s; eventually works). Likely the resident-fix LaunchMenu/SystemStatus path or Wave-1. Makes HOME-from-game hard to auto-test (game won't foreground promptly). 
- **HOME-from-game: resident architecture IN, NOT YET HW-VERIFIED** (blocked by the launch slowdown). Device STABLE + recoverable (no inject). v3.7.48 = current on device (uMenu 3.7.48 + uSystem 3.7.47 resident).

## v3.7.49 — DUAL-HOLDER GATE FIX (2026-06-20)
- ROOT of the v3.7.47 bog (slow icons/launch, intermittent hang): the dual-holder broke the launch-serialization gate in main.cpp `HandleAction`. Every launch action gated on `!la::IsActive()`, which (pre-dual-holder) stayed true until uMenu terminated itself → launch waited for uMenu to release the display. With the dual holder, `la::IsActive()` only checks the (empty) NRO slot → returns false instantly → `app::Start()` fired WHILE uMenu was foreground → uMenu's per-frame appletRequestToGetForeground() fought app::SetForeground() → AM-IPC tug-of-war → ~4fps bog.
- **FIX (v3.7.49, main.cpp HandleAction):** new `const bool la_idle = !la::IsActive() && !la::IsMenuActive();` + all 12 launch-action guards changed `!la::IsActive()` → `la_idle`. Launch now fires only once uMenu has terminated itself (both holders empty). Restores v3.7.46 serialization; Track B refocus (post-launch) unaffected.
- **VERIFIED:** icons now load ~4s (was ~30s) — boot bog FIXED. Device boots clean on v3.7.49.
- **TEST-HARNESS LIMITATION:** `/launchnro` (debug route) does NOT make uMenu exit/Finalize, so with `la_idle` the gate never opens for it → can't auto-launch a game remotely. NOT a product bug — the normal tap-a-game flow makes uMenu Finalize → gate opens → launch. **HOME-from-game must be verified by the creator manually (tap game → HOME).**
- STATE: v3.7.49 on device (uMenu 3.7.49 + uSystem 3.7.49 gate fix). Wins locked: no-inject, freeze-fix, boot-bog-fix. HOME-from-game = resident arch in place, awaiting manual HW verdict. If manual tap-launch fails, the la_idle gate is too aggressive — loosen it.

## v3.7.50 — RESIDENT APPROACH DISPROVEN FOR GAMES → REVERT (2026-06-20)
- **DECISIVE EVIDENCE** (uMenu log_uMenu.log): `LaunchIcon i=71 name='Cuphead' kind=2` → `LaunchApplication(0x0100a5c00d162000) failed rc=0xD37C — desktop stays live`. So `app::Start(game)` FAILS with **rc=0xD37C** when uMenu's library-applet holder is kept RESIDENT (this worked in pre-resident v3.7.46). **A library applet (uMenu) cannot coexist with an Application (game) launch — AM rejects it.**
- **CONCLUSION:** the resident-uMenu (dual-holder) approach is a DEAD END for games. uMenu MUST exit to launch a game → HOME-from-game then necessarily needs uMenu to RELAUNCH → which is the original in-session library-applet relaunch hang (6+ fixes failed). The residency detour can't fix HOME-from-game; it only broke game-launching (rc=0xD37C), the boot (bog/flaky), and added the heartbeat-freeze.
- **ACTION: REVERTED the resident fix** (v3.7.51) → single g_LibraryAppletHolder, `!la::IsActive()` launch gates, LaunchMenu relaunch on HOME. KEPT: resilience (no-inject), freeze-fix (heartbeat disabled), Wave-1 uMenu fixes, instrumentation. Result = stable v3.7.46-equivalent: games launch (app::Start works), HOME-from-app blacks-but-recovers (no inject).
- **HOME-from-game REAL FIX (future, fresh approach — NOT resident):** must fix the in-session 2nd-library-applet relaunch hang itself (uMenu#2 alive-but-parked before fsdevMountSdmc; appletInitialize/OpenLibraryAppletProxy block hypothesis from a652517c). OR a deeper architectural change. The resident path is closed (rc=0xD37C).

## v3.7.51 — CONFIRMED STABLE BASE (audit 2026-06-20)
- **rc=0x3257C game-launch failure was DEVICE-STATE, not code.** A creator cold power-cycle cleared it; audit then launched Pokémon Brilliant Diamond (foreground @~2s) on v3.7.51. (Cause: ~15 failed app::Start attempts + dozens of cmd_crash fatal-reboots in one day corrupted NS's app-launch state; only a cold power-off clears it. cmd_crash/fatal-reboot does NOT fully reset NS — operational note.)
- **v3.7.51 = the solid stable base.** Confirmed working: games launch (~2s), boots clean, icons load fast. LOCKED WINS: no-inject resilience (HandleSleep/Unk_Shutdown/SdCardRemoved dev-gates + Reboot→hekate), freeze-fix (heartbeat disabled), Wave-1 data-safety/security (path-traversal, unchecked save-parse, debug-server OOB/leak/dead-code, NRO bounds, QdAudio module).
- **LONE open functional bug: HOME-from-game/app → BLACK** (the in-session relaunch hang). Recoverable (cmd_crash, no inject). Resident detour proven-dead (rc=0xD37C). Real fix = the relaunch hang itself, fresh approach.
- DEVICE: v3.7.51 (uMenu+uSystem). Working tree: 200+ uncommitted files (creator gates commits). On-device.
- OPERATIONAL: after heavy test thrashing, a COLD power-cycle (not cmd_crash) is needed to reset NS/Application state.

## v3.7.53 — ★ ROOT CAUSE of HOME-from-game/relaunch BLACK FOUND + am-layer FIXED (2026-06-20) ★
- **It was an `am` (Applet Manager) crash, NOT a uMenu hang.** Evidence chain (evidence-first per RSI):
  1. v3.7.52: added a raw-fs `EarlyTrace` in uMenu `__appInit` that writes BEFORE fsdevMountSdmc (works the instant fsInitialize returns). On a relaunch, uMenu#2 early-trace + boot-trace are BOTH EMPTY → uMenu#2's process never reached `fsInitialize`.
  2. uSystem log = crash-LOOP: `"No application or library applet is active... Launching uMenu with start mode 3"` repeated.
  3. Atmosphère crash report `*_0100000000000023.log`, **Process Name "am"**: Data Abort / NULL deref @0x0, whole stack in am's module. So `am` dies setting up the 2nd library-applet create; uMenu#2 never spawns; uSystem relaunches → loop → black.
- **CAUSE:** `ul::system::la::Create` (la_LibraryApplet.cpp) only `appletHolderClose`d the holder INSIDE `if(HolderIsActive)`. When uMenu#1 FINISHED on its own (exiting to launch a game / HOME flow), the holder is NOT active (appletHolderCheckFinished==true) yet still owns an OPEN accessor service `h->s`. `appletCreateLibraryApplet` on that DIRTY holder → am NULL-deref.
- **FIX (v3.7.53):** moved the close OUT of the active-only branch — always `appletHolderClose(&g_LibraryAppletHolder)` when `serviceIsActive(&h->s)` before the create. First-ever launch (zero'd holder) safe. CONFIRMED on HW: am no longer crashes (crash-report count flat across a cmd_resetmenu relaunch).
- **REMAINING 2nd layer:** after the am-fix uMenu#2 SPAWNS but EXITS CLEANLY before fsInitialize (empty EarlyTrace, NO crash report) → uSystem still loops → still black via cmd_resetmenu. Exit is in crt0/__libnx_init/uLoader (before any uMenu user code). **Possibly an artifact of the rc=0x3257C device-state corruption** (games also fail to launch right now) rather than a real 2nd bug.
- **BLOCKED ON: creator COLD POWER-CYCLE.** The device NS state is corrupted (rc=0x3257C, games won't launch); warm reboot/fatal does NOT clear it; remote shutdown is dev-gated → cannot cold-cycle remotely. After a cold-cycle: deploy v3.7.53, launch a kind-1 game, press HOME → if it returns to uMenu, HOME-from-game is FIXED; if still black, the 2nd-layer early-exit is a real bug to chase (instrument uLoader/crt0).
- IDs confirmed: uMenu process = **010000000000100d**; am sysmodule = **0100000000000023**.
- DISPROVES the OpenLibraryAppletProxy/appletInitialize-block AND "uMenu#2 parked-alive" hypotheses — it was an am crash-loop, not a hang.

## ★★★ HOME-FROM-GAME — FINAL DIAGNOSIS + ATMOSPHÈRE-ABSORPTION FIX PLAN (2026-06-21) ★★★
*(This is the cataloged ground-truth for the Atmosphère absorption phase. The HOME-from-game fix REQUIRES modifying `am`, which requires the absorbed CFW stack — this is "the part that fixes everything".)*

### DEFINITIVE ROOT CAUSE
The HOME-from-game / any in-session menu relaunch black screen = **`am` (Applet Manager) REJECTS the 2nd in-session ECS-backed library-applet creation** with **am error 2128-0035** (exit_rc `0x4680`; module 128 = am, desc 35 — undocumented). uMenu#2 abnormal-exits (exitreason=2) BEFORE its own code runs (empty pre-fsdev raw-fs trace; no CPU-exception creport). **This is stock `am` behavior — NOT fixable from uMenu/uSystem.**

### EVIDENCE CHAIN (proven on hardware)
1. uMenu#2 raw-fs pre-`fsInitialize` trace EMPTY → uMenu#2 never reaches its code.
2. uSystem log = relaunch crash-loop ("no applet active → Launching uMenu start mode 3").
3. Layer-1 (separate, FIXED): am NULL-deref CRASH on a dirty holder (creport program 0023 "am", 2168-0002) → fixed by always `appletHolderClose` on the finished holder (v3.7.53). am no longer crashes.
4. Layer-2 (the real bug): DIAG reads uMenu#2's exit Result via `serviceDispatch(&holder.s, 30)` (GetResult) = **0x4680 = am 2128-0035**.
5. **DECISIVE:** the `[DIAG-L2]` line APPEARS on the failed relaunch → the holder-close path (Join+Close) EXECUTES → yet the next create STILL returns 0x4680. So **am rejects the create regardless of holder state — not a dirty holder.**

### HYPOTHESES RULED OUT (all on hardware)
- **AppletId:** eShop (0x14) ≡ PhotoViewer (0x15) — both fail identically 0x4680.
- **Dirty holder:** the close executes (DIAG-L2 proves it) and it still fails.
- **ecs UnregisterExternalContent:** upstream has none; disabling QOS's didn't help.
- **IndirectLayer:** mode is `LibAppletMode_AllForeground` (la_LibraryApplet.cpp:93); BackgroundIndirect removed.
- **Upstream divergence:** QOS is AHEAD of upstream (QOS has the holder-close + the Terminate-Join that upstream LACKS). Upstream HOME-from-game is NOT confirmed working (may also fail; issue #68 "cannot return home" closed-unresolved). The upstream diff found NO QOS-only regression — QOS's relaunch code is more correct than upstream's.

### WHY uMenu/uSystem CANNOT FIX IT
`am` permits a FOREGROUND applet to self-create (`appletCreateLibraryAppletSelf` — how QOS's homebrew-windowed path works) but REJECTS the BACKGROUND daemon (uSystem) creating the 2nd in-session library applet. After a game exits there is NO foreground uMenu to self-create (it MUST exit for the game — keep-resident is impossible, `app::Start` → rc=0xD37C). So the re-creation must come from the background daemon → am rejects (0x4680). Every uMenu/uSystem-side avenue is exhausted.

### THE FIX = ATMOSPHÈRE ABSORPTION (modify `am`)  ← the absorption phase's first target
- **Target:** `am`'s `ILibraryAppletCreator::CreateLibraryApplet` (and/or the applet-creation validation path) — the check that yields **2128-0035 (am desc 35)** for the 2nd in-session ECS-backed create from a background/system-applet caller.
- **Approach:** an **am-mitm** sysmodule via Atmosphère's mitm framework (intercept the library-applet-create IPC and permit/repair the create), OR a binary patch of `am`. `am` is stock Nintendo — Atmosphère's mitm/patch stack is the only lever, hence absorbing Atmosphère.
- **Absorption-phase to-dos:** (1) locate `am`'s CreateLibraryApplet handler + the exact 2128-0035 condition; (2) decide mitm vs patch; (3) build/deploy the am-mitm inside QOS's absorbed Atmosphère; (4) re-run the HOME-from-game HW test (needs a clean cold-cycle first — see operational note).

### BANKED WINS (all verified on HW, keep)
no-inject resilience (HandleSleep/Unk_Shutdown/SdCardRemoved dev-gates + Reboot→hekate) · freeze-fix (heartbeat disabled, frame advances) · am NULL-deref CRASH fix (close-on-finished) · Wave-1 data-safety/security. HOME-from-game blacks but RECOVERS via cmd_crash (no inject) = recoverable-known-issue until the am fix.

### OPERATIONAL REALITIES
- **rc=0x3257C** (game-launch fails) = device-state NS corruption from heavy test-thrashing (failed app::Start + fatal-reboots). Clears ONLY on a TRUE cold-cycle (= RCM re-inject on Erista; warm reboot / hard-reset that chainloads back to CFW does NOT clear it). → MINIMIZE test-thrashing; HOME-from-game HW verification needs a fresh cold-cycle.
- Reproduce the relaunch WITHOUT a game: `cmd_resetmenu` (same LaunchMenu(MainMenu) path).
- IDs: uMenu process = 010000000000100d; am sysmodule = 0100000000000023 (module 128).

### OPTIONAL STOPGAP (until the am fix; HW-UNVERIFIED, not yet implemented)
Graceful auto-recovery: when uSystem detects N consecutive failed relaunches (uMenu abnormal-exit 0x4680 in the MainLoop relaunch-loop), auto `RebootToHekate` (clean, no inject) instead of looping black → the device self-heals to the desktop (loses the suspended game, ~50s) rather than a permanent black screen. Implement in uSystem MainLoop loop-detection; gate so it never boot-loops on a clean boot.

### AM-FIX DETAILED PLAYBOOK (absorption-phase, vetted 2026-06-21)
**TARGET (HIGH confidence):** `am` (program 0100000000000023, module 128) → `ILibraryAppletCreator::CreateLibraryApplet` = **IPC cmd 4** (fw 3.0+). Obtained via `IAllSystemAppletProxiesService::OpenSystemAppletProxy → ISystemAppletProxy::GetLibraryAppletCreator`. uSystem (background) calls cmd 4 → am returns 2128-0035 on the 2nd in-session create. NOTE: `appletCreateLibraryApplet` (cmd 4) and `appletCreateLibraryAppletSelf` (cmd 10) are **SEPARATE am commands** (libnx `_appletHolderCreate` creating_self flag → different cmd), which is exactly why foreground self-create works and background create fails.
**FIX LEVER (HIGH confidence):** **MITM is NOT viable** (the rejection is inside am's own handler; ams_mitm only intercepts a sysmodule's OUTBOUND calls). → **binary IPS patch of am's NSO**, deployed at `SD:/atmosphere/exefs_patches/am/<BuildID-first8>.ips`, applied in-memory at boot, removable by deleting the file (no NAND write). Precedent: `misson20000/exefs_patches` am_dev_function.
**PRECONDITION (MEDIUM — must RE-verify):** hypothesis = am enforces a per-system-applet-proxy-session library-applet slot/quota; a background caller's 2nd create is rejected if the prior accessor isn't fully released / a counter not reset. desc 35 = "applet launch failure". MUST confirm by reading the am binary — could instead be a focus/caller-type check or an AppletAttribute-flag check.
**FIRST 3 TASKS (human-gated before any boot):** (1) extract am NSO from the emuMMC fw + record BuildID/.text-offset/SHA256/exact-fw-version; (2) Ghidra-RE (Switch loader) the cmd-4 handler → find the conditional branch that returns 0x4680 + its NSO offset + the bytes (this RE is the MAKE-OR-BREAK; it confirms the precondition + the exact patch); (3) draft the IPS, dry-run-validate by re-loading the patched NSO in Ghidra, then **creator reviews** before SD deploy.
**SAFETY:** emuMMC-only; RCM injector on hand; one variable at a time (am patch alone, not + uSystem changes); minimal patch surface (only the one branch); rollback = delete the .ips (or RCM→hekate→delete if boot hangs).
**AUDIT NOTES:** Option C (uSystem uses cmd 10 instead) is a DEAD-END for game→menu (cmd 10 needs a library-applet caller context; after a game there's no foreground uMenu, only the system applet) — the cmd-4 am patch is the only path for the primary bug. PREREQS before RE: confirm Stratosphere doesn't already reimplement am (patch that if so); pin the EXACT emuMMC fw version (am differs per fw).

### ★ STATUS CORRECTION (2026-06-21): NOT READY for absorption — the HOME-from-game am-limit is just ONE concluded item; a LARGE uMenu program remains. ★

## MASTER PROGRAM ROADMAP — the real "finish everything" (must complete A–E before the absorption phase F)
**A. NRO ABSORPTION — replace hbmenu by re-implementing the useful homebrew NATIVELY in QOS.**
Device /switch inventory: JKSV, EdiZon, DBI, haze, tinwoo, daybreak, aio-switch-updater, ThemezerNX, hb-appstore, sphaira, sys-clk-manager, sys-con, reboot_to_hekate/payload, Quick-Reboot, Switch_90DNS_tester, retroarch, mgba, linkalho, nso-icon-tool, SimpleModDownloader/Simple_Mod_Alchemist, Ultrahand-Reload, breeze, Switch_themes_Installer.
- ABSORB natively: save backup/restore (JKSV→qd_SaveBackup, PARTIAL) · save editor + cheats (EdiZon→qd_SaveEditorLayout, PARTIAL) · USB file transfer/MTP (DBI/haze→MISSING) · NSP install (tinwoo/DBI→qd_NspInstaller, STUBBED/gated) · fw+CFW update (daybreak/aio-switch-updater→MISSING) · themes (ThemezerNX→PARTIAL) · overclock (sys-clk→MISSING) · homebrew store (hb-appstore→MISSING) · 90DNS test (MISSING) · mod download (SimpleMod→mods dir).
- KEEP launchable (apps, don't absorb): retroarch, mgba (emulators) · sys-con (sysmodule). sphaira→obsoleted by QOS launcher.
**B. FULL POKÉMON APP — complete BDSP + SwSh save viewer/editor/backup.** qd_BDSPSaveParser (offsets UNCONFIRMED, ~10 TODO) · qd_SaveEditorLayout (PartyBox/Inventory/Trainer = placeholders, ~9 TODO) · qd_SwShSaveParser · qd_SaveAutoscan · qd_SaveBackup. Verify offsets vs PKHeX SAV8BS/SAV8SWSH; finish party/inventory/trainer edit + viewer; HW-test with a real save.
**C. DE-UPSTREAM RENAME (ul::→qos::)** — 127 files still `namespace ul`; ~1111 uses, path literals, boot migrator. PRECEDES the AMS phase (item 3.1).
**D. PERFECT TESTS** — build the test rig (AUTONOMOUS-TEST-RIG-DESIGN.md exists, rig NEVER built) + comprehensive HW coverage per the existing test-plan docs.
**E. BACKLOG (ORPHAN_BACKLOG.md)** — DO-NOW security/data-safety (verify Wave-1 did C-3/H-4/M-1/M-2; finish 4.1 /observe, 4.3 DebugServer.Stop, 5.6 dead-fn, 3.2 STATE.toml) + BIG features (Tesla overlay, QdAudio, Z2 right-click, runtime cheat toggle, NSP installer).
**F. THEN absorption: am IPS patch (cmd 4) for HOME-from-game** + de-upstream finalize.
PHASING per the delivery doctrine: each item build→agent-verify→HUMAN HW-test→creator sign-off→next. Device caveat: game-launch is rc=0x3257C-corrupted (needs a cold-cycle/re-inject) so app-launch-dependent HW tests are blocked until then; menu/desktop/save/file features test fine now.
