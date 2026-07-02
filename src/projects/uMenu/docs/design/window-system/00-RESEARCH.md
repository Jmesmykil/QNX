# 00 — Cross-OS Window-System Behavior Reference

**Status:** research / reference (behavior only — no Q OS code claims here)
**Scope:** the canonical, precise behavior of window / focus / input / selection / scroll
interaction across Windows, macOS, Linux (GNOME + KDE), iOS, Android, and console UIs
(Nintendo Switch *Horizon*, PlayStation 5, Xbox).
**Sibling docs:** `01-QOS-CURRENT-AUDIT.md` (how Q OS does it today),
`02-UNIFIED-SPEC.md` (the target system), `03-OVERHAUL-PLAN.md` (migration).

> This document is written as **"this is exactly how X should work."** It is the
> ground truth the unified spec is measured against. Every behavior here is something
> a polished desktop OS gets right and that Q OS must match or deliberately adapt for a
> controller-first Switch context. **Visual styling (colors, art, the look of the
> selection outline and cursor) is explicitly out of scope** — that is owned by the
> separate visual-design track in `../claude-design/`. This doc covers *mechanics*:
> *when* an indicator is shown, *what* receives input, *how* arbitration resolves.

---

## 0. The one model that ties it all together: the **active input source**

Every modern multi-input UI resolves the "mouse pointer vs keyboard/gamepad focus"
problem with the same core idea, under different names:

| OS / framework | Name of the concept | Behavior |
|---|---|---|
| Windows (WinUI / UWP) | `FocusVisualKind`, "Reveal Focus" | Focus rectangle is drawn **only after a keyboard/gamepad input**; it is **suppressed while the mouse is driving**. |
| macOS (AppKit) | "full keyboard access" + key-view loop | The focus ring appears on keyboard traversal; pointer hover/click does not draw a persistent ring. |
| GNOME / GTK, KDE / Qt | `:focus-visible` / "show focus on keyboard nav" | Same: a focus outline shows for keyboard nav, is hidden for pointer interaction. |
| Web / CSS | `:focus-visible` pseudo-class | The browser heuristically shows focus rings for keyboard but not mouse. |
| Consoles (PS5, Xbox, Switch) | "the highlight" / system cursor | Exactly one indicator is live at a time: the **stepped highlight** (D-pad/stick) **or** a **pointer** (touch / touchpad / Joy-Con-2 mouse). The other disappears. |

**The canonical rule — "last input wins, the other indicator hides":**

1. The system tracks a single variable: *which input source most recently did something
   meaningful*. Call it `ActiveInputSource ∈ { Pointer, Directional }`.
2. **Pointer activity** (mouse move past a small threshold, touch down, touchpad swipe,
   trackpad scroll) flips the source to **Pointer**: the pointer becomes visible and the
   stepped-focus outline is **hidden**.
3. **Directional activity** (arrow keys, D-pad, left-stick navigation, Tab) flips the
   source to **Directional**: the stepped-focus outline becomes visible and the pointer is
   **hidden** (or, on desktop, left static but no longer the authority).
4. **Non-directional buttons do NOT flip the source.** Pressing Enter / A / Space / a
   shortcut acts on *whatever indicator is currently live*; it never switches modes.
5. Each source maintains **its own selection**: the pointer has a hover target; the
   directional system has a focused element. They are tracked independently so switching
   back and forth feels stable (you return to where the focus was, not where the pointer is).
6. A tiny **movement threshold** (a few pixels) guards the flip so analog-stick jitter or
   a nudged mouse does not thrash the indicators every frame.

This is the spine of the whole system. Everything else (focus trapping, dropdowns,
scroll-into-view) is layered on top of this single arbiter.

**Real-world validation:** the Switch 2's Joy-Con-2 mouse mode shows this exactly — set a
Joy-Con on a flat surface and *a cursor appears* (source → Pointer); pick it up and use the
stick and *the cursor goes away and the stepped highlight returns* (source → Directional).
The PS5 behaves identically with its touchpad-as-pointer vs D-pad focus.

---

## 1. Focus & modality

### 1.1 What "focus" means at two levels

There are always **two** focus scopes, and confusing them is the #1 source of bugs:

- **Window focus (z-order top / "active window").** Exactly one top-level window is the
  *active/key* window. It paints its title bar as active; all others paint inactive. On
  every desktop OS the active window is the one that receives keyboard input.
- **Element focus (the focused control *inside* the active window).** Within the active
  window, exactly one control holds the caret/highlight and receives typed/confirm input.

A background (non-active) window has **no** element focus and shows **no** focus ring.

### 1.2 Click-to-focus vs focus-follows-pointer

- **Click-to-focus (raise-on-click)** — Windows, macOS, GNOME default, all consoles.
  Clicking/activating a window makes it active *and* raises it to the top of the z-order.
  This is the model every mainstream user expects.
- **Focus-follows-pointer (a.k.a. "sloppy focus")** — an *opt-in* X11/KDE/GNOME-tweak
  behavior where merely hovering a window makes it the keyboard target **without raising
  it**. Power-user feature; never a default for a consumer/console OS. *Q OS should not
  adopt this — click/confirm-to-focus is correct for a controller desktop.*
- **Raise-on-hover** is even rarer and universally considered hostile; do not use.

### 1.3 Z-order

- Windows form a back-to-front stack. The active window is normally the topmost *normal*
  window. Activating any window raises it to the top of its band.
- **"Always-on-top"** windows (utilities, pinned widgets) form a higher band that stays
  above normal windows regardless of activation.
- Newly opened windows open **on top** and become active.
- Minimizing the active window passes activation to the next window down the stack.

### 1.4 Modality and **focus trapping** — the hard rule

A **modal** window (dialog, sheet, alert, popover-with-modal-scrim) **traps focus
completely**. While a modal is open:

- **You cannot interact with anything behind or outside it.** Clicks outside the modal are
  either ignored or do nothing but flash/bounce the modal (macOS app-modal sheets bounce;
  Windows task-modal dialogs `MessageBeep`). The content behind is made **inert**
  (non-hit-testable, non-focusable) — the WAI-ARIA term is literally "remove focusability
  of the disabled main content underneath."
- **Keyboard/gamepad focus is trapped** inside the modal. Tab / Shift-Tab (or D-pad) cycles
  **only** through the modal's own controls and **wraps** at the ends — it can never escape
  to a background window or the desktop.
- **Focus moves into the modal on open** — to the first meaningful control (or a default/
  "safe" button), not left behind on the opener.
- **Esc / B / Cancel dismisses** (unless the dialog is a forced choice), and on dismiss
  **focus returns to the exact element that opened the modal** ("return focus to the
  triggering element"). This is what makes keyboard/controller flow feel seamless.
- **Click-outside-to-dismiss** applies to *light-dismiss* surfaces (menus, popovers,
  comboboxes) but **not** to true modals — a true modal needs an explicit choice.

Modality has degrees, all of which trap to *some* boundary:
- **Application-modal** — blocks its own app's other windows; other apps still usable
  (macOS sheets, most app dialogs).
- **Window-modal** — blocks only its parent window (a sheet attached to one document).
- **System/task-modal** — blocks the whole session (shutdown confirm, UAC prompt). Rare.

For a single-foreground console OS like Q OS, the practical rule is the strong one:
**an active in-OS window traps input; the desktop and other windows behind it are inert
until it is closed or explicitly backgrounded.**

### 1.5 What a background window may / may not receive

- **May:** repaint, run timers/animations, update live data, receive hover *highlight*
  feedback under the pointer (macOS lets you scroll a background window with the wheel and
  shows hover on its controls — "inactive window scrolling"), accept a single
  *click-through* on some controls (macOS allows click-through on a few control types only).
- **May not:** receive keyboard/gamepad/confirm input, hold element focus, or be navigated
  by Tab/D-pad. Typed input and directional navigation always go to the active window.
- **The reconciliation:** hover feedback on a background window is allowed *visually*, but
  the **directional focus model never targets a background window** — only the pointer can
  touch it, and only for raise-on-click or the few click-through controls.

### 1.6 Focus return after close (non-modal too)

Even for non-modal windows: when the active window closes, activation returns to the
**most-recently-active** remaining window (an MRU stack), not a random one. Within a
window, when a focused control is removed/disabled, focus moves to the nearest sensible
neighbor, never to "nothing."

---

## 2. Scaling

### 2.1 Window resize

- Windows have a **min size** and (often) a **max size**. Drag handles on edges/corners
  resize; the corner handle resizes both axes. Content **reflows** (controls reposition)
  or the window exposes **scrollbars** when content no longer fits.
- **Maximize** fills the work area (screen minus taskbar/dock/menu bar). **Restore** returns
  to the pre-maximize geometry (the OS remembers it). Double-clicking the title bar toggles
  maximize on Windows and (by default) zoom on macOS.
- **Snap / tiling:** dragging a window to a screen edge or corner snaps it to a half or
  quarter (Windows "Snap"/FancyZones, macOS Sequoia tiling, KDE quick-tile, GNOME
  half-tile). Dragging it back off the edge restores free-floating geometry. A small
  **snap-zone threshold** near the edge arms the snap; a larger **restore threshold** of
  drag distance un-snaps it (hysteresis, so it doesn't flicker).
- **Maximize and snap are mutually exclusive states**, each remembering the pre-state
  geometry to restore to.

### 2.2 Content scaling / zoom inside a window vs DPI scaling

Two different things, often conflated:

- **DPI / display scale factor** is a *global* multiplier (125%, 150%, 200%, Retina @2x)
  applied so UI is physically legible. The app renders at native pixels × scale; text and
  vector chrome stay crisp. This is *not* per-window content zoom.
- **Content zoom** is a *per-document* user action (Ctrl/Cmd-+ / −, pinch) that scales the
  *content* of one view, leaving chrome fixed. Browsers, PDF viewers, map apps.
- **Fit-to-content / "size to fit"** computes the natural size of content and sizes the
  window (or the zoom) so it shows without scrollbars where reasonable.
- The clean architecture (which Q OS already follows) is: **the window host owns the scale
  transform and the viewport; the content is drawn at its natural 1:1 coordinates and is
  agnostic to scale.** Input coordinates are inverse-transformed back into content space
  before the content hit-tests. This keeps every content view from re-implementing scaling.

### 2.3 Min/max and aspect

- Min size prevents controls from clipping; the host clamps resize to it.
- Some content is **uniform-scale** (preserve aspect, letterbox the extra axis — a fixed
  card, a dashboard). Some content is **width-bound** (a list/grid: lock horizontal scale
  to the viewport width and let extra height become *scroll*, never shrink rows to fit).
  The host should let content **declare which mode it wants** rather than guessing.

---

## 3. Scrolling & scrollbars

### 3.1 Scrollbar appearance & space model — overlay vs reserved gutter

There are two scrollbar regimes, and the choice changes layout:

- **Overlay scrollbars** (macOS default, iOS, Android, GNOME) — the bar is **drawn on top
  of content**, takes **no layout space**, and **auto-hides** when idle. It **fades in on
  scroll/at the pointer** and fades out after a moment. No layout shift ever. On macOS this
  is the "Show scroll bars: Automatically based on mouse or trackpad" setting.
- **Reserved-gutter (classic) scrollbars** (Windows default, macOS "Always", web default on
  Windows) — the bar occupies a **permanent gutter** the same width as the bar; content is
  narrowed to make room. Always visible when content overflows. The web exposes this as
  `scrollbar-gutter: stable` to *reserve* the gutter and avoid layout shift when a bar
  appears/disappears.

The QoL trade-off: overlay is cleaner and maximizes content but can occlude the last few
pixels of content and is harder to grab; reserved never occludes and is always grabbable but
costs space and can cause layout shift if toggled. A polished system **picks one regime per
context and is consistent** — and if it auto-hides, it still **reserves or stabilizes the
gutter** so content doesn't jump when the bar appears.

### 3.2 Scrollbar parts and interactions

A full scrollbar supports **all** of these (people rely on the obscure ones):

- **Thumb drag** — press the thumb and drag; content tracks proportionally. The thumb's
  *size* encodes the visible fraction (small thumb = lots of off-screen content).
- **Click in the track (paging)** — clicking the empty track **above/below the thumb**
  pages by ~one viewport (Windows/Linux default). macOS default is "jump to the spot that's
  clicked"; both behaviors are selectable. A polished system supports page-on-track-click at
  minimum.
- **Arrow buttons** (classic Windows/Linux) — step by a small line increment; usually
  omitted on overlay scrollbars.
- **Auto-repeat** — holding on the track or an arrow repeats the page/line step.

### 3.3 Input → scroll mappings (every input must scroll)

- **Mouse wheel** — vertical scroll; **Shift+wheel** scrolls horizontally; trackpad
  two-finger scroll does both axes. A *notch* of the wheel scrolls a few lines; the OS may
  apply **wheel acceleration** for fast spins.
- **Touch / finger drag** — content follows the finger 1:1 ("direct manipulation"), then
  **momentum/inertia** carries it after release (flick), decelerating smoothly. At the
  content ends, **rubber-banding / elastic overscroll** lets it drag slightly past the edge
  and **springs back** (iOS/macOS); Android shows a **stretch/glow** at the edge instead.
- **Keyboard** — Arrow keys (line), PageUp/PageDown (viewport page), Home/End
  (top/bottom), Space/Shift-Space (page in readers). On a controller: **D-pad/stick** moves
  the *focus*, and **scroll-into-view** (below) does the scrolling; an explicit
  scroll-modifier (a trigger/bumper + stick) handles free pixel scrolling when there's no
  focusable target.
- **Drag-to-edge autoscroll** — while dragging an item (or a selection) and the pointer
  reaches the viewport edge, the view **auto-scrolls** in that direction so you can drag
  beyond the visible region.

### 3.4 **Scroll-into-view on focus change** — the critical QoL behavior

When focus (keyboard/D-pad) moves to an element that is **partly or fully outside** the
viewport, the scroll container **automatically scrolls the minimum amount** to bring the
focused element fully into view (usually with a small margin, sometimes centering it). This
is what makes D-pad/keyboard navigation of a long list feel correct — you press Down at the
bottom visible row and the list scrolls one row to reveal the next, keeping the highlight
on-screen. Without it, focus "disappears" off-screen and the UI feels broken. **Every list,
grid, and menu must implement scroll-into-view tied to the focus model.**

### 3.5 Nested scroll regions

- When scroll regions are nested (a scrollable list inside a scrollable page), input scrolls
  the **innermost** region under the pointer/focus first.
- **Scroll chaining / boundary hand-off:** when the inner region reaches its end, continued
  scrolling **chains** to the parent (Android/most web). Some surfaces deliberately
  **trap** scroll (a modal's body should not chain to the page behind it — same spirit as
  focus trapping). The polished rule: **chain by default, trap inside modals.**
- Pointer wheel targets the region **under the pointer**; touch targets the region the
  **gesture started in** (so a flick that began in the inner list keeps scrolling the inner
  list even if the finger drifts).

---

## 4. Pointer vs keyboard/gamepad selection (the reconciliation, in detail)

This expands §0 into concrete behaviors a polished system exhibits:

### 4.1 Two indicators, never both live

- The **pointer hover** indicator (highlight/cursor under the mouse/finger) and the
  **stepped focus** indicator (outline/highlight from D-pad/keyboard) are **mutually
  exclusive on screen**. The active input source decides which is shown. (§0.2–0.4)
- This prevents the "two highlights" confusion where the user can't tell what Enter/A will
  act on.

### 4.2 Discrete stepped focus vs focus-follows-pointer *within* a view

- **Directional source:** focus moves in **discrete steps** between focusable elements
  following a **traversal order** (§4.4). Each press = one neighbor.
- **Pointer source:** the "focus" is wherever the pointer is; moving the pointer over an
  element highlights it (hover), and a press acts on it. The pointer does **not** advance a
  discrete index — it's continuous.
- When the user switches from pointer back to directional, directional navigation resumes
  from the **last directionally-focused element** (each source keeps its own selection,
  §0.5) — *or*, in some designs, from the element currently under the pointer (a deliberate
  "seed focus from pointer" choice). Either is acceptable as long as it is **consistent**.

### 4.3 Per-input tracking

- The focused element for the directional source is stored as an **index/id into the view's
  focusable set** (e.g. "row 4", "the Save button").
- The pointer's target is computed by **hit-testing** the pointer position each frame.
- Activation (Enter/A/click/tap) resolves against the **currently live** indicator's target.

### 4.4 Traversal order (Tab order / spatial navigation)

- **Linear (Tab) order** — controls have an explicit or implicit tab order; Tab goes
  forward, Shift-Tab back; it **wraps** at the ends within the focus scope (and is trapped
  inside a modal, §1.4).
- **Spatial / 2-D directional navigation** — D-pad/arrow navigation picks the **nearest
  focusable neighbor in the pressed direction** (the geometry-aware model consoles and
  10-foot UIs use: "XY focus"). Each focusable element effectively has up/down/left/right
  neighbors; the system computes them from layout or they're authored.
- **Groups** — related controls form a group navigated as a unit (arrow keys move *within*
  a radio group / list; Tab jumps *between* groups). Consoles use bumpers (L/R) to jump
  between tabs/sections and the stick/D-pad to move within.

### 4.5 Console-specific notes (the model Q OS lives in)

- **Switch (Horizon) HOME menu:** primary input is **D-pad/stick stepped focus** with an
  **A = confirm / B = back** spine; **touch** is a fully parallel pointer that, when used,
  drives selection directly. Switching between them is seamless and the highlight tracks
  whichever you last used. Switch 2 adds Joy-Con-2 **mouse** as a third pointer source under
  the same arbiter.
- **PS5:** D-pad/stick stepped focus is primary; **A/✕ = confirm, B/○ = back**; the
  **touchpad** can act as a pointer (drag = move cursor, tap = click) where supported.
- **Xbox:** D-pad/stick stepped focus with **A = confirm, B = back**; bumpers page between
  sections; no pointer in the system UI (controller-only), which is the purest stepped-focus
  model.
- **Universal console confirm/back spine:** **A/✕ = activate/confirm**, **B/○ = back/cancel/
  up-a-level/close**, applied *consistently at every level*. "Back" composes: B in a submenu
  closes the submenu; B again closes the menu; B at the top backs out of the screen. This
  composability is what makes console navigation feel coherent.

---

## 5. Dropdowns, context menus, popovers (light-dismiss surfaces)

These are **transient, light-dismiss** surfaces — distinct from modals (§1.4). Their
canonical behavior:

### 5.1 Open & position

- A dropdown/combobox opens **anchored to its trigger** (below it, flipping **above** if
  there's no room — "auto-flip"); it is clamped to stay on-screen (shift horizontally/
  vertically into view rather than clip).
- A **context menu** opens at the **pointer position** (right-click / long-press) or, for a
  keyboard/controller invocation (Menu key / a dedicated button), **anchored to the focused
  element**.
- Opening **moves focus into the menu** and highlights a sensible first item (the first
  enabled item, or the currently-selected value for a combobox).

### 5.2 Navigate

- **Pointer:** hover highlights items; moving onto a **submenu parent** opens the submenu
  after a short hover delay; click/tap activates.
- **Keyboard/D-pad:** Up/Down move the highlight (wrapping); **Right / A** opens a submenu
  *parent's* submenu (does **not** confirm the parent); **Left / B** closes the submenu back
  to the parent; type-ahead jumps to an item by first letter (desktop). Enter/A on a leaf
  confirms.
- Disabled items are skipped by directional navigation and are not activatable.

### 5.3 Submenus

- Open to the **side** of the parent (right by default, **flipping left** with no room),
  vertically aligned to the parent row. Only **one submenu chain** is open at a time per
  branch; moving to a different parent closes the previous submenu.

### 5.4 Dismiss & focus return

- **Click/tap outside** the menu closes it **without** selecting (light dismiss).
- **Esc / B** closes the current level (submenu first, then the menu) — composable back.
- Selecting an item closes the whole menu chain and runs the action.
- **On close, focus returns to the trigger** (the control that opened the menu), so
  keyboard/controller flow continues from where it was.
- A menu is **not** a modal: it doesn't make the whole app inert, but it **does** capture
  the next click/press for dismissal and traps directional navigation to its own items while
  open.

### 5.5 The "opening tap" pitfall (touch/long-press)

When a menu is opened by a **long-press** (finger still down), the **lift that ends the
long-press must not be treated as a selection** — otherwise the finger's resting position
fires a random item. The correct behavior: open on long-press, **consume the opening lift**,
then accept a **separate** tap to confirm an item. (This is a well-known touch-menu bug class
and every polished touch menu handles it.)

---

## 6. Consolidated QoL checklist (the things people overlook)

A window/UI system is "polished" when **all** of these are true:

1. Exactly **one** selection indicator visible at a time; it follows the **last-used input
   source**; non-directional buttons don't switch the source. *(§0, §4.1)*
2. A small **movement threshold** debounces the pointer↔directional flip. *(§0.6)*
3. Each input source keeps **its own selection**; switching back resumes where you were.
   *(§0.5, §4.2)*
4. An **active/modal window traps input**: nothing behind it is hit-testable or focusable;
   directional traversal wraps inside it; Esc/B dismisses; focus returns to the opener.
   *(§1.4, §1.6)*
5. **Click-to-focus + raise-on-click**, not focus-follows-pointer, for the consumer default.
   *(§1.2)*
6. **Scroll-into-view** on every focus change in a scroll container. *(§3.4)*
7. **All inputs scroll:** wheel, Shift-wheel/horizontal, touch drag **with momentum +
   rubber-band/stretch**, keyboard page/home/end, controller scroll-modifier. *(§3.3)*
8. Scrollbars: consistent **overlay-or-reserved** regime, **thumb drag**, **page-on-track-
   click**, **auto-hide** if overlay, **stable gutter** to avoid layout shift. *(§3.1–3.2)*
9. **Nested scroll** targets innermost first, **chains** to parent at the boundary, **traps**
   inside modals. *(§3.5)*
10. **Resize/maximize/snap** with remembered restore geometry and hysteresis on snap. *(§2.1)*
11. The **window host owns scale + viewport + input-inverse-transform**; content is natural-
    coordinate and scale-agnostic; content declares uniform vs width-bound scaling. *(§2.2–2.3)*
12. **Menus/dropdowns:** anchored + auto-flip + on-screen-clamp; pointer & directional nav;
    submenus open-to-side/flip; **light dismiss** (click-outside / Esc/B); **focus returns
    to trigger**; long-press opening lift is consumed. *(§5)*
13. A **consistent confirm/back spine** (A confirm / B back) that **composes** across nesting
    levels. *(§4.5)*
14. **Dismiss conventions are uniform:** Esc (desktop) / B (console) / click-outside (light-
    dismiss surfaces) all mean "cancel/back," and never accidentally fall through to the
    surface behind. *(§1.4, §5.4)*

---

## 7. Sources

- Microsoft Learn — *FocusVisualKind enum* and *Reveal Focus* (last-input focus-visual model;
  primary/secondary border + glow): https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.focusvisualkind
  and https://learn.microsoft.com/en-us/windows/uwp/ui-input/reveal-focus
- Microsoft Learn — *Programmatic focus navigation with keyboard, gamepad, and accessibility
  tools* (XY focus / spatial navigation):
  https://learn.microsoft.com/en-us/windows/apps/design/input/focus-navigation-programmatic
- W3C WAI-ARIA APG — *Modal Dialog pattern* (focus trap, move-focus-in-on-open, Esc to close,
  return focus to trigger, inert background):
  https://www.w3.org/WAI/ARIA/apg/patterns/dialog-modal/examples/dialog/
- Deque — *Building an Accessible Widget: WAI-ARIA Modal Alert Dialogs* (remove focusability
  of underlying content): https://www.deque.com/blog/aria-modal-alert-dialogs-a11y-support-series-part-2/
- MDN — *scrollbar-gutter* (reserved-gutter vs overlay, stable gutter to prevent layout
  shift): https://developer.mozilla.org/en-US/docs/Web/CSS/Reference/Properties/scrollbar-gutter
- Bram.us — *Prevent unwanted Layout Shifts caused by Scrollbars* (overlay vs classic,
  macOS default "automatically based on mouse or trackpad"):
  https://www.bram.us/2021/07/23/prevent-unwanted-layout-shifts-caused-by-scrollbars-with-the-scrollbar-gutter-css-property/
- TechRadar — *Nintendo Switch 2 Joy-Con 2 mouse controls can be used to navigate the Home
  Menu* (cursor appears on pointer use, stick scrolls — real-world active-input-source model):
  https://www.techradar.com/gaming/the-nintendo-switch-2-joy-con-2-mouse-controls-can-be-used-to-navigate-the-home-menu
- Nintendo UK — *Nintendo Switch HOME Menu Overview* (D-pad/touch parallel navigation, A/B
  spine): https://www.nintendo.com/en-gb/Support/Nintendo-Switch/Nintendo-Switch-HOME-Menu-Overview-1406405.html
- Square Enix — *Can a PS5/PS4/Xbox controller function as a mouse?* (touchpad-as-pointer:
  drag=cursor, tap=click): https://na.finalfantasyxiv.com/uiguide/faq/faq-interface/interface_controller_mouse.html

> General desktop behaviors (z-order, click-to-focus, snap/maximize, wheel/Shift-wheel,
> momentum + rubber-band scrolling, track-click paging, menu auto-flip and light dismiss,
> A/B back composition) are long-standing, cross-implementation platform conventions on
> Windows, macOS, GNOME/KDE, iOS, Android, and the three consoles; the citations above
> anchor the specific, less-obvious claims.
