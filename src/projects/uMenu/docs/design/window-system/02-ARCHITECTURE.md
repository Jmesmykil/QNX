# 02 — Window System Architecture (from-scratch rebuild)

> **Status:** implementation-ready spec.
> **Scope:** the new window + window-button + frame architecture for Q OS uMenu
> (clean-room XorTroll/uLaunch fork; C++20 + libnx + SDL2 + Plutonium; Switch Erista).
> **Supersedes:** the rendering of `qd_Window.cpp` (~1769 lines). Reuses the old
> file only as a **behavioral** reference (hit-testing, drag, focus, animation).
> **Inputs:** `00-RESEARCH.md` (architecture survey), `01-QOS-CURRENT-AUDIT.md`
> (what the old window does wrong), the design-system v6 handoff (tokens), and the
> five-angle engineering research (real-OS frames, libtesla, hekate/NYX, old qd_Window).

---

## 0. The one-paragraph design

A window is **two rectangles**: an outer **frame rect** and an inner **client rect**,
exactly as every desktop OS models it (Win32 window-rect vs client-rect; AppKit
`frame` vs `contentLayoutRect`). The frame **chrome** (rounded body, 40 px titlebar,
34 px status bar, accent border, drop shadow) is **not code-drawn** — it is the
per-theme **buttonless SVG master** at `romfs:/window/q-os-{idx}.svg`, painted with a
**nine-patch** so the corners stay crisp and the two bars stay a fixed height while the
body stretches. The **window-control buttons** (close / minimize / maximize-restore)
are **drawn in code** in the title bar with hover/press state, styled to match the toast
texture and the `core/Button` spec. Everything else — the content element — draws into
the **computed client rect**, which is "a barrier that calculates how much room is
inside." The window class owns geometry, drag/resize/focus/animation, and the scale +
scroll viewport; the content stays passive (it paints at natural coords, like the old
`QdContentElement`). That passive-content contract is the one genuinely good idea in the
old window and is preserved verbatim.

```
   ┌──────────────────────────────────────────────┐  ← frame rect (outer)
   │  ● ●                                       ●   │  ← TITLEBAR band  (40px, FIXED)
   │     "Files"                                    │     buttons code-drawn here
   ├──────────────────────────────────────────────┤
   │                                                │
   │            CLIENT RECT (content draws)         │  ← BODY band     (stretches)
   │                                                │
   ├──────────────────────────────────────────────┤
   │   A Launch  ·  B Close  ·  Drag titlebar       │  ← STATUS band   (34px, FIXED)
   └──────────────────────────────────────────────┘
```

---

## 1. Component breakdown

Six pieces, each with a single responsibility. Three are new code, one is the existing
rasterizer, one is the existing content base, one is the existing manager (lightly
amended).

| # | Component | File (new unless noted) | Responsibility |
|---|---|---|---|
| 1 | **`QdFrame`** | `qd_Frame.{hpp,cpp}` (new) | Pure chrome: nine-patch SVG frame renderer + code-drawn window buttons + computed client-rect math + hit-testing. **No content, no scroll, no animation.** A value-ish helper owned by `QdWindow`. |
| 2 | **`QdWindow`** (rebuild) | `qd_Window.{hpp,cpp}` (rewrite) | One window. Owns geometry, focus, drag/resize/snap/maximize, minimize/restore animation, and the **scale + scroll viewport**. Delegates all chrome to `QdFrame`. Hosts one `QdContentElement`. |
| 3 | **`QdNinePatch`** | `qd_NinePatch.{hpp,cpp}` (new) | Stateless utility: given a source `SDL_Texture*` + source insets, blit 9 regions into a dest rect. Used by `QdFrame` (and reusable for panels/toasts later). |
| 4 | **`RasterizeSvgFile`** | `qd_SvgRaster.hpp` (**exists**) | nanosvg → `SDL_Texture` (ABGR8888, BLEND). Already wired. `QdFrame` calls it to rasterize the buttonless master **once at native source resolution**, then nine-patches it. |
| 5 | **`QdContentElement`** | `qd_ContentElement.hpp` (**exists, keep**) | Passive content base: `GetNaturalW/H`, `IsNaturalSizeDirty`, `PrefersWidthBoundScale`, `OnRender(ox,oy)`, `OnInput(...)`. Unchanged contract. |
| 6 | **`QdWindowManager`** | `qd_WindowManager.cpp` (**exists, amend**) | z-order list, lifecycle, input fan-out, render fan-out, minimize snapshot, dock entries. Only its calls into the window change; its own logic is preserved. |

**Why split `QdFrame` out of `QdWindow`:** the creator called the old single-class
window "over-complicated" (1769 lines). The decisive cut is **chrome vs behavior**.
`QdFrame` is ~250 lines of pure "given a rect + theme + button states, paint it and tell
me what was hit." `QdWindow` keeps behavior. Neither file is a 1700-line grab-bag.

---

## 2. The two-rectangle model + exact client-area math

This is the heart of the rebuild — the "barrier that calculates how much room is inside."

### 2.1 Frame insets (the fixed chrome differences)

From the design tokens and the shipped SVG masters (`q-os-{0..9}.svg`, viewBox
**640×400**, all 10 structurally identical):

| Inset | Value | Source |
|---|---|---|
| Border (left/right/bottom, the painted 1.5 px ring) | **`kBorder = 1` px** (layout) | SVG body rect is inset 1 px (`x=1 … width=638`); the 1.5 px stroke is visual, the layout inset is 1 |
| **Titlebar height** | **`kTitlebarH = 40` px** (FIXED) | SVG `<rect y=1 height=40>`; design tokens "titlebar 40, does not stretch" |
| **Status-bar height** | **`kStatusH = 34` px** (FIXED) | SVG `<rect y=365 height=34>`; design tokens "hint 34, does not stretch" |
| Body corner radius | **`kBodyRadius = 14` px** | SVG `rx=14` + window README "Geometry"; resolves the tokens 12-vs-14 conflict in favor of **14** |

> **Note on the legacy 42 px constant.** `qd_WmConstants.hpp` defines `TITLEBAR_H = 42`
> and `BOTTOM_BAR_H = 42` (the old traffic-light-era chrome). The **new design-system
> truth is 40 / 34**. The rebuild uses **40 / 34** (matching the SVG masters the chrome
> is actually rendered from) and the new `QdFrame` carries its own `kTitlebarH = 40` /
> `kStatusH = 34`. Leave the old WmConstants values in place for now (other call sites
> read them); `QdFrame` is the single source of truth for the new chrome bands. A later
> hygiene pass can retire the 42s.

### 2.2 The client rect (computed, never stored as magic numbers)

Given the frame rect `{fx, fy, fw, fh}`:

```cpp
// QdFrame::ComputeClientRect(const SDL_Rect& frame) -> SDL_Rect
SDL_Rect client;
client.x = frame.x + kBorder;                               // = fx + 1
client.y = frame.y + kTitlebarH;                            // = fy + 40   (content starts below titlebar)
client.w = frame.w - 2 * kBorder;                           // = fw - 2
client.h = frame.h - kTitlebarH - kStatusH;                 // = fh - 40 - 34 = fh - 74
```

That is the entire "two-rectangle" relationship — the same shape as the Win32
`client = window - {frameX, caption+frameY, frameX, frameY}` formula, specialized to our
fixed bands. The content viewport then subtracts scrollbar gutters (§5):

```cpp
// QdWindow::GetViewportSize(vw, vh)  — SINGLE source (the old file computed this twice)
SDL_Rect c = frame_.ComputeClientRect(FrameRect());
vw = c.w - (vsb_visible_ ? kScrollbarW : 0);   // kScrollbarW = 6
vh = c.h - (hsb_visible_ ? kScrollbarW : 0);
```

**Min size** is enforced so the body band never collapses to negative:
`WIN_MIN_W = 320`, `WIN_MIN_H = kTitlebarH + kStatusH + 80 = 154` (≈ the old 180; pick
the larger of the two so the body keeps ≥ 80 px). Clamp on every resize, exactly like
Win32 `WM_GETMINMAXINFO` / `SDL_SetWindowMinimumSize`.

### 2.3 Maximize/snap = a geometry swap only

`QdWindow` keeps `maximized_` + `pre_max_*` and `snapped_` + `pre_snap_*` (preserve the
old behavior). Maximize fills the work area between the top bar and the dock; the chrome
bands stay 40/34, only the body grows. No special-case rendering — the nine-patch handles
any size. (This mirrors the old window's "remembered restore geometry + hysteresis,"
which `00-RESEARCH.md` §2.1 endorses.)

---

## 3. The nine-patch frame renderer

### 3.1 Why nine-patch (not full-size rasterize)

The shipped masters bake the titlebar at `y=1 h=40` and the status bar at `y=365 h=34`
inside a **640×400** box. If you rasterize the whole SVG to the live window size (what
v3.7 did), the 40 px titlebar scales to `40 * (winH/400)` — on an 800 px window that's an
80 px titlebar, and the corner radius distorts with aspect. **Nine-patch fixes this**:
corners blit 1:1, the two bars stretch only horizontally at their fixed height, and only
the middle body band stretches in both axes. This is the standard resizable-chrome
technique (Android `*.9.png`, CSS `border-image`, libGDX `NinePatch`).

### 3.2 Source insets (guide lines on the 640×400 master)

The master is sliced with two vertical guides `x0,x1` and two horizontal guides `y0,y1`.
Chosen to keep the rounded corners + both bars intact:

```
Source master: sw = 640, sh = 400
  x0 = 24      (left  margin: corner radius 14 + a few px slack)
  x1 = 616     (right margin start  → right = sw - x1 = 24)
  y0 = 40      (TOP band = full titlebar height; the titlebar is a fixed top slice)
  y1 = 366     (BOTTOM band start → bottom = sh - y1 = 34 = full status-bar height)
```

So the fixed margins are: `left = 24`, `right = 24`, `top = 40`, `bottom = 34`.

- **top = 40** captures the entire titlebar (the titlebar never stretches vertically).
- **bottom = 34** captures the entire status bar (likewise).
- **left = right = 24** captures the rounded body corners + the start of the accent ring.
- The **center cell** (the body interior) is the only region that stretches in Y, which
  is correct: the body is the part that should grow.

> The bars do stretch **horizontally** (their cells are top-center and bottom-center,
> region 2 and 8), which is fine — they are flat fills + a hairline, no horizontal detail
> to distort. The corner cells (1,3,7,9) never scale, so the 14 px radius is pixel-exact
> at any window width/height.

### 3.3 The 9 blit regions (source → dest)

Rasterize the master at native size into `src_tex_` (640×400). For a destination frame
of `dw × dh` (`dw = client.w + 2*kBorder` etc. — i.e. the full frame rect):

```
left=24  right=24  top=40  bottom=34
midW = dw - left - right     // clamp to >= 0
midH = dh - top  - bottom    // clamp to >= 0
```

| # | region | src (x,y,w,h) | dst (x,y,w,h) |
|---|---|---|---|
| 1 TL | corner | (0,    0,    24, 40) | (0,         0,         24,   40)   |
| 2 T  | titlebar mid | (24,  0,    592,40) | (24,        0,         midW, 40)   |
| 3 TR | corner | (616,  0,    24, 40) | (dw-24,     0,         24,   40)   |
| 4 L  | body left edge | (0,   40,   24, 326)| (0,         40,        24,   midH) |
| 5 C  | body interior | (24,  40,   592,326)| (24,        40,        midW, midH) |
| 6 R  | body right edge | (616, 40,   24, 326)| (dw-24,     40,        24,   midH) |
| 7 BL | corner | (0,    366,  24, 34) | (0,         dh-34,     24,   34)   |
| 8 B  | status mid | (24,  366,  592,34) | (24,        dh-34,     midW, 34)   |
| 9 BR | corner | (616,  366,  24, 34) | (dw-24,     dh-34,     24,   34)   |

That is **nine `SDL_RenderCopy(r, src_tex_, &src, &dst)` calls**. The renderer does each
per-region scale for free. (Center can be tiled instead of stretched if a body texture is
ever added; for the flat glass fill, stretch is correct and cheapest.)

### 3.4 Caching

Rasterizing SVG is the expensive step; nine-patch blitting is cheap. So:

- **Cache the rasterized source** `src_tex_` keyed on **theme index only** (640×400 is
  fixed — it does NOT depend on window size). Re-raster only when
  `g_active_theme_pack_idx` changes. (The old code re-rasterized per `(size, theme)`,
  which was both slower and the thing that made it skip rendering during animation.)
- **Nine-patch every frame** from that cached source into the live frame rect — it's 9
  textured quads, trivially cheap, and works at any size including mid-resize and
  mid-animation. **No "Normal-state-only" gate needed** (the old gate existed because
  re-rastering mid-animation was too slow; with a size-independent cache the gate is gone).
- Fallback: if `RasterizeSvgFile` returns `nullptr` (missing file/parse/OOM), `QdFrame`
  falls back to a **trivial code-draw**: one `DrawRoundedRect(body, surface_glass@0.96,
  r=14)` + two band fills + a 1.5 px accent outline. This is ~15 lines, not the old
  fully-procedural chrome. (The old procedural glyph engine — `ThickLine`,
  `StrokeRoundedSquare`, X/dash/double-arrow — is **deleted**; buttons are code-drawn
  shapes, see §4, and the frame fallback is a flat rect.)

### 3.5 Buttonless masters

The shipped `q-os-{idx}.svg` files bake a "Window" title and a hint line **and would bake
buttons if we drew them in SVG**. For the rebuild the masters must be **buttonless and
textless** (frame chrome only): body rect + titlebar fill + titlebar hairline + status
fill + status hairline, nothing else. The title text and hint text are drawn in code
(they are dynamic per window), and the buttons are drawn in code (§4). **Action:**
regenerate the 10 masters stripping the `<text>` nodes (keep everything else). The
nine-patch source insets above already assume no baked text in the stretch zones.

---

## 4. Window-control buttons (code-drawn)

The frame is buttonless; `QdFrame` draws the controls **in code** over the rasterized
titlebar, so they get live hover/press state and match the `core/Button` + toast texture.

### 4.1 Which buttons, where

The design master places the controls as **corner discs** (TL close, TR maximize, BL
minimize, BR resize). For the rebuild we **consolidate the three window-state controls
into the title bar** (close / minimize / maximize-restore), which is simpler than the
old four-corner split (close TL + max TR + min BL + resize BR scattered across two
bands) and matches both the real-OS research (a horizontal strip of buttons in the
caption) and the libtesla "buttons live in the frame" model.

- **Layout:** right-anchored in the titlebar (Windows convention; cleaner than the old
  corner scatter). Order left→right: **minimize, maximize/restore, close** (close
  outermost-right). Each is a **disc Ø24** (`kDiscDia = 24`), vertically centered in the
  40 px titlebar (`cy = fy + 20`), spaced **`kDiscGap = 8`** apart, first disc inset
  **`kDiscInset = 12`** from the right edge.

```cpp
// QdFrame::LayoutButtons(const SDL_Rect& frame)
const int cy   = frame.y + kTitlebarH/2;                 // 20
const int r    = kDiscDia/2;                              // 12
int cx = frame.x + frame.w - kBorder - kDiscInset - r;   // close center x
close_   = { cx - r, cy - r, kDiscDia, kDiscDia };  cx -= (kDiscDia + kDiscGap);
maximize_= { cx - r, cy - r, kDiscDia, kDiscDia };  cx -= (kDiscDia + kDiscGap);
minimize_= { cx - r, cy - r, kDiscDia, kDiscDia };
```

> **Resize:** dropped as a *button*. Resize is a **drag affordance**, not a disc — the
> BR corner of the frame is a resize hit-zone (§6), the cleaner real-OS pattern. This
> removes one of the old four corner buttons and the whole bottom-bar relocation hack
> (the old code moved BL/BR into a 42 px bottom bar and then had to paint buttons *after*
> content because the clip rect overwrote them — all of that disappears).

### 4.2 Disc colors + glyphs (design tokens)

Per the GLYPH-SPEC and `QdTheme`: dark glyph on a bright disc (the corner-button
signature). Disc fill = the button's own color; glyph color `#0A0A14` (`kGlyphDark`),
**2.2 px** stroke, round caps, glyph ~**14 px** inside the Ø24 disc.

| Button | Disc color (token) | Glyph |
|---|---|---|
| **Minimize** | `g_QdTheme.button_minimize` `#FBBF24` amber | single horizontal dash |
| **Maximize** | `g_QdTheme.button_maximize` `#4ADE80` green | rounded square (radius/sm); **two offset squares = Restore** when `maximized_` |
| **Close** | `g_QdTheme.button_close` `#F87171` red | two diagonal strokes (×) |

Glyphs are drawn with **2–3 primitive calls each** (lines for ×/dash, a rounded-rect
outline for the square) — not a procedural glyph engine. (This is the deliberately small
replacement for the old `PaintCornerBtn`.)

### 4.3 Button state machine (one implementation, shared by all three)

This is the standard idiom from the real-OS research (decoupled visible vs interactive,
capture-on-press, fire-only-if-release-inside):

```cpp
enum class BtnState : uint8_t { Normal, Hover, Pressed, Disabled };

// On pointer move (mouse / ZR-cursor):  btn.state = Contains(btn, p) ? Hover : Normal
// On pointer down inside btn:            btn.state = Pressed;  capture = btn   (the pressed disc id)
// While captured + move:                 btn.state = Contains(btn, p) ? Pressed : Hover
// On pointer up:                         if (captured == btn && Contains(btn,p)) fire(action);
//                                        capture = none; btn.state = Contains?Hover:Normal
// On touch-down directly inside btn:     treat as immediate press→up (Switch tap)
```

- **Hover** = disc brightens (+ a faint `--glow-accent`-style ring, matching the
  `core/Button` hover and the toast accent ring). **Pressed** = `scale(0.94)` of the disc
  (echoes the Button spec's `scale(0.97)` press). These two states are the entire "feel."
- The **load-bearing detail** (from the research): the action fires **only if the release
  happens inside the same disc that captured the press** — drag-out-then-release cancels.
- Capture: on Switch there's no OS mouse-capture, but the window already owns the input
  loop, so "capture" is just `pressed_disc_id_` held on the window until pointer-up.

### 4.4 Toast / Button alignment (the "one material" rule)

The discs and the title bar must read as the same family as the `core/Button` and the
toast. Hold these invariants (from the design-language angle):

- **Shared glass body:** the frame body is `surface_glass #12122A` @ 0.96; the toast is
  the same hex @ 0.92; secondary buttons the same hex. Do not introduce a new surface.
- **Accent is the unifying ring:** active frame border 1.5 px `accent #7DD3FC`, toast
  border 1 px same accent, button hover ring same accent, focus ring `focus_ring`. The
  hover glow on a disc = the same accent the active border uses.
- **Dark-glyph-on-bright-fill** is shared between the close/min/max discs and the toast's
  green status dot (which echoes maximize-green). Success state is consistent across
  chrome and notifications.
- **Glow vs shadow discipline:** structural lift = neutral black shadow
  (`--shadow-window 0 18px 60px -12px rgba(0,0,0,0.7)` for the window; the elevation/2
  token **6,6 @ 0x80** is the on-device approximation the old code used and the rebuild
  keeps). Emphasis = tinted accent glow. Never tint the structural shadow.

---

## 5. Content layout inside the client rect + the scale/scroll viewport

The good architecture from the old window is **preserved exactly**: the host owns scale +
scroll + clip + the touch inverse-transform; content is passive.

### 5.1 Render contract (unchanged from SP3)

```
QdWindow::OnRender:
  1. frame_.Paint(renderer, FrameRect(), focused_, btn_states_)   // nine-patch + buttons + title text + hint text
  2. SDL_Rect c = frame_.ComputeClientRect(FrameRect());
  3. SDL_RenderSetClipRect(r, &c);                                // clip to client
  4. compute scale (uniform; width-bound if content->PrefersWidthBoundScale())
  5. SDL_RenderSetScale(scale_x, scale_y);
  6. content_->OnRender(origin_x, origin_y);                      // content paints at NATURAL coords
  7. SDL_RenderSetScale(1,1);  SDL_RenderSetClipRect(r, nullptr);
  8. PaintScrollbars(r, alpha);                                   // 6 px gutters, single visibility formula
```

- **Scale:** uniform (min of width/height ratios) to preserve aspect; `PrefersWidthBoundScale()`
  opt-in for list/grid content (rows stay design-size, overflow → scroll). Centering
  offset when uniform scale leaves margin (preserve `cur_offset_x_/y_`).
- **Touch inverse:** `local = (screen - client.origin)/scale - offset + scroll`, using
  `lroundf` (round, not truncate — the old fix). The inverse now subtracts the **client
  rect** origin (`fx+kBorder`, `fy+kTitlebarH`) instead of recomputing border + titlebar
  inline — one source for the inset.
- **Scrollbars:** fixed **6 px** gutter, shown when overflowing, thumb-drag. **Compute
  `vsb_visible_/hsb_visible_` ONCE** in a `RecomputeScrollVisibility()` helper that both
  `GetViewportSize` and `PaintScrollbars` read (the old file computed two different
  formulas in two places — single-source it).

### 5.2 Resize recompute loop (every toolkit runs this)

```
size change (drag BR corner, snap, maximize)
  → clamp to [WIN_MIN_W, WIN_MIN_H] … [work area]
  → frame_.LayoutButtons(FrameRect())        // re-anchor discs (right-anchored)
  → (nine-patch dst rects are recomputed lazily next Paint — nothing cached per size)
  → RecomputeScrollVisibility()              // may add/remove a 6 px gutter
  → mark dirty
```

No 9-patch dst caching is needed (it's 9 cheap quads); only the **SVG source** is cached
(by theme). This is the key simplification over the old per-(size,theme) raster cache.

---

## 6. Hit-testing (priority order) + drag/resize/focus behavior

`QdFrame::HitTest(point) -> Region` returns one enum; `QdWindow` acts on it. Priority
order is the proven Win32 table (corners > buttons > caption > client):

```cpp
enum class Region : uint8_t {
    None, ResizeBR, Close, Minimize, Maximize, Titlebar, StatusBar, Client
};

Region HitTest(SDL_Point p, const SDL_Rect& frame) const {
    if (!PointInRect(p, frame)) return Region::None;
    // 1. resize corner (BR) — a kGrip×kGrip zone at the frame's bottom-right
    if (InBR(p, frame, kGrip /*=18*/)) return Region::ResizeBR;
    // 2. window buttons (discs) — buttons beat the caption band
    if (PointInRect(p, close_))    return Region::Close;
    if (PointInRect(p, maximize_)) return Region::Maximize;
    if (PointInRect(p, minimize_)) return Region::Minimize;
    // 3. caption (titlebar) — drag-move + double-tap-maximize
    if (p.y < frame.y + kTitlebarH) return Region::Titlebar;
    // 4. status bar (no interaction beyond hint display)
    if (p.y >= frame.y + frame.h - kStatusH) return Region::StatusBar;
    // 5. everything else is content
    return Region::Client;
}
```

Behavior wired in `QdWindow` (all of this is **ported behavior** from the old window —
the *only* thing being rebuilt is rendering):

- **`Titlebar` drag** → origin-delta drag model (`drag_origin_win_*` + `drag_origin_touch_*`)
  so Plutonium's held-frame `(-1,-1)` touch doesn't stall the drag. **Keep this** — the
  research and audit both flag it as the right fix. Double-tap titlebar = toggle maximize.
- **`ResizeBR` drag** → origin-delta resize, clamp to min/work-area. Keep the **watchdog**
  that force-ends a resize if no touch update arrives for ~10 frames (the cure for the
  stuck-flag "nothing opens afterwards" HW bug) — but it now lives in one place, not three.
- **`Close/Minimize/Maximize`** → run the §4.3 state machine; fire on release-inside.
- **Focus:** `QdWindowManager` owns z-order; topmost = focused. `QdFrame` paints the
  **focused** chrome (accent border 1.5 px + the 2-pass soft glow halo + 3 px focus ring
  at radius 14) vs **inactive** (titlebar→`titlebar_inactive`, border→`titlebar_inactive`,
  title text→`text_secondary`). Keep the old halo/ring look (it matches SELECTION-SPEC).
- **`ResetInteractionState()`** safety valve — keep the concept (clears drag/resize/press
  flags on focus loss / minimize / close), simplified.

### 6.1 Input source arbitration (gap the audit flagged — fix while here)

The old window reacted to **both** D-pad highlight and pointer hover with no mutual
exclusion. Add a single `active_input_source_ ∈ {Pointer, Pad}` consulted **inside** the
window: only the active source drives button hover. Touch/ZR-move → `Pointer`; D-pad/stick
→ `Pad`. This is the "one global input-source arbiter applied inside windows" from
`00-RESEARCH.md` §3.

---

## 7. Class sketches (signatures)

### 7.1 `qd_NinePatch.hpp`

```cpp
namespace ul::menu::qdesktop {
struct NinePatchInsets { int left, right, top, bottom; };           // 24,24,40,34

// Blit `src` (sized src_w×src_h) into `dst` using 9-slice. Corners 1:1,
// edges stretch on one axis, center stretches both. Clamps mid bands to >= 0.
void DrawNinePatch(SDL_Renderer* r, SDL_Texture* src, int src_w, int src_h,
                   const NinePatchInsets& ins, const SDL_Rect& dst, u8 alpha);
}
```

### 7.2 `qd_Frame.hpp`

```cpp
namespace ul::menu::qdesktop {

enum class FrameRegion : uint8_t { None, ResizeBR, Close, Minimize, Maximize, Titlebar, StatusBar, Client };
enum class BtnState    : uint8_t { Normal, Hover, Pressed, Disabled };

class QdFrame {
public:
    // Fixed chrome bands (the NEW design-system truth; not the legacy 42s).
    static constexpr int kBorder      = 1;
    static constexpr int kTitlebarH   = 40;
    static constexpr int kStatusH     = 34;
    static constexpr int kBodyRadius  = 14;
    static constexpr int kDiscDia     = 24;
    static constexpr int kDiscGap     = 8;
    static constexpr int kDiscInset   = 12;
    static constexpr int kGrip        = 18;   // BR resize hit-zone

    // The barrier: how much room is inside.
    SDL_Rect ComputeClientRect(const SDL_Rect& frame) const;

    // Recompute the three disc rects (right-anchored) for this frame.
    void LayoutButtons(const SDL_Rect& frame);

    // Paint chrome: nine-patch SVG (cached source by theme) + discs + title + hint.
    // Falls back to a flat code-draw frame if the SVG master is missing.
    void Paint(SDL_Renderer* r, const SDL_Rect& frame, bool focused, bool maximized,
               BtnState close, BtnState min, BtnState max,
               const std::string& title, SDL_Texture* hint_tex, u8 alpha);

    // Single hit-test, priority: corner > buttons > caption > status > client.
    FrameRegion HitTest(SDL_Point p, const SDL_Rect& frame) const;

    void FreeTextures();                       // destroys the cached SVG source

private:
    SDL_Texture* src_tex_ = nullptr;           // rasterized buttonless master (640×400)
    int          src_theme_idx_ = -1;          // cache key = theme index ONLY
    SDL_Rect close_{}, minimize_{}, maximize_{};
    void EnsureSource(SDL_Renderer* r);        // raster once per theme change
};
}
```

### 7.3 `qd_Window.hpp` (rebuild — surface stays compatible)

Keep the **public API identical** to today so the WmBridge + WindowManager compile
unchanged: `New(title, elem, x, y, w, h)`, `OnRender`, `PollEvent`, `UpdateHoverForCursor`,
`TryActivateAtCursor`, `Begin/Update/EndCursorDrag`, `Begin/Update/EndResizeDrag`,
`ApplySnap`/`RestoreFromSnap`/`ToggleMaximize`/`MoveTo`, `AdvanceAnimation`, geometry
queries, `SetContent`, `SetScrollOffset`, `GetViewportSize`, `SetHintText`, and the
callbacks (`on_close_requested`, `on_minimize_requested`, `on_minimize_begin_`, `on_tick`,
`on_scroll_update`). **Internally** it delegates all chrome to a `QdFrame frame_;` member
and deletes: `EnsureTitlebarTexture`/`titlebar_tex_`, `PaintCornerBtn`, the procedural
glyph helpers, `corner_*` rects, `corner_tip_tex_[4]`, `kTrafficR`/`TRAFFIC_*`, `fbo_`,
the duplicate scroll-visibility math, and the five-way scroll input fan-out (collapse to
the inputs the hardware has — see §8).

---

## 8. Simplifications carried over from the audit (what gets cut)

From `01-QOS-CURRENT-AUDIT.md` "OVER-COMPLICATED — SIMPLIFY":

1. **Decompose `PollEvent`** (was ~470 lines inline) into named handlers:
   `HandleTitlebarDrag`, `HandleResize`, `HandleButtons`, `HandleContentDrag`,
   `HandleScrollbar`, `ForwardToContent`.
2. **Collapse the five scroll paths** (thumb-drag, finger-drag, mouse-wheel, left-stick,
   ZL+D-pad) to the **three the Switch actually has**: scrollbar thumb-drag, finger
   drag-scroll (8 px jitter threshold), and stick/D-pad pan. **Drop mouse-wheel** (no
   wheel on Switch hardware; the audit calls this out).
3. **Unify the drag owner.** The old triple-path drag (touch in PollEvent + ZR-cursor in
   WM + 3 watchdog blocks) collapses to: one `HitTest` → one drag state, with one
   watchdog. ZR-cursor and touch both feed the same `{x,y,pressed}` pointer (the NYX /
   libtesla "synthesize one pointer, hit-test once" pattern).
4. **Delete the dead `FocusSurface`/`FocusLevel`/`FocusElement` formal focus model** in
   `qd_WmConstants.hpp` (zero references, per audit §2.6) — OR wire it; for the rebuild,
   **delete** (focus is z-order + the §6.1 source arbiter).
5. **Procedural glyph engine deleted** (§3.4/§4.2) — buttons are 2–3 primitive shapes.
6. **No version-archaeology comments** in the new files — rationale lives in this doc.

**Gaps to add while rebuilding** (from `00-RESEARCH.md` §3/§4):
- **Scroll-into-view** on focus change (the old window had none).
- **One input-source arbiter inside windows** (§6.1).
- **A real focus trap** — a pointer/ZR click on empty desktop must NOT launch icons
  *behind* an open window (today focus is inferred from geometry). The WindowManager
  consumes clicks that land on the topmost window's frame rect before the desktop sees them.
- **A single confirm/back spine:** A = confirm, B = back/close, consistently (today A vs
  ZR/R activate different selections).

---

## 9. WIRING PLAN — swap the new chrome into the live flow

The public `QdWindow` API is unchanged, so **the WmBridge and WindowManager call sites do
not change**. The swap is internal + the SVG masters. Step-by-step, each step
independently buildable and HW-gated per the project's phased-delivery doctrine:

### Step 1 — Land `QdNinePatch` (leaf, no deps)
- Add `qd_NinePatch.{hpp,cpp}` (§7.1). Pure function over `SDL_RenderCopy`.
- **Exit test:** a throwaway call nine-patches any 640×400 texture into a 1280×800 rect;
  eyeball that corners are crisp and bands are fixed height. Build green.

### Step 2 — Regenerate the 10 buttonless/textless SVG masters
- Strip the `<text>` nodes from `romfs/window/q-os-{0..9}.svg` (keep body rect, both band
  fills, both hairlines, per-theme colors). Verify all 10 stay structurally identical
  (only colors differ) so the §3.2 insets hold for every theme.
- **Exit test:** `RasterizeSvgFile` on each returns a non-null 640×400 texture; visual
  check shows no baked text/buttons. Build green.

### Step 3 — Land `QdFrame` (depends on 1 + 2 + existing `RasterizeSvgFile`)
- Add `qd_Frame.{hpp,cpp}` (§7.2): `EnsureSource` (raster once per theme), `Paint`
  (nine-patch + discs + title text + hint via `hint_tex`), `ComputeClientRect`,
  `LayoutButtons`, `HitTest`, code-draw fallback.
- **Exit test:** a standalone harness window using only `QdFrame` renders the chrome at
  several sizes; discs hover/press; resizing keeps bands fixed. Build green.

### Step 4 — Rebuild `QdWindow` internals on top of `QdFrame`
- Replace chrome rendering in `OnRender` with `frame_.Paint(...)`; replace all inline
  inset math with `frame_.ComputeClientRect` / `frame_.HitTest`. Keep the SP3 scale/scroll
  viewport, drag/resize/focus/animation **behavior** verbatim (port, don't redesign).
- Delete the dead members (§7.3) and decompose `PollEvent` (§8.1).
- Preserve the **exact public API** + all callbacks so WmBridge/WM compile unchanged.
- **Exit test:** project builds; `OpenFolderWindow`/`OpenTaskManagerWindow`/etc. open
  windows that look like the new chrome, drag, resize, focus, minimize/restore, and host
  their content (Vault grid, Task list) scrolling correctly. Build green + creator HW test.

### Step 5 — WindowManager touch-ups (small)
- `qd_WindowManager.cpp`: keep `OpenWindow`/`CloseWindow`/`Minimize…`/`Restore…`/
  `BringToFront`/`PollWindowEvents`/`RenderAll` as-is. Two amendments only:
  (a) **focus trap** — in `PollWindowEvents`, a pointer/ZR click inside the topmost
  window's frame rect is consumed before desktop icons see it (§8 gap);
  (b) **gate the per-frame hover walk** to `Pointer`-source frames (audit §2.8 — it
  currently walks all windows every frame regardless of input source).
- Minimize snapshot path (`MinimizeWindow` → blit to `SNAP_W×SNAP_H` 108×60 dock tile) is
  unchanged — it renders the live window, which now includes the new chrome for free.
- **Exit test:** minimize animation still lerps to the dock tile with the new chrome in
  the snapshot; clicking empty desktop behind a window no longer launches an icon.

### Step 6 — Remove the old chrome remnants + the legacy 42 constants (hygiene)
- Once Steps 1–5 are HW-green, delete `qd_WmConstants.hpp`'s `TRAFFIC_*` and the dead
  `Focus*` model, and migrate any remaining readers of `TITLEBAR_H = 42` / `BOTTOM_BAR_H`
  to `QdFrame::kTitlebarH` / `kStatusH` (40 / 34). `kFolderWinH`/`kContentH` arithmetic
  in WmBridge that subtracts `TITLEBAR_H + kCornerBtn` is recomputed from the new bands.
- **Exit test:** grep shows no `TRAFFIC_`, no `BOTTOM_BAR_H`, no `kCornerBtn` readers;
  build green; windows visually unchanged from Step 4.

### Wiring at the call site (unchanged — shown for reference)
`OpenFolderWindow` keeps exactly:
```cpp
auto win = QdWindow::New(meta.title, std::move(elem), wx, wy, kFolderWinW, kFolderWinH);
win->SetHintText("A: launch  ·  ZL: menu  ·  Swipe/L–R: page  ·  B: close");
win->on_minimize_begin_ = [...]{ ... wm_.SetPendingReopen(...); };
wm_.OpenWindow(std::move(win));
```
No change — the new chrome is entirely behind `QdWindow`.

---

## 10. Exact numbers (single reference table)

| Token | Value | Where |
|---|---|---|
| Frame body radius | **14** px | `QdFrame::kBodyRadius`; SVG `rx`, README |
| Titlebar height (FIXED) | **40** px | `kTitlebarH`; SVG `<rect h=40>` |
| Status-bar height (FIXED) | **34** px | `kStatusH`; SVG `<rect h=34>` |
| Border (layout inset) | **1** px | `kBorder`; SVG body inset; visual stroke 1.5 px accent |
| Client rect | `x+1, y+40, w-2, h-74` | §2.2 |
| Nine-patch source insets | left **24**, right **24**, top **40**, bottom **34** | §3.2 (on the 640×400 master) |
| Window disc Ø | **24** px (r=12) | `kDiscDia`; tokens "disc Ø24" |
| Disc gap / right inset | **8** / **12** px | `kDiscGap` / `kDiscInset` |
| Disc glyph | ~14 px, **2.2** px stroke, color `#0A0A14` | GLYPH-SPEC |
| Resize grip (BR) | **18** px | `kGrip`; `GRIP_SIZE` |
| Scrollbar gutter | **6** px | `kScrollbarW` |
| Min window | **320 × 154** (≥80 px body) | §2.2 |
| Surface glass | `#12122A` @ **0.96** (window) / **0.92** (toast) | `surface_glass` |
| Accent (border/ring) | `#7DD3FC`, border **1.5** px | `accent` |
| Close / Minimize / Maximize disc | `#F87171` / `#FBBF24` / `#4ADE80` | `button_close/minimize/maximize` |
| Focus ring / glow | 3 px ring at r=14 + 2-pass halo (`focus_ring` @ 0x40,0x20) | §6 |
| Drop shadow (on-device) | offset **6,6** @ **0x80**, radius 14 | elevation/2 token |
| Press scale | disc **0.94** (Button spec analog 0.97) | §4.3 |
| Animation | **16** frames (~267 ms), cubic ease | `kAnimFrames`; ported |
| Default window | **1280 × 800** | `DEFAULT_WIN_W/H` |
| Folder window | **1133 × 720** | `kFolderWinW/H` |

---

## 11. Invariants to preserve (do not regress)

1. **Three-layer separation:** `QdFrame` (chrome) / `QdWindow` (behavior + viewport) /
   `QdContentElement` (passive content) / `QdWindowManager` (lifecycle). Content never
   touches scale.
2. **Host-owns-scale-viewport-inverse-transform** contract (SP3) — the single best part
   of the old window.
3. **Centralized scroll viewport** in the host, single visibility formula.
4. **Origin-delta drag** model (survives held-frame `(-1,-1)`).
5. **State-machine gating** of content paint/input to `Normal` (anti-crash guard for
   `scale_y → 0` during minimize).
6. **`ResetInteractionState()`** safety valve + the single resize watchdog.
7. **Theme-token coloring** + the **per-theme SVG chrome** (now nine-patched + cached by
   theme) with a trivial code-draw fallback.
8. **One material:** window body, panels, toast all `surface_glass`; accent is the
   unifying ring; fixed 40/34 bands.
