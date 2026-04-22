# Q OS uLaunch Fork — Integration Spec
# Agent: uLaunch Upstream Analyst (Sonnet bg)
# Date: 2026-04-18 HST
# Upstream: XorTroll/uLaunch v1.2.3 (GPLv2)

---

## 1. Fork Strategy: Hard Fork Recommended

**Recommendation: Hard fork (`git clone` → detach, rename remote to `upstream`, maintain diverged branch).**

Rationale based on evidence:

- The primary Q OS UX goals (Vault, Dispatch palette, dock magnify, Dark Liquid Glass, Cold Plasma Cascade wallpaper, ZR/ZL input semantics) each require modifications to uMenu C++ source and/or Plutonium layout code. The theme system alone cannot deliver them.
- A soft fork (upstream tracking branch + cherry-picks) would require continuous rebase maintenance against an active upstream. XorTroll releases regularly and the code base changes substantially between versions.
- A plugin-only approach is not viable: uLaunch has no plugin API; any behavioral extension requires source modification.
- Hard fork with an `upstream` remote allows periodic cherry-picking of upstream bug fixes (SDL2 audio fix, Atmosphère compatibility patches) without forcing full rebases.
- **Upgrade-safety rule**: theme-deliverable items stay in theme files only, never in source, so they absorb upstream UI layout changes with minimal churn.

---

## 2. Q OS UX Port Plan

For each UX item from `STATE.toml [qos_ulaunch_fork].ux_to_port_from_mock_desktop_gui`:

### 2a. Vault File Browser (CRITICAL)
- **uLaunch module**: uMenu — new `ui_VaultLayout.cpp` (parallel to `ui_MainMenuLayout.cpp`). uSystem needs no changes.
- **Theme vs source**: SOURCE REQUIRED. Vault is a new layout with column-view file browser, sidebar, favorites panel. No analog exists in upstream. Must add a new `MenuType::Vault` enum value and a new Plutonium Layout subclass.
- **Effort**: HIGH. New layout, new Plutonium element types (column list, sidebar panel), FS traversal via uCommon's `ul::fs` wrappers. Dock slot 0 wiring is a config + source change in `ui_MenuApplication::EnsureLayoutCreated`.

### 2b. Dispatch Command Palette (CRITICAL)
- **uLaunch module**: uMenu — new `ui_DispatchLayout.cpp`. Triggered from main menu via a new `SystemMessage::OpenDispatch` (added to both uSystem and uMenu SMI protocol).
- **Theme vs source**: SOURCE REQUIRED. Fuzzy-search overlay with keyboard-first input has no upstream analog. Requires adding a new layout and extending the SMI protocol enum in `smi_Protocol.hpp`.
- **Effort**: HIGH. New layout, fuzzy string matching over the title/homebrew list, new SMI message type. The title list is already cached in uSystem — just needs a new query path.

### 2c. Dark Liquid Glass Aesthetic — Colors and Wallpaper Replacement
- **uLaunch module**: Theme only (`default-theme/ui/UI.json` + `default-theme/ui/Background.png`).
- **Theme vs source**: THEME — fully achievable. Replace `Background.png` with a Dark Liquid Glass render. Update all color hex values in `UI.json` (text_color, menu_focus_color, menu_bg_color, dialog colors) to the Q OS palette.
- **Effort**: LOW. PNG swap + JSON edit. No C++ changes needed.

### 2d. Cold Plasma Cascade Procedural Wallpaper (seed 1364153183)
- **uLaunch module**: Theme file for the static render; uMenu source for animated version.
- **Theme vs source**: THEME for static render (pre-rendered PNG from the existing NRO wallpaper generator). SOURCE REQUIRED for animated/live wallpaper — upstream has no animated background support (background is a static PNG). If static is acceptable for v0.1-v0.3, theme path suffices. Live animation requires adding a Plutonium background element with per-frame painting.
- **Effort**: LOW (static, theme-only) / HIGH (animated, source change to uMenu renderer).
- **Recommendation**: Ship static pre-rendered PNG in theme for v0.1. Add live animation in v0.5+.

### 2e. Dock with Magnify 1.4x/1.2x/1.05x + 5s/12-frame Auto-Hide
- **uLaunch module**: uMenu `ui_MainMenuLayout.cpp` and `ui_EntryMenu.cpp`.
- **Theme vs source**: SOURCE REQUIRED. The upstream entry menu renders icons at fixed size; no magnify or auto-hide logic exists. Requires adding focus-distance scaling to the icon render loop and a timer-driven hide/reveal animation.
- **Effort**: MED. The entry menu is already parameterized with UI.json positions. Magnify requires adding a scale factor derived from focus distance; auto-hide requires a timeout counter in the layout's `OnInput`/`OnFrame` path.

### 2f. D-pad Focus Tree + ZR/ZL Click Semantics (ZR=LEFT, ZL=RIGHT, A=LEFT alt)
- **uLaunch module**: uMenu `ui_IMenuLayout.cpp` — `OnLayoutInput` handler.
- **Theme vs source**: SOURCE REQUIRED. Upstream uses HidNpadButton_ZR/ZL for standard Switch functions. Remapping to navigation requires intercepting those keys before Plutonium's default handler.
- **Effort**: LOW-MED. Single function change in `OnLayoutInput`. ZR/ZL interception is a 10-line change; D-pad focus tree re-wiring depends on how many layouts need updating.

### 2g. EVENT Telemetry Grammar
- **uLaunch module**: uMenu — new `ul_EventLog.hpp` utility header added to uCommon.
- **Theme vs source**: SOURCE REQUIRED. Log emission hooks need to be added at key event sites (CURVE, ANIM, INPUT, FINDER, VAULT). Follows existing `UL_LOG_INFO` pattern — additive, no restructuring.
- **Effort**: LOW. Additive logging, no structural change.

### 2h. WM Multi-Window Z-Stack + Drag/Resize/Snap Zones
- **uLaunch module**: uMenu — new WM layer on top of Plutonium application.
- **Theme vs source**: SOURCE REQUIRED. Upstream is single-layout, no multi-window concept. Requires a new window manager abstraction above `MenuApplication::OnLoad` that routes input to the focused window.
- **Effort**: HIGH. Foundational architecture change. Defer to v0.7+.

---

## 3. Theme vs Fork Decision Summary

| UX Item | Path | Version |
|---------|------|---------|
| Dark Liquid Glass colors (UI.json) | THEME | v0.3.0 |
| Cold Plasma Cascade wallpaper (static PNG) | THEME | v0.3.0 |
| Sound effects replacement | THEME | v0.3.0 |
| Dock magnify + auto-hide | SOURCE (uMenu) | v0.4.0 |
| ZR/ZL navigation semantics | SOURCE (uMenu) | v0.4.0 |
| EVENT telemetry grammar | SOURCE (uCommon) | v0.4.0 |
| Vault file browser layout | SOURCE (uMenu + uSystem) | v0.5.0 |
| Dispatch command palette | SOURCE (uMenu + uSystem SMI) | v0.6.0 |
| Cold Plasma Cascade live/animated | SOURCE (uMenu renderer) | v0.7.0 |
| WM multi-window z-stack | SOURCE (uMenu + new WM layer) | v0.8.0+ |

---

## 4. NRO Interop: Launching qos-mock-desktop-gui-v1.0.0.nro

The path exists and is already wired in uLaunch upstream:

1. uMenu sends `SystemMessage::LaunchHomebrewApplication` (or `LaunchHomebrewLibraryApplet`) to uSystem via SMI storage push.
2. The message carries `nro_path = "/switch/qos-mock-desktop-gui-v1.0.0.nro"` and a donor `app_id` (any installed title used as the application process host).
3. uSystem receives this in `MainLoop()` at the `LaunchHomebrewApplication` action handler and calls uLoader, which chains into the NRO.
4. On NRO exit, uSystem resumes uMenu.

For Q OS fork, this becomes: Vault "Launch Q OS Desktop" entry triggers `SystemMessage::LaunchHomebrewApplication` with path `/switch/qos-mock-desktop-gui-v1.0.0.nro`. No new protocol needed — this is existing upstream machinery. This proves the ns:am2 IPC wrapper path that TUI v0.10 game-launch slot also needs.

Shared protocol between uLaunch fork and mock-desktop-gui NRO: none required at v0.1. Future versions may add a lightweight argument-passing convention via the NRO argv mechanism (existing in libnx).

---

## 5. Build Pipeline

### Current System State
- devkitPro detected at `/opt/devkitpro/devkitA64/`
- `aarch64-none-elf-gcc` available but not in `$PATH` by default — add: `export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH`
- `export DEVKITPRO=/opt/devkitpro`

### Missing Dependencies (install via dkp-pacman)
```
dkp-pacman -S switch-sdl2       # MUST use revision 2.28.5-3, not latest
dkp-pacman -S switch-freetype
dkp-pacman -S switch-glad
dkp-pacman -S switch-libdrm_nouveau
dkp-pacman -S switch-sdl2_gfx
dkp-pacman -S switch-sdl2_image
dkp-pacman -S switch-sdl2_ttf
dkp-pacman -S switch-sdl2_mixer
```
**SDL2 version pin is critical** — see README warning. Modern SDL2 causes `audren` crash when suspending games.

### Submodule Init (required before first build)
```
cd tools/qos-ulaunch-fork-upstream-clone  # or fork working dir
git submodule update --init --recursive
```

### Build and Stage to SD
```
cd tools/qos-ulaunch-fork-upstream-clone
make                             # builds all components, produces SdOut/
# Stage to Switch SD:
cp -r SdOut/atmosphere /Volumes/SWITCH\ SD/
cp -r SdOut/ulaunch    /Volumes/SWITCH\ SD/
cp -r SdOut/switch     /Volumes/SWITCH\ SD/
# Archive per ROADMAP §6 ritual:
cp uLaunch-v*.zip archive/vX.Y.Z/
```

### SD Card Structure (Atmosphère contents)
```
atmosphere/contents/0100000000001000/exefs.nsp   ← uSystem (replaces qlaunch)
ulaunch/bin/uMenu/main                           ← uMenu NSO
ulaunch/bin/uMenu/romfs.bin                      ← theme + UI assets
ulaunch/bin/uLoader/applet/main                  ← uLoader NSO (applet variant)
ulaunch/bin/uLoader/application/main             ← uLoader NSO (application variant)
switch/uManager.nro                              ← management tool
```

---

## 6. Version Chain (v0.1.0 → v1.0.0)

Each version = one discrete change per ROADMAP §3 rule.

| Version | Description | Gate |
|---------|-------------|------|
| v0.1.0 | Scaffolding: empty fork directory tree, license audit, this INTEGRATION-SPEC.md, ROADMAP.md. No code changes to upstream. | Spec files present, no build required |
| v0.2.0 | Upstream baseline: submodule init, first clean build of unmodified uLaunch v1.2.3 producing `SdOut/`. Add `archive/v0.2.0/` with clean build artifacts. | `make` exits 0, `uLaunch-v1.2.3.zip` exists |
| v0.3.0 | Theme pass: Dark Liquid Glass color palette in `UI.json` + Cold Plasma Cascade static wallpaper PNG. Sound effects stripped/replaced with silent stubs or Q OS sfx. No C++ changes. | uLaunch boots on device with Q OS visual identity |
| v0.4.0 | Input: ZR=LEFT, ZL=RIGHT, A=LEFT-alt semantics in `IMenuLayout::OnLayoutInput`. D-pad focus tree verified working. EVENT telemetry grammar emitted to log. | Input nav matches mock-desktop-gui behavior |
| v0.5.0 | Dock: magnify 1.4x/1.2x/1.05x + 5s/12-frame auto-hide. C++ changes to `ui_EntryMenu.cpp`. | Dock animation matches mock-desktop-gui demo |
| v0.6.0 | NRO launch: "Q OS Desktop" entry in main menu that launches `qos-mock-desktop-gui-v1.0.0.nro` via `LaunchHomebrewApplication` SMI path. | NRO chainloads cleanly, uLaunch resumes on exit |
| v0.7.0 | Vault layout: new `ui_VaultLayout.cpp` — 6-slot dock position 0, sidebar, column view, favorites. Read-only FS browsing via uCommon fs wrappers. | Vault opens, displays SD card tree |
| v0.8.0 | Dispatch palette: new `ui_DispatchLayout.cpp` + SMI `OpenDispatch` message. Fuzzy search over title + homebrew list. | Palette opens, filters titles in real time |
| v0.9.0 | Cold Plasma Cascade live wallpaper: animated background element in uMenu renderer (6 plasma blooms, 80 stars, 18 data-streams). | Wallpaper animates at stable framerate |
| v1.0.0 | Integration complete: all v0.3–v0.9 features stable, telemetry confirmed on real hw (Tegra X1), safe-return to Nyx verified, archive tagged v1.0.0. | 100% hw green, archive staged |

---

## 7. Top 3 Risks + Mitigation

### Risk 1: Atmosphère Version Lock (HIGH probability)
uSystem replaces title `0100000000001000` via `exefs.nsp` override. Atmosphère updates frequently and occasionally change the ABI for system applets or rename IPC service behaviors. An Atmosphère update can black-screen the device until uLaunch is updated or removed.

**Mitigation**: Pin Atmosphère version used for testing in ROADMAP. Always keep `uManager.nro` on SD so uLaunch can be disabled from hbmenu without needing RCM. Add a version check in `InitializeSystemModule()` that logs the Atmosphère version on every boot.

### Risk 2: SDL2 Audio Version Pin (MED probability, high blast radius)
The build MUST use `switch-sdl2` revision `2.28.5-3`. The current dkp-pacman package is newer. Installing the wrong SDL2 version causes the audio sysmodule to crash when suspending games — a system-level fault visible as audio cutting out or the console requiring reboot.

**Mitigation**: Pin the SDL2 package version in the build documentation and archive the specific SDL2 .pkg.tar.xz in `tools/qos-ulaunch-fork/deps/` before the first build. Add a version assertion to the build script that fails fast if the wrong SDL2 is detected.

### Risk 3: GPLv2 Distribution Obligation on Q OS Binary Releases (LOW probability if private, HIGH if public)
If Q OS uLaunch fork binaries are ever distributed publicly (GitHub release, Discord, SD card image), the full corresponding source must be available. Failing to provide source while distributing GPLv2 binaries is a license violation.

**Mitigation**: Keep the fork repo public on GitHub from day one (it's a fork of a public OSS project — there's no reason to hide it). Any binary release should link to the fork's source. This is consistent with STATE.toml's "No upstream weaponization" constraint. If creator ever wants proprietary distribution, the Q OS-specific UX code must be written as a separate, theme-only or IPC-only plugin that doesn't statically link against the GPLv2 tree — which is architecturally desirable anyway.

---

*End of Integration Spec — Agent: uLaunch Upstream Analyst — 2026-04-18 HST*
