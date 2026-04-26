# K+3 Edit Mode + K+4 LRU Recents — Design SSOT

> **Authoring SSOT:** this file
> **Drafted:** 2026-04-25T17:50:00Z
> **Status:** Design only. Implementation gated on creator approval AND on K+1+K+2 landing first.
> **Cross-refs:** `K+1-FOLDERS-CATEGORIES-DESIGN.md`, `K+2-SETTINGS-FILTER-CHAIN-DESIGN.md`,
> `qd_DesktopIcons.cpp` (HitTest, FocusedSlot, MoveTo gestures),
> `qd_Launchpad.cpp` (frame_tick_ for animation timing).

---

## Why this exists

After K+1 (folders) and K+2 (settings + filters), the user needs a way to actually
**arrange** their desktop. iPhone's "long-press → wiggle mode" pattern is the
established UX for touch-grid editing. K+3 brings it to Q OS uMenu.

K+4 (LRU recents) is small enough to include here because:
- The persistence layer (`per-app-prefs.bin` schema) is defined in K+2.
- The launch hook is one function in `QdDesktopIconsElement::LaunchIcon`.
- The Launchpad section is one new `LpSortKind::Recent` value.

K+3 is what users will FEEL first; K+4 is what they'll NOTICE in week two.

---

# K+3 — Long-press Edit Mode

## Trigger

Two equivalent triggers (both required for accessibility):

1. **Touch:** long-press any non-builtin desktop icon for ≥ 800 ms.
2. **D-pad:** focus an icon (existing focus ring), press `Plus + Y` simultaneously.

On trigger:
- Enter edit mode globally (one shared mode flag in `QdDesktopIconsElement`).
- Icon "wiggle" animation begins on every entry (sine-wave rotation ±2°, period 1.0s,
  driven by `frame_tick_` already incremented in `OnRender`).
- A red "−" badge appears in the top-left of every removable entry (Special and
  Builtin icons get NO badge — they can't be removed).
- The status bar at the bottom of the screen swaps from app shortcuts to
  `[A: Drag/Drop] [Y: Properties] [B: Done] [Plus: Restore Defaults]`.

## State machine

```
NORMAL ──long-press / Plus+Y──→ EDIT_MODE
                                    │
                                    ├──A on icon──→ DRAGGING (with picked icon following touch/cursor)
                                    │                    │
                                    │                    ├──A on empty slot──→ EDIT_MODE (icon dropped at slot)
                                    │                    ├──A on another icon──→ "Create folder?" prompt → EDIT_MODE
                                    │                    ├──A on existing folder──→ adds to folder → EDIT_MODE
                                    │                    └──B──→ EDIT_MODE (cancel drag, icon returns to origin)
                                    │
                                    ├──Y on icon──→ PROPERTIES_SHEET (rename / recolor / hide / set glyph)
                                    ├──"−" badge tap──→ "Hide this entry?" prompt → EDIT_MODE
                                    ├──Plus──→ "Restore default layout?" prompt → NORMAL with reset prefs
                                    └──B──→ NORMAL (commit changes to per-app-prefs.bin)
```

`EDIT_MODE` exit always commits any layout/visibility changes to `per-app-prefs.bin`
in one atomic write (write-to-tmp + rename pattern). No mid-edit-mode persistence.

## Drag-reorder semantics

Each entry has a `grid_slot_index` (computed at scan time from its position in the
`icons_` array, mapped via `(slot % cols, slot / cols)` to grid x/y). Drag-reorder
mutates this index:

```cpp
void QdDesktopIconsElement::SwapIcons(size_t src_slot, size_t dst_slot) {
    if (src_slot == dst_slot) return;
    if (src_slot >= icon_count_ || dst_slot >= icon_count_) return;

    // Capture src icon
    NroEntry tmp = icons_[src_slot];

    // Shift other icons toward src position
    if (src_slot < dst_slot) {
        for (size_t i = src_slot; i < dst_slot; ++i) {
            icons_[i] = icons_[i + 1];
        }
    } else {
        for (size_t i = src_slot; i > dst_slot; --i) {
            icons_[i] = icons_[i - 1];
        }
    }
    icons_[dst_slot] = tmp;

    // Mark dirty for re-render and persist on EDIT_MODE exit.
    layout_dirty_ = true;
}
```

A "shadow icon" (semi-transparent ghost) renders at the cursor position during drag.
The original slot keeps a 30% alpha placeholder so the user can see where the icon
came from (in case they cancel with B).

## Drop-on-icon → folder creation

Per K+1 design, dropping icon A onto icon B:
1. Inflates a "Create folder?" dialog with auto-suggested name (lowest common
   category — "Homebrew" if both Nro, etc.).
2. On confirm: removes A and B from `icons_`, inserts a synthetic
   `NroEntry { kind = IconKind::Folder, members = [A, B], … }` at B's grid slot.
3. Updates `folders.json` in the same atomic-write sequence as per-app-prefs.bin.
4. Returns to EDIT_MODE — user can keep editing.

Cancel returns A to its origin slot, no folder created.

## Properties sheet (Y on icon)

Modal sheet (Plutonium Dialog) with rows:
- **Rename** (text input) — overrides display name; persists to `per-app-prefs.bin`
  via a new optional `display_name[64]` field.
- **Color** (8-color picker from brand palette) — overrides the bg color; persists
  via new `bg_override_rgb[3]` + `has_bg_override` bit.
- **Glyph** (single-char input) — overrides ASCII glyph; persists via `glyph_override`.
- **Hide from desktop** (toggle) — sets per-app-prefs `flags & 0x01`.
- **Pin to dock** (toggle, with slot picker if EnableFavorites=true) — sets
  `flags & 0x04` + `dock_slot`. Pinning to a slot displaces whatever was there.

Cancel: closes sheet, no changes. Confirm: writes to per-app-prefs.bin (tmp+rename)
and reloads icons.

## Wiggle animation math

Driven by `frame_tick_` from existing render loop. Per-icon phase offset based on
slot index so icons don't all wiggle in unison (looks more lively):

```cpp
// In PaintIconCell, when in EDIT_MODE:
const float phase = (float)frame_tick_ / 60.0f * 2.0f * M_PI;  // 1 Hz
const float per_icon_offset = (float)slot * 0.5f;              // ~30° between icons
const float angle_deg = sinf(phase + per_icon_offset) * 2.0f;  // ±2° amplitude

// Apply rotation to the icon's bg_rect when blitting.
SDL_RenderCopyEx(r, icon_tex_[slot], NULL, &bg_rect, angle_deg, NULL, SDL_FLIP_NONE);
```

SDL2 has GPU-accelerated rotation via `SDL_RenderCopyEx`, so this is ~1ms additional
per frame at 60fps for ~50 icons. No allocation. No state.

## Anti-stub gates for K+3

1. **Properties sheet** — three of the four rows (Rename/Color/Glyph/Hide) are
   trivial-to-implement. "Pin to dock" requires K+2's per-app-prefs flag layer,
   so K+3 can ship Rename/Color/Glyph/Hide without it; the sheet just doesn't show
   the Pin row when EnableFavorites=false.
2. **Folder creation prompt** — depends on K+1 landing. K+3 ships without folder
   creation if K+1 hasn't shipped — the drop-on-icon case shows "Cannot drop here"
   notification.
3. **Atomic write** — `per-app-prefs.bin.tmp` then `rename(tmp, final)` MUST be
   implemented correctly; partial writes corrupt the user's layout. Test path:
   write 1MB of garbage to .tmp, kill process mid-write, verify final still loads.

---

# K+4 — LRU Recents

## Trigger (passive)

Every time `QdDesktopIconsElement::LaunchIcon(slot)` fires, before delegating to
the actual launch path:

```cpp
void QdDesktopIconsElement::LaunchIcon(size_t slot) {
    // K+4: LRU tracking
    if (g_GlobalSettings.GetConfigBool(ConfigEntryId::EnableRecents)) {
        per_app_prefs_.UpdateLRU(icons_[slot]);  // ns timestamp + ++count
        per_app_prefs_.WriteToDisk();             // atomic .tmp+rename
    }

    // Existing launch dispatch
    DispatchLaunch(icons_[slot]);
}
```

Cost per launch: one disk write (~few KB of `per-app-prefs.bin`). User won't notice;
adds ~5-15ms to launch latency. Acceptable.

## Surface in Launchpad

Add `LpSortKind::Recent = 0` (insert at front, shifting other values up by 1):

```cpp
enum class LpSortKind : u8 {
    Recent      = 0,  // K+4: top section, populated only when EnableRecents=true
    Application = 1,
    Nro         = 2,
    Builtin     = 3,
};
```

Section header: "Recent" (rendered above Applications when non-empty).

Population rule in `Open()`:
- Take top N entries by `last_launched_ns` DESC (N defaults to 8, configurable
  via a new ConfigEntryId `RecentsCount` in K+4.1 if creator wants).
- Skip entries with `last_launched_ns == 0` (never launched).
- If less than 4 recents exist, hide the section entirely (don't show empty "Recent"
  header).
- Recents are **duplicated** in the Launchpad list — they appear in their normal
  category section AND in the Recent section. This matches iPhone Spotlight behavior.

In the desktop grid: recents do NOT appear separately. They're just where the user
put them. (The Launchpad's Recent section is for findability; the desktop is the
user's curated arrangement.)

## Privacy

`EnableRecents` defaults to `false`. The user opts in via QSettings.

When toggled OFF:
- All `last_launched_ns` and `launch_count` fields in `per-app-prefs.bin` reset to
  0 on the next write.
- The Launchpad Recent section disappears.
- The launch hook still calls `UpdateLRU` but it's a no-op.

When toggled ON:
- Tracking begins on the next launch.
- No retroactive data — first launches fill in.

## Anti-stub gates for K+4

1. **`RecentsCount` ConfigEntryId** — designed-not-implemented in K+4. Default 8 is
   hardcoded for K+4.0. Adding the picker comes in K+4.1 only if creator asks.
   Per anti-stub: don't add the enum value to ConfigEntryId until the picker exists.
2. **Reset semantics** — when EnableRecents flips OFF, the "next write" must
   actually happen. If nothing else triggers a per-app-prefs write, the data sits
   there indefinitely. Solution: toggle-OFF triggers an immediate write that
   zeroes the LRU fields. Implement in QSettings handler, not buried in the toggle.
3. **`launch_count` overflow** — u32 max = ~4 billion. At 1 launch/sec for 100
   years = ~3 billion. Acceptable; no overflow guard needed for K+4.

---

## Implementation order

```
K+1 (folders + categories) ──┐
                              ├──→ K+3 (edit mode uses both)
K+2 (settings + filters) ────┤
                              └──→ K+4 (uses K+2 per-app-prefs schema + K+2 EnableRecents toggle)
```

K+3 and K+4 can ship in the same release once K+1+K+2 are stable. K+3 is the larger
implementation (state machine + drag handling + properties sheet); K+4 is small
(launch hook + Launchpad section).

Estimated implementation effort (sonnet-agent hours):
- K+1: ~6-8 hours (categories ~2h, folders ~6h)
- K+2: ~4-6 hours (config schema ~1h, filter chain ~2h, QSettings rows ~3h)
- K+3: ~6-8 hours (state machine ~2h, drag rendering ~2h, properties sheet ~3h, persistence ~1h)
- K+4: ~2-3 hours (launch hook ~1h, Launchpad section ~1h, settings toggle wiring ~1h)

Total K+1..K+4: ~18-25 hours. Spread across 3-5 swarmed agents = ~6-10 hours wall
clock if parallelized cleanly.

---

## Open questions for creator

1. **Long-press duration** — 800ms feels right (matches iPhone). Faster (500ms) is
   more responsive but risks accidental triggers; slower (1.2s) is safer.
2. **Drag visual** — semi-transparent ghost at cursor, or actual icon detached from
   grid following cursor? (Suggest ghost; cleaner.)
3. **Shake-to-undo** — iPhone has it. Does Switch joycon shake detection work via
   libnx HID? If yes, worth adding for "I made a mistake" recovery in EDIT_MODE.
4. **K+3 ships before K+4 or together?** — Together makes one big "Customizable
   Desktop" release. Separate makes EDIT_MODE testable without LRU complexity.
5. **Restore Defaults** — full reset (wipes folders, layout, prefs) or layout-only?
   Strong opinions either way.

---

## Cross-references

- `K+1-FOLDERS-CATEGORIES-DESIGN.md` — folder creation drop target
- `K+2-SETTINGS-FILTER-CHAIN-DESIGN.md` — per-app-prefs.bin schema (K+3 + K+4 both
  read/write this file)
- `qd_DesktopIcons.cpp` — HitTest, LaunchIcon, MoveTo (extend here)
- `qd_Launchpad.cpp` — RebuildFilter, SectionLabel (K+4 adds Recent section)
- `qd_SettingsLayout.cpp` — EnableRecents toggle row (K+2 + K+4)
