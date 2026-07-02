# Q OS — Asset Formats (SVG-first)

The whole design system is now **vector-first**: every asset that *can* be SVG
is authored and shipped as an SVG master. PNG is generated from it only where the
device runtime requires raster.

## Why SVG masters
- **One scale, no conflicts.** SVG has no native resolution — the 720p→1080p
  upscale blur (and any future 4K-dock case) disappears. Rasterize to the exact
  target size at integration.
- **Tiny.** Wallpaper/splash SVGs are ~1–4 KB vs ~50–90 KB PNG (10–40× smaller).
  The full vector set is a few hundred KB instead of multiple MB.
- **Editable + diffable.** Plain text; tweak a color/stroke in one line, version
  it in git, re-export. No binary churn.
- **Single source of truth.** No PNG/SVG drift — the PNG is a build artifact.

## What's SVG now
| Asset | Location | Notes |
|---|---|---|
| Icon packs (170) | `icons/packs/q-os-*/` | 17 × 10 themes, treatment+accent baked |
| Base icons (24) | `icons/base/*.svg` | + sample PNGs (export proof) |
| Glyphs (7) | `glyphs/masters/*.svg` | window buttons, Q, star |
| Logo | `glyphs/masters/logo.svg` | brand Q magnifier, → `Logo.png` 256 |
| Window frames (10) | `window/q-os-*.svg` | full chrome, 9-slice guides |
| Wallpapers (10) | `wallpapers/q-os-*/Background.svg` | render native 1080p |
| Splashes (10) | `wallpapers/splash/*.svg` | loading screens |
| Cursor (2) | `cursor/*.svg` | default + right-click |
| Selection frame | `selection/selection-frame.svg` | 9-slice halo ring |

## What MUST stay raster (device runtime)
The Switch UI engine (Plutonium/SDL2) has **no SVG rasterizer** — at runtime it
draws procedural shapes or **pre-rasterized PNG textures**. So:
- **Ship PNG to the device**, generated from these SVG masters at the **native
  panel size** (1920×1080 wallpapers/splashes; 192² icons; 256² Logo).
- **Code-drawn elements** (window chrome, selection ring, cursor) don't even need
  a file — the SVG is the visual spec the engine draws from.
- **Fonts** stay `.ttf` (a different binary format entirely — see `font/`).
- The **GitHub-imported brand photos** (`assets/branding/*`, `qos-rebrand/*`) are
  photographic/pre-existing — left as-is; not vectorizable.

## Build: SVG → PNG (one line per target)
```bash
# wallpapers / splashes → native 1080p
rsvg-convert -w 1920 -h 1080 Background.svg -o Background.png
# icons → 192
for f in icons/packs/*/*.svg; do rsvg-convert -w 192 -h 192 "$f" -o "${f%.svg}.png"; done
# logo → 256
rsvg-convert -w 256 -h 256 glyphs/masters/logo.svg -o Logo.png
```
(`rsvg-convert` from librsvg; Inkscape or `npx svgexport` work too. All preserve
transparency and the `feGaussianBlur` glow/blur effects.)

## Net effect
- Repo footprint for generated art: **MB → a few hundred KB.**
- Zero scale/upscale artifacts.
- Edit once (text), export to any resolution the hardware needs.
