# 01 — Q OS Current-State Audit (window / input / selection / scroll)

**Status:** audit — read-only; every claim cited to real source.
**Source root:** `src/projects/uMenu/source/ul/menu/qdesktop/` (cpp) and
`src/projects/uMenu/include/ul/menu/qdesktop/` (hpp), plus the layout host
`src/projects/uMenu/source/ul/menu/ui/ui_MainMenuLayout.cpp`.
**Baseline:** working tree as of 2026-06-14 (v3.6-wip). File:line citations reflect that
tree; line numbers drift with edits — function/identifier names are the durable anchors.
**Sibling docs:** `00-RESEARCH.md` (canonical behavior), `02-UNIFIED-SPEC.md` (target),
`03-OVERHAUL-PLAN.md` (migration).

> **Read this against `00-RESEARCH.md` §6.** Q OS already implements a *surprising amount*
> of the polished model — but **only at the desktop layer**, and **re-implemented from
> scratch in every window**. The result is duplicated logic, wasted per-frame compute, and
> three concrete input conflicts the creator reported.

---

## 1. The architecture as it exists today

### 1.1 The pieces (what each component owns)

| Component | File(s) | Owns |
|---|---|---|
| **`QdDesktopIconsElement`** | `qd_DesktopIcons.cpp` (~362 KB), `qd_DesktopIcons.hpp` | The desktop hub. The icon grid + dock + favorites strip, **and** it is the single Plutonium `Element` whose `OnInput` routes nearly all input: window-manager gate, hot-corner dropdowns, context menus, desktop D-pad nav, ZR/touch launch, the **`InputSource` arbiter**. |
| **`QdWindowManager`** | `qd_WindowManager.cpp`, `.hpp` | Owns `open_windows_` (z-order vector, `.back()` = topmost), `minimized_entries_`, `suspended_app_entries_`. `OpenWindow`/`CloseWindow`/`MinimizeWindow`/`RestoreWindow`/`BringToFront`. Input fan-out via `PollWindowEvents`. Two embedded `QdContextMenu`s (`suspended_ctx_menu_`, `minimized_ctx_menu_`). |
| **`QdWindow`** | `qd_Window.cpp` (~77 KB), `qd_Window.hpp` | One window: chrome (titlebar + bottom bar + 4 corner buttons), drag/resize/snap/maximize, **the centralized scale + scroll viewport** (`scroll_x_/y_`, `SetScrollOffset`, `GetViewportSize`, `PaintScrollbars`), touch→content coordinate inverse-transform, controller→content forwarding via `nav_mask`. |
| **`QdContentElement`** | `qd_ContentElement.hpp` | Abstract base for window content. Contract: content paints at **natural 1:1 coords**, host applies scale + scroll + clip; `OnInput` receives **pre-translated content-local touch coords**. Declares `PrefersWidthBoundScale()`. |
| **`QdCursorElement`** | `qd_Cursor.cpp`, `qd_Cursor.hpp` | The software pointer sprite. `current_x_/y_` in 1920×1080 space; `SetCursorPos`, `SetVisible`. **Self-drives from touch** in its own `OnInput` (`qd_Cursor.cpp:326-328`). |
| **Input pump** | `qd_Input.cpp`, `qd_Input.hpp` | `pump_input()` → `PolledFrame` (edge events + held flags + `stick_r_x/y` + multitouch). Host-testable button/touch frame processors. |
| **Stick→cursor driver** | `ui_MainMenuLayout.cpp:1401-1445` | Calls `pump_input`, runs the five-zone velocity curve (`ComputeStickSpeed`), and moves the cursor with the **right stick**. `slow = zr_held` (`:1427`). |
| **Dropdowns** | `qd_HotCornerDropdown.cpp/.hpp`, `qd_HotCornerRightDropdown.cpp/.hpp` | Two near-identical popout menus (left + right hot corner). Each has its own `Open/Render/HandleInput/Close/UpdateHover/TryClickAt/FireHovered`. |
| **Context menu** | `qd_ContextMenu.cpp/.hpp` | A *separate* generic vertical menu w/ one-level submenus, used for dock/folder/window/suspended/minimized context actions. Its own D-pad+touch+mouse `HandleInput`, `Open` (2 overloads), submenu open/flip. |
| **Window-opener bridge** | `qd_DesktopIcons_WmBridge.cpp` | The `OpenXxxWindow()` factory methods: build a layout element, `SetContentSize`, `QdWindow::New(title, elem, …)`, wire `on_minimize_begin_`/`SetPendingReopen`. |

### 1.2 Per-frame input flow (today)

`ui_MainMenuLayout::OnMenuUpdate` (per frame): `pump_input` → stick moves cursor
(`ui_MainMenuLayout.cpp:1416-1444`). Plutonium then routes button/touch events to elements;
in `QDESKTOP_MODE` the authoritative consumer is **`QdDesktopIconsElement::OnInput`**, which
runs (in order):

1. Drain open dropdowns / right-dropdown if open (`qd_DesktopIcons.cpp:4073-4124`, `:5554-5569`).
2. **WM gate**: a block of *bypass* conditions (`goto skip_wm_gate`) for the hot-corner zones
   and the dock band (`:4640-4689`), then — if any window/dock entry exists —
   `wm_.PollWindowEvents(...)`; **return** if it consumed (`:4690-4696`).
3. ZR-up snap commit (`:4698-4719`).
4. Desktop ZR launch (`:4814-4916`), A launch (`:4736-4812`), ZL context menus (`:4973-…`).
5. Desktop D-pad navigation + the `InputSource` latch (`:5571-5600` and the cursor-move
   latch at `:3696-3719`).

This is **one giant `OnInput`** carrying the whole arbitration policy inline.

---

## 2. How each concern is handled today

### 2.1 The `InputSource` arbiter — **already correct, but desktop-only**

Q OS *already implements* the active-input-source model from `00-RESEARCH.md` §0:

- `enum class InputSource { DPAD, MOUSE }` — `qd_DesktopIcons.hpp:276-279`, with the
  transition rules documented in the header comment (`:258-275`): **DPAD** on directional
  press; **MOUSE** on cursor move >4px or touch down; **A/B/X/Y/L/R/ZL/ZR do not change the
  source.** Render semantics: MOUSE → `cursor_ref_->SetVisible(true)` + suppress D-pad ring;
  DPAD → `SetVisible(false)` + show ring.
- Implemented at:
  - directional press → `active_input_source_ = InputSource::DPAD` + hide cursor
    (`qd_DesktopIcons.cpp:5588-5594`);
  - cursor move >4px Manhattan → `MOUSE` + show cursor (`:3707-3717`);
  - touch down → `MOUSE` + show cursor (`:5797-5803`);
  - ZR press → `MOUSE` (ZR is treated as "mouse click", `:4816-4823`).
- Render branches consume it: e.g. `focused = (active_input_source_==DPAD) && …` vs
  `hovered = (active_input_source_==MOUSE) && …` (`:1227-1230`, `:1401`, `:3808-3828`).

**The gap:** this arbiter lives **entirely inside `QdDesktopIconsElement`**. It governs the
**desktop grid / dock / favorites** only. The moment focus is inside a `QdWindow`, the
window's content layouts have **no awareness of `active_input_source_`** — see §3 below.

There is also a **legacy redundant flag**, `last_input_was_dpad_`, set in lockstep with
`active_input_source_` at every transition (`:4820`, `:5584`, `:5796`, `:3712`) and read in
parallel (`:3362` `show_hover_ring = is_mouse_hovered && !last_input_was_dpad_`). It is a
second source of truth for the same fact — dead weight that should collapse into the enum.

### 2.2 Window focus & z-order — solid

- `open_windows_.back()` = topmost; `BringToFront` rotates the target to the back and sets
  `focused_` true on it, false on all others (`qd_WindowManager.cpp:439-460`).
- Opening pushes to back + `BringToFront` (`:108-115`); clicking/activating a window calls
  `BringToFront` (`PollWindowEvents` lines `:696-715`). This is correct **click-to-focus +
  raise-on-click** (`00-RESEARCH.md` §1.2).
- `QdWindow::IsFocused()` / `SetFocused()` track element-level "this window is active." The
  header notes this is intra-desktop stack focus, distinct from libnx `AppletFocusState`
  (`qd_Window.hpp:351-358`).

### 2.3 The window viewport / scale / scroll — **centralized and good**

This is the one concern that *is* properly centralized:

- `QdWindow` owns the scale transform and viewport; content is passive natural-coords
  (`qd_ContentElement.hpp:7-23`). Host applies `SDL_RenderSetScale` + clip; content must not
  (`qd_Window.hpp:257-289`).
- Scroll viewport: `scroll_x_/y_`, `SetScrollOffset` (clamped + fires `on_scroll_update`),
  `GetViewportSize` (subtracts scrollbar strips), `PaintScrollbars` (`qd_Window.hpp:275-322`,
  `:423-450`).
- Scroll input the window already supports: **VSB/HSB thumb drag** (`vsb_drag_active_`,
  `hsb_drag_active_`), **finger drag-scroll over content** with a jitter threshold
  (`content_drag_*`, `qd_Window.hpp:439-450`), and **ZL+D-pad pixel scroll**
  (`qd_Window.cpp:1169-1179`).
- Content scaling modes: uniform-scale default, width-bound opt-in via
  `PrefersWidthBoundScale()` (`qd_ContentElement.hpp:105-118`).

**Gaps vs `00-RESEARCH.md` §3:** no **scroll-into-view on focus change** (§3.4) — because
the window doesn't know the content's focused element; no **momentum / rubber-band** on
finger flick (§3.3); **track-click paging** on the scrollbar is not implemented (only thumb
drag); scrollbars are a fixed 6px strip (`kScrollbarW`, `qd_Window.hpp:339`) that is **always
reserved when overflowing** but does **not** auto-hide (a reasonable choice, but undocumented
as a policy). No mouse-**wheel** scroll because there is no wheel on Switch — the stick is the
analog, but the stick is bound to the **cursor**, not to scroll, so a focused window has no
free-scroll input except the ZL+D-pad fallback.

### 2.4 Window chrome interaction — rich

Drag (origin-delta, `dragging_titlebar_`), double-tap-title → maximize
(`last_titlebar_tap_tick_`, `qd_Window.hpp:369-373`), corner buttons (TL close / TR maximize /
BL minimize / BR resize, relocated into a bottom bar `qd_WmConstants.hpp:55-65`), ZR
cursor-drag + resize-drag with watchdog (`resize_no_touch_frames_`), snap zones with
hysteresis (`SNAP_EDGE_THRESHOLD`/`SNAP_RESTORE_BAND`, `qd_WmConstants.hpp:77-85`),
`ResetInteractionState()` safety valve (`qd_Window.hpp:163-167`). All matches
`00-RESEARCH.md` §2.1.

### 2.5 Dropdowns / context menus — three overlapping implementations

- `QdHotCornerDropdown` and `QdHotCornerRightDropdown` are **two separate classes** with the
  same lifecycle surface (`Open/Render/HandleInput/Close/UpdateHover/TryClickAt/FireHovered/
  SetSkipFirstLift`) — see `qd_HotCornerDropdown.hpp:54-101`. The right one is the larger
  file (`qd_HotCornerRightDropdown.cpp`, ~40 KB). They differ only in items/anchor.
- `QdContextMenu` is a **third** menu implementation (generic, with one-level submenus,
  keybind hints, disabled rows) — `qd_ContextMenu.hpp:85-161`. Used for dock/folder/window/
  suspended/minimized context actions.
- All three independently re-implement: panel geometry + on-screen clamp, **mouse hover
  tracking** (`UpdateHover` + `prev_cursor_x_/y_` short-circuit), **touch fire-on-release**
  with the **long-press opening-lift consume** (`SetSkipFirstLift`/`skip_first_lift_` —
  `00-RESEARCH.md` §5.5), **outside-tap dismiss arming** (`armed_for_outside_close_`), and
  D-pad nav. The `SetSkipFirstLift` BUG-7 fix and the `armed_for_outside_close_` release-
  arming fix had to be written **twice** (dropdowns) **and** again in `QdContextMenu`.

This is the single biggest *literal* code duplication in the subsystem.

### 2.6 The dead focus-surface model

`qd_WmConstants.hpp:127-210` defines a complete formal focus model — `enum class
FocusSurface { Desktop, Dock, Window, CommandPanel, Cursor }`, `FocusLevel { Screen, Window }`
tagged union, and `FocusElement` with a 4-way neighbor adjacency table (`-1` sentinel). **It
is entirely unused** — a repo-wide search finds **zero** references to `FocusSurface`,
`FocusLevel`, or `FocusElement` outside their own header. The intended single "which surface
owns input" arbiter was specced and never wired. Its absence is *why* arbitration is
scattered across ad-hoc gates (§3.3).

---

## 3. The per-window DUPLICATION / CENTRALIZATION MAP

Every window's content is a `QdContentElement` subclass. Each one **re-implements its own
selection, D-pad navigation, and confirm/back handling** from scratch. None of them use a
shared selection model, and **none are `InputSource`-aware** (verified: a search for
`active_input_source` / `InputSource::` / `mouse_hover` / `last_input_was_dpad` across all
`qd_*Layout.cpp` returns **nothing**).

### 3.1 Distinct, independently-managed selection/focus fields (one per concept, per file)

| Window layout | Own selection/focus index fields (sampled) | Own scroll? | D-pad nav | A/B handling | InputSource-aware? |
|---|---|---|---|---|---|
| `qd_SaveEditorLayout` | `box_list_sel_`, `box_slot_sel_`, `bag_sel_`, `party_focus_`, `panel_focus_`, `title_focus_`, `prev_focus`, `game_index` | host | yes (`:1385-1422`) | yes | **no** |
| `qd_SettingsLayout` | `tab_idx`, `sidebar_focus_row_`, `detail_row_`, `row_idx`, `value_idx`, `lang_idx`, `label_idx`, `disp_cal_idx_` | host | yes (`:1532-1581`) | yes | **no** |
| `qd_CheatsLayout` | `title_focus_`, `cheat_focus_`, `active_title_idx_`, `detail_last_focus_` | host | yes (`:773-815`) | yes | **no** |
| `qd_VaultLayout` | `focus_idx_`, `sidebar_idx_` (+ key-repeat via `repeat_up/down`) | host | yes (`:1814-1836`) | A **and** ZR both confirm (`:1865`) | **no** |
| `qd_NintendoAppsLayout` | `album_sel_`, `hovered_idx_` | host | yes (`:646-664`) | yes | **no** (has `hovered_idx_` but not arbiter-driven) |
| `qd_TaskManagerLayout` | `hovered_row_`, `row_idx`, `can_focus` | **OWN `scroll_y_`** (`:254`, `:608`, `:634-638`) | yes | yes | **no** |
| `qd_MonitorLayout` | `tile_idx`, `res_row_` | host | minimal | B-only (`:812`) | **no** |
| `qd_AboutLayout` | — (static card) | host | none | B-only (`:662`) | **no** |
| `qd_LockscreenLayout` | — | host | mask gate (`:368`) | A/B mask | **no** |

That is **25+ distinct selection/focus index variables** across the windows, each with its
own bounds logic, its own Up/Down handlers, and its own confirm path — solving the **same
problem** nine different ways.

### 3.2 What is duplicated (and what it costs)

| Duplicated logic | Where it repeats | Cost |
|---|---|---|
| **Selection index + bounds + Up/Down stepping** | every layout in §3.1 | N copies to maintain; a fix (e.g. wrap-at-end, skip-disabled) must be applied N times. |
| **Confirm/back (A / B) handling** | every layout | inconsistent: Vault confirms on **A *and* ZR** (`qd_VaultLayout.cpp:1865`); SaveEditor treats **Y *or* B** as back (`:1397`, `:1422`); others A/B only. No single back-spine. |
| **Mouse hover tracking** | desktop grid + all 3 menus (`prev_cursor_x_/y_` short-circuit pattern repeated) | repeated per-frame hit-tests; repeated "stationary cursor" guards. |
| **Touch fire-on-release + long-press lift consume** | `QdHotCornerDropdown`, `QdHotCornerRightDropdown`, `QdContextMenu` (`SetSkipFirstLift`) | the *same* BUG-7 fix authored 3×. |
| **Outside-tap dismiss arming** | all 3 menus (`armed_for_outside_close_`) | same release-arming fix 3×. |
| **Panel geometry + on-screen clamp** | all 3 menus | same clamp math 3×. |
| **Per-frame hover update on all windows** | `PollWindowEvents` calls `UpdateHoverForCursor` on **every** open window every frame (`qd_WindowManager.cpp:628-630`) | O(windows) hover hit-tests each frame even when the source is DPAD and no hover indicator is shown — wasted compute the arbiter could gate. |
| **Scroll re-implementation** | `qd_TaskManagerLayout` keeps its **own** `scroll_y_` while the host `QdWindow` already provides a full scroll viewport | two scroll systems fighting in one window; the host's VSB and the layout's `scroll_y_` are not the same offset. |

**Compute angle (the creator's "wasted compute"):** the hover walk over all windows every
frame (`:628-630`) runs regardless of input source; the menus each run their own per-frame
hover/clamp; the legacy `last_input_was_dpad_` duplicates the enum's work. A single arbiter
that *gates hover work to MOUSE frames only* and a single menu/selection component eliminate
most of this.

---

## 4. The three reported conflicts — root-caused

### CONFLICT 1 — **A vs R collide** (`A` activate vs `R`/`ZR` click)

**What's happening in code.** Q OS has **two parallel "activate" verbs bound to two
different selection systems**, and `ZR` is additionally **triple-overloaded**:

- **`A`** = "activate the **D-pad**-focused thing." Desktop: launches `dpad_focus_index_` /
  `fav_strip_focus_index_` (`qd_DesktopIcons.cpp:4736-4812`). Inside a window: `A` is in the
  window's `nav_mask` and is forwarded to content (`qd_Window.cpp:1204`, `:1208-1211`), where
  each layout activates **its own** selection index.
- **`ZR`** = "activate the thing under the **cursor**" — explicitly "mouse button pressed,"
  and it **forces `active_input_source_ = MOUSE`** (`qd_DesktopIcons.cpp:4815-4823`). It
  drives cursor-targeted launches, dropdown `TryClickAt`, window corner-button activation,
  and window cursor-drag (`qd_WindowManager.cpp:687-706`).
- **`ZR` is also the cursor "slow mode" modifier** (`ui_MainMenuLayout.cpp:1427`
  `slow = zr_held`) — so holding ZR to fine-aim the cursor and pressing ZR to click are the
  **same physical control**.
- **`R` (bumper, `HidNpadButton_R`, distinct from `ZR`)** is *also* in the window `nav_mask`
  (`qd_Window.cpp:1206`) and forwarded to content with no defined semantic.

So "activate" is split across **A (D-pad selection)** and **ZR (cursor selection)**, plus the
Vault makes it worse by accepting **A *and* ZR as the same confirm** (`qd_VaultLayout.cpp:1865`)
— meaning in that window the two systems' activate verbs both fire the *same* (D-pad) target,
while everywhere else they fire *different* targets. The collision the creator feels is: **A
and the R-trigger family sometimes do the same thing, sometimes different things, depending on
which window has focus and which input source is live — with no single rule.** The fix
(`02-UNIFIED-SPEC.md` §4) is to make activate **resolve against the active input source's own
selection** and define one role per button.

### CONFLICT 2 — **Mouse/pointer vs D-pad selector collide**

**What's happening in code.** The arbiter that *should* make these mutually exclusive
(`active_input_source_`) **exists but only governs the desktop** (§2.1). The problems:

- **Inside windows there is no arbiter at all.** Content layouts are D-pad-only and also
  hit-test pre-translated touch; they never consult `active_input_source_`. So a window can
  show a D-pad selection highlight **and** react to a cursor hover/click with no coordination
  — two selections, no mutual exclusion (`00-RESEARCH.md` §4.1 violated inside windows).
- **Two sources of truth** for the desktop arbiter (`active_input_source_` *and*
  `last_input_was_dpad_`) can disagree if any future edit sets one without the other.
- **The flip threshold is desktop-only** (the 4px Manhattan check at
  `qd_DesktopIcons.cpp:3707-3710`); windows have no equivalent debounce.
- **Cursor visibility is toggled from many sites** (`SetVisible` called at `:4822`, `:5592`,
  `:5802`, `:3714`, plus Home-recenter in `ui_MainMenuLayout.cpp:1584`) rather than being a
  pure function of the arbiter state — easy to desync.

Net: the model is *right* but **not global and not single-sourced**, so within windows and at
the seams the two selectors are not actually mutually exclusive.

### CONFLICT 3 — **Focus leak: you can select things OUTSIDE the active window**

**What's happening in code.** There is **no true focus trap**. `PollWindowEvents` consumes
input **only when an event geometrically hits a window** and otherwise **returns false and
lets desktop handling run**:

- A focused window forwards & consumes **directional + A/B** *only when* `focused_ && content_`
  (`qd_Window.cpp:1168`, returns true at `:1211`). So D-pad nav is trapped **when a window has
  content focus** — good — but this is the *only* trap, and it's per-window-content, not a
  system gate.
- **`ZR` over empty desktop falls through on purpose**: "ZR over empty desktop — not consumed
  here; fall through so caller handles" (`qd_WindowManager.cpp:707`). After
  `PollWindowEvents` returns false, the desktop's ZR-launch path
  (`qd_DesktopIcons.cpp:4814-4916`) fires on whatever cell is under the cursor — **even with a
  window open**. So a **pointer click outside the window launches desktop icons behind it**.
- The **dock and hot corners are *deliberately* exempted** from the WM gate via
  `goto skip_wm_gate` (`qd_DesktopIcons.cpp:4640-4689`) — intentional (taskbar-style always-
  reachable chrome), but it means "trapping" is a patchwork of explicit bypasses rather than a
  rule.
- A historical comment notes the desktop **A/launch behind windows** symptom was patched by
  trusting "PollWindowEvents already consumes clicks INSIDE an open window's bounds"
  (`qd_DesktopIcons.cpp:4744-4751`) — i.e. the design relies on *geometric* containment, not on
  a *focus-owns-input* rule. Anything **outside** the window's rectangle (empty desktop, the
  grid behind it) is therefore still live.

Root cause: the **dead `FocusSurface` model (§2.6)** was supposed to be the single arbiter
("Window owns input → Desktop/Dock get nothing unless explicitly chrome"). Because it was
never wired, "focus" is inferred from geometry per-call-site, and the gaps (empty-desktop ZR,
non-modal windows) leak. The fix (`02-UNIFIED-SPEC.md` §5) is a real focus owner + an
inert-background rule for active/modal windows.

---

## 5. Summary of findings

- The **active-input-source model is already implemented and basically correct** — but
  **scoped to the desktop only**, with a **redundant second flag** (`last_input_was_dpad_`).
- The **window host (`QdWindow`) already centralizes scale + scroll + viewport + input
  inverse-transform** — the hardest part — but is **missing scroll-into-view, momentum, and
  track-click paging**, and one window (`TaskManager`) **bypasses it with its own scroll**.
- **Selection/navigation is duplicated 9×** across window layouts (25+ bespoke index fields),
  none arbiter-aware, with an **inconsistent confirm/back spine**.
- **Three menu implementations** (`QdHotCornerDropdown`, `QdHotCornerRightDropdown`,
  `QdContextMenu`) re-implement the same hover/touch/dismiss/clamp logic, including the same
  bug-fixes 3×.
- A **complete formal focus model exists as dead code** (`FocusSurface`/`FocusLevel`/
  `FocusElement`) — the missing spine that would resolve all three conflicts.
- **The three conflicts** all reduce to the same architectural absence: **no single,
  global input-arbitration owner.** A/R collide because "activate" isn't bound to "the active
  source's selection"; pointer↔D-pad collide because the arbiter isn't global; focus leaks
  because there's no focus-owns-input gate (only geometry).
