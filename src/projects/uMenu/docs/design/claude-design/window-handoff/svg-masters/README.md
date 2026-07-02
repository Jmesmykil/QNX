# Q OS — Window Frame (SVG masters)

Scalable vector masters of the full window chrome — titlebar, glass body, four
colored corner controls (close / maximize / minimize / resize), and the bottom
hint bar — one per theme, in the retuned palettes.

```
window/q-os-{0..9}-<slug>.svg     # 10 themed masters, 640×400 reference box
```

## Why SVG here
- **Scalable authoring source** — exports crisp at any size; single source of
  truth for the chrome look, instead of a fixed bitmap.
- **Theme-tinted** — colors are baked per theme (surface, accent, the three
  button hues, text), so no runtime tinting needed if shipped as raster.

## Runtime note (important)
The on-device engine (Plutonium/SDL2) does **not** render SVG, and the window is
**resizable**. So use these masters one of two ways:
1. **Code-draw (current path):** the engine draws the frame procedurally; these
   SVGs are the visual spec/reference for proportions, radii, colors.
2. **9-slice asset:** rasterize to PNG and slice. The SVG header comments mark
   the fixed corner size and the stretchable X/Y bands. **Titlebar (40px) and
   hint bar (34px) heights stay fixed**; only the middle stretches — this keeps
   the corner discs and bars undistorted at any window size.

## Export
```bash
rsvg-convert -w 1280 -h 800 q-os-0-q-os.svg -o q-os-0-q-os.png   # or any size
```

## Geometry (matches tokens.json + spacing-chrome card)
- Body radius 14, 1.5px accent border, surface fill @ 96%.
- Corner discs Ø24, 14px margin; glyphs from `../glyphs/masters/`.
- Titlebar 40 (accent-tint 10% over surface), hint bar 34 (#0A0A14 @ 50%).
