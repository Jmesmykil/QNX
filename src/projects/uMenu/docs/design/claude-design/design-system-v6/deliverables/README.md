# Q OS — Visual Overhaul Handback

Repo-ready output for the visual overhaul brief (`uploads/00–03`). Maps 1:1 onto
the repo (`src/projects/uMenu/`). Engineers drop these in via the documented load
paths; two ⚙️ wire-ups are flagged in the specs.

## Structure

```
deliverables/
├─ design-system/
│  ├─ palettes.json     # 10 retuned palettes, 18-role QdPalette schema + change notes
│  └─ tokens.json       # type / spacing / radius / elevation / motion tokens
├─ selection/SELECTION-SPEC.md     # rounded, theme-consistent focus/selected/hover ring
├─ cursor/CURSOR-SPEC.md           # theme-aware liquid-glass bubble + right-click state
├─ glyphs/
│  ├─ GLYPH-SPEC.md                # window buttons · Q · star · badges
│  └─ masters/*.svg                # editable glyph masters
├─ icons/ICON-PACK-RECIPE.md       # repeatable pipeline for new icon packs
├─ wallpapers/
│  ├─ WALLPAPER-PACK-RECIPE.md
│  └─ q-os-{0..9}-<slug>/Background.png   # 10 art-directed 1280×720 wallpapers
└─ font/FONT-SPEC.md               # OFL typeface recommendation (Space Grotesk) + integration
```

## Where each piece lands

| Deliverable | Repo destination |
|---|---|
| `palettes.json` (per theme) | `romfs/themes/q-os-{idx}-*.ultheme :: ui/QdPalette.json` **+** `qd_Theme.hpp` factories ⚙️ |
| `tokens.json`, specs | `docs/design/claude-design/` (engineering reference) |
| Selection spec | code: `qd_Launchpad.cpp` (PaintCell, folder-tab, search-bar — drop `#0080AA`), `qd_Window.cpp` |
| Cursor spec | code: `qd_Cursor.cpp` (replace `BRAND_CYAN_*` with `cursor_*`, add right-click texture) |
| Glyph masters | code-drawn from proportions; `Logo.png` 256² → `romfs/Logo.png`; `theme/Icon.png` 256² per bundle |
| Wallpapers (10) | `romfs/themes/q-os-{idx}-*.ultheme :: ui/Background.png` ⚙️ (wire `QdImageWallpaperElement` into boot) |
| Font | `Space-Grotesk.ttf` → `ui/Font.ttf` in all 10 bundles (or `romfs/default/`) + `font/LICENSE` |

## Two ⚙️ engineering prerequisites (not design work)
1. Mirror the retuned palettes into the C++ factories (`qd_Theme.hpp`) — SSOT is `palettes.json`.
2. Wire `QdImageWallpaperElement` into the boot layouts so shipped `Background.png` images render.

## Interactive specimens (Design System tab → "Overhaul" group)
- **Selection outline** — focused/selected/hover ×10 themes + list-row reconciliation.
- **Theme-aware cursor** — pointer + right-click ×10, on split light/dark stages.
- **Glyph family** — window buttons, Q, star, badges.
- **Wallpapers** — all 10 thumbnails.
- **Overhaul showcase** — full mini-desktop with a live 10-theme switcher.
- Plus **Icon-pack system** (Brand) and **Loading spinner** (Brand).

## What's authored vs. sourced
- **Authored here:** palettes, tokens, all specs, glyph SVG masters, 10 wallpapers, the icon vocabulary (see Icon-pack system card).
- **You source:** the `.ttf` binary (OFL — Space Grotesk, per FONT-SPEC) and the 192px icon-pack PNG exports (per ICON-PACK-RECIPE + the icon system card as the visual source of truth). Icon raster export of all 194 PNGs is the remaining production step.
