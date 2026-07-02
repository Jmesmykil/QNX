// qd_LazyLoader.hpp — W11-FOOTPRINT v3.3 / W12-LAZYTHEME v3.4: lazy utilities.
//
// Part 3 (v3.3): WallpaperResolution() — query appletGetOperationMode() once
// per bake cycle and return appropriate texture dimensions:
//
//   Docked   (AppletOperationMode_Console):   1280×720  (~3.5 MiB RGBA)
//   Handheld (AppletOperationMode_Handheld):   960×540  (~2.0 MiB RGBA, -1.5 MiB)
//
// Part 4 (v3.4 W12-LAZYTHEME): two-phase theme load helpers.
//
//   Phase 1 — metadata only (~200 bytes / theme, 2 KiB total for 10 themes):
//     LoadAllThemeMetadata()  — populate g_theme_meta[10] from in-binary tables.
//     Runs eagerly at boot. No zip I/O, no SDL textures, no PNG decode.
//     Sufficient to populate the Settings → Themes picker (names + indices).
//
//   Phase 2 — full assets (~40-60 MiB when all 10 pre-loaded):
//     LoadThemeAssets(idx)    — palette, wallpaper dirty-flag, PNG cache warm.
//     UnloadThemeAssets(idx)  — wallpaper dirty-flag + PNG cache invalidation.
//     Both called only when the active theme pack changes. The previous theme's
//     assets are freed before the new theme's assets are loaded.
//
// Combined, only ONE theme's assets reside in memory at a time after a switch.
// At boot only the active (persisted) pack is warmed — others remain metadata-only.
#pragma once
#include <switch.h>
#include <cstddef>

namespace ul::menu::qdesktop {

namespace QdLazyLoader {

// ── Part 3: wallpaper texture resolution ─────────────────────────────────────

// W11-FOOTPRINT Part 3: wallpaper texture resolution selected by operation mode.
struct WpRes {
    u32  w;         // texture width  (1280 docked / 960 handheld)
    u32  h;         // texture height  ( 720 docked / 540 handheld)
    bool handheld;  // true  = AppletOperationMode_Handheld
};

// Query appletGetOperationMode() and return the appropriate texture resolution.
// Falls back to docked dimensions if the query returns an unrecognised value.
WpRes WallpaperResolution();

// ── Part 4: two-phase theme load helpers ─────────────────────────────────────

// Lightweight per-theme metadata (~200 bytes). Populated by LoadAllThemeMetadata()
// at boot from the in-binary tables. No heap allocation — no zip, no SDL.
struct ThemeMetadata {
    const char *name;    // pointer into kThemeNames[] rodata — do not free
    size_t      index;   // pack index 0..9
    bool        loaded;  // true once metadata has been populated
};

// Per-theme metadata array. Index 0..kThemePackCount-1.
// extern so qd_Theme.cpp can write it and callers can read it.
// All 10 entries are populated by LoadAllThemeMetadata() before first use.
extern ThemeMetadata g_theme_meta[10];

// Populate g_theme_meta[0..9] from in-binary kThemeNames/kThemePackCount.
// No I/O — runs in < 1 µs. Safe to call before SDL is initialised.
// Called once in MainLoop() before the renderer is created.
void LoadAllThemeMetadata();

// Load the full assets for pack `idx` into the active-theme globals:
//   - Calls SetActivePalettePack(idx)  → updates g_QdTheme
//   - Raises g_wallpaper_dirty          → QdWallpaperElement re-bakes on next render
//   - Calls InvalidatePngPackCache()    → folder PNGs re-load from new pack on next draw
// Ledger: no new tracking here — the individual subsystems (QdWallpaperElement,
// DrawPngPack) each call UL_LEDGER_TRACK when they allocate their textures.
// idx must be 0..kThemePackCount-1. Out-of-range silently no-ops.
void LoadThemeAssets(size_t idx);

// Free the assets for pack `idx`:
//   - Raises g_wallpaper_dirty so QdWallpaperElement drops its cached texture
//     on the next render (UL_LEDGER_UNTRACK happens inside OnRender's dirty path)
//   - Calls InvalidatePngPackCache() to destroy + untrack PNG folder textures
// Note: the palette (g_QdTheme) is NOT reset because it is pure data (~200 bytes)
// — resetting it to Glass would cause a one-frame flicker before LoadThemeAssets
// sets the correct palette. Callers should call LoadThemeAssets(new_idx)
// immediately after UnloadThemeAssets(old_idx).
// idx must be 0..kThemePackCount-1. Out-of-range silently no-ops.
void UnloadThemeAssets(size_t idx);

} // namespace QdLazyLoader

} // namespace ul::menu::qdesktop
