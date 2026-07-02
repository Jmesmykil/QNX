# Q OS — Wallpaper-Pack Recipe

Replaces the runtime-procedural wallpapers (C++ pixel math in `qd_Wallpaper.cpp`
/ `qd_WallpaperPacks.cpp`) with art-directed images that drop into the existing
(built-but-unwired) image path.

## Format & size (verified)
- **Author/export at 1280×720** (the bake resolution; stretched to 1920×1080 at
  blit — a GPU-VRAM decision). PNG preferred (JPEG/WebP/BMP also load).
- Full-bleed **16:9**. Keep critical/branded content inside the central **~92%**
  (TV overscan safety).
- Keep file size modest (one full-screen texture ~3.5 MB budget; source is
  scaled down at load). Optional 1920×1080 editable master in `wallpapers/masters/`.

## Drop path
`ui/Background.png` **inside each `.ultheme`**. When present in the active theme
cache (`sdmc:/ulaunch/cache/active/ui/Background.png`), the image element renders
it.

> ⚙️ **Engineering prerequisite:** the image path
> (`QdImageWallpaperElement`) is built but **not yet wired into boot** — the
> active layouts always instantiate the *procedural* `QdWallpaperElement`
> (`ui_MainMenuLayout.cpp:738`, `ui_StartupMenuLayout.cpp:69`,
> `qd_LockscreenLayout.cpp:331`). Shipping image wallpapers needs the one-time
> "Phase B" swap: instantiate `QdImageWallpaperElement` when `Background.png`
> exists in the active theme cache. Assets are authored to this contract now.

## Art direction — keep the concept, modernize the execution
One wallpaper per theme, in that theme's palette, expressing its identity. The
current procedural intents are good briefs — clean them up, don't replace them:

| Theme | Concept |
|---|---|
| Q OS | Cold Plasma Cascade — deep navy, soft cyan plasma, magenta/lavender point-lights |
| Neon | 4 horizontal neon bands, magenta + electric cyan + lime, black base |
| Minimal | quiet warm — stone gradient, one soft dusty-amber arc |
| Retro | amber band + CRT scanlines over navy, phosphor-green glow |
| Cards | layered translucent cards on blue slate, warm amber edges |
| Pastel | four soft pastel circles, powder pink / mint / lavender on slate |
| Dark | embers in a void — pure black, drifting ember-orange sparks |
| Gradient | 3-stop vertical indigo → violet → cyan, smooth |
| Blueprint | drafting grid + dimension lines, cyan on blueprint blue |
| Pixel | 32×32 colorful checkerboard, NES primaries on dark navy |

## Chrome safety
The top bar (48px) and dock (148px) and bright `focus_ring`/`accent` UI sit on
top. **Keep wallpaper contrast moderate** behind those zones — no busy detail
under the dock band or top bar; let the center carry the art.

## What a "wallpaper pack" is (for the future)
A **named set of 1280×720 images** shipped together (e.g. a "Seasons" pack, an
"Abstract" pack) that either (a) map 1:1 to the 10 themes, or (b) stand alone as
a user-selectable wallpaper independent of palette. Naming:
`wallpapers/<pack-name>/q-os-{idx}.png` (theme-mapped) or
`wallpapers/<pack-name>/<slug>.png` (standalone). Each pack ships its own
`Background.png` per target bundle (or a future picker reads a pack folder).

## Export checklist
- [ ] 1280×720, full-bleed 16:9.
- [ ] Central 92% safe; no critical content in outer 4%.
- [ ] Moderate contrast under top bar + dock zones.
- [ ] In the theme's retuned palette (palettes.json).
- [ ] Modest file size; optional 1080p master saved.
- [ ] Named `Background.png` → that theme's `.ultheme`.
