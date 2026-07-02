# Q OS — Selection Outline Spec  (overhaul priority 🔴1)

Replaces the hard-cornered 2×1px double rectangle (`qd_Launchpad.cpp PaintCell`,
`qd_Window.cpp`) with ONE unified, rounded, theme-consistent selection language
for the icon grid, folder tabs, dock, windows, and (reconciled) list rows.

The ring color is already per-theme (`focus_ring`). **You ship a geometry spec,
not 10 assets** — the engine draws it and tints with `g_QdTheme.focus_ring`.

---

## 1. One language, three states

All states use the **same color** (`focus_ring`), the **same corner radius** as
the element they wrap (`radius/sm` = 8px @1080 for icons/tiles), and a ring that
sits **outside** the element. Only weight + glow + fill differ:

| State | Ring | Glow | Element fill | Badge |
|---|---|---|---|---|
| **Hover** (pointer over, not chosen) | 2px, `focus_ring` @ 55% α | none | unchanged | none |
| **Focused** (D-pad/pointer target) | 3px, `focus_ring` @ 100% | `focus_ring` @ 25% α, ~8px soft halo | brighten +10% luminance | none |
| **Selected** (toggled / multi-select) | 3px, `focus_ring` @ 100% | same halo | brighten +10% | **accent check** badge, top-right |

- **Focused vs Selected are distinct but related:** focused is transient (where
  the cursor is); selected persists and adds a filled **accent** disc (Ø ~22px
  @1080) with a dark check glyph in the corner — unmistakable from focus alone.
- Hover is a quieter focused (thinner, no glow) so mouse-mode reads as one family.

## 2. Geometry (icon grid, the canonical site)

Icon art is 168×168 in a 180×180 cell (`LP_ICON_W/H`, `LP_CELL_W/H`).

```
radius        = 8 px                       (radius/sm — matches tile/window corners)
ring_thickness= 3 px @1080                 (≥3px so it reads at 720p / TV)
ring_inset    = -3 px (outside the art)    → rounded ring at (icon_x-3, icon_y-3, 174, 174)
glow          = 2 concentric rounded rects outside the ring,
                focus_ring @ 0x40 then 0x20 (cheap 2-pass "halo", no real blur)
fill_brighten = +10% luminance on the focused/selected tile bg (keep today's idea,
                tuned from the old +40/channel which over-blew dark themes)
```

Draw order per focused cell: fill-brighten → glow (outer→inner) → ring → badge.

## 3. Reconcile the two parallel systems

- **Grid/tiles/windows** = rounded ring (above).
- **List rows** (Task manager, Cheats, Save editor, Home mini-menu) = the SAME
  language as a *filled row*: `focus_ring` @ 14% α fill + a 3px `focus_ring`
  left-edge bar + `radius/sm` corners. Same color, radius, thickness — one idea,
  two forms (outline for free-floating tiles, filled-bar for full-width rows).
  This kills the outline-vs-fill split.
- **Legacy `Selected.png` halo (416×416)** used by the non-qdesktop `ui/` menu:
  redraw it to match the new ring exactly (a transparent PNG of the rounded
  ring + halo, tintable/grayscale) OR retire that menu path. Don't keep two
  looks. See deliverables (`selection/Selected.png`, 416×416).
- **Remove the hardcoded `#0080AA`** search-bar inner ring — use `focus_ring`.

## 4. Per-theme behaviour

- Color is `focus_ring` per theme — palettes were retuned so `focus_ring` clears
  a **consistent contrast target** (≥3:1 vs the surface it rings) in all 10
  themes, so the ring reads with the **same prominence** everywhere (fixes the
  "Pixel screams / Minimal whispers" spread). See `palettes.json` (Minimal &
  Pastel `focus_ring` were bumped).
- **Corners stay rounded on all 10 themes** (incl. Pixel) — full cohesion, per
  decision. No `radius/sm = 0` exception.
- Glow tint = `focus_ring`. Selected badge = `accent` (the only place the two
  roles meet, intentionally — "this is the live one" vs "this stays chosen").

## 5. Integration

- Primary deliverable is **this spec**; the ring is code-drawn with
  `DrawRoundedRect` + `SDL_RenderFillRect` halos in:
  - `qd_Launchpad.cpp` → `PaintCell()` focus-ring block (~L2674), folder-tab
    ring (~L1996), search-bar ring (~L1901, drop `#0080AA`).
  - `qd_Window.cpp` → window focus ring (~L292) — same rounded language at the
    window's 8px (→ propose `radius/md` 12px) corners.
- **Optional 9-slice** (`selection/selection_frame.png`, ~64×64 corner,
  transparent center, grayscale) only if the soft glow is cheaper to blit than
  to draw per-frame; engine multiplies it by `focus_ring`. Prefer code-draw.

## 6. Motion

Focus is **instant** (ring + fill-brighten appear on focus). Optional 1–2 frame
"pop" (scale 1.0 → 1.04 → 1.0) is allowed but must be cheap and skippable. No
continuous/idle selection animation.
