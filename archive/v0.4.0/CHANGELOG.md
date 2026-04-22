# Q OS uLaunch Fork — v0.4.0 CHANGELOG

Released: 2026-04-18
Base: v0.3.0 (uMenu main SHA256: c7caad19556f7be1ea1dc40bbbdad798ec424ed8bc1c437dccd82fd17d1de9bc)
Build: devkitPro / devkitA64, gnu++23, aarch64-none-elf

## Build Output

- uMenu main SHA256: `3bc65f5328bad7b091b547c7c4cbc5264bf0b085613739debfcf0580b7da469b`
- Status: CLEAN BUILD — zero errors, zero warnings
- Staged: SdOut/ulaunch/bin/uMenu/main

## Changes

### QOS-PATCH-009: PlasmaWallpaperElement — Cold Plasma Cascade procedural wallpaper

**New files:**
- `include/ul/menu/ui/ui_PlasmaWallpaperElement.hpp`
- `source/ul/menu/ui/ui_PlasmaWallpaperElement.cpp`

**Modified:**
- `include/ul/menu/ui/ui_MainMenuLayout.hpp` — `#include ui_PlasmaWallpaperElement.hpp` + `plasma_wallpaper` member
- `source/ul/menu/ui/ui_MainMenuLayout.cpp` — constructor: init `plasma_wallpaper(nullptr)`, construct + `Add()` at layer 0
- `source/ul/menu/ui/ui_BackgroundScreenCapture.cpp` — `InitializeScreenCaptures`: when `PLASMA_WALLPAPER_ENABLED`, skip non-suspended boot-capture load; suspended-app capture still loaded for resume-fade correctness

## Wallpaper Design

Port of `mock-nro-desktop-gui/src/wallpaper.rs` "Cold Plasma Cascade" into a
`pu::ui::elm::Element` subclass.

Render pipeline (identical pass order to Rust):
1. Background fill: deep-space `#0A0A14`
2. 6 plasma blooms: radial additive glow, xorshift32 deterministic positions
3. 18 data streams: diagonal cold-palette streaks, triangle alpha envelope
4. 80 single-pixel stars: alpha-over blend, 4-colour cold palette
5. Grid overlay: 64px cell, alpha 22/256
6. Radial corner vignette: 60% darken at corners

PRNG: xorshift32 seeded at `0x514F535F` ("QOS_"), bit-identical to Rust source.
Rendering: SDL2 `RenderRectangleFill` 1x1 per pixel, CPU-only.
Telemetry: `EVENT UX_WALLPAPER_FRAME fps=60 blooms=6 streams=18 stars=80` every 60 frames via `fprintf(stderr, ...)`.
Compile flag: `PLASMA_WALLPAPER_ENABLED` (default 1); set to 0 to revert to Background.png.

## Diff Summary vs v0.3.0

```
projects/uMenu/include/ul/menu/ui/ui_MainMenuLayout.hpp
  + #include <ul/menu/ui/ui_PlasmaWallpaperElement.hpp>
  + PlasmaWallpaperElement::Ref plasma_wallpaper;   // private

projects/uMenu/source/ul/menu/ui/ui_MainMenuLayout.cpp
  + plasma_wallpaper(nullptr),                      // init-list
  + #if PLASMA_WALLPAPER_ENABLED ... Add() at layer 0 before top_menu_default_bg

projects/uMenu/source/ul/menu/ui/ui_BackgroundScreenCapture.cpp
  + #include <ul/menu/ui/ui_PlasmaWallpaperElement.hpp>
  + #if PLASMA_WALLPAPER_ENABLED guard in InitializeScreenCaptures()

NEW: projects/uMenu/include/ul/menu/ui/ui_PlasmaWallpaperElement.hpp
NEW: projects/uMenu/source/ul/menu/ui/ui_PlasmaWallpaperElement.cpp
```
