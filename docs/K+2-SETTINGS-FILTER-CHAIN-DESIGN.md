# K+2 Settings + Filter Chain — Design SSOT

> **Authoring SSOT:** this file
> **Drafted:** 2026-04-25T17:35:00Z (post-K-launchpadfix verified, post-P4 art rebrand)
> **Status:** Design only. Implementation gated on creator approval.
> **Cross-refs:** `cfg_Config.hpp` (ConfigEntryId), `qd_Launchpad.cpp` (RebuildFilter),
> `qd_DesktopIcons.cpp` (icon scan + grid render), `qd_SettingsLayout.cpp`
> (existing QSettings UI scaffold), K+1 design doc (IconCategory).

---

## Why this exists

The Launchpad's search bar today filters by **name substring only**. The desktop grid
shows **all entries** with no user control over visibility. Users with 50+ apps need
a way to:

1. **Filter** the Launchpad by category (Nintendo only, Homebrew only, etc.)
2. **Hide** specific entries from the desktop grid (e.g. "I never want to see Album")
3. **Pin** favorites to the dock or top of grid
4. **Adjust icon size** for accessibility (creator already requested this in K-iconsfix
   feedback: "later one of the settings will be for the user to be able to choose the
   icon size")
5. **Toggle desktop mode** (handheld 9×5 vs docked-projection layouts)
6. **Sort recents** (related to K+4 LRU tracking)

K+2 ships **the Settings infrastructure + the filter chain**. K+3/K+4 build on top.

---

## New ConfigEntryIds

Extend `enum class ConfigEntryId : u8` in `cfg_Config.hpp`:

```cpp
enum class ConfigEntryId : u8 {
    // ── existing (K-1.2.3 baseline) ──
    MenuTakeoverProgramId                     = 0x00,
    HomebrewAppletTakeoverProgramId           = 0x01,
    HomebrewApplicationTakeoverApplicationId  = 0x02,
    UsbScreenCaptureEnabled                   = 0x03,
    ActiveThemeName                           = 0x04,
    MenuEntryHeightCount                      = 0x05,
    LockscreenEnabled                         = 0x06,
    LaunchHomebrewApplicationByDefault        = 0x07,

    // ── K+2 new (8 entries) ──
    IconSize                                  = 0x10,  // u64: enum IconSize { Small, Medium, Large }
    DesktopMode                               = 0x11,  // u64: enum DesktopMode { Handheld, Docked, Projection }
    ShowHomebrew                              = 0x12,  // bool: include Nro entries in desktop grid
    ShowApplications                          = 0x13,  // bool: include Application entries in desktop grid
    ShowSpecial                               = 0x14,  // bool: include SpecialEntry* in desktop grid
    LaunchpadDefaultCategory                  = 0x15,  // u64: enum LpDefaultCategory { All, Nintendo, Homebrew, Extras }
    EnableRecents                             = 0x16,  // bool: track LRU; surfaces as "Recents" section in Launchpad (K+4)
    EnableFavorites                           = 0x17,  // bool: allow pinning favorites; surfaces as "Favorites" section
};
```

**Why explicit hex IDs:** the binary cfg file format uses the raw enum byte value as
the entry discriminator. Using sequential `0x10..0x17` for K+2 reserves `0x08..0x0F`
for any K+1 additions (folder-related config) without re-numbering K+2 entries on
disk — so an existing user's `ulaunch.cfg` stays compatible across K+1/K+2 ordering.

### Defaults (returned when entry absent)

| Entry | Default | Why |
|---|---|---|
| `IconSize` | `Medium` | Current 9×5 grid is sized for Medium |
| `DesktopMode` | `Handheld` | Current default; Docked auto-detects via appletGetOperationMode |
| `ShowHomebrew` | `true` | Don't surprise current users |
| `ShowApplications` | `true` | Don't surprise current users |
| `ShowSpecial` | `true` | Don't surprise current users |
| `LaunchpadDefaultCategory` | `All` | Backward-compatible — Launchpad opens showing everything |
| `EnableRecents` | `false` | Privacy-conservative default; opt-in |
| `EnableFavorites` | `false` | Opt-in (avoids breaking grid layout if user hasn't set favorites) |

Defaults are computed in `cfg::Config::GetEntry<T>()` — when the ID isn't present in
the file, return the default per the table above. No file write happens until the
user changes the value (lazy-write semantics).

### Per-app overrides (separate file, NOT in ulaunch.cfg)

Three of the new ConfigEntryIds (`ShowHomebrew`, `ShowApplications`, `ShowSpecial`)
are coarse global toggles. For finer-grained control we introduce a sibling SSOT:

```
sdmc:/ulaunch/per-app-prefs.bin
```

Schema (binary, packed):

```
[u8 magic:0x4Q] [u8 version:1] [u32 count]
  for each entry:
    [u8 kind]               // IconKind (0=Builtin, 1=Nro, 2=Application, 3=Special)
    [u64 key_low]           // app_id (Application/Special) OR DJB2(nro_path) (Nro)
    [u8 flags]              // bit0=hidden, bit1=favorite, bit2=pinned_to_dock
    [u8 dock_slot]          // 0xFF if not pinned; else 0..4
    [u32 last_launched_ns]  // K+4 LRU; 0 if never
    [u32 launch_count]      // K+4 LRU
```

This file is read once at icon-scan time by `QdDesktopIconsElement::ScanIcons()` and
applied as a post-pass on the flat NroEntry list:
- `flags & 0x01` (hidden) → entry skipped from desktop grid (still in Launchpad)
- `flags & 0x02` (favorite) → entry sorts before others within its category
- `flags & 0x04` (pinned_to_dock) → entry inserted at `dock_slot` (overrides builtins)

---

## The Filter Chain

The existing `QdLaunchpadElement::RebuildFilter()` only does:

```cpp
if (query_.empty()) {
    // include all
} else {
    // include where lowercase(name).contains(lowercase(query))
}
```

Replace with a chained filter pipeline:

```cpp
struct FilterContext {
    std::string query;                  // search-bar query (existing)
    IconCategory required_category;     // K+1: All | Nintendo | Homebrew | Extras | Builtin
    bool show_homebrew;                 // K+2: from ConfigEntryId::ShowHomebrew
    bool show_applications;             // K+2: from ConfigEntryId::ShowApplications
    bool show_special;                  // K+2: from ConfigEntryId::ShowSpecial
    bool only_favorites;                // K+2: toggleable via L button in Launchpad
    bool only_recents;                  // K+2: toggleable via R button in Launchpad
};

void RebuildFilter() {
    filtered_idxs_.clear();
    for (size_t i = 0; i < items_.size(); ++i) {
        const LpItem &it = items_[i];
        if (!PassesCategoryFilter(it, filter_ctx_.required_category)) continue;
        if (!PassesKindFilter(it, filter_ctx_)) continue;
        if (!PassesFavoriteFilter(it, filter_ctx_.only_favorites)) continue;
        if (!PassesRecentFilter(it, filter_ctx_.only_recents)) continue;
        if (!PassesQueryFilter(it, filter_ctx_.query)) continue;
        filtered_idxs_.push_back(i);
    }
    filter_dirty_ = false;
}
```

Each predicate is a small `static inline bool Passes*Filter(const LpItem&, …)` —
fast, branchless where possible, no allocations in the hot loop.

### New Launchpad keybindings

| Key | Effect |
|---|---|
| `L` (top-left button) | Toggle `only_favorites` filter |
| `R` (top-right button) | Toggle `only_recents` filter (no-op if EnableRecents=false) |
| `ZL` | Cycle `required_category`: All → Nintendo → Homebrew → Extras → Builtin → All |
| `ZR` | Launch focused (existing) |
| `Plus` / `Minus` | Cycle `IconSize` Small ↔ Medium ↔ Large at runtime (also persists to config) |

Visual feedback: a small ribbon below the search bar shows active filters
("Showing: Homebrew · Favorites only · 23 results").

---

## QSettings UI surfaces

`QdSettingsLayout` (already exists at `src/projects/uMenu/source/ul/menu/qdesktop/
qd_SettingsLayout.cpp`) gets new rows for each ConfigEntryId. The existing layout
uses a row-per-config pattern; extend with:

| Row | Type | Source |
|---|---|---|
| Icon Size | enum picker (Small/Medium/Large) | `ConfigEntryId::IconSize` |
| Desktop Mode | enum picker (Handheld/Docked/Projection) | `ConfigEntryId::DesktopMode` |
| Show Homebrew | toggle | `ConfigEntryId::ShowHomebrew` |
| Show Applications | toggle | `ConfigEntryId::ShowApplications` |
| Show System Applets | toggle | `ConfigEntryId::ShowSpecial` |
| Launchpad opens to | enum picker (All/Nintendo/Homebrew/Extras) | `ConfigEntryId::LaunchpadDefaultCategory` |
| Track Recents | toggle | `ConfigEntryId::EnableRecents` |
| Enable Favorites | toggle | `ConfigEntryId::EnableFavorites` |

Existing rows (Theme, Lockscreen, USB Screen Capture, etc.) remain unchanged.

---

## IconSize semantics

The Plutonium Image elements' rendered size is set in code (not via UI.json), so
`IconSize` is read once at MainMenu construction and applied uniformly:

| Setting | Cell W×H | Grid (cols×rows) | Total slots |
|---|---|---|---|
| Small | 140×140 | 11 × 6 | 66 |
| Medium (current) | 172×168 | 9 × 5 | 45 |
| Large | 220×220 | 7 × 4 | 28 |

Math (Small): `74 + 11*140 + 10*28 = 74 + 1540 + 280 = 1894 ≤ 1920 ✓`. Vertical:
`72 + 6*140 = 912 ≤ 932 (dock top) ✓`.

Math (Large): `74 + 7*220 + 6*28 = 74 + 1540 + 168 = 1782 ≤ 1920 ✓`. Vertical:
`72 + 4*220 = 952 ≤ 932 ⨯ overflows by 20`. Solution: Large clips dock band to
DOCK_H=128 (was 148) — dock icons get slightly smaller, total slots stays at 28.

Dimension changes happen at boot, not live. Changing `IconSize` in QSettings shows
a "Restart uMenu to apply" notification — actual layout reload requires a fresh
MainMenu construction (Plutonium grid coords are baked at element-create time).

---

## Migration / Data integrity

1. **Old `ulaunch.cfg` files** (without K+2 entries) continue to work — `GetEntry`
   falls back to defaults. No migration script needed.
2. **`per-app-prefs.bin`** is created on first write. If absent at scan time, all
   per-app fields are treated as defaults (visible / not favorite / no LRU).
3. **Schema versioning**: `per-app-prefs.bin` has a u8 version byte. Bump when
   schema changes; reader downgrades gracefully (logs WARN, ignores unknown fields).

---

## Anti-stub gates

Per global rule R42:

1. **`ConfigEntryId::DesktopMode` enum** — Handheld + Docked are real modes today
   (auto-detect via `appletGetOperationMode`). `Projection` is a forward-looking
   stub for HDMI external-display mode. Cannot ship Projection in the picker until
   the actual rendering path exists. Either:
   - Ship K+2 with only Handheld + Docked (Projection added in K+6 when projection-
     mode rendering ships), OR
   - Ship Projection but disable the picker option and grey-it-out with "Coming soon".
   Per anti-stub mandate: option A (don't ship the enum value at all yet).

2. **`EnableRecents` + LRU tracking** depends on K+4. Ship the toggle and the persist-
   field in `per-app-prefs.bin`, but don't surface a "Recents" section in Launchpad
   until K+4 actually populates `last_launched_ns`. Flag in design doc: this is
   forward-staging, NOT a stub — the persistence layer is fully functional, just
   the consumer (Launchpad section) is gated.

3. **`pinned_to_dock` flag** in per-app-prefs depends on K+3 long-press edit mode
   for the user to set it. Ship the field but until K+3 lands, no UI surface
   modifies it — the current dock builtins (Vault/Monitor/Control/About/AllPrograms)
   are the only entries with `pinned_to_dock`.

---

## Open questions for creator

1. **IconSize granularity** — 3 sizes (Small/Medium/Large) or finer (5 steps)?
2. **DesktopMode auto-detect override** — should "Auto" be a 4th option that picks
   Handheld/Docked from `appletGetOperationMode`, or always trust the dock sensor?
3. **per-app-prefs.bin location** — `sdmc:/ulaunch/per-app-prefs.bin` (per K+1 conv.)
   or `sdmc:/switch/.config/qos-prefs.bin` (more standard XDG-ish)?
4. **Show System Applets toggle** — does hiding Special entries also hide them from
   Launchpad, or only from desktop grid? (Suggest: only desktop grid; Launchpad
   always shows everything for findability.)
5. **L/R/ZL Launchpad bindings** — collide with anything? Hot-corner behavior is
   already L+R combo in upstream uLaunch; ZL is currently unused; L alone is unused.

---

## What this doc does NOT cover

- **Folders + categories** — K+1 (separate doc).
- **Long-press iPhone edit mode + drag-reorder** — K+3 (separate doc).
- **LRU recents tracking** — K+4 (separate doc; depends on `per-app-prefs.bin` schema
  defined here).

---

## Cross-references

- `cfg_Config.hpp` — ConfigEntryId, GetEntry/SetEntry templates (extend here)
- `qd_Launchpad.cpp` — RebuildFilter (extend here)
- `qd_SettingsLayout.cpp` — QSettings UI rows (extend here)
- `K+1-FOLDERS-CATEGORIES-DESIGN.md` — IconCategory feeds the filter chain
- `QOS-REBRAND-ASSET-INVENTORY.md` — UI text strings need rebrand if added
