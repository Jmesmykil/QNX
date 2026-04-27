# F-Plan — uMenu stabilize-5

**Version target:** v1.7.0-stabilize-5
**Base:** v1.7.0-stabilize-4 (last known HW-green candidate)
**Author:** F-Plan agent (read-only investigation pass)
**Date:** 2026-04-26
**Mandate:** Plan only. No edits. All evidence is file+line citations from source.

---

## Investigation summary

All four features were fully traced from source. The struct size pins (NroEntry
1632 bytes, LpItem 1632 bytes) are respected in every plan below — no feature
extends those structs. The K+1 Phase 1 category infrastructure is confirmed
fully implemented and not duplicated here.

---

## P1 — Hot-corner Q glyph restore

### 1. Current state

Two `#if 0` blocks exist that together constitute the deferred hot-corner visual:

**Block A — qd_Launchpad.cpp lines 624–645** (Launchpad overlay render path):
```
#if 0  // F1 (stabilize-4): "Q" glyph placeholder deferred
       // — Plutonium's RenderText crashes on certain font metrics …
       // Defer until a dedicated romfs glyph asset is authored for the hot-corner.
```
This block calls `pu::ui::render::RenderText("Q", white)` and blits the resulting
texture centered within the 96×72 hot-corner rect. The `#if 0` was added because
Plutonium's RenderText is called on each frame with a `static SDL_Texture*` —
a cold-init pattern that is fine. The stated crash concern is conditional on "certain
font metrics" but no reproduction is documented.

**Block B — qd_DesktopIcons.cpp lines 1729–1784** (desktop background render path):
```
#if 0  // F2 (stabilize-4): 4-dot grid placeholder deferred.
       // The existing dark fill + white border paints OVER the top-left desktop icons
       // (alpha bleed), and the dot grid causes visible 60 Hz flicker.
```
This block renders a dark rounded rect (0x10,0x10,0x14,0xC0) with a white-alpha
border and a 2×2 dot grid (12×12 px dots, 6 px gap). The actual root problem is
identified in the comment: the desktop background pass runs while icon cells are
active and the `SDL_BLENDMODE_BLEND` alpha compositing bleeds the hot-corner fill
into underlying icon cells.

**Launchpad hot-corner background (active, not gated):**
qd_Launchpad.cpp lines 609–623 paint a lighter-fill rect + accent border
for the hot-corner every frame while the Launchpad is open — this visual IS live.
The missing element is only the "Q" glyph inside it.

**Desktop hot-corner (fully gated — Block B):**
When the desktop is showing (Launchpad closed), the hot-corner has zero visual
affordance beyond the raw topbar region (neither fill nor glyph).

**Hit-test:** Always fires regardless of either `#if 0` block. Both Open and
OnInput handle the tap; the `#if 0` gates are visual only.

### 2. Target state v1

**Launchpad overlay (Block A):** Re-enable the "Q" glyph render. The static
texture pattern is safe. The font-metric crash note is speculative — the same
`RenderText` call works correctly for the search-bar caret and folder tile labels
throughout OnRender. No crash evidence exists in source.

**Desktop hot-corner (Block B):** Render a simple opaque rect (no alpha blend)
drawn last in the desktop OnRender chain so there is no bleed. Drop the dot grid
entirely for stabilize-5; render only the "Q" glyph (same RenderText pattern as
Block A) over the opaque fill. The dot grid is a future enhancement.

**Visual spec v1:**
- 96×72 px top-left rect
- Fill: BLENDMODE_NONE, solid (0x10, 0x10, 0x14) — same as existing lighter-rect
  logic but with NONE not BLEND, so no alpha bleed
- Right and bottom edges: 1px accent-colour line (same as the live Launchpad path)
- Centered "Q" glyph: white, DefaultFontSize::Medium, cached in `static SDL_Texture*`
  (identical to Block A's existing pattern)

The flicker concern (Block B comment) is resolved by rendering the hot-corner AFTER
the icon loop in the desktop OnRender chain. The existing desktop OnRender already
renders the hot-corner last (code comment at line 1721 confirms this ordering).
The real bleed source was `SDL_BLENDMODE_BLEND` being set when the dark fill was
drawn; switching to `SDL_BLENDMODE_NONE` for the fill rect eliminates the bleed.

**Mac aesthetic note:** A small "Q" or the Q OS logo glyph on a dark pill is the
v1 representation. A proper squircle asset goes in v2 (P1 v2, see section 6).

### 3. Implementation plan

**File:** `qd_Launchpad.cpp`
1. Remove `#if 0` / `#endif` wrapping lines 624–645 (Block A).
2. The `static SDL_Texture *hc_tex = nullptr;` cold-init with `RenderText("Q",
   white)` is the correct pattern — no change to the body.

**File:** `qd_DesktopIcons.cpp`
3. Remove `#if 0` / `#endif` wrapping lines 1729–1784 (Block B).
4. Replace the `SDL_BLENDMODE_BLEND` fill call inside the block with
   `SDL_BLENDMODE_NONE` for the fill rect (the original bleed root cause).
5. Remove the 2×2 dot-grid sub-block entirely — replace with the same
   `static SDL_Texture *hc_tex` / `RenderText("Q", white)` pattern from Block A.
6. The right-edge and bottom-edge accent lines remain.

**No new files.** No struct changes. No romfs assets required for v1.

### 4. LOC estimate + assets

- qd_Launchpad.cpp: remove 2 lines (`#if 0` / `#endif`), no other change = **−2 lines**
- qd_DesktopIcons.cpp: remove 2 gate lines, modify fill call (1 line), delete
  dot-grid block (~15 lines), add RenderText glyph block (~15 lines) = **net 0 lines
  diff, ~20 lines touched**
- Total effective change: ~22 lines across 2 files
- Assets needed: none for v1

### 5. Risks / open questions

**Risk R1.1 — RenderText font metric crash (Block A comment):**
The crash is undocumented. The identical pattern is used without issue for the
search-bar caret and folder tile labels. Confidence: low that it will recur.
Mitigation: the `if (hc_tex)` null-guard in the existing Block A code already
suppresses any rendering failure silently. If the crash appears, the `#if 0`
can be restored in one line.

**Risk R1.2 — Desktop hot-corner render ordering:**
The comment at desktop line 1721 states "Renders LAST". If the ordering has
drifted and icons now render after the hot-corner, the solid fill will be
covered. Verify by checking the desktop OnRender call order at link time.
Mitigation: add a log line confirming hot-corner paint in the first boot after
this lands.

**Open question OQ1.1:** Should the glyph be "Q" (ASCII letter) or the Logo.png
asset that already exists at `romfs/Logo.png`? Logo.png is available via
`TryFindLoadImage` with no new code. This is deferred to v2 per section 6.

### 6. Defer to v2

- Replace ASCII "Q" with `romfs/Logo.png` loaded via `TryFindLoadImage("Logo")`
  (same call as Special entry icons). Asset is already present.
- Add squircle mask / rounded corner geometry to the hot-corner fill.
- Animate the hot-corner on hover (brightness boost on mouse-over).

---

## P3 — Auto-categorized folder strip (re-enable deferred strip)

### 1. Current state

K+1 Phase 1 (categories) is **fully implemented and live:**
- `IconCategory icon_category` is already a field in NroEntry (confirmed
  qd_DesktopIcons.hpp line 114).
- `LpSortKind sort_kind` is already a field in LpItem (qd_Launchpad.hpp line 156).
- `Open()` maps IconCategory → LpSortKind and calls stable_sort (qd_Launchpad.cpp
  lines ~100–180).
- K+1 Phase 1 pre-warm warms first 60 items after sort (confirmed line ~190).

**Auto-folder tile strip (visual layer): GATED at two locations:**

(a) `qd_Launchpad.cpp` lines 723–778 (OnRender section 3.5): entire tile strip
    render in `#if 0`. Comment:
    ```
    v1.7.0-stabilize-3: auto-folder tile strip deferred to v1.7.1 K+1 phase 2.
    Cause: v1.6.12 instability (creator-reported "way too many icons", regressed
    hot corner / default icons). Re-enable when QdFolderSheet modal lands.
    ```

(b) `qd_Launchpad.cpp` lines 453–527 (OnInput section): folder tile touch
    hit-testing in `#if 0`. Comment: same deferral notice.

**The tile strip render code is complete and correct.** It:
- Counts items per AutoFolderIdx bucket by walking `items_` and calling
  `LookupFolderIdx(StableIdForItem(it))` (lines 737–743).
- Skips empty buckets (no tile for a bucket with 0 items).
- Calls `PaintFolderTile()` for each non-empty bucket plus an "All" tile.
- Uses `active_folder_` to highlight the active bucket with an accent border.
- `PaintFolderTile()` is fully implemented (lines 932–1003, confirmed live).

**The filter code is also complete:** `RebuildFilter()` (lines 1007–1069) already
applies `active_folder_` to filter `filtered_idxs_`. The folder filter path
is live code that runs on every frame — only the visual tile strip and its
touch handler are gated.

**Root cause of stabilize-3 deferral:** The comment cites "way too many icons"
instability from v1.6.12, which was traced to alpha bleed and icon regression —
the same context as the P1 hot-corner deferral. The tile strip itself does not
cause those problems; it was caught in the same `#if 0` sweep as a conservative
measure. The `RebuildFilter()` folder filter was NOT gated and runs live.

**K+1 Phase 2 (desktop folder entries):** NOT implemented and NOT targeted for
stabilize-5. The design doc (K+1-FOLDERS-CATEGORIES-DESIGN.md) requires
`IconKind::Folder`, `folders.json`, and `QdFolderSheet` modal — a full
separate feature, gated on creator approval. This plan addresses only the
Launchpad tile strip (Phase 1 completion).

### 2. Target state v1

Re-enable the auto-folder tile strip in both OnRender and OnInput by removing
the two `#if 0` wrappers. No new code required.

The strip renders between the search bar and the icon grid, left-aligned at
`LP_SEARCH_BAR_X`. Tile geometry: 200×36 px tiles, 8 px gap. "All" tile
always shown at `LP_SEARCH_BAR_X - 208`. Only non-empty buckets get tiles.
Active bucket gets accent border; inactive tiles get a dimmer border.

Touch input: tapping a tile sets `active_folder_` to the bucket's
`AutoFolderIdx` (or `None` for "All") and sets `filter_dirty_ = true`.
`RebuildFilter()` then re-populates `filtered_idxs_` for the new filter.

### 3. Implementation plan

**File:** `qd_Launchpad.cpp`

1. Remove `#if 0` / `#endif` wrapping lines 723–778 (OnRender tile strip).
   The interior code is correct and unchanged.

2. Remove `#if 0` / `#endif` wrapping lines 453–527 (OnInput touch handler).
   The interior code is correct and unchanged.

**No other changes.** No new files. No struct changes. No new logic.

The `active_folder_` member already exists in `QdLaunchpadElement`
(qd_Launchpad.hpp private member). `PaintFolderTile()`, `StableIdForItem()`,
`LookupFolderIdx()`, `RebuildFilter()` are all live.

### 4. LOC estimate + assets

- qd_Launchpad.cpp: remove 4 lines (two `#if 0` / `#endif` pairs), no other
  change = **−4 lines**
- Assets needed: none
- No new files

### 5. Risks / open questions

**Risk R3.1 — "All" tile position at LP_SEARCH_BAR_X − 208:**
The rendered position is `LP_SEARCH_BAR_X - FTILE_W - FTILE_GAP =
300 - 200 - 8 = 92 px`. At 1920-wide display this is visually correct
(92 px from left edge). Confirm LP_SEARCH_BAR_X is still 300 in the
current build; if it changed, the "All" tile will overlap the hot-corner
or clip off-screen.

**Risk R3.2 — strip layout when all 5 buckets are populated:**
Five tiles at 200 px + 8 px gap = 1040 px. Strip starts at LP_SEARCH_BAR_X=300.
Maximum right edge: 300 + 1040 = 1340 px. Within 1920 px, no clipping.
If a future bucket is added (Phase 2 desktop folder), the strip needs
horizontal scroll or truncation. Not a stabilize-5 concern.

**Risk R3.3 — grid starting y-position with strip showing:**
The tile strip sits at `LP_SEARCH_BAR_Y + LP_SEARCH_BAR_H + 6` (FTILE_STRIP_Y).
The grid starts at `LP_GRID_Y = 144`. With the strip at approximately
y = 76 + 36 + 6 = 118 (rough calculation pending LP_SEARCH_BAR_Y value
confirmation), there are ~26 px between strip bottom and grid top.
This is tight. If the search bar or strip heights have changed from their
definition-time values, the strip may overlap the grid top row.

**Open question OQ3.1:** Was the "way too many icons" instability in v1.6.12
caused by the tile strip itself or by an unrelated icon enumeration bug
that happened to be present at the same time? If by the tile strip, the
re-enable may re-introduce it. The description in the comment ("creator-reported
'way too many icons'") sounds like the NS service flood bug (icon_size==0),
which was fixed separately in stabilize-4. No evidence links the tile strip to
that crash. Verify by confirming the NS-flood fix is still present before
enabling.

### 6. Defer to v2

- K+1 Phase 2 (desktop folder entries with `IconKind::Folder`, `folders.json`,
  `QdFolderSheet` modal). Full design exists in K+1-FOLDERS-CATEGORIES-DESIGN.md;
  gated on creator approval per the design doc.
- Horizontal scroll for tile strip if bucket count grows past 5.
- Keyboard / D-pad navigation between folder tiles.

---

## P5 — Mac-class icons for five built-in Q OS apps

### 1. Current state

`BUILTIN_ICON_DEFS[]` at qd_DesktopIcons.cpp lines 314–320:
```cpp
static constexpr BuiltinIconDef BUILTIN_ICON_DEFS[BUILTIN_ICON_COUNT] = {
    { "Vault",       'V', 0x7D, 0xD3, 0xFC },  // sky-blue
    { "Monitor",     'M', 0x4A, 0xDE, 0x80 },  // green
    { "Control",     'C', 0x4A, 0xDE, 0x80 },  // green (same as Monitor)
    { "About",       'A', 0xF8, 0x71, 0x71 },  // coral
    { "AllPrograms", 'P', 0xA7, 0x8B, 0xFA },  // purple
};
```

`PopulateBuiltins()` (lines 547–580): fills NroEntry with glyph + RGB color,
sets `icon_path[0] = '\0'` and `nro_path[0] = '\0'` — no path to load from.

Desktop `OnRender` icon path (lines 1130–1142): the condition for cache lookup is:
```cpp
if (entry.kind == IconKind::Application && entry.icon_path[0] != '\0') ...
else if (entry.kind == IconKind::Special && entry.icon_path[0] != '\0') ...
else if (entry.nro_path[0] != '\0') ...
```
Builtin entries have `kind = IconKind::Builtin`, `icon_path[0] = '\0'`,
`nro_path[0] = '\0'` — they hit none of these branches. They render via
the glyph fallback only (single-char letter + solid fill).

**Dock special icons** (`DockVault.png`, `DockMonitor.png`, `DockControl.png`,
`DockAbout.png`, `DockAllPrograms.png`) already exist in romfs at
`default/ui/Main/EntryIcon/`. These are dock-slot icons used in the desktop
sidebar, not the grid cells. They confirm the naming convention.

**Loading infrastructure:** `LoadJpegIconToCache()` (line 2545) uses SDL2_image's
`IMG_Load()` which handles PNG natively. It does not require a JPEG. The name is
historical. It accepts any SDL2_image-supported path including PNG files.

**Special entry icons** (Settings, Album, Themes, etc.) use
`TryFindLoadImage("ui/Main/EntryIcon/<Name>")` (line 1106) — a romfs-relative
call that does NOT go through `QdIconCache`. The result is stored directly in
`icon_tex_[entry_idx]`. This path requires no cache key and no `icon_path` on
the struct.

**Key constraint:** NroEntry `icon_path[769]` and `nro_path[769]` are already
fields in the 1632-byte pinned struct. We CAN populate `icon_path` with a romfs
path — no struct extension is needed. However, the `icon_path` cache lookup
branch only fires for `IconKind::Application` and `IconKind::Special`. Builtin
kind is excluded. Two options exist:

**Option A — populate icon_path and extend the cache lookup:**
Set `icon_path` to a romfs pseudo-path in PopulateBuiltins, then add a
`IconKind::Builtin` branch in the cache lookup at line 1132. Call
`LoadJpegIconToCache(romfs_path, romfs_path)` during PopulateBuiltins for
each builtin. Uses QdIconCache; the icon renders from BGRA data.

**Option B — use TryFindLoadImage directly (mirrors Special icons):**
In the desktop OnRender per-cell block, add an `IconKind::Builtin` branch
that calls `TryFindLoadImage("ui/Main/EntryIcon/<name>")` and stores the
result in `icon_tex_[entry_idx]`. Does NOT go through QdIconCache. Matches
exactly how Special entries load their romfs icons (Settings, Album, etc.).

**Option B is recommended.** It mirrors the already-working Special icon path
exactly, requires no change to the cache infrastructure, and is ~10 lines.

### 2. Target state v1

Five squircle PNG assets (256×256 px each) authored and placed at:
- `romfs/default/ui/Main/EntryIcon/Vault.png`
- `romfs/default/ui/Main/EntryIcon/Monitor.png`
- `romfs/default/ui/Main/EntryIcon/Control.png`
- `romfs/default/ui/Main/EntryIcon/About.png`
- `romfs/default/ui/Main/EntryIcon/AllPrograms.png`

(Note: `DockVault.png`, `DockMonitor.png`, etc. are already present for the
dock sidebar. The grid-cell variants use non-Dock names per the Special entry
pattern: `Settings.png` not `DockSettings.png`.)

Desktop OnRender adds an `IconKind::Builtin` lazy-load block that calls
`TryFindLoadImage("ui/Main/EntryIcon/<name>")` on the first frame, stores in
`icon_tex_[entry_idx]`, and blits on subsequent frames. Glyph fallback remains
as the no-asset safety net.

### 3. Implementation plan

**Step 1 — Author 5 PNG assets (art task, not code):**
Each icon is a 256×256 squircle PNG. The squircle mask provides the rounded
shape; the background is a gradient; the foreground glyph is white on a colored
background. Suggested palettes:

| Name        | Background gradient                    | Glyph/symbol  |
|-------------|----------------------------------------|---------------|
| Vault       | #1A1A2E → #7DD3FC (dark navy → sky)    | Lock/shield   |
| Monitor     | #052e16 → #4ADE80 (dark green → mint)  | Waveform/bars |
| Control     | #1E1B4B → #6366F1 (indigo → violet)    | Gear/toggle   |
| About       | #3B0764 → #A855F7 (deep purple → lilac)| Letter "Q" or info circle |
| AllPrograms | #172554 → #3B82F6 (navy → blue)        | Grid/apps     |

Note: The existing DockVault.png etc. are already present and can be reused
for the grid cell icons by simply adding the non-Dock name variant. If DockX.png
is the correct resolution and art style, the grid can use the Dock assets
directly via symlink or copy at build time.

**Step 2 — Add Builtin icon load block in qd_DesktopIcons.cpp OnRender:**

At the existing Special-icon load block (line 1089), insert an analogous
`IconKind::Builtin` block immediately after the `#endif` at line 1118:

```cpp
// ── 2a'. Builtin icon: romfs PNG asset (P5 stabilize-5) ─────────────────
if (entry.kind == IconKind::Builtin && entry_idx < MAX_ICONS
        && icon_tex_[entry_idx] == nullptr) {
    // Build the romfs path "ui/Main/EntryIcon/<name>".
    char asset_path[96];
    snprintf(asset_path, sizeof(asset_path),
             "ui/Main/EntryIcon/%s",
             entry.name);  // entry.name is "Vault", "Monitor", etc.
    icon_tex_[entry_idx] = ::ul::menu::ui::TryFindLoadImage(asset_path);
    if (icon_tex_[entry_idx] != nullptr) {
        UL_LOG_INFO("qdesktop: Builtin icon loaded name=%s path=%s",
                    entry.name, asset_path);
    }
    // Fallback: leave icon_tex_ nullptr; glyph render path activates below.
}
```

That is ~16 lines of code. No struct changes. No cache changes.

**Step 3 — Verify the Launchpad PaintCell path:**
`qd_Launchpad.cpp` PaintCell (lines 1131–1141) uses `icon_path` and `nro_path`
as cache keys to look up `icon_cache_->Get()`. Builtin items have both empty,
so `cache_key == nullptr`. The Launchpad will continue to show the glyph fallback
(colored square + letter). To get the PNG in the Launchpad as well, the Launchpad
needs a TryFindLoadImage path too.

Add an analogous block in `PaintCell()` after the `cache_key` section:

```cpp
// P5 stabilize-5: lazy-load Builtin PNG from romfs into icon_tex_ slot.
if (cache_key == nullptr && item.is_builtin
        && item_idx < icon_tex_.size()
        && icon_tex_[item_idx] == nullptr) {
    char asset_path[96];
    snprintf(asset_path, sizeof(asset_path),
             "ui/Main/EntryIcon/%s", item.name);
    icon_tex_[item_idx] = ::ul::menu::ui::TryFindLoadImage(asset_path);
}
```

This is ~10 lines. Requires `TryFindLoadImage` to be accessible in qd_Launchpad.cpp;
confirm the include path — it is already used in qd_DesktopIcons.cpp, so the header
is available.

### 4. LOC estimate + assets

- qd_DesktopIcons.cpp: +16 lines (builtin load block)
- qd_Launchpad.cpp / PaintCell: +10 lines (builtin load block)
- Assets: 5 PNG files (256×256 px squircle), ~20–40 KB each
- Total code: ~26 lines across 2 files

### 5. Risks / open questions

**Risk R5.1 — TryFindLoadImage in qd_Launchpad.cpp:**
Confirm `::ul::menu::ui::TryFindLoadImage` is declared in a header already
included by qd_Launchpad.cpp. If not, add the include. This is a 1-line change.

**Risk R5.2 — romfs path naming collision:**
`Settings.png`, `Album.png` etc. already exist. `Vault.png`, `Monitor.png`,
`Control.png`, `About.png`, `AllPrograms.png` do NOT currently exist (confirmed
by file listing). No collision risk.

**Risk R5.3 — Launchpad icon_tex_ slot reuse:**
`icon_tex_` in QdLaunchpadElement is indexed by `item_idx` (the slot in
`items_`). The builtin items occupy the first BUILTIN_ICON_COUNT slots (set by
`dock_slot` order in `Open()`). Confirm the `item_idx` in `PaintCell()` matches
the slot index in `icon_tex_` — the current code uses `item_idx` directly (line
1144), which is the position in `items_`, not the filtered visual position.
This is already correct per the existing code path; the builtin slots occupy
stable positions in `items_` regardless of sorting.

**Open question OQ5.1:** Should the grid-cell Builtin icons be the same art as
the dock-sidebar icons (`DockVault.png` etc.) or new 256×256 variants? The Dock
icons exist but their resolution and layout may be optimized for the sidebar
context (smaller, wide-crop). For v1, using the Dock assets by name (e.g.,
mapping `Vault` → `DockVault`) is the fastest path without new art.

### 6. Defer to v2

- `AllPrograms.png` to use a grid-of-dots motif (Mac Launchpad icon aesthetic).
- Animated glow on focus (brightness overlay on the squircle background).
- Separate Launchpad-scale (LP_ICON_W × LP_ICON_H) versus desktop-scale
  (cell icon size) variants if the resolutions diverge.
- Squircle compositing in code (currently entirely asset-driven — v2 could add
  a software mask for consistent roundness regardless of art).

---

## P6 — Launchpad pagination

### 1. Current state

**No pagination state exists anywhere in QdLaunchpadElement.** Confirmed via
full review of the private member list in qd_Launchpad.hpp lines 259–320:
- No `page_index_`
- No `page_count_`
- No `items_per_page`
- No `scroll_offset_`
- No dot-indicator state

**CellXY() maps vpos to absolute screen coordinates (lines 1087–1092):**
```cpp
void QdLaunchpadElement::CellXY(size_t vpos, s32 &out_x, s32 &out_y) {
    const s32 col = vpos % LP_COLS;        // LP_COLS = 10
    const s32 row = vpos / LP_COLS;
    out_x = LP_GRID_X + col * (LP_CELL_W + LP_GAP_X);  // LP_GRID_X=60
    out_y = LP_GRID_Y + row * (LP_CELL_H + LP_GAP_Y);  // LP_GRID_Y=144
}
```
Row increases without bound. The cull condition at OnRender line 839:
```cpp
if (cy + LP_CELL_H > 1032) { continue; }
```
silently hides items below y=1032, but does NOT page them. Items beyond the
visible area are allocated texture slots (name_tex_, glyph_tex_, icon_tex_,
icon_loaded_) but never rendered.

**Visible rows calculation:**
- Display height: 1080 px
- Status line: ~48 px at bottom → usable height = 1032 px
- Grid starts at LP_GRID_Y = 144
- Usable grid height = 1032 − 144 = 888 px
- Rows: floor(888 / (LP_CELL_H + LP_GAP_Y)) = floor(888 / 162) = 5 rows
- Items per page: 5 rows × LP_COLS=10 = **50 items per page**

**Pre-warm:** Open() warms `LP_PREWARM_ITEMS = 60` entries (10 cols × 6 rows).
With 50 items visible, the 6th row is partially pre-warmed but off-screen.
Post-pagination, pre-warm should warm exactly 1 page (50 items).

**Texture slots:** `icon_tex_`, `name_tex_`, `glyph_tex_`, `icon_loaded_` are
`std::vector<...>` sized to `items_.size()` on Open() (confirmed in hpp).
With pagination, only the current page's items need hot textures. All other
slots can remain nullptr until the page is navigated to.

**D-pad navigation:** currently wraps within `filtered_idxs_.size()` —
Up = −10, Down = +10, Left = −1, Right = +1, all clamped. D-pad focus index
is an absolute visual position in filtered_idxs_. D-pad navigation crossing
a page boundary needs to trigger a page turn.

**ZR / touch input:** ZR fires on `mouse_hover_index_` which is set by
touch coordinate → grid cell hit-test. Touch positions currently map to
absolute grid positions. After pagination, they must be interpreted relative
to the current page's cell layout.

### 2. Target state v1

**Page model:**
- Items per page: 50 (5 rows × 10 cols)
- Page count: ceil(filtered_count / 50), recomputed after RebuildFilter()
- Active page: 0-indexed `page_index_` in [0, page_count_)

**CellXY change:**
Offset the row by `page_index_ * rows_per_page`:
```cpp
void QdLaunchpadElement::CellXY(size_t vpos, s32 &out_x, s32 &out_y) {
    const size_t ROWS_PER_PAGE = 5;
    const size_t ITEMS_PER_PAGE = ROWS_PER_PAGE * LP_COLS;
    const size_t page_vpos = vpos % ITEMS_PER_PAGE;      // position on current page
    const s32 col = page_vpos % LP_COLS;
    const s32 row = page_vpos / LP_COLS;
    out_x = LP_GRID_X + col * (LP_CELL_W + LP_GAP_X);
    out_y = LP_GRID_Y + row * (LP_CELL_H + LP_GAP_Y);
}
```
Items not on the current page are excluded from the render loop via the cull
condition (which now effectively becomes: `if (vpos < page_start || vpos >= page_end) continue`
or equivalently the modified CellXY ensures off-page items always map to y > 1032).

**Better approach for the render loop:** Rather than modifying CellXY signature,
add a page-slice filter in the OnRender loop:
```cpp
const size_t page_start = page_index_ * ITEMS_PER_PAGE;
const size_t page_end   = std::min(page_start + ITEMS_PER_PAGE, nf);
for (size_t vpos = page_start; vpos < page_end; ++vpos) {
    // CellXY uses (vpos - page_start) as the visual position on this page
    s32 cx, cy;
    CellXY(vpos - page_start, cx, cy);   // pass page-relative index
    ...
}
```
This preserves CellXY's contract (vpos 0 = top-left cell) and requires no
change to CellXY itself.

**Dot indicator strip:**
Rendered at y ≈ 1044 (4 px above status line at 1048), centered horizontally.
Max dots: page_count_, displayed as small filled circles (~8 px diameter, 12 px
gap). Current page dot is accent-colored; others are dimmer. If page_count_ > 15,
render "N / M" text instead of dots.

**Page turn:**
- L-button: page_index_ = max(0, page_index_ - 1)
- R-button: page_index_ = min(page_count_ - 1, page_index_ + 1)
- Touch swipe left: page_index_++
- Touch swipe right: page_index_--
- D-pad overrun: when dpad_focus_index_ exits the page boundary (next/prev page),
  auto-advance page_index_ and wrap dpad_focus_index_ to the first/last item of
  the new page.

**Pre-warm change:** warm `ITEMS_PER_PAGE = 50` entries starting at
`page_index_ * ITEMS_PER_PAGE` on Open() and on page turn.

**Filter interaction:** `RebuildFilter()` must reset `page_index_ = 0` and
recompute `page_count_`. A filter change always returns to page 1.

### 3. Implementation plan

**New private members in QdLaunchpadElement (qd_Launchpad.hpp):**
```cpp
size_t page_index_;          // current page, 0-indexed
size_t page_count_;          // total pages for current filter
```
These are plain `size_t` members — no impact on NroEntry or LpItem size.
QdLaunchpadElement is not size-pinned.

**Changes to qd_Launchpad.hpp:**
1. Add `size_t page_index_` and `size_t page_count_` to private members section.
   (~2 lines)

**Changes to qd_Launchpad.cpp:**

2. `Open()`: Initialize `page_index_ = 0`, `page_count_` after sort = ceil(items_
   count / 50). Adjust pre-warm to warm `min(ITEMS_PER_PAGE, items_.size())` items.
   (~5 lines changed)

3. `Close()`: Reset `page_index_ = 0`, `page_count_ = 0`. (~2 lines)

4. `RebuildFilter()`: After rebuilding `filtered_idxs_`, compute:
   ```cpp
   page_index_ = 0;
   const size_t ITEMS_PER_PAGE = 50u;
   page_count_ = (filtered_idxs_.empty())
       ? 1u
       : (filtered_idxs_.size() + ITEMS_PER_PAGE - 1u) / ITEMS_PER_PAGE;
   ```
   (~5 lines)

5. `OnInput()`: Add L/R button handling and D-pad boundary crossing.
   ```cpp
   if (keys_down & HidNpadButton_L) {
       if (page_index_ > 0u) { --page_index_; dpad_focus_index_ = 0u; }
   }
   if (keys_down & HidNpadButton_R) {
       if (page_index_ + 1u < page_count_) { ++page_index_; dpad_focus_index_ = 0u; }
   }
   ```
   D-pad overrun detection: if Up/Down motion would move `dpad_focus_index_` to
   a vpos outside `[page_start, page_end)`, advance the page instead.
   (~20 lines)

6. `OnRender()` icon grid loop (section 5, line 830): replace the existing loop
   with the page-sliced version:
   ```cpp
   const size_t ITEMS_PER_PAGE = 50u;
   const size_t page_start = page_index_ * ITEMS_PER_PAGE;
   const size_t page_end   = std::min(page_start + ITEMS_PER_PAGE, nf);
   for (size_t vpos = page_start; vpos < page_end; ++vpos) {
       const size_t item_idx = filtered_idxs_[vpos];
       if (item_idx >= items_.size()) { continue; }
       s32 cx = 0, cy = 0;
       CellXY(vpos - page_start, cx, cy);  // page-relative visual pos
       const bool cell_highlighted = (vpos == dpad_focus_index_)
                                  || (vpos == mouse_hover_index_);
       PaintCell(r, items_[item_idx], item_idx, cx, cy, cell_highlighted);
   }
   ```
   (~10 lines, replaces 10 lines — net 0)

7. Add `PaintPageDots()` helper (new private method):
   Renders dot indicators at y ≈ 1044, centered. Accent-colored dot for
   current page; dim dots for others. Falls back to "N / M" text when
   page_count_ > 15.
   (~30 lines for the implementation)

8. Call `PaintPageDots()` in OnRender after section 5 (before section 6
   status line), only when `page_count_ > 1`.
   (~4 lines)

9. Declare `PaintPageDots()` in qd_Launchpad.hpp private section. (~1 line)

**Total code change:**
- qd_Launchpad.hpp: +3 lines (2 members + 1 method decl)
- qd_Launchpad.cpp: ~75 lines net added (Open/Close/RebuildFilter/OnInput
  page management + PaintPageDots + render loop change)

### 4. LOC estimate + assets

- qd_Launchpad.hpp: +3 lines
- qd_Launchpad.cpp: +75 lines (net, across 5 touch points)
- Assets: none (dot indicators are SDL primitives)
- Total: **~78 lines**

### 5. Risks / open questions

**Risk R6.1 — dpad_focus_index_ absolute vs page-relative:**
Currently `dpad_focus_index_` is an absolute index into `filtered_idxs_` and is
compared directly with `vpos` in the render loop highlight check. With pagination,
the render loop now iterates `vpos` from `page_start` to `page_end`, and the
highlight check `(vpos == dpad_focus_index_)` still works correctly because both
are absolute filtered_idxs positions. No change needed to the comparison.
However, on page turn, `dpad_focus_index_` is reset to `page_start` (first item
of new page). This means D-pad position is lost on page change — by design for v1.

**Risk R6.2 — mouse_hover_index_ with touch coordinates:**
`mouse_hover_index_` is set from touch coordinate hit-testing in OnInput. The
current hit-test maps `(tx, ty) → vpos` using `CellXY`'s inverse. After
pagination, the touch coordinate maps to a page-relative cell position, not an
absolute filtered_idxs position. The hit-test must be updated to add
`page_start` offset when computing `mouse_hover_index_`. This is ~5 lines in
the touch-hit-test block.

**Risk R6.3 — Pre-warm on page turn:**
When the user navigates to page 2, items 50–99 have no cached textures yet
(name_tex_, glyph_tex_, icon_tex_ are all nullptr). The first frame on the new
page will show gray/glyph tiles while the lazy-load infrastructure populates.
For v1 this is acceptable. For v2, a pre-warm pass on page turn (similar to
Open's pre-warm) would eliminate the one-frame flash.

**Risk R6.4 — items_per_page constant vs LP geometry:**
`ITEMS_PER_PAGE = 50` is derived from `5 rows × LP_COLS=10`. If LP_COLS or the
visible row count changes (e.g., the folder tile strip from P3 adds height),
the visible row count drops and the hardcoded 50 would clip the last visible row.
**Recommendation:** Compute ITEMS_PER_PAGE from LP geometry constants rather
than hardcoding 50:
```cpp
constexpr s32 LP_VISIBLE_H = 1032 - LP_GRID_Y;             // 888
constexpr size_t LP_ROWS_PER_PAGE = LP_VISIBLE_H / (LP_CELL_H + LP_GAP_Y);  // 5
constexpr size_t LP_ITEMS_PER_PAGE = LP_ROWS_PER_PAGE * LP_COLS;             // 50
```
Place these in qd_Launchpad.hpp alongside the existing LP_* constants.

**Risk R6.5 — L/R button conflict:**
Check whether HidNpadButton_L and HidNpadButton_R are used for any other
purpose in the Launchpad OnInput. From the investigated code, L and R are not
currently consumed in the Launchpad — only A, B, Up/Down/Left/Right, ZR, and
touch are handled. Confirm before shipping.

**Open question OQ6.1:** Should a swipe gesture (touch drag with velocity)
also trigger page turns? For v1, L/R buttons only. Swipe detection is v2.

### 6. Defer to v2

- Swipe-to-page gesture (touch velocity threshold).
- Pre-warm on page turn (eliminate one-frame gray flash).
- Animated page slide transition (items slide out left/right).
- Keyboard shortcut for "jump to page N" (quick-jump on L+L or R+R).
- Save/restore page_index_ across Launchpad open/close cycles (so re-opening
  returns to the same page instead of always starting at page 1).

---

## Budget summary

| Feature | Files changed         | Net LOC    | New assets        | Struct changes |
|---------|-----------------------|------------|-------------------|----------------|
| P1      | qd_Launchpad.cpp      | ~0 (−2 + 20 touched) | None   | None           |
|         | qd_DesktopIcons.cpp   | ~0 (−2 + 20 touched) |        |                |
| P3      | qd_Launchpad.cpp      | −4 (remove #if 0s)   | None   | None           |
| P5      | qd_DesktopIcons.cpp   | +16                  | 5 PNGs (256×256)  | None |
|         | qd_Launchpad.cpp      | +10                  |        |                |
| P6      | qd_Launchpad.hpp      | +3                   | None   | None (LP class only) |
|         | qd_Launchpad.cpp      | +75                  |        |                |

**Total new code:** ~100 lines net across 3 files.
**Total assets:** 5 PNG files (~20–40 KB each).
**Struct size impact:** Zero. NroEntry and LpItem remain at 1632 bytes.
**Must-NOT-Regress items (from last HW-green v1.7.0-stabilize-4):**

- Boot to desktop without crash — unaffected (P1 adds no boot-path code)
- Login chime plays exactly once — unaffected
- Hot corner widget visible AND tappable — P1 adds visual; hit-test is live
- Touch tap on a tile launches that tile — P6 modifies the render loop but
  not the launch dispatch path
- D-pad + A launches dpad-focused tile — P6 modifies focus index management;
  launch path unchanged
- ZR launches mouse-hovered tile — P6 requires R6.2 fix (mouse_hover_index_
  page offset); must be verified after P6 lands
- icon_size==0 no NS flood — unaffected by all four features
- MakeFallbackIcon emits #3A3A3A gray — unaffected
- Pre-warm cache loads first-page entries — P6 changes pre-warm count from 60
  to ITEMS_PER_PAGE=50; first-page is still fully warmed
- SD-root NROs appear in Launchpad — unaffected

---

## Open questions (unresolved, require creator input)

**OQ-P1:** Should the hot-corner Q glyph in the desktop path use the ASCII "Q"
character (code only, no asset) or the `romfs/Logo.png` asset (code + asset
but no new art required — file already exists)?

**OQ-P3:** Confirm the auto-folder tile strip re-enable does not reintroduce the
v1.6.12 instability. Specifically: was the "way too many icons" creator report
caused by the tile strip code or by the NS service flood (icon_size==0)? The NS
flood fix landed in stabilize-4 per the source comments; if confirmed separate,
P3 is safe to re-enable.

**OQ-P5:** Can the existing DockVault.png, DockMonitor.png, DockControl.png,
DockAbout.png, DockAllPrograms.png be used as the grid-cell icons (by loading
them under their Dock name), or must separate non-Dock variants be authored?
If the Dock art is acceptable, P5 requires zero new assets — only ~26 lines of
code.

**OQ-P6:** Is L/R button already committed to another action in the Launchpad
context? If so, what input should trigger page turn?

---

*End of F-Plan — stabilize-5*
