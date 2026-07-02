# Changelog — Q OS uMenu

All notable changes to this project are documented here. Entries follow the
sprint/wave structure used in development (W2, W3, …) and are grouped under a
single semantic version per sprint.

The build status tags are:

- `[HW-GREEN]` — verified working on real Switch Erista hardware.
- `[HW-YELLOW]` — builds clean, partial HW verification.
- `[HW-RED]` — builds clean, known HW regression in the build.

---

## v3.5.0 — Stabilization, hardening, in-OS cheat installer [HW-GREEN]

A consolidation release that delivered three substantial new capabilities
while passing a 4-agent pre-release audit with zero code FAILs. Eight sub-
waves landed across two days of dispatch-fix-verify cycles.

### Headline capabilities

- **In-OS cheat installer.** Press **X** in the Cheats window's TitleList
  to fetch the latest community cheat database from
  [HamletDuFromage/switch-cheats-db](https://github.com/HamletDuFromage/switch-cheats-db)
  over HTTPS, filtered against your installed games via
  `nsListApplicationRecord`. No PC browser, no SD eject, no bulk-install
  boot hang. Worker thread + progress UI; B aborts.
- **Read-only Pokémon Sword/Shield save viewer.** Open the **Saves** dock
  tile, pick Sword or Shield, view your 6 party Pokémon with real
  nicknames, levels, held items, and shiny indicators. Built on a
  clean-room SwSh `SwishCrypto` round-trip (verbatim XOR pad +
  SHA-256 + 0x7F-byte chunk advance from PKHeX, GPL-2 compatible) and a
  full PK8 decoder ported from PKHeX/G8PKM.cs.
- **Hardened remote shell.** The port 9999 telnet shell, carried
  unhardened since v2.3.x, now requires a 6-digit PIN (displayed in the
  top-right hot-corner dropdown), binds to the active network IP rather
  than `INADDR_ANY`, refuses loopback addresses, and locks itself out
  after three failed authentication attempts in 60 seconds. Path-prefix
  validation now also rejects `NUL`, `//`, and trailing `..` injection
  attempts.

### Stabilization (W15 4-agent pre-release audit)

The audit found 0 code FAILs but produced a punch list of leaks, refcount
bugs, and per-frame allocations. Every item was fixed inline before ship:

- **Session refcount fixes.** `QdTaskManagerLayout` and `QdMonitorLayout`
  were calling `nsInitialize`/`psmInitialize`/`nifmInitialize` on top of
  the already-active process sessions opened by `__appInit`. The matching
  `*Exit` calls in their destructors decremented the refcount on close,
  invalidating sessions for any other consumer mid-IPC. Both now reuse
  the process-lifetime sessions.
- **Capture-buffer leak.** `ui_BackgroundScreenCapture.cpp` allocated a
  3.5 MB capture buffer via `new u8[CaptureBufferSize]` then dropped the
  pointer. Wrapped in `std::unique_ptr<u8[]>` for exception-safe cleanup.
  (Gated off in QDESKTOP_MODE; fixed anyway for upstream-merge hygiene.)
- **Window manager ledger UNTRACK.** The dtor cleared `open_windows_`
  and `minimized_entries_` but never iterated the parallel
  `*_ledger_handles_` maps, leaving phantom ledger entries after
  teardown. Both maps now iterated and `UL_LEDGER_UNTRACK`'d before
  container clear.
- **Save autoscan entry cap.** `result_.entries` previously unbounded;
  capped at 64 with a one-shot warn log on overflow.
- **Cheats detail-pane texture cache.** `RenderCheatList`'s
  per-frame `RenderText` + `DeleteTexture` for the focused cheat's
  info + status lines (~120 alloc/sec) replaced with shadow-compared
  cached textures. Rebuild only when focus index or enabled state
  changes.
- **CheatsManager writeback rename → direct truncate-write.** FAT32 /
  newlib `rename()` does not atomically replace existing destinations.
  The previous remove+retry fallback could lose the file entirely if
  both renames failed. Replaced with direct truncate-write (`fopen "w"`)
  + `fsync`. `.qos-backup` made on first edit covers the corruption
  window.
- **CheatsManager parser ignored disabled cheats.** Lines starting with
  `;` were skipped wholesale; disabled cheats became invisible in the
  UI, so re-enabling them was impossible. Parser now strips leading
  `;` and records `currently_enabled` per entry.
- **Cheats sidecar TOML missing on first toggle.** `mkdir` was called
  with a trailing-slash path; libnx/newlib rejects with ENOTDIR.
  Trailing slash stripped + post-mkdir `stat` verification + errno
  logging on failure.
- **Cheats UI `enabled_` seeding.** The set defaulted empty even when
  the underlying `.txt` had every cheat uncommented (= enabled). First
  toggle press was a no-op (set-to-already-set). Layout load now seeds
  `enabled_` from each entry's `currently_enabled`, then overlays any
  sidecar TOML.
- **CheatsLayout `nsGetApplicationControlData` IPC loop removed.**
  Earlier scan called NS once per TID + 24 KB heap alloc per call. With
  2,500+ TID directories present (post-bulk-install), this was the
  multi-second freeze that masked as boot hang. Title display falls
  back to `"TID 0x<hex>"`; async resolver queued for v3.6.

### Boot speed and lockscreen reactivity (W17)

- **Deferred application enumeration.** `MainMenuLayout::Initialize`
  previously blocked on `g_GlobalSettings.InitializeEntries()` — a
  `nsListApplicationRecord` loop that consumed ~1,547 ms on a 64-app
  library. First paint is now driven by the pre-baked `records.bin`
  (~50 ms) while a detached thread runs the live IPC enumeration in
  parallel. `OnMenuUpdate` polls an atomic flag and applies the live
  data on the main thread when it arrives. **~1.5 seconds removed
  from time-to-first-interactive.**
- **Background theme cache extraction.** `EnsureQosThemesOnSd`, which
  walks the romfs `.ultheme` files and extracts any missing entries to
  SD, moved off the synchronous pre-SDL boot path onto a detached
  thread. The Settings → Themes path gates on its completion flag for
  the rare first-boot collision. **0-120 ms removed depending on SD
  speed.**
- **Lockscreen unlock fast path.** `LoadMenu(MenuType::Main)` from the
  lockscreen now passes `fade=false`. The lockscreen and the desktop
  render the same wallpaper; there was no visual reason for the cross-
  fade. **~667 ms removed per unlock.**

### Visual fixes (W17)

- **Stray grey square in top-left.** `qd_HotCornerOverlay::OnRender`
  Pass 1 painted a 96×72 opaque `surface_glass` rectangle BEHIND the
  Q glyph. Introduced in v1.9.7 when widget paint moved to a dedicated
  overlay element. The Q glyph and accent border strips remain; only
  the spurious backplate was removed.
- **One-pixel grey line at the bottom of the screen.** Plutonium's
  `SDL_RenderClear` defaults to `#E1E1E1` light grey. SDL2's bilinear
  filter scaling 1280×720 (or 960×540 in handheld) to 1920×1080 leaves
  a sub-pixel rounding gap at the bottom edge, exposing the grey clear
  color. `MainMenuLayout` now sets the background color to `#0A0A14`
  (matching the Cold Plasma Cascade wallpaper base), making any
  sub-pixel gap invisible across all 10 themes.

### Theme system audit (W16)

All 10 themes verified clean on a 170-field comparison (`QdPalette.json`
vs C++ factory). One stale wallpaper_pack string in
`romfs/themes/q-os-0-q-os.ultheme` corrected from "Glass" to "Q OS"
(was silently falling through to idx=0 — accidentally correct, now
explicit). Two documentation-only comment corrections in `qd_Theme.hpp`
and `qd_Theme.cpp`.

### Roadmap realignment (W15-D + product direction)

The v3.1.0 headline feature — windowed external homebrew — was
researched and ruled hardware-infeasible without an Atmosphère
extension that does not exist. The deliverable was deferred indefinitely
and `ROADMAP.md` updated to reflect the new product direction:

> Q OS uMenu is becoming the unified clean-room desktop OS for the Switch
> homebrew ecosystem. Every release absorbs another well-loved community
> tool into the OS proper, until users no longer need to juggle a dozen
> separate NROs.

The substitute already shipping is L+2 window-state persistence: open
windows survive NRO round-trips. The roadmap was rewritten to sequence
the next three releases (v3.6 footprint phase 2 + save write-back, v3.7
NSP installer + FTP + overlay manager, v3.8+ HDR + Atmosphère config
UI + cloud save backup) around this absorption-first vision.

### Build artifact

`uMenu.nso md5: f8895b79965a61407188b2f4f8b2009e`

### Known limitations

- SwSh save editing is **read-only**. Write-back is queued for v3.6 once
  the round-trip is verified against backup-restored saves.
- Cheat title names display as `"TID 0x<hex>"`. Async `nsGetApplicationControlData`
  resolver queued for v3.6.
- Pokémon Brilliant Diamond has no cheats in the community database.
  Shining Pearl and the other six Pokémon TIDs have full sets.
- mGBA exits to HOME instead of Q OS. Root cause is inside mGBA's own
  libnx exit mode; not fixable from our side.
- Process resident-set baseline is ~312 MiB / 407 MiB. v3.6 footprint
  phase 2 (lazy theme loading) targets ~40-60 MiB reduction.

---

## v3.4.0 — Save autoscan + Cheats system [HW-GREEN]

Three sprint waves on cheats + save editor discoverability, plus a
parallel 4-agent audit (W14-A/B/C/D) that gated the release with
56+32+many PASS / 0 code FAILs.

### Save autoscan (W12-SAVE-DISCO + W12B-AUTOSCAN)

- New **Saves** dock tile (slot 5) opens singleton `QdSaveEditorLayout`
  window 960×600.
- Autoscan probes 4 candidate save paths:
  - `sdmc:/atmosphere/contents/<TID>/save/` + `/save_data/` + `/saveMeta/`
  - `sdmc:/JKSV/<game>/<user>/<backup>/` (W12B-HOTFIX: was `/JKSV/Saves/`)
  - `sdmc:/switch/Checkpoint/saves/<title>/`
  - `sdmc:/saves/` → `kOtherGameIndex` slot
- TID-to-game-index map for 10 Pokémon mainline titles.
- TitlePicker shows real "found N saves" / "no saves" entries.
- Y in TitlePicker = rescan.
- Vault per-file ctx menu "Edit Pokémon save" — filtered to `.sav`/`.bin`/`.dat` files and directories.
- `.zip` recognized as save extension (JKSV backups).
- `CountSaveFiles(dir, depth=1)` dives past JKSV user-uid layer.
- *Deferred to v3.5:* per-game save parser (PartyBox / Inventory /
  Trainer panels still show "Coming soon").

### Cheats system (W12-CHEATS + W12-CHEATS-WB + W13-* hotfixes)

- New **Cheats** dock tile (slot 6) opens singleton `QdCheatsLayout`
  window 960×600.
- `qd_CheatsManager` scans `sdmc:/atmosphere/contents/<TID>/cheats/*.txt`
  (canonical Atmosphère cheats path).
- Parser handles `[Name]` and `;[Name]` (commented = disabled) headers;
  W13-BUG-FIX strips leading `;` so disabled cheats remain visible in
  the UI.
- Two-mode UI: TitleList ↔ CheatList. A toggles, Y views hex code,
  B back.
- Master Code (first cheat in file) always preserved as enabled —
  UI blocks toggling.
- Vault ctx menu "View Cheats" — shown only on 16-hex TID directories.
- Desktop ctx menu "View Cheats" — shown only on installed `IconKind::Application` icons.
- **Runtime toggle = actual file write.** Toggling a cheat OFF prepends
  `;` to the `[Name]` header AND every following hex-code line.
  Atmosphère's parser respects `;` as comment, so disabled cheats
  do not apply at game launch.
- `.qos-backup` safety copy created on first write (never overwritten).
- W13-DIRECT-WRITE: switched from `tmp+rename` (which fails on FAT32 /
  newlib due to non-atomic rename-over-existing) to direct truncate-write.
  `.qos-backup` covers the corruption window.
- Sidecar TOML at `sdmc:/ulaunch/cheats-enabled/<TID>_<BID>.toml` for
  fast reload of toggle state.
- W13-SIDECAR-FIX: trailing slash stripped from `mkdir` (libnx/newlib
  rejects `mkdir("path/")` → ENOTDIR).
- W13-B-HOTFIX: removed `nsGetApplicationControlData()` IPC loop per TID
  — was causing multi-second freeze on first Cheats window open with
  2,500+ TID dirs installed (e.g. after a HamletDuFromage bulk install).
  Titles now display as `"TID 0x<hex>"` until W14 async resolver lands.
- W14-B-WARN-01 fix: blank lines inside cheat blocks no longer reset
  the in-block context — fixes silent toggle skip on community .txt
  files with blank lines between header and first hex line.

### Window manager polish

- Stale dock-cell comments corrected from "5 cells" to
  "BUILTIN_ICON_COUNT cells" (v3.4 = 7 dock tiles).
- D-pad nav comment for dock indices updated.

### Atmosphère cheat-bundle compatibility note

Q OS does NOT scan `atmosphere/contents/` at boot or use `dmnt:cht` IPC
— our cheats work is **filesystem-only**, runtime application is
Atmosphère's job. Verified clean against W13 investigation triad
(W13-A: our code, W13-B: EdiZon, W13-C: sphaira + AIO).

### Build artifact

`uMenu.nso md5: fc7851e4577c4e560ca1dd75a93fc72d`

---

## v3.3.0 — Footprint + scroll + save editor + filesystem shortcuts [HW-GREEN]

Parallel-dispatched 5-agent sprint (W11-FOOTPRINT, W11-SCROLL, W11-SAVE,
W11-SURVEY, W12-FIX) plus an inline Vault-sidebar extension. All landed
HW-verified on Erista; the v3.2.4 → v3.3.0 transition went through one
Vault-crash regression that was fixed before ship.

### Footprint reduction

- **Lazy icon prewarm** — only ~37 of 77 NRO icons decoded at boot (5 dock +
  32 first-page); off-screen icons decode lazily on first paint. ~10-15 MiB
  saved at idle.
- **Wallpaper at display resolution** — handheld 960×540, docked 1280×720
  (was 1920×1080 for both). ~1.5 MiB saved in handheld.
- **Wallpaper texture ledger-tracked** — bytes accounted for in Monitor
  Resources view.
- New `qd_LazyLoader.{hpp,cpp}` shared utility (`WallpaperResolution()`).
- *Deferred to v3.4:* lazy theme loading (~40-60 MiB savings target).

### Launchpad — continuous single-canvas scroll

- Replaced paginated rendering with one tall canvas. `GetNaturalH()` returns
  full canvas size for all visible items at fixed scale.
- `EnsureFocusVisible()` auto-scrolls when D-pad navigation moves focus
  off-screen.
- D-pad L/R = page-step scroll. Touch drag-scroll active in BOTH modes:
  - Windowed launchpads (folder windows) use QdWindow T2 universal scroll.
  - Fullscreen launchpad has its own `lp_scroll_y_` + drag-scroll state
    machine with 8 px deadband; engages `lp_swipe_fired_` to suppress
    tap-to-launch during scroll.
- Per-tile cache from W5-FIX-1 preserved. Touch tap / long-press / swipe
  state machines (W3 + post-W3 touch-prev snapshot) preserved.

### Pokémon save editor wired up

- New `OpenSaveEditorWindow()` singleton in `qd_DesktopIcons_WmBridge.cpp`
  (mirrors Vault/Monitor/Settings pattern).
- "Edit Pokémon save" entry in Vault per-file context menu (ZL on any file
  or long-press → menu → entry).
- `QdSaveEditorLayout::GetBottomHint()` for mode-aware chrome hint text
  (TitlePicker vs PartyBox/Inventory/Trainer panels).
- 960×600 window. State machine for TitlePicker → panel transitions
  complete; per-game save parsers still placeholder ("Coming soon").
- *Deferred to v3.4:* discoverability (dock tile, autoscan of known save
  paths) — creator confirmed the ctx menu route is hard to find as the
  primary discovery surface.

### Vault filesystem shortcuts

Sidebar grew from 6 → 12 entries with explicit shortcuts to important
filesystem locations:

`Homebrew · Saves · NSP/NCA · Payloads · Atmosphère · Themes · Q OS ·
Logs (atmosphere/crash_reports/) · Bootloader · Nintendo · Switch (deep)
· SD Root`

### Bug fixes that landed during the sprint

- **Vault crash** (`std::_Function_base::~_Function_base()` calling through
  garbage manager pointer) — explicit `std::function<void()>
  on_open_save_editor{}` default-initializer + clean rebuild. The crash
  was an Alignment Fault at PC=0xffe07affc0889aff on the first
  `OpenVaultWindow()` after the W11-SAVE field addition.
- **Lockscreen invisible** — `OnFinishedSleep` was loading upstream
  `MenuType::Lockscreen` which depends on missing PNG assets. Wrapped in
  `#ifdef QDESKTOP_MODE` to load `MenuType::QLockscreen` (Q OS native
  layout, wallpaper + clock only, no missing assets) instead.

### Absorption roadmap (from W11-SURVEY)

Next 4-5 wave candidates (now sorted into v3.4 / v3.5):

- **v3.4** — JKSV save backup/restore (~2 waves, low risk, GPL-3)
- **v3.4** — Capture Album Browser (~2 waves, low risk, native `caps:a`)
- **v3.4** — Save Editor discoverability (dock tile + autoscan)
- **v3.4** — Lazy theme loading (~40-60 MiB footprint reduction)
- **v3.5** — Sphaira NSP / NSZ / XCI installer (~3 waves, medium risk)
- **v3.5** — Overlay Manager (~1 wave, low risk)
- **NEVER:** EdiZon (legal), NX-Themes (zero value), Tinfoil (closed
  source), Atmosphère internals (brick risk).

### Build artifact

`uMenu.nso md5: 8ff2f58a2a23c402c978f0f5483cbcd7`

---

## v3.2.4 — Observability + window-pin sprint [HW-GREEN]

Twelve sub-waves landed across one continuous session, all verified on
Erista. The headline deliverables:

- **`QdResourceLedger`** — central in-memory registry of every long-lived
  resource (textures, services, sessions, threads, file handles, windows,
  minimized snapshots, icon cache, sfx). `UL_LEDGER_TRACK(kind, tag, bytes)`
  / `UL_LEDGER_UNTRACK(handle)` macros instrument 13 hot owners. Thread-safe
  via libnx `Mutex`. Snapshot API returns by value.
- **`QdPerfLogger`** — file-backed CSV at `sdmc:/ulaunch/perf-log.csv` with
  29 columns (frame timing, CPU/GPU MHz, SoC/PCB temp, RAM, ledger
  per-kind live counts + bytes, event stamps). 5 MiB rotation through
  `.1..9`. Pre-rotate-at-Init so mid-session SD `rename()` stalls can't hit
  the UI thread. Verbose mode rate-capped at 10 Hz with per-row `fflush`
  and write-failure guard.
- **Monitor Resources tab** — `L/R` toggles between Stats and Resources
  view; `Y` dumps live ledger to `log_uMenu.log`; `X` toggles perf-logger
  verbose mode (moved from `ZR` because `ZR` is the global cursor-click
  button and isn't forwarded through `QdWindow::nav_mask`).
- **Window manager hardening** — singleton checks for Vault / Settings /
  Monitor / About / TaskManager / Nintendo Apps (tap the dock tile twice =
  focus existing, not spawn duplicate). Dock zone bypass so dock tiles
  remain reachable while any window is focused. `kMinY = 0` so windows can
  cover the system top bar when pinned. New `QdWindowOverlay` element
  inserted into `MainMenuLayout` *after* status bar elements to enforce
  the Z-order. Snap content area expanded from `(0, TOPBAR_H, …)` to
  `(0, 0, …, SCREEN_H - DOCK_H)` so corner-pinned windows actually cover
  the top bar geometrically.
- **Perf hotspot eliminations** — Monitor per-tile texture cache (was
  1440 alloc/destroy per second), Launchpad folder-tab text cache (480
  alloc/destroy per second), Settings ts/nifm/audctl/lbl service hoist
  (12 IPC round-trips per Refresh eliminated), Monitor clkrst session
  hoist (4 IPC round-trips per 0.5 s eliminated), `RefreshSuspendedApps`
  per-frame gate (already had it; W5 verified).
- **Leak / orphan cleanup** — PNG-pack texture cache with sentinel +
  invalidate-on-pack-change, Settings hint-bar rebuild in `Refresh()`,
  `g_failed_extract_paths` capped at 1024, `minimized_entries_` capped
  at 8, `ui_InputBar::bar_bg` freed in dtor, DevTools usb-snapshot
  thread now `threadClose`'d, `QdNsIconCache` no longer opens a duplicate
  `ns:` session, `boot_chime_sfx` Mix_Chunk now released.
- **UX fixes that landed along the way** — B/+ closes any windowed
  layout (was forwarded to content as a no-op), ctx menu dismisses when
  its owning window starts dragging, Settings canvas size 800×600 →
  uniform-fit (hint bar visible, no clipping), wallpaper persists across
  homebrew exit (was only persisting across reboot), corner-snap eats by
  hot corner fixed via cooldown + drag-in-progress gate, mGBA exit
  investigated (root cause inside mGBA's own libnx — not fixable on our
  side; documented), `tools/log-tail.sh` host-side log tail script with
  ANSI keyword highlighting + filter + rotation handling.
- **Settings ts API** — same FW-14+ `tsGetTemperatureMilliC` removal that
  bit Monitor in v3.2.0 also bit Settings's System Info temp rows.
  Mirrored the `TsSession` + legacy fallback pattern; hoisted into
  ctor/dtor like Monitor.

**Files net delta (since `ff2ad942`):** +~3500 LOC. Five new file pairs:
`qd_ResourceLedger.{hpp,cpp}`, `qd_PerfLogger.{hpp,cpp}`,
`qd_WindowOverlay.hpp`. All other changes are extensions to existing
layouts.

**Build:** `Done!` zero warnings. `uMenu.nso md5 1ac9d4f659486336f9a35806aceff4c6`.

**Known remaining items deferred to v3.3:**
- Launchpad full single-canvas scroll (swipe-to-page removed; D-pad
  L/R still pages, drag-scroll works in windowed launchpads). Full
  unlimited-scroll mode is a ~150 LOC refactor.
- D-pad navigation of the dock while a window is focused (touch and ZR
  cursor work; the keypress path is consumed by nav_mask forwarding).
- Process resident-set reduction (the 312/407 MiB baseline is high but
  stable — needs a separate footprint sprint: lazy-load themes, lazy-
  decode icons, shrink wallpaper to display res, audit Plutonium widget
  tree).

---

## v3.2.0 — Wave 2 + Wave 3 bug-fix sprint [HW-GREEN]

Comprehensive bug-fix sweep across the desktop, launchpad, vault, monitor,
settings, and homebrew launch paths. Driven by hardware-reported issues from
the v3.1.0 deploy; all fixes verified on Erista.

### Wave 2 — Critical HW bugs

- **ZL context menu now reaches content elements.** `HidNpadButton_ZL` was
  missing from `QdWindow::nav_mask` so ZL was never forwarded to hosted
  launchpads / folder windows. Added the bit and reordered the chrome menu
  gate to require cursor over titlebar or bottom-bar (`IsCursorOverChrome`),
  not anywhere in the window rect. (`qd_Window.{hpp,cpp}`, `qd_DesktopIcons.cpp`)

- **Launchpad horizontal swipe-to-page.** Horizontal swipe ≥ 80 px with
  < 60 px vertical drift now flips pages inside the launchpad. Hint text
  updated to mention swipe. (`qd_Launchpad.{hpp,cpp}`)

- **Y favorite toggle now shows a toast.** `ShowNotification("Added/Removed
  '<name>' to/from Favorites.")` fires immediately after
  `ToggleFavoriteByLpItem`. Previously the icon would vanish silently when
  removed from the Favorites tab. (`qd_Launchpad.cpp`)

- **Vault homebrew launches now return to uMenu on exit.** The Vault was
  calling `FadeOutToNonLibraryApplet()` + `Finalize()` *before*
  `smi::LaunchHomebrewLibraryApplet`, which tore down the MenuApplication
  dispatcher before Horizon could record uMenu as the library-applet parent
  — so the NRO exited to qlaunch instead of uMenu. Reordered to Launch →
  Fade → Finalize, matching `qd_DesktopIcons.cpp::LaunchIcon` and upstream
  `ui_MainMenuLayout::HandleHomebrewLaunch`. Applied to both
  `EnterFocused` (default A-launch) and `DoLaunchAsApplication` (context
  "Launch as Application"). (`qd_VaultLayout.cpp`)

- **Monitor: live CPU + GPU clock rows.** Added clkrst-based CPU/GPU MHz
  sampling and corresponding tiles. `clkrst_inited_` lifecycle in
  ctor/dtor. (`qd_MonitorLayout.{hpp,cpp}`)

- **Settings NTP sync.** `0x275` socket-init failure decoded and surfaced
  in the status row. NTP path now wraps `socketInitializeDefault()` /
  `socketExit()` and renders human-readable errors ("Sync failed: no
  network", "bad response", "bad timestamp", …) instead of bare hex codes.
  Sized `sys_ntp_status_[48]` to match `Row.value` width to avoid
  `-Werror=stringop-truncation`. Qualified `::socket(...)` for libnx
  namespace disambiguation. (`qd_SettingsLayout.{hpp,cpp}`)

- **Wallpaper persists across reboot.** `main.cpp` re-applies
  `qos-wallpaper.toml` after `LoadThemeFromCache` so wallpaper and palette
  pack are decoupled from each other across boots. (`main.cpp`)

- **About: SwitchIdent system info.** Filled missing rows (HOS version,
  serial mask, AMS / EmuNAND, region, nickname, battery lot). (`qd_AboutLayout.{hpp,cpp}`)

- **Reboot to Hekate** properly routes through the `reboot_to_hekate.nro`
  payload chain instead of falling through to OFW.
  (`qd_HotCornerRightDropdown.{hpp,cpp}`)

- **QdContextMenu submenus + `skip_first_lift_`.** Context menus now
  support nested submenu items and skip the first touch-lift to avoid
  the open-tap immediately being read as a confirm-tap. (`qd_ContextMenu.{hpp,cpp}`)

- **QdSuspendedAppDockEntry** — first-class dock entry for the currently
  suspended foreground app, surfaced separately from launcher tiles.
  (`qd_SuspendedAppDockEntry.{hpp,cpp}`)

- **QdVisibility singleton + `qos-visibility.toml`.** Hidden flag survives
  reboots; hide/unhide are first-class context-menu actions.
  (`qd_Visibility.{hpp,cpp}`)

- **T2 universal finger scroll inside `QdWindow`.** Touch-down anywhere in
  the content zone engages drag mode after 8 px delta;
  `SetScrollOffset(origin_sx − Δx, origin_sy − Δy)` produces smooth
  finger-scroll without per-element scroll plumbing. (`qd_Window.cpp`)

### Wave 3 — Position, gestures, modern APIs

- **Context-menu anchor now lands next to the focused tile.** ZL and
  long-press ctx menus used to spawn at screen-center because the
  `Open(x, y, items)` call passed a fallback. New anchor priority:
  long-press origin → `mouse_hover_index_` cell top-right →
  `dpad_focus_index_` cell top-right. Adds `render_origin_x_/y_` so
  windowed folder launchpads inherit the window-relative origin.
  *Known residual:* under certain focus-stale conditions the menu may
  still appear off-anchor; tracked for v3.3. (`qd_Launchpad.{hpp,cpp}`)

- **Launchpad tap = release-driven.** Touch-down now CAPTURES the cell
  into `lp_touch_down_vpos_` without dispatching; the launch fires on
  touch-LIFT only if neither long-press nor swipe fired during the
  gesture. This unblocks long-press (it now actually gets its 500 ms
  window before the launch fires) and matches the desktop dock's tap
  tolerance. (`qd_Launchpad.{hpp,cpp}`)

- **Folder window long-press parity.** `QdFolderLaunchpadElement`
  delegates `OnInput` to the root launchpad element, so the long-press +
  swipe + tap-on-release fixes apply inside folder windows automatically.

- **Monitor thermal + PCB temperature rows.** The legacy
  `tsGetTemperature` / `tsGetTemperatureMilliC` wrappers were removed in
  HorizonOS 14.0.0+; the panel was silently rendering "N/A" on modern
  firmware. Switched to the `TsSession` API
  (`tsOpenSession(TsDeviceCode_LocationExternal/Internal)` →
  `tsSessionGetTemperature`) with a three-step fallback ladder
  (Session → MilliC → Celsius) for older firmware. Fixed the inverted
  SoC/PCB label: per libnx ts.h, `TsLocation_Internal` is PCB and
  `External` is SoC. Added a dedicated "Thermal (PCB)" tile. Per-sensor
  rc is logged and rendered as `N/A (rc=0x%08X)` on failure for in-field
  decode. (`qd_MonitorLayout.{hpp,cpp}`)

- **Settings system-info temp rows.** Same FW 14+ ts-API regression
  applied to the Settings panel — same fix mirrored from the Monitor
  pattern. Hoisted `tsInitialize` + persistent `TsSession`s into
  `QdSettingsElement` ctor/dtor (was open/close per refresh), with
  rc-logging on every failure. Labels preserved
  (`sys_temp_pcb_ ← Internal`, `sys_temp_soc_ ← External`).
  (`qd_SettingsLayout.{hpp,cpp}`)

- **mGBA exit (and similar bypass-libnx NROs):** investigated, root
  cause located in mGBA's own statically linked libnx exit mode
  (`__nx_applet_exit_mode = 1` or direct `svcExitProcess`), NOT in
  uMenu. uMenu's launch path is canonical (`LaunchHomebrewLibraryApplet`
  → Fade → Finalize) and is identical for every working NRO. No code
  change in this release; possible future workarounds documented in
  the W3 retrospective.

- **`tools/log-tail.sh`.** New host-side log tail script for following
  `/Volumes/SWITCH SD/ulaunch/log_uMenu.log` and `log_uLoader.log`
  during HW testing. ANSI-coloured keyword highlighting (`WARN`, `ERR`,
  `Finalize`, `Launch`, `applet`, `focus`, `frame`, `nxlink`, `shell`),
  `--filter <regex>`, optional parallel loader-log follow, rotation
  handling, stock-macOS-bash compatible. (`tools/log-tail.sh`)

### Post-W3 — Launchpad touch-state snapshot fix

The launchpad's `OnInput` snapshots its previous-frame touch state into
`lp_was_touch_prev` (line ~833) *before* the hot-corner close-tap handler
overwrites the underlying field `lp_was_touch_active_last_frame_` with the
current frame's value. The long-press detector and swipe detector were
reading the field *directly* after that mutation, so on every touch-down
frame they observed `touch_prev = true` instead of the correct previous-
frame value. Result: `touch_now && !touch_prev` was always false inside
the launchpad → long-press start tick never armed and swipe origin never
recorded. Long-press worked everywhere else (desktop dock, hot-corner)
because those state machines capture their own pre-mutation snapshots.
Both detectors now read `lp_was_touch_prev`. (`qd_Launchpad.cpp`)

### New experimental skeletons (not wired into the UI yet)

- **Pokémon save editor (`QdSaveEditorLayout`).** PR-skeleton in
  `qd_SaveEditorLayout.{hpp,cpp}` — title picker for Let's Go / SwSh /
  BDSP / PLA / SV, named placeholder panels for Party-Box / Inventory /
  Trainer, host-buildable. Wiring into the Vault context menu deferred
  to the follow-up PR. Notes at `src/projects/uMenu/docs/save-editor-next-steps.md`.

- **SwishCrypto round-trip (`qd_SwishCrypto.{hpp,cpp}`).** Minimal
  SwishCrypto interface (SwSh-era SCBlock + XOR pad + SHA-256 hash) for
  host-side unit testing. Source: PKHeX SwishCrypto.cs + pkHouse
  (GPL-2.0 / GPL-2.0-or-later). The 128-byte XOR pad is a numeric fact
  embedded in the save format, not copyrightable expression — same
  posture as pkHouse's own implementation. No libnx / Switch SDK
  dependency in either file.

### Build / tooling

- `src/Makefile` `VERSION` bumped 3.1.0 → 3.2.0.
- README badge bumped to v3.2.0.
- `docs/CHANGELOG.md` introduced (this file).
- `docs/Z-RIGHT-CLICK-TEST-PLAN.md` added with the HW exercise plan that
  drove the W2 + W3 bug list.

---

## v3.1.0 — OS-grade ZL right-click + Tesla-overlay foundation [HW-GREEN]

Previous release. See git log entry `f01d2fed` for the diff.

## Older releases

Reconstruct from `git log --oneline -- src/Makefile docs/`. Pre-v3.1.0
changes are tagged in commit messages: v3.0.x = windowing finalize +
per-theme icon packs + HBMenu absorption, v2.8.x = sleep/wake + missing-
games + telnet RCE close-out, v2.4.0 = 10-of-everything theme system,
v2.3.7 = Reboot-to-Hekate composition + repo overhaul.
