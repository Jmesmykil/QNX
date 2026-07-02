# 03 — Window System Comparison

> Compares the **new** Q OS uMenu window design (`02-ARCHITECTURE.md`) against the
> **old** `qd_Window` (~1769 lines), real desktop OS frames (Win32 / AppKit / GTK),
> uLaunch upstream, Hekate/NYX, and Atmosphère/libtesla. Sources: the five-angle
> engineering research + `00-RESEARCH.md` + `01-QOS-CURRENT-AUDIT.md`.

---

## The matrix

| Dimension | **Q OS (new)** | Q OS (old `qd_Window`) | Real desktop OS (Win32 / AppKit / GTK) | uLaunch (upstream) | Hekate / NYX | Atmosphère / libtesla |
|---|---|---|---|---|---|---|
| **Frame / client separation** | **Explicit two-rect model.** `QdFrame::ComputeClientRect(frame)` → `{x+1, y+40, w-2, h-74}`. The frame is "a barrier that calculates how much room is inside." | Implicit. Client area derived inline in several places; insets recomputed in `GetViewportSize` AND `PaintScrollbars` with two different formulas. | The canonical model — Win32 window-rect vs client-rect via `WM_NCCALCSIZE`; AppKit `frame` vs `contentLayoutRect`; GTK content + CSD margin. | No real windows — full-screen menu pages; no client-rect concept. | LVGL `lv_obj` tree; each object has bounds; "client" = a `lv_cont`/`lv_page` child, not a formal frame/client split. | `OverlayFrame` lays out exactly one child between a fixed header (y≈50–70) and footer (y≈693); a header/footer/body split, not a resizable frame. |
| **Resize handling (nine-patch?)** | **Yes — 9-slice.** Corners 1:1, titlebar (top) + status (bottom) stretch X only at fixed height, body stretches both. Source insets 24/24/40/34 on the 640×400 master. | No nine-patch. v3.7 **rasterized the whole SVG to live window size** → the 40 px titlebar scaled with the window and the radius distorted by aspect. Cached per `(size, theme)`. | Win32 nine-patch-ish via frame metrics + DWM; AppKit `NSThemeFrame`; GTK `border-image`/CSS. Nine-patch is the universal resizable-chrome primitive (Android `*.9.png`, libGDX). | Not applicable (no resizable windows). | LVGL styles use `radius` + `border` per object; resizes via layout reflow, not bitmap 9-slice; backgrounds are solid/gradient, not sliced art. | Fixed 448×720 framebuffer, no resize; rounded ends faked with `drawRect` + two `drawCircle`. |
| **Window-button layout** | **Code-drawn discs, right-anchored in the 40 px titlebar:** minimize / maximize-restore / close (Ø24, gap 8, inset 12). Resize is a BR drag-grip, not a button. Shared press/hover state machine; fire-only-if-release-inside. | **Four corner discs** (close TL / max TR / min BL / resize BR) split across the titlebar AND a 42 px bottom bar; buttons painted *after* content because the clip rect overwrote them; procedural glyph engine (`ThickLine`/`StrokeRoundedSquare`). | Win32 right-anchored 46×32 min/max/close; AppKit left "traffic lights" Ø~14 @ 20 inset via `standardWindowButton`. Hit-test via `WM_NCHITTEST` HT-codes; capture-on-press. | Header buttons via `lv_win_add_btn` (NYX-style) — none in the bare menu. | `lv_win_add_btn()` header buttons; per-state styling first-class (`LV_BTN_STYLE_REL/PR/TGL`). | No window buttons; whole rows are `ListItem`s; footer shows ` OK /  Back` glyph hints only. |
| **Content layout model** | Passive `QdContentElement` paints at **natural coords**; host owns **uniform scale** (+ width-bound opt-in) + centering + clip into the computed client rect. | Same SP3 passive-content + centralized-scale model (this part was good and is **kept**). | Anchor/spring or box/flex layout against the client rect; never absolute coords. | Each page hand-lays its widgets; no scale viewport. | **Constraint layout:** `lv_cont_set_layout(CENTER/COL/ROW/GRID)` + `lv_obj_align(ref, ALIGN_*, dx,dy)` — relative, resolution-flexible. | `layout(parentBounds)` recursive top-down; `List` stacks children; geometry computed once on layout. |
| **Theming** | One global `g_QdTheme` (17 tokens) + **per-theme SVG master** nine-patched; SVG source cached **by theme index only**; code-draw fallback. Dark-glyph-on-bright-disc; accent is the unifying ring across window/toast/button. | Same `g_QdTheme` + per-theme SVG overlay, but cached per `(size,theme)` and gated to Normal-state; chrome colors via macros. | DWM/WindowServer system theme; apps mostly inherit. WPF `WindowChrome` exposes the invisible-band model. | uLaunch JSON theme (background/colors) for menu assets. | **One accent `hue` → `lv_color_hsv_to_rgb` fans out to every widget**; `bg_color` derives shades by offset; `lv_theme_hekate`. The cleanest "one parameter → whole UI" theme. | **No theme engine** — a `namespace style` of `constexpr Color`; forks later made it data-driven. RGBA4444 colors. |
| **Rendering complexity** | Nine-patch = **9 `SDL_RenderCopy`** from a theme-cached source + 3 discs (2–3 prims each) + 2 text textures. SVG rasterized **once per theme**, not per frame/size. | Heavy: full SVG re-raster per (size,theme), procedural glyph engine, plus the code-draw chrome underneath that the SVG overpaints (redundant). | Compositor-backed (GPU); shadows/rounding free from DWM/WindowServer; app draws content only. | Plutonium/SDL2 immediate draws per page. | LVGL retained tree + **dirty-region invalidation**; flush callback copies only changed rects; VIC hardware rotate. | Software rasterizer: `setPixel` + 2 blend funcs; whole 448×720 repainted per frame (fine at that size). |
| **Lines-of-code feel** | **`QdFrame` ~250 + `QdNinePatch` ~80 + slimmed `QdWindow`** (behavior only; `PollEvent` decomposed into named handlers; 5 scroll paths → 3; dead `Focus*`/`TRAFFIC_*`/`fbo_`/`corner_tip_tex_` deleted). | **~1769 lines in one file** for a single primitive; `PollEvent` ~470 lines inline; triple-pathed drag + 3 watchdog blocks; dead formal focus model in WmConstants. | N/A (toolkit/OS code), but the *pattern* is "thin invisible interactive bands + paint on top" (WPF `WindowChrome`). | Small per-page; no window primitive. | NYX is a **thin app layer over vendored LVGL** — the toolkit is the library, not hand-rolled. | **Single-header `tesla.hpp` ~3659 lines** for the whole library (renderer+elements+loop); but any one widget is tiny (`OverlayFrame::draw` ≈ 6 calls). |

---

## Verdict — why the new design is right for Q OS

**It adopts the real-OS two-rectangle model, which the old window only half-had.** Every
desktop toolkit separates an outer frame from a computed client rect; the old `qd_Window`
derived insets inline and even computed scrollbar visibility with two divergent formulas.
`QdFrame::ComputeClientRect` makes "how much room is inside" a single function — the exact
"barrier" the brief asks for.

**Nine-patch is the correct fix for the one thing v3.7 got wrong.** The shipped masters
bake a 40 px titlebar and 34 px status bar into a 640×400 box; rasterizing that whole SVG
to the live window size stretched the bars and warped the 14 px radius. Nine-patch keeps
corners 1:1 and the two bands at fixed height — the universal resizable-chrome technique
(Android/CSS/libGDX) — while letting only the body stretch. And because the SVG source is
now cached **by theme index only** (size-independent), the per-frame raster cost and the
"don't re-raster during animation" gate both vanish.

**It keeps the genuinely good parts and cuts the rest.** The SP3 passive-content +
host-owned-scale/scroll contract (the part `00-RESEARCH.md` §2.2 praises) is preserved
verbatim. What's cut is the bloat the creator flagged: the 1769-line single class, the
four-corner-buttons-across-two-bars scheme with its paint-after-content hack, the
procedural glyph engine, the five overlapping scroll paths (three of which the Switch
hardware can't even produce), the triple-pathed drag with three watchdogs, and the dead
`Focus*`/`TRAFFIC_*`/`fbo_` state. `QdFrame` (~250 lines) + `QdNinePatch` (~80) + a
behavior-only `QdWindow` is dramatically simpler than one 1769-line file.

**It borrows the strongest idea from each neighbor.** From **libtesla**: the frame is just
a widget and the shell is an ordinary client; one hit-test enum with corner > button >
caption > client priority; the focus/animation "feel" (ring + press flash) owned by the
base. From **Hekate/NYX**: synthesize one pointer from touch/ZR and hit-test once
(instead of three drag paths), and a single accent fanning across the whole UI. From
**real OSes**: invisible interactive bands with paint on top, capture-on-press,
fire-only-if-release-inside, min-size clamping. And it stays aligned to the design
language — window body, panels, and toast are one `surface_glass` material with the accent
as the unifying ring — so the new chrome reads as the same family as the toast and the
`core/Button`, which is the whole point of the rebuild.

**Net:** the new design is *more like a real OS* (two rectangles, nine-patch, a real
focus trap, one input-source arbiter) while being *less code* than the old one, and it
sheds the Switch-irrelevant machinery the audit called out — which is exactly the
"engineer it like the real ones, but keep it simple" mandate.
