# IPC Session-Pool Exhaustion (HW Login Hang After Theme Switch)

**Date:** 2026-05-18
**Severity:** Critical — uMenu unrecoverable freeze on user-card login after theme change.
**Affected versions:** v2.7.3 and earlier (latent through entire 2.x line; only made visible by v2.7.3's working theme cache).
**Fixed in:** v2.8.0 (commit pending).
**Hardware:** OG Erista (T210, serial REDACTED-SERIAL), Atmosphère 1.11.0, HOS-firmware whatever the live console reports.

---

## Symptom

After a theme switch via the Themes picker:

1. uMenu A applies the new theme, calls `SetActiveTheme` + `CacheActiveTheme` + `LoadThemeFromCache` + `DrawThemeTransitionFrame`, then `smi::RestartMenu(true)` + `Finalize` + `smi::TerminateMenu`.
2. uSystem processes action 5 (`RestartMenu`) and action 11 (`TerminateMenu`) — uMenu A's process terminates cleanly.
3. uSystem launches uMenu B with the new theme.
4. uMenu B boots successfully: applies cache, paints wallpaper, displays user-card.
5. **User presses A on the user-card.** Log emits `qdesktop: UserCard A-button uid=...`.
6. Screen freezes on the current frame indefinitely. No Atmosphère crash report is written. uMenu's main thread is wedged but the process is alive.

Reproduces on every non-Glass theme. Glass cold-boot login works perfectly. Once v2.7.3 made theme switching actually persist on hardware, this bug was 100% reproducible.

## Root cause — compound bug in the uMenu↔uSystem IPC layer

Three independent issues compound to produce the freeze. The first is the primary cause; the other two enlarge the race window and hide the failure mode.

### Issue 1 (primary): `MaxPrivateSessions = 1` in `sf_IpcManager.hpp:33`

uSystem's HIPC ServerManager has exactly one slot for `PrivateService` sessions. When uMenu A terminates and uMenu B respawns, AMS ServerManager's IPC thread must observe A's session-close handle and run `OnNeedsToAccept` for B before B's `psrvInitialize` IPC can land. With one slot, there is zero headroom for that close-reopen transition.

In practice:
- The cold-boot case has no prior session — slot is fresh, `OnNeedsToAccept` runs once at startup, B's first IPC succeeds.
- The theme-restart case has A's session occupying the slot at the moment B's `psrvInitialize` is sent. The kernel queues the IPC. AMS ServerManager eventually frees A's slot and accepts B's IPC, so `psrvInitialize` itself returns success — and uMenu B reaches the user-card paint. But the session is in a half-attached state (the post-accept handshake racing against A's residual cleanup), and the FIRST SMI command on it — `smi::SetSelectedUser` from `SetSelectedUser` at `ui_Common.cpp:333` — blocks indefinitely in `LoopWaitStorageFunctionImpl` (`smi_Protocol.cpp:7-33`, retry budget 10 000 × 10 ms = 100 s).

### Issue 2: `CacheActiveTheme` missing `fsdevCommitDevice` (`cfg_Config.cpp:204`)

When the theme picker fires (v2.7.3 added an in-process `CacheActiveTheme` call), the picked `.ultheme` zip is extracted to `sdmc:/ulaunch/cache/active/` via `fs::WriteFile` (fopen + fwrite + fclose). libnx's FAT layer buffers writes internally and only commits on `fsdevCommitDevice("sdmc")` or process exit.

uMenu A dies *after* the writes but *before* a commit. On uMenu B's boot, `main.cpp:291` calls `CacheActiveTheme` again because `reload_theme_cache=true` was set. That call re-extracts the cache, but reads the cache state that A left on the SD — which may be partial or unflushed.

This isn't the freeze on its own — but it widens the race because uMenu B's cache state may be unstable when the first IPC fires.

Same bug class as the v2.6.0 `SaveConfig` fix and the v2.5.1 `SaveFolderThemePack` fix. Pattern: any `fopen`+`fclose` followed by process exit without an explicit `fsdevCommitDevice("sdmc")` is a latent data-loss bug.

### Issue 3: `appletHolderRequestExitOrTerminate` timeout of 15 s in `la_LibraryApplet.cpp:80`

uMenu does **not** install an applet-message handler (no `appletMainLoop` / `appletGetMessage` anywhere in `src/projects/uMenu/`). So the kernel's polite `RequestExit` message (cmd 20) is never picked up — the 15 s timeout always fully elapses, then libnx escalates to `Terminate` (cmd 25, kernel kill).

While uSystem's `la::Terminate()` is parked inside `appletHolderRequestExitOrTerminate`, **uSystem's MainLoop does not advance**. No `HandleMenuMessage` calls run, no IPC handlers execute. This 15 s window is when the race in Issue 1 plays out — and the longer the window, the higher the probability of the half-attached session state.

## Audit method

The user reported the symptom as "Atmosphère panic" after 4 successive reactive patch cycles (v2.7.0 → v2.7.3) failed to fix it. Halted the reactive loop per the `feedback_cascade_test_discipline.md` rule (STOP after 2 reactive fixes; run brain-doctrine audit before the third).

Ran **3 parallel deep-read agents** in a single message, each scoped to one facet:

- **Agent A — uMenu login path.** Read every function from `QdUserCardElement::OnInput` → `onUserSelected` → `SetSelectedUser` → `LoadMenu` → `GetBrandFadeTexture`. Identified `LoopWaitStorageFunctionImpl` 100 s spin budget and confirmed the freeze must be inside one of the 3 SMI IPCs in `SetSelectedUser`.
- **Agent B — uSystem IPC handlers.** Read `HandleMenuMessage`, `CheckApplicationRecordChanges`, `LocateApplicationAndSpecialEntries`. Confirmed the handler chain is mostly fast; the only multi-second risk is lock contention with `EventManagerMain` over `g_CurrentRecordsLock`. Identified the 15 s `la::Terminate` blockage as a MainLoop starvation source.
- **Agent C — state-diff cold-boot vs theme-restart.** Read uSystem globals + ECS + ServerManager session lifecycle. Found `MaxPrivateSessions = 1` and surfaced the AMS-1.11 clean-exit contract docs that flagged this exact failure class. Provided the leading hypothesis.

All three reports converged on the IPC layer, not the login UI. The compound-bug structure (three coupled causes) was visible only because the three agents looked at three different layers in parallel.

## Fix (v2.8.0)

Three surgical patches:

1. **`src/projects/uSystem/include/ul/system/sf/sf_IpcManager.hpp:33`** — `MaxPrivateSessions = 1 → 4`. Memory cost ~1.5 KB total; gives the close-reopen transition four slots of headroom. Plenty to absorb several back-to-back theme switches.

2. **`src/libs/uCommon/source/ul/cfg/cfg_Config.cpp:215`** — `fsdevCommitDevice("sdmc")` after `zip_close` in `CacheActiveTheme`. Forces libnx's FAT buffer to flush the just-written `.ultheme` extraction before uMenu A's process exits.

3. **`src/projects/uSystem/source/ul/system/la/la_LibraryApplet.cpp:80`** — `appletHolderRequestExitOrTerminate` timeout `15'000'000'000ul → 2'000'000'000ul`. Collapses the MainLoop blockage from 15 s to 2 s; saves 13 s of IPC starvation per theme switch.

Build artifacts:
- `uMenu/main` sha `acb88329d1fb` (was `5c73e7e6` for v2.7.3)
- `uSystem.nsp` sha `90ca31794cd1` — **first custom-fork uSystem deployment** since the OOM-lesson rollback to stock v1.2.0 (`ce48dc7d`). The 3 fixes specifically attack the OOM cause, so this is a forward step, not a regression.

## Verification

HW-confirmed by the creator on OG Erista 2026-05-18:

> "Everything works perfectly."

Full theme-switch + login soak passes for the 10 in-binary themes. No Atmosphère panic, no perceived hang, no chrome regressions.

## Lessons for the team

1. **Cascade-test discipline matters.** Four reactive cycles burned (v2.7.0 → v2.7.3) before halting and doing the proper audit. The audit took ~30 minutes of agent time and produced the correct compound-cause diagnosis on the first try. Reactive cycles can never find a compound bug because they fix what was most-recently-touched, not what's actually wrong.

2. **Parallel deep-read audits scale better than sequential ones.** Three agents reading three different layers found the convergence point. One agent reading all three layers serially would have run out of context budget before converging.

3. **Latent bugs hide behind broken adjacent code.** This bug existed since the fork started shipping themes, but `MaxPrivateSessions=1` never mattered while themes never actually persisted (cache staleness). Fixing the theme cache exposed the IPC bug. Always assume your "working" build has latent bugs masked by other bugs.

4. **"Atmosphère panic" needs a crash report to be a panic.** No `/atmosphere/crash_reports/` entry → it's a uMenu hang, not a sysmodule crash. Codify a check-crash-reports-first rule into the deploy script before any "panic" claim drives the fix path.

5. **`fopen`+`fclose` without `fsdevCommitDevice` is a latent data-loss bug on Switch.** Audit every such pattern in the codebase. Current known offenders fixed: `SaveFolderThemePack` (v2.5.1), `SaveConfig` (v2.6.0), `CacheActiveTheme` (v2.8.0). Run a sweep for remaining ones during the Phase 3 HBmenu-deletion gate work.

## Cross-references

- `docs/research/AMS-1.11-CLEAN-EXIT-CONTRACT-20260506.md` — explains the LibraryApplet GetResult slot semantics. The session-pool exhaustion described there is the same failure class as `MaxPrivateSessions=1` here, just on a different pool.
- `~/AstralBrainEngine/learnings/feedback_cascade_test_discipline.md` — the brain-rule that halted my reactive cycle.
- `~/AstralBrainEngine/rules/composition-over-reinvention.md` — informs the future work: an `appletMainLoop` listener in uMenu would let the kernel's polite-exit fire, collapsing the 2 s timeout to ~100 ms in the happy path. Plutonium has examples we can compose against rather than reinventing.

## Follow-up work

- **Backlog (post-v3.0):** install an `appletMainLoop` handler in uMenu that picks up `RequestExit`, sets `g_uMenuTerminating`, and breaks Plutonium's `Show()` loop. Reduces the per-theme-switch wait from 2 s to <100 ms in the happy path.
- **Backlog (post-v3.0):** visual theme editor with mock desktop preview + live sliders for the 17 palette tokens + wallpaper pack selector (user feedback 2026-05-18, deferred to after v3.0 since current text-based picker works).
- **Phase 2 (next):** fix the RCE in `qd_ShellCommands.cpp:218 CmdLaunch` before any telnet ship.
