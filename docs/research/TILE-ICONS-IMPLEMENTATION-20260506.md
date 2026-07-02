# Nintendo Apps Tile Icons + Builtin Nintendo Dock Icon — 2026-05-06

uMenu UX gap fix. Nintendo Apps grid was solid colored squares with text;
Builtin Nintendo dock entry was the generic `DefaultApplication.png`.

## NintendoApp struct (qd_NintendoApps.hpp)

Added a fourth field after `launch`: `const char *icon_path` — theme-relative
path with no extension, resolved at first paint via `TryFindLoadImage`.
`nullptr` means "render solid-color tile only" (legacy path).

Sample row:

```cpp
{ "Album", { 0x22u, 0x8Bu, 0xE4u, 0xFFu }, LaunchAlbum, "ui/Main/EntryIcon/Album" },
```

Five entries reference shipped PNGs (Album, Controllers, MiiEdit,
WebBrowser, Settings); three pass `nullptr` because no shipped EntryIcon
exists yet (Profile, Keyboard, Error Info — they keep the original solid
fill so they stay readable).

## RenderTile diff highlights (qd_NintendoAppsLayout.cpp)

- **Lazy load + cache.** First paint of a tile with a non-null `icon_path`
  calls `ul::menu::ui::TryFindLoadImage(app.icon_path)` and stores the
  result keyed by the literal `icon_path` pointer. Failed loads cache
  `nullptr` so they never retry.
- **Background dim when icon present.** Tile color alpha drops from
  `0xC0/0xF0` (idle/hover) to `0x50/0x80` so the icon isn't fighting a
  saturated panel; legacy alpha kept for icon-less tiles.
- **Aspect-preserving fit.** Icon box = 70% × tile_w by 80% of
  (tile_h − 28 px label strip). Larger of the two dimensions caps; whole
  thing centred above a 28-px label strip.
- **Label moves.** With icon: label centred in bottom 28-px strip. Without
  icon: legacy centred-on-tile render preserved.
- **Graceful failure.** Null texture pointer (load fail or
  `icon_path == nullptr`) skips the icon block entirely; render falls
  through to the original solid-color tile.

## Icon-cache member + dtor cleanup

Header (`qd_NintendoAppsLayout.hpp`):

```cpp
mutable std::map<const char *, SDL_Texture *> icon_tex_cache_;
```

`mutable` is required because `RenderTile` is `const`. Pointer-keyed
because `icon_path` literals are program-lifetime stable. Includes `<map>`.

Dtor frees every non-null value via `pu::ui::render::DeleteTexture` (LRU-
safe API used elsewhere) then clears the map. nullptr sentinel entries are
skipped.

## Builtin Nintendo dock fix

Location: `qd_DesktopIcons.cpp` → `IconKind::Builtin` branch in the
default-icon fallback section (around line 2864). Added a special-case
above the standard `Dock<Name>.png` lookup:

```cpp
if (std::strcmp(entry.name, "Nintendo") == 0) {
    icon_tex_[entry_idx] = pu::ui::render::LoadImageFromFile("romfs:/Logo.png");
    if (icon_tex_[entry_idx] != nullptr) { ... break; }
    // fall through on failure
}
```

`romfs:/Logo.png` is bundled by `src/Makefile` line 63
(`cp assets/Logo.png projects/uMenu/romfs/Logo.png`) and loaded by the
same POSIX `IMG_Load` path used by the existing 2a-romfs branch. On
success the slot is marked `CellRenderState::SpecialPng` so 2a/2c skip on
later frames; on failure the original `Dock<Name>` → `DefaultApplication`
chain runs, preserving prior behavior.

## Build

`cd src && make umenu` → green.
`uMenu.nso` md5: `c6466adb86c9dd8dc04dd559e38681db` (7,097,166 bytes).
Not deployed.
