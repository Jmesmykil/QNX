# Q OS — Icon masters (exported, no drift)

These are the **exported SVG masters** for the per-theme icon packs and the base
set — written straight from the same path data + treatment + accent the
**Icon-pack system** card renders. They do **not** re-derive the art, so there's
zero drift from what you've approved.

## What's here
```
icons/
├─ base/                       # 24 universal/default icons (Glass treatment, cyan)
│  ├─ *.svg  (master)  + *.png (192×192, transparent)   ← base PNGs included
├─ packs/q-os-{0..9}-<slug>/   # 17 named icons per theme, treatment+accent baked
│  └─ *.svg  (master)          # 170 SVG masters
└─ ICON-PACK-RECIPE.md
```
Each SVG is `viewBox="0 0 24 24"`, sized `192×192`, stroke = the theme accent
(or the gradient for Gradient), stroke-width per treatment (fill 2.6 / glow 2.1 /
line 1.7 / pixel 2.5 / gradient 2.2). File names match the repo's exact
`EntryIcon` names — drop straight into `ui/Main/EntryIcon/`.

## SVG → 192px PNG (the only remaining step)
The base set is already rasterized. For the 10 packs, batch-convert with any of:

```bash
# rsvg-convert (brew install librsvg) — one-liner over a pack:
for f in packs/q-os-1-neon/*.svg; do rsvg-convert -w 192 -h 192 "$f" -o "${f%.svg}.png"; done

# or Inkscape:  inkscape --export-type=png -w 192 -h 192 file.svg
# or npx:       npx svgexport file.svg file.png 192:192
```
Transparent background is preserved (no fill in the masters).

> Neon's **glow** and the soft elevation are render-time effects (the card adds a
> drop-shadow); the masters are the clean stroked art. Add glow at composite time
> if you want it baked into the PNG, or let the runtime do it.

## Treatment + accent reference
| Pack | Treatment | Accent |
|---|---|---|
| q-os-0 Glass | fill (bold stroke) | #7DD3FC |
| q-os-1 Neon | glow | #FF2AD0 |
| q-os-2 Minimal | line | #D4C8B4 |
| q-os-3 Retro | pixel | #FFA83A |
| q-os-4 Cards | fill | #FF9A3C |
| q-os-5 Pastel | fill | #FBC6E4 |
| q-os-6 Dark | fill | #FF6040 |
| q-os-7 Gradient | gradient stroke | #A070FF→#7AE0FF |
| q-os-8 Blueprint | line | #7AE0FF |
| q-os-9 Pixel | pixel | #FFCC00 |

The folder/category + `HotCornerQ` glyphs differ in **shape** per theme (vault in
Glass ≠ vault in Pixel); dock/status glyphs keep a constant metaphor with the
theme treatment. This matches the Icon-pack system card exactly.
