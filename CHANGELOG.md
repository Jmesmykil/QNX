# Changelog

All notable changes to Q OS uMenu are documented here. Format is per-release, newest first.
Older v0.x / v1.x / early-v2.x archived changelogs live under [`archive/`](./archive/).

---

## v3.7.0 — SVG themed window chrome + 4-corner buttons (2026-06-18)

**Status: HW-verified GREEN on OG Switch Erista, Atmosphère + Hekate, HOS 20.0.0.** Boots clean and fast.

The window manager moves from code-drawn flat chrome to **per-theme SVG window
frames**. Each theme ships a `window/q-os-{0..9}.svg` master that is rasterized
once (vendored nanosvg, symbol-renamed to avoid the SDL2_image collision) and
drawn as a **nine-patch** so corners stay crisp and the center bands stretch to
any window size.

### Added

- **Per-theme SVG window chrome** — `QdFrame` paints all chrome (shadow, focus
  glow, nine-patch frame, focus ring, discs, title, hint/tooltip text) from a
  theme-keyed 640×400 SVG master. `QdWindow` keeps behaviour/scroll; chrome is a
  separate single-responsibility component (`qd_Frame`, `qd_NinePatch`,
  `qd_SvgRaster`).
- **Four-corner window buttons** — Close (×) top-left, Maximize/Restore (□)
  top-right, Minimize (–) bottom-left, resize grip (↗) bottom-right; each with
  its own colour and glyph. Title text insets clear the top discs; status-bar
  hint insets clear the bottom-left disc.

### Fixed

- **SVG loader no longer re-reads on failure every frame** — `EnsureSource`
  caches a failed load per theme index (records the attempt before loading,
  early-returns on a cached miss) instead of hitting the file ~60×/sec. This was
  the "super slow" stall.
- **Theme index clamped to [0,9]** — `g_active_theme_pack_idx` (a `size_t`) could
  exceed the 10 shipped masters and request a non-existent SVG path; now clamped
  before the chrome lookup.

## v3.6.10 — BDSP save editor, full-mode homebrew, premortem hardening (2026-05-29)

**Status: HW-verified GREEN.** BDSP (Gen 8) save parser with clean-room checksum,
full-mode homebrew launch via `smi::LaunchHomebrewApplication`, and a pass of
premortem hardening across the save/restore path.

## v3.5.0 — Stabilization, hardening, in-OS cheat installer (2026-05-27)

**Status: HW-verified GREEN.** Stabilization sweep plus the in-OS cheat installer.

## v3.4.0 — Save autoscan + Cheats system (2026-05-21)

**Status: HW-verified GREEN.** Pokémon save autoscan surface and the Atmosphère
cheats browser/toggle.

## v3.3.0 — Footprint + scroll + save editor + filesystem shortcuts (2026-05-20)

**Status: HW-verified GREEN.** Memory-footprint work, window scroll, the save
editor surface, and the Vault filesystem shortcuts.

## v3.2.4 — Observability + window-pin sprint (2026-05-20)

**Status: HW-verified GREEN.** `QdResourceLedger` observability and the
pin-to-corner window-manager sprint (12 sub-waves).

## v3.2.0 — W2 + W3 bug-fix sprint (2026-05-19)

**Status: HW-verified GREEN.** Fifteen HW-reported issues fixed across two waves.

## v3.1.0 — OS-grade ZL right-click foundation: Phase Z1 design + Z2.0-Z2.5 (2026-05-19)

**Status: HW-verified GREEN on OG Switch Erista, Atmosphère + Hekate 6.5.2, HOS 20.0.0.**

Lays the foundation for treating every clickable surface in uMenu qdesktop as a
right-clickable OS object. Z1 audit mapped 18 distinct surfaces; Z2.0-Z2.5
shipped six surfaces wired end-to-end with the unified `QdContextMenu` primitive
extended to support submenus. Persistence primitive `QdVisibility` plumbed for
upcoming Z2.3 (Hide / per-icon visibility).

### Added

- **`docs/Z-RIGHT-CLICK-OS-DESIGN.md`** — 18-surface audit, 5 locked design
  decisions (visibility TOML, full dock reorder, folder rename, submenu
  primitive first, hybrid dispatcher), Z2-Z2.10 phasing roadmap.
- **`QdContextMenu` submenu support** — `QdContextMenuItem` struct with
  optional `submenu_items`; `QdContextMenuSelection { parent_index, sub_index }`;
  back-compat preserved via existing `Open(std::vector<std::string>...)`
  overload. Submenu panel auto-flips to the left when there's no room right.
  ESC/B/Left closes submenu only; second ESC closes everything. Chevron `▸`
  marks rows with submenus.
- **`QdVisibility` singleton** — `IsHidden(stable_id) / SetHidden(...)` API,
  process singleton, lazy-load + atomic tmp+rename to
  `sdmc:/ulaunch/qos-visibility.toml`. Mirrors `SaveFavorites` I/O pattern
  (`fsdevCommitDevice` + Switch RenameFile EEXIST workaround). Plumbed for
  Z2.3 usage; no surface consumes it yet.
- **`CtxSurface::EmptyDesktop`** — ZL on truly empty desktop area opens
  `[New Custom Folder, Change Wallpaper ▸ (10 theme packs), Refresh, Cancel]`.
  "New Custom Folder" calls existing `CreateFolderFlow()` (already-written
  swkbd helper that was unwired). "Change Wallpaper" cycles 10 unified
  theme packs (`SetActiveThemePack` + `SaveActiveThemePack`). Hot-corner
  widgets (top-left + top-right 96×72 areas) excluded so the dropdown
  owner still handles those.
- **Folder tile context menu** — extended `CtxSurface::DesktopFolder` from
  `[Re-classify, Cancel]` to `[Open, Re-classify Apps in <name>,
  Choose Folder Theme ▸ (10 packs), Cancel]`. "Open" calls
  `OpenFolderWindow(idx)`; submenu shares the wallpaper picker.
- **`CtxSurface::MinimizedTile`** — ZL on a minimized-window dock tile opens
  `[Restore, Close Window, Cancel]`. New `QdMinimizedDockEntry::PollAction`
  enum (`None / Restore / OpenContextMenu`); `QdWindowManager::minimized_ctx_menu_`
  drain mirrors the suspended-app pattern. New `WindowManager::CloseMinimizedEntry`
  drops the entry from `minimized_entries_` without restoring (snapshot
  texture freed by dtor).
- **`CtxSurface::FavoriteItem`** — ZL on a favorites-strip tile opens
  `[Launch, Remove from Favorites, Cancel]`. Reuses existing
  `LaunchIcon(icon_idx)` and `ToggleFavorite(NroEntry)`.

### Fixed

- **HW bug: Terminate-suspended-app left dock tile + Task Manager stuck.**
  `wm_.on_terminate_suspended_requested` was firing
  `smi::TerminateApplication()` but NOT clearing
  `g_GlobalSettings.system_status.suspended_app_id` locally. Both
  `QdWindowManager::RefreshSuspendedApps` and
  `QdTaskManagerLayout` read that field directly to drive their renders,
  so the killed app appeared "still suspended" to both UIs until uSystem
  eventually caught up. Now mirrors the dock-icon terminate path at
  `qd_DesktopIcons.cpp:4029` — calls `ResetSuspendedApplication()` on
  R_SUCCEEDED + shows "Closed running app." notification. Fixes the
  user-reported regression on Sphaira.

### Z order / dispatcher

- Folder-tile menu open site refactored from `std::vector<std::string>` to
  `std::vector<QdContextMenuItem>` (needed for the theme submenu).
  Empty-desktop, favorite-item, and folder-tile all build their menus
  through the rich `QdContextMenuItem` path so submenus work uniformly.
- `QdDesktopIconsElement::OnRender` now calls
  `wm_.RenderMinimizedContextMenu(r)` alongside the existing
  `wm_.RenderSuspendedContextMenu(r)` to render the new minimized-tile
  context menu above the dock band.
- Existing `context_menu_` (desktop layer) ladder extended: Path 1 Window,
  Path 2 Dock, Path 3 DesktopFolder, Path 3.5 FavoriteItem, Path 4
  EmptyDesktop (was no-op).

### Deferred to subsequent Z2 sub-phases (after HW gate)

- **Z2.2-extra**: Disable Folder + Rename Folder (needs `qos-folder-settings.toml`
  schema extension + swkbd UX).
- **Z2.3**: Dock-icon `Move to Folder ▸` submenu + per-icon Hide (consumes
  `QdVisibility` already plumbed).
- **Z2.6**: Dock reorder model — `QdDockLayout` + `qos-dock-layout.toml`.
- **Z2.7-Z2.10**: Window snap context options, hot-corner customization,
  top-bar ZL, Vault popup migration to `QdContextMenu`.

---

## v3.0.2 — Tesla-style overlay foundation + cumulative tech-debt audit (2026-05-19)

**Status: local — not a public release. Cumulative cleanup + groundwork for v3.1.**

### Added

- **uSystem overlay layer (T1–T2.3)**: persistent `vi:m` max-Z managed layer
  owned by uSystem that survives applet transitions. Renders chrome (cyan
  border + dark-navy title bar + "Q OS" title + ✕/− glyphs) on top of uMenu,
  homebrew NROs, AND retail Switch games during active gameplay. First
  production-proven persistent overlay from a SystemApplet (not sysmodule)
  in the Switch homebrew ecosystem.
- New uSystem modules under `source/ul/system/overlay/`: `overlay_TestLayer`,
  `overlay_Renderer` (RGBA_4444 primitives), `overlay_FontCache` (stbtt +
  pl:u Standard shared font with glyph caching), `overlay_Actions` (atomic-
  flag bridge for the deferred T3 touch input).
- `viInitialize(ViServiceType_Manager)` claimed at uMenu `__appInit` BEFORE
  `__nx_win_init()` for libnx vi-service-type stickiness.

### Fixed

- **printf UB**: `main.cpp:545` `[Verify-…] prog: %.2f%` → `%%` (trailing
  bare `%` was undefined behavior on the application-verify progress log).
- **uLoader program_id typo**: both `uLoader_applet.json` and
  `uLoader_application.json` `program_id` corrected from 15-digit
  `0x01000000000FFFF` to 16-digit `0x010000000000FFFF`.
- **`__nx_vi_layer_id` reset** in `FinalizeTestLayer` after
  `viDestroyManagedLayer` so a subsequent re-init can't reuse a destroyed
  managed-layer id.
- **Duplicate `g_desktop_folder_bg_tex` free** in `~QdDesktopIconsElement`
  removed (FIX-4 was inadvertently a duplicate of the v1.7.2 free).

### Removed (~1450 LOC of dead code)

- v3.1 Phase 1 + Phase 2.1 boot-time probes (`qd_FrameCaptureExperiment`,
  `qd_WindowedLaunchExperiment`) — architectural conclusion captured in
  `docs/archive/v3.1-bg-indirect-pivot/`; capssc rc=0xE0CE and
  `viGetIndirectLayerImageMap` rc=2114-0011 (handle-ownership check that
  can't be bypassed from a SystemApplet).
- BG-indirect uSystem surface (`la::IsBackgroundIndirectActive`,
  `TerminateBackgroundIndirect`, `StartBackgroundIndirect`,
  `Push/PopBackgroundIndirect`, `g_BackgroundIndirectHolder` static,
  `ecs::RegisterLaunchAsBackgroundIndirectApplet` inline) — pivoted to
  uMenu-side `appletCreateLibraryAppletSelf`.
- `qd_DevConsoleElement.{cpp,hpp}` (784 LOC orphan — never instantiated).
- Test-rectangle scaffolding constants in `overlay_TestLayer.cpp`.
- `src/README.md` (upstream XorTroll/uLaunch verbatim — top-level
  `README.md` is the Q OS SSOT).
- `.cpp.bak` / `.cpp.bak2` files under `src/projects/uMenu/source/`.

### Performance

- Overlay render-thread vsync timeout 1 s → 100 ms (still 6× nominal
  16.67 ms frame time, but a single stall no longer wastes 60 frames of
  wall time).
- Frame-counter log throttled `% 300` (5 s) → `% 3600` (60 s).
- `MeasureString` lifted out of per-frame render loop into lazy-init
  statics for the chrome's static glyphs.

### Build / Tree hygiene

- `umanager` moved out of default `package:` target (parallels the
  2026-04-25 treatment of `uscreen`) — it needs `-lcurl` which devkitPro
  portlibs doesn't ship on creator's macOS env. Existing on-SD
  `uManager.nro` continues to work; rebuild via explicit `make umanager`.
- Makefile dep on `ul_Results.gen.hpp ← ul_Results.rc.hpp` so codegen
  staleness can't silently survive a .rc.hpp edit.
- `.gitignore`: `logs/`, `staging/` added.
- `docs/` cleanup: archived 6 stale doc groups (v3.1 BG-indirect pivot,
  abandoned-renderer pivots, stabilization cycles, 2026-04 stabilization)
  to `docs/archive/`. `v1.2.3` → `v3.0.2` in release-process docs.
  `/Users/nsa/` → `/Users/astral/` username drift fixed in active docs.

---

## v3.0.1 — Phase 1 frame-capture probe + Tesla overlay R&D groundwork (2026-05-18)

**Status: tagged + pushed.** Establishes the v3.1 windowed-homebrew research
foundation. uMenu ships with the `qd_FrameCaptureExperiment` capssc probe
that logs which `ViLayerStack` values return valid JPEG headers. Result was
decisive: all four stacks returned rc=0xE0CE (`capsrv` module 206 desc 112,
permission-denied) → the production frame-capture primitive must be
`viGetIndirectLayerImageMap`, not `capsscCaptureJpegScreenShot`. Phase 2
research consolidated into the v3.1 plan doc.

---

## v3.0.0 — Windowing finalized, icon packs, theme auto-upgrade (2026-05-18)

**Status: HW-verified GREEN on OG Switch Erista, Atmosphère + Hekate 6.5.2, HOS 20.0.0.**

The first major release since v2.0. Ten themes, full window chrome, real icon-pack differentiation, and the HBMenu absorption completed end-to-end.

### Added

- **Per-theme icon packs** — ten themes, each a fully distinct shape vocabulary across the six desktop folder categories, the launchpad dock roles, and a per-theme hot-corner emblem. 170 PNGs total, generated offline by `scripts/generate-qos-theme-icons.py` and shipped inside each `.ultheme` bundle. Glass keeps the Q OS Q; Neon, Minimal, Retro, Cards, Pastel, Dark, Gradient, Blueprint, and Pixel each get their own identity emblem (lightning, dots, Pac-Man wedge, spade, heart, flame, prism, compass, 8-bit star).
- **Window finalization**:
  - Bottom-bar hint text on every window — instructions live IN the chrome, not on a separate screen-level strip outside.
  - Four corner buttons each have a distinct color AND distinct glyph (× close / □ maximize / – minimize / ↗ resize) so they're unmistakable.
  - Default window 780×480 → 1280×800 (40% × 44% of screen → 66% × 74%). 2.7× the content area.
  - Vertical-list layouts (Settings, Monitor, Tasks) now opt into width-bound scale so content fills the viewport horizontally with a real V-scrollbar instead of letterboxing.
- **Theme auto-upgrade**:
  - `EnsureQosThemesOnSd` in `main.cpp` compares SD `.ultheme` bundles to the romfs-packed versions every boot. Size mismatch → SD bundle is renamed `<name>.qos-prev.<size>.bak` and the romfs version is written through.
  - When any bundle is refreshed, the active theme cache is invalidated so `CacheActiveTheme` re-extracts from the freshly-rewritten zip — no manual "apply theme" needed after a fork upgrade.
- **Brand-fade loading screen is themed** — gradient colors now come from `g_QdTheme.desktop_bg` + `accent` instead of hardcoded cyan→lavender. No Q OS branding flashed on every applet load.
- **HBMenu absorption (Stage 4 — final)**:
  - `tools/migrate-hbmenu/migrate.sh` removes `hbmenu.nro` from the SD and reclaims the disk footprint.
  - Script handles both canonical (`/hbmenu.nro` at SD root) and alternate (`/switch/hbmenu.nro`) install layouts.
  - `hbloader` (the NRO runtime) is explicitly guarded and never touched.
  - Migration log written to `sdmc:/qos-shell/migrations/hbmenu-<timestamp>.log`.
  - Vault file manager has been the de-facto HBMenu replacement since v2.7; v3.0 closes out the removal half.

### Changed

- **About removed from the dock builtin list** (`BUILTIN_ICON_COUNT` 6 → 5). About is still reachable via the top-left hot-corner dropdown (case 4) which is where the user actually goes to find it. `DockAbout.png` is preserved in `romfs/default/` and inside every `.ultheme` bundle for future use.
- **Dispatch switch renumbered**: Vault=0, Monitor=1, AllPrograms=2, Tasks=3, Nintendo=4.
- **About PrefersWidthBoundScale reverted to false** — About is a centred-card layout, not a vertical list. Uniform-fit lets the card letterbox sides but stay fully visible at correct proportions.

### Fixed

- **"Nothing opens after a few resizes"** — stuck `resize_drag_active_` flag was getting set when a window lost focus mid-resize, after which every input event got swallowed by the PollEvent "consume while resizing" early-return. Three layers of defense added:
  1. Watchdog frame counter — force-ends the drag after ≥10 no-touch frames (~167 ms at 60 fps).
  2. New `QdWindow::ResetInteractionState()` API wipes resize/cursor/titlebar drag flags in one call.
  3. WindowManager calls `ResetInteractionState()` on every Close and Minimize so stale flags can't outlive the window through restore.
- **Theme switch reverting to fallback icons** — root cause was `EnsureQosThemesOnSd`'s `access(dst, F_OK) == 0 → continue` skip-if-exists guard, which meant once any SD `.ultheme` was seeded (even from an older v2.x release) no upgrade ever refreshed it. v3.0 size-mismatch detection now handles fork upgrades correctly.
- **Stale-bundle "Pastel boot CORRECT but theme switch reverts"** HW regression from v2.9.7/v2.9.8 — diagnostic logging confirmed all six folder PNGs + HotCornerEmblem loaded successfully once the cache was repopulated from the fresh bundle. Permanent fix wired through `EnsureQosThemesOnSd` + `RemoveActiveThemeCache` on upgrade.

### Removed

- About icon from the dock band. (Kept on the top-left hot-corner dropdown.)
- Q OS branding from the brand-fade transition splash. (Now a themed gradient sourced from the active palette.)

### Known issues (carried forward)

- Telnet shell on port 9999 has a security gap — anyone on the same LAN can run arbitrary NRO paths via `telnet <ip> 9999; launch <path>`. **Only enable the DevConsole on a LAN you trust.** PIN auth + path guards + bind-to-local-IP queued for v3.0.x.
- Third-party NROs (sphaira, JKSV, hbmenu replacements) still launch full-screen. Windowed-NRO support is the headline v3.1 feature and needs a custom hbloader fork plus deko3d compositing — multi-week work.
- Nintendo applets (Album, Controllers, Mii, Web) still launch full-screen. They're sysmodule processes with firmware-rendered UI; unlikely to ever be windowable without firmware patches.
- RetroArch crashes on Home press during a game (its own teardown path crashes during forced applet shutdown — same on every Switch homebrew launcher). Workaround: use RetroArch's Quit menu instead of Home.

---

## v2.9.x — Icon-pack iteration cycle (2026-05-17 → 2026-05-18)

Rapid-iteration cycle that produced the v3.0 icon-pack system. Twelve HW-test rounds:

- **v2.9.0** — first attempt at per-theme procedural folder draws. Hit ~9 000 SDL primitives/sec; UI thread pegged.
- **v2.9.1** — tried caching procedural draws to render-target textures. `SDL_CreateTexture` failed silently on the new render targets, falling back to per-frame procedural every frame. Made it worse.
- **v2.9.2** — reverted to v2.8.8 tinted-Folder.png path. Performance restored. Login regression fixed.
- **v2.9.3** — separate dock-icon size constants (`DOCK_ICON_BG_W/H = 128`) to stop dock icons overflowing the 148-px dock band.
- **v2.9.4** — favorites strip moved from y=726 → y=666 to clear the dock, scaled from 11 → 8 visible tiles for the new 192² grid.
- **v2.9.5** — first offline PNG generator. 17 roles × 10 themes = 170 PNGs. Wrote Glass theme to the wrong directory (build wipes `projects/uMenu/romfs/default/` before pack).
- **v2.9.6** — fixed Glass output path. Per-category folder PNGs reach romfs. Cache miss on non-Glass themes still falls back to Glass.
- **v2.9.7** — folder cell geometry fix (square icon below 62-px label band, no 2× horizontal stretch). Diagnostic logging added for HotCornerQ load path.
- **v2.9.8** — distinct per-theme glyph vocabularies (NES pad, arcade joystick, sword, club, heart, flame, prism, compass, star). Brand-fade themed. Folder cell tile bg removed (glyph-only).
- **v2.9.9** — stale-bundle upgrade lane (`EnsureQosThemesOnSd` size-compare + cache invalidation). Active theme cache re-extracts from fresh bundles automatically.
- **v2.9.10** — windows finalization: hint text on every window, distinct corner glyphs, larger default window, width-bound vertical-list scale.
- **v2.9.11** — resize watchdog (3 defense layers) — fixes "nothing opens after a few resizes" HW bug.
- **v2.9.12** — About removed from dock; reverted About scale to uniform-fit so the card stops getting clipped at the bottom.

---

## v2.8.x — Stabilization (2026-05-12 → 2026-05-16)

- **v2.8.8** — sleep/wake fix + missing-games fix + count-label cleanup. HW-GREEN.
- **v2.8.1** — telnet `CmdLaunch` RCE close-out (defensive; the LAN-side gap remains until PIN auth lands).
- **v2.8.0** — IPC hardening + 10-theme system foundations.

## v2.4.0 — Ten-of-everything theme system (2026-04-30)

First pass at the multi-theme infrastructure. Palette swapping landed; full icon-pack vocabularies came later in the v2.9.x cycle.

## v2.3.7 — Reboot-to-Hekate composition + repo overhaul (2026-05-16)

Composition over reinvention landed for the hot-corner Reboot-to-Hekate action. Three lines of `smi::LaunchHomebrewLibraryApplet` glue replaced ~200 lines of failed direct-reimplementation attempts (NPDM patches, spsm fallback, Mariko branches — all wrong).

## v2.3.x — HBmenu Absorption Phases 2 & 3 (2026-05-09 → 2026-05-12)

- **v2.3.6** — SMI tile delegation, nxlink autolaunch, recursive NRO scan, Vault launch-as-application.
- **v2.3.3** — Theming Phase B initial wire-up.
- **v2.3.0** — Telnet/netcat shell server (the `CmdLaunch` gap originates here; hardening tracked for v3.0.x).

## v2.1.0 — Public preview baseline (2026-04-28)

First public-mirror release. Vault + dock + hot corners + initial theme system.

---

Older releases (v0.x, v1.x, early v2.x) are archived under [`archive/`](./archive/) with their individual changelogs preserved. The chain back to upstream uLaunch is documented in [LICENSE-AUDIT.md](./LICENSE-AUDIT.md).
