# 46 — Stabilization Handoff (autonomous run 2026-04-24 evening)

**Author:** Orchestrator working autonomously while creator offline.
**Window:** Creator authorized work until 11 PM.
**Status snapshot at handoff:** stabilization commit `90cf352` pushed; three agents in flight; one binary deployed in user's hands.

---

## What's deployed on the Switch right now

The build the creator last deployed is the **regression-fix iteration** (`main.pre-regfix` is the safety backup; the live one is the regression-fix build). It contains:

✅ Top-bar 48 px translucent backing rect (no more clutter)
✅ `atexit()` telemetry flush + 180-frame periodic flush
✅ `WARN: nro_path empty` log for "app0" entries that don't launch
✅ Login screen v2 (Q OS text logo, cards visible at y=380, hint bar)
✅ Liquid Glass Bubble cursor (28 px — pre-refinement)
✅ Per-zone mouse curve

**Not yet on hardware (in source + commit `90cf352`, awaiting next deploy):**

- Cursor refinement to 18 px + black/white click point on cyan
- Mac-style desktop SFX (Pop / Tink / Glass / Hero / Funk / Bottle from `/System/Library/Sounds/`)
- BGM volume policy (Startup=96 LOCKED, Main=20 subtle, Themes/Settings=32, Lockscreen=20)
- Per-icon SDL_Texture cache (fixes "lags after playing for a bit" — was 1200 GPU allocs/sec)
- Splash leftover fix (uLaunch logo replaced with programmatic Q OS branded panel)
- "Q OS v..." About dialog title (was "uLaunch v...")

The next deploy iteration ships everything in `90cf352` plus whatever the three in-flight agents land.

---

## Three agents in flight (autonomous)

### 1. Volume-button global integration

**Goal:** physical Switch volume buttons control all Q OS audio (BGM + SFX) globally.
**File scope:** new `audio_SystemVolume.{cpp,hpp}` + `Main.cpp` (init/exit) + `ui_MenuApplication.cpp` (StartPlayBgm scaling).
**Mechanism:** libnx `audctlGetSystemOutputMasterVolume` polled every ~250 ms, multiplied with per-menu policy percentage.

### 2. Vault Phase 1 scaffold

**Goal:** start the HBMenu replacement per `docs/45_HBMenu_Replacement_Design.md` Stage 1.
**File scope:** new `qd_VaultLayout.{cpp,hpp}`, no edits to existing files.
**Mechanism:** Finder-style two-pane file browser with sidebar (Desktop / Switch / Logs / Atmosphère / SD Root / Themes), NRO scan + ASET-icon decode, D-pad nav, A-button launches NROs via existing `smi::LaunchNro` path.
**Wiring:** intentionally NOT wired into the desktop's launch path yet — separate later step. Stand-alone buildable layout.

### 3. Translation-layer cleanup

**Goal:** "nearly flawless and clean" transitions — fewer perceptible delays.
**File scope:** `BGM.json` fade-ms tuning, `MenuApplication::OnLoad` Plutonium fade step count, layout pre-warm in `EnsureLayoutCreated`, optional texture-cache eviction in `qd_DesktopIcons.cpp`.
**Mechanism:** drop layout fade to ~12 frames (~200 ms), drop main-menu BGM fade-in from 1500 ms to 400 ms, eagerly construct Main + Startup layouts during boot fade-in so first navigation is hot.

---

## What ships when all three agents land

A second commit will cover volume + vault + translation-layer changes. Then a single deploy that the creator tests.

Expected user-perceptible improvements over the current `regression-fix` build on hardware:

1. **Cursor**: smaller (18 px), brighter cyan, black centre ring + 2 px white click-point dot. Click point clearly visible on any background.
2. **Desktop sounds**: each menu interaction makes a Mac-style glass/bubble sound (Pop on navigate, Glass on launch, Tink on page move, Funk on error). Cursor moves are silent.
3. **Login music**: unchanged (creator's directive — "I love the login sound LOCK it in!").
4. **Desktop music**: barely audible (15 % of system volume, was full).
5. **Volume buttons**: physical buttons on the side of the Switch now control everything globally — adjust BGM + SFX in real time without restarting the song.
6. **Lag**: fixed. The texture leak (1200 SDL textures/sec on the desktop) caused progressive slowdown after a few minutes of use; per-icon caching eliminates it.
7. **Top bar**: clean horizontal row inside the 48 px backing strip — time/date left, connection icon + battery right, no overlap with desktop icons.
8. **Splash leftover**: gone. The uLaunch logo that briefly flashed between login and desktop is replaced with a programmatic Q OS branded panel. About dialog says "Q OS v..." not "uLaunch v...".
9. **Transitions**: noticeably tighter. Layout fades drop from ~530 ms to ~200 ms; BGM fade-in to main menu drops from 1.5 s to 400 ms.

---

## Where to find things

| Area | Path |
|---|---|
| Latest commit | `90cf352` on `origin/main` of `https://github.com/Jmesmykil/QOS.git` |
| Build artifact (current source state) | `tools/qos-ulaunch-fork/src/SdOut/ulaunch/bin/uMenu/main` |
| Backup binaries on SD | `main.pre-sp3.4`, `main.pre-loginscreen`, `main.pre-loginv2-cursor`, `main.pre-regfix` |
| Telemetry logs | `tools/qos-ulaunch-fork/logs/<test-name>/uMenu.0.log` (post-test pulls) |
| Roadmap | `tools/qos-ulaunch-fork/docs/44_Three_Phase_Roadmap.md` |
| Vault design | `tools/qos-ulaunch-fork/docs/45_HBMenu_Replacement_Design.md` |
| Splash research | `tools/qos-ulaunch-fork/docs/43_Splash_Replacement_Research.md` |
| Splash assets (placeholder Q OS PNG + Hekate BMP) | `tools/qos-ulaunch-fork/tools/splash/` |
| State SSOT | `/Users/nsa/QOS/STATE.toml` (section `[qos_ulaunch_fork]`) |

---

## To deploy on return

```
# 1. Verify SD mounted + UMS active
ls "/Volumes/SWITCH SD/"

# 2. Pull current logs (telemetry should have INFO/WARN now post-flush-fix)
cp "/Volumes/SWITCH SD/qos-shell/logs/uMenu.0.log" \
   /Users/nsa/QOS/tools/qos-ulaunch-fork/logs/post-stabilization/

# 3. Backup current main and deploy the new build
cp "/Volumes/SWITCH SD/ulaunch/bin/uMenu/main" \
   "/Volumes/SWITCH SD/ulaunch/bin/uMenu/main.pre-stabilization"
cp /Users/nsa/QOS/tools/qos-ulaunch-fork/src/SdOut/ulaunch/bin/uMenu/main \
   "/Volumes/SWITCH SD/ulaunch/bin/uMenu/main"

# 4. Verify + eject
cmp /Users/nsa/QOS/tools/qos-ulaunch-fork/src/SdOut/ulaunch/bin/uMenu/main \
    "/Volumes/SWITCH SD/ulaunch/bin/uMenu/main"
diskutil eject "/Volumes/SWITCH SD"
```

The orchestrator will deploy automatically when the creator next flips UMS up + says "Deploy now."

---

## What to watch for post-deploy

1. **Cursor click point** — is the 2 px white dot visible at all backgrounds?
2. **Lag** — play with the desktop for 5+ minutes. Should not slow down. If it does, check `/qos-shell/logs/uMenu.0.log` for memory or texture warnings.
3. **Sound feel** — does it now sound Mac-like? Glass/bubble vibes?
4. **BGM volume** — barely there but present?
5. **Volume buttons** — does adjusting them on the Switch change Q OS audio in real time?
6. **Vault** — won't appear in this build (not wired). Phase 1 work continues separately.
7. **Splash** — the brief uLaunch logo flash between login and desktop should be gone.
8. **Top bar** — clean horizontal row, no clutter, no overlap with desktop icons.
9. **Transitions** — layout swaps and BGM fades should feel snappier.

---

## Phase 1 next steps (when stabilization is verified)

1. Wire the vault into the desktop's launch path (add a "Files" dock icon).
2. Build the inline text + image viewers (`QdTextViewer`, `QdImageViewer`).
3. Author the HBMenu removal migration script (delete `sdmc:/hbmenu.nro` + `sdmc:/switch/hbmenu/`, reclaim ~6-17 MB).
4. Build dev-tool windows (NXLink session manager, USB serial console, log flush) — wraps existing `qd_DevTools.cpp` API.

These five sub-tasks together complete Phase 1 per the roadmap. Phase 2 (game-title hijack) is gated on Phase 1 being feature-complete and bumping into a real applet-mode ceiling.

