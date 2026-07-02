// qd_FolderTheme.hpp — Folder tile theme-pack registry for Q OS desktop.
//
// Provides 10 named theme packs, each of which can draw a themed logo for
// any of the 5 auto-folder categories (NxGames, Homebrew, System, Payloads,
// Builtin) plus the "custom" category used by user-created folders.
//
// Pack 0-3: procedural (SDL2 primitives only, no PNG assets).
// Pack 4-9: romfs PNG paths under romfs/folder-themes/<pack-name>/<cat>.png;
//           fall back to the pack-0 procedural draw if the PNG is missing.
//
// Persistence: the current pack index is saved/loaded at
//   sdmc:/ulaunch/qos-folder-theme.toml
// (one line: "pack=N")
//
// Thread-safety: all callers are on the main UI thread. No mutex needed.
#pragma once

#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <SDL2/SDL.h>
#include <cstddef>

namespace ul::menu::qdesktop {

// ── Category selector ─────────────────────────────────────────────────────────
// Matches AutoFolderIdx ordering (for auto-folders) plus a Custom slot.
enum class FolderThemeCat : u8 {
    NxGames  = 0,
    Homebrew = 1,
    System   = 2,
    Payloads = 3,
    Builtin  = 4,
    Custom   = 5,
    Count    = 6,
};

// ── Pack count ────────────────────────────────────────────────────────────────
static constexpr size_t kFolderThemePackCount = 10;

// ── Public API ────────────────────────────────────────────────────────────────

/// Return the display name for pack index (0–9).
const char *FolderThemePackName(size_t pack_idx);

/// Draw the themed icon for `cat` in pack `pack_idx` into the SDL renderer.
/// The icon is drawn inside the bounding box [x, y, w, h].
/// If pack_idx is out of range, pack 0 (procedural) is used.
/// If a PNG-based pack's asset is missing, falls back to pack 0 drawing.
void DrawFolderThemeIcon(SDL_Renderer *r, size_t pack_idx, FolderThemeCat cat,
                         s32 x, s32 y, s32 w, s32 h);

/// Load the current pack index from sdmc:/ulaunch/qos-folder-theme.toml.
/// Returns 0 if the file is absent or corrupt.
size_t LoadFolderThemePack();

/// Save the current pack index to sdmc:/ulaunch/qos-folder-theme.toml.
/// Uses atomic tmp+rename pattern. Silently no-ops on write error.
void SaveFolderThemePack(size_t pack_idx);

/// Invalidate the PNG-pack texture cache (W5-TRANSITIONS #2).
/// Destroys all non-sentinel SDL_Texture* slots and resets them to nullptr
/// so the next DrawFolderThemeIcon call re-loads from the new pack.
/// Call after any theme change that may affect folder-tile appearance.
void InvalidatePngPackCache();

// v3.1.1 (BUG/UX 2026-05-19): Wallpaper pack persistence — decoupled from
// folder-theme.  Previously SetActiveThemePack(idx) wrote both palette and
// wallpaper to qos-folder-theme.toml as a unified switch, so the empty-desktop
// "Change Wallpaper" and the desktop-folder-tile "Choose Folder Theme"
// effectively did the same thing.  Now wallpaper lives in its own file so
// each menu can mutate its own concern.
//
// File: sdmc:/ulaunch/qos-wallpaper.toml (one line: "pack=N").
// Fallback: if the file is absent, LoadActiveWallpaperPack() returns
// LoadFolderThemePack() so existing on-disk state stays coherent.
size_t LoadActiveWallpaperPack();
void   SaveActiveWallpaperPack(size_t pack_idx);

} // namespace ul::menu::qdesktop
