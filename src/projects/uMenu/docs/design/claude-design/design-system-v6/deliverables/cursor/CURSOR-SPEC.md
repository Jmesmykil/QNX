# Q OS — Cursor Spec  (overhaul priority 🔴2)

**Chosen path: spec-driven procedural** (no PNG). The engine already builds the
cursor as a 44×44 ABGR8888 texture in `qd_Cursor.cpp::BuildCursorTexture`. This
spec swaps the hardcoded `BRAND_CYAN_*` constants (cyan `#00E5FF`, the
theme-ignoring leak) for the per-theme `cursor_*` tokens that already exist in
all 10 palettes, and adds the missing right-click state.

---

## 1. Silhouette — "Liquid Glass Bubble v3" (keep + retheme)

- **Texture 44×44, hotspot at the CENTER (22,22)** — unchanged. The click point
  is the middle of the bubble (a ring cursor, not a tip). Blit offset stays
  `(cursor_x − 22, cursor_y − 22)`.
- **Body:** filled circle, radius 18, color **`cursor_fill`** at **α ≈ 110/255
  (~43%)** — the see-through "glass."
- **Outline:** 2-pass anti-aliased ring in **`cursor_outline`** —
  `radius+1` @ α 80 (soft halo) then `radius` @ α 255 (crisp edge).
- **Contrast-survival core (KEEP — this is why it's findable on any bg):** a
  small **`cursor_outline`** disc (Ø 5) with a **`cursor_fill`** dot (Ø 2) on top
  — a light tip ringed by dark, legible on light and dark surfaces alike.

The silhouette is **identical across all 10 themes** (cohesion). Only the three
colors change.

## 2. Token mapping (the fix)

| Texture part | Old (hardcoded) | New (per-theme) |
|---|---|---|
| Body fill | brand cyan `#00E5FF` | `g_QdTheme.cursor_fill` |
| Outline / halo | brand cyan | `g_QdTheme.cursor_outline` |
| Right-click accent | — (didn't exist) | `g_QdTheme.cursor_right_click` |
| **Centre core dot (secondary)** | white tip | **`g_QdTheme.accent`** — the bubble's secondary color, a theme-accent core inside the glass so every cursor carries the brand accent (still ringed by `cursor_outline` for contrast survival) |

So Retro gets its green cursor (`#6AFF82`), Dark its warm `#FFE6D8`, Pixel pure
white, etc. — instead of cyan everywhere.

## 3. Right-click state (NEW — token existed, was unused)

When a right-click / ZL context action is armed or open:
- Recolor the **outline ring** to **`cursor_right_click`** (crisp edge pass only;
  keep the soft halo for findability).
- Add a small **`cursor_right_click`** badge disc (Ø 6) at the lower-right of the
  bubble (≈ +7,+7 from center) — a clear "context mode" marker.
- Body fill stays `cursor_fill` so the bubble silhouette is constant; only the
  ring + badge signal the mode.

Build this as a **second cached 44×44 texture** (`g_cursor_rc_tex`) alongside the
default so the swap is a pointer change, not a per-frame redraw.

## 4. Optional: busy variant

If cheap, overlay the **time-driven ASCII spinner** (see brand/loading spec) just
above-right of the bubble while a heavy op runs — driven by the monotonic clock
so it spins even mid-load. Optional; not required for v1 of the overhaul.

## 5. Rebuild triggers

`cursor_fill/outline/right_click` are read at texture-build time. Rebuild both
cached textures (default + right-click) whenever the palette changes
(`SetActivePalettePack` / `LoadThemeFromCache`) — same hook that re-bakes the
wallpaper (`g_wallpaper_dirty`-style flag, e.g. `g_cursor_dirty`).

## 6. Integration

- File: `source/ul/menu/qdesktop/qd_Cursor.cpp` (+ `.hpp`).
  1. Delete `BRAND_CYAN_R/G/B`; read `g_QdTheme.cursor_fill/outline` in
     `BuildCursorTexture`.
  2. Add `BuildCursorRightClickTexture()` using `cursor_right_click`.
  3. Select the texture by interaction mode in the blit.
  4. Add `g_cursor_dirty` + rebuild on palette change.
- No asset ships. Footprint + hotspot math unchanged (44×44, center 22,22), so
  nothing downstream of the blit needs touching.
- **No hardware mouse** — the cursor follows touch in 1920×1080 space; it's a
  floating pointer, not a corner-anchored arrow. Center hotspot is correct for a
  ring cursor — **kept**.
