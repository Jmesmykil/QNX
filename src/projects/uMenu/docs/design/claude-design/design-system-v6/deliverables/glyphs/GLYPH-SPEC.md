# Q OS — Glyph Spec  (overhaul 🟡)

One coherent monochrome glyph family — consistent stroke weight, corner radius,
and optical metrics — replacing today's grab-bag (primitive SDL shapes for
window buttons, a 5-rect "Q", font `★`, font chars for status). All glyphs are
**monochrome + tintable**; the engine colors them per state/theme. Most are
code-drawn → this is a spec + SVG masters (`glyphs/masters/*.svg`), not shipped
PNGs (except `Logo.png` + theme `Icon.png`).

## Family rules
- **Grid:** 24×24 design units, 2px safe margin → 20px live area.
- **Stroke:** 2.2 units, round joins + caps (pixel-theme variant: 2.5, miter,
  crisp). Optical, not geometric — overshoot curves slightly.
- **Corner radius:** `radius/sm` family feel (soft, ~2u) on rectangular forms.
- **Two-value max:** silhouette + (optional) one accent detail. Reads at dock
  size (~84px) and TV distance.

## Window-button glyphs (live ~18px inside the 32px disc, dark `#101018` on the bright fill)
| Glyph | Form |
|---|---|
| **Close** | X — two strokes, equal weight, 12u long, centered |
| **Maximize** | square — 14u, 2px stroke, `radius/sm` corners |
| **Minimize** | dash — single 12u horizontal stroke, centered |
| **Restore** | two overlapped squares (10u), offset +3,−3 — the maximize alternate |
| **Resize** | diagonal double-arrow ⤡ (BR) — `M7 7 L17 17` + arrowheads both ends (see masters) |

Restore == maximize as a *state pair* (one button, two glyphs) — matches the
`button_restore == button_maximize` color rule.

## Brand "Q" mark
- Open circular ring (stroke ~3u) + a short diagonal tail bottom-right (the
  q-ring master). Used at **36px** (hot corner), **~180px** (transition splash),
  **256px** (`Logo.png`). Color = `accent`.
- Per-theme hot-corner *emblem* is a different mark per theme (Q only for Glass):
  ⚡ Neon, ⋮ Minimal, ◗ Retro wedge, ♠ Cards, ♥ Pastel, 🔥 Dark, ▲ Gradient
  prism, ✛ Blueprint compass, ✦ Pixel star — see Icon-pack system card. Shapes
  constant per theme; the Q is the brand constant.

## Favorites star
Replace the font `★` with a 5-point star master (even optical weight), filled,
top-right of favorited icons, in `accent`. Optional raster `star.png` ~32px.

## Badges / overlays (`OverIcon/`, transparent PNGs over the 168px cell)
| Badge | Form | Tint |
|---|---|---|
| Count | pill, mono tabular number | `accent` fill, dark text |
| NeedsUpdate | down-arrow into tray | `button_minimize` |
| Suspended | two vertical bars (pause) | `text_secondary` |
| Corrupted | circle + slash | `button_close` |
| Selected (halo) | the new rounded selection ring (see SELECTION-SPEC) | `focus_ring` |

Pure system status (battery, signal) stays functional/separate — but Wi-Fi +
Bluetooth glyphs are in the family (see Icon-pack system).

## Integration
- Window-button + Q glyphs are **code-drawn** (`qd_Window.cpp`, `qd_Theme.cpp`
  `DrawThemeTransitionFrame`, hot-corner overlay) → implement from the masters'
  proportions; keep them procedural, just to-spec.
- Raster deliverables: `Logo.png` (256×256) → `romfs/Logo.png`; theme
  `Icon.png` (256×256) → each `.ultheme :: theme/Icon.png` (replaces the 1×1
  placeholders); optional `star.png`, `OverIcon/*` overlays.
- Masters: `glyphs/masters/*.svg` (editable), not shipped to device.
