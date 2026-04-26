# K+1 Folders & Categories — Design SSOT

> **Authoring SSOT:** this file
> **Drafted:** 2026-04-25T17:20:00Z (post-K-launchpadfix verified)
> **Status:** Design only. Implementation gated on creator approval.
> **Cross-refs:** `qd_DesktopIcons.hpp` (IconKind, NroEntry), `qd_Launchpad.cpp`
> (LpSortKind, RebuildFilter, SectionLabel)

---

## Why this exists

Today the Launchpad sorts entries into **3 sections** via `LpSortKind`:

| Section | Source |
|---|---|
| Applications | IconKind::Application (installed Switch titles) |
| Homebrew | IconKind::Nro (sdmc:/switch/*.nro) |
| Built-in | IconKind::Builtin + IconKind::Special (dock + system applets) |

The desktop grid (MainMenu) currently has **no grouping at all** — every NRO and every
installed application competes for the 9×5 = 45 grid slots. With 40-50+ entries the user
sees a flat alphabetical sea, no way to find "the one homebrew tool I always use" without
scrolling past every game.

K+1 introduces **categories** (group-by-purpose) and **folders** (user-created groups).
The two are different: categories are inferred from the entry source, folders are
user-named buckets the user puts entries into manually.

---

## Categories (auto-classified)

Add a fourth field to `NroEntry`:

```cpp
enum class IconCategory : u8 {
    Nintendo  = 0,  // Installed application from a Nintendo-published title
                    // (heuristic: title-id high nibble in ranges Nintendo uses,
                    // OR icon JPEG NACP publisher field starts with "Nintendo")
    Homebrew  = 1,  // Any IconKind::Nro from sdmc:/switch/
    Extras    = 2,  // Installed application NOT from Nintendo (third-party, indie),
                    // PLUS IconKind::Special (Album/MiiEdit/Themes etc.)
    Payloads  = 3,  // Reserved for future Hekate-payload integration. Currently
                    // empty; the section header still renders to communicate intent.
    Builtin   = 4,  // Q OS dock builtins (Vault/Monitor/Control/About/AllPrograms).
                    // Always rendered last in the Launchpad, never in the desktop grid.
};

struct NroEntry {
    // ... existing fields ...
    IconCategory category;  // Computed at scan-time from kind + NACP metadata.
};
```

### Classifier rules (computed at icon-scan time, NOT runtime)

```cpp
// In QdDesktopIconsElement::ClassifyEntry(...)
IconCategory ClassifyEntry(const NroEntry &e) {
    switch (e.kind) {
        case IconKind::Builtin: return IconCategory::Builtin;
        case IconKind::Nro:     return IconCategory::Homebrew;
        case IconKind::Special: return IconCategory::Extras;  // System applets
        case IconKind::Application:
            // Title ID range OR NACP publisher heuristic.
            // Nintendo title-id high bits: typically 0x0100..0x01FF for first-party.
            // Cross-check with the NACP publisher string read from the icon JPEG header.
            if (IsNintendoPublisher(e.app_id, e.icon_path)) {
                return IconCategory::Nintendo;
            }
            return IconCategory::Extras;
    }
    return IconCategory::Extras;
}
```

`IsNintendoPublisher` reads the NACP at runtime (Switch NACP is at 0x4000 offset inside
the NRO; for installed apps it's accessible via `nsGetApplicationControlData`). The
result is cached per-app-id in `sdmc:/ulaunch/cache/nintendo-classify.bin` to avoid
re-reading on every boot. Cache invalidates when the system catalog changes (already
notified via `MenuMessage::ApplicationRecordsChanged`).

### Why these four categories

- **Nintendo** is the user's mental model for "real games I bought" — top of the list.
- **Homebrew** is the user's mental model for "tools I installed myself" — second.
- **Extras** absorbs everything else (third-party indies, system applets, Album, etc.) —
  third, intentionally heterogeneous so Launchpad doesn't fragment into 10+ tiny sections.
- **Payloads** is forward-looking: when Hekate-payload integration ships, .bin payloads
  scanned from `sdmc:/bootloader/payloads/` get a section. Until then the section is
  empty and `RebuildFilter` skips it from rendering.

---

## Folders (user-created)

Folders are persisted in a single SSOT JSON file:

```
sdmc:/ulaunch/folders.json
```

Schema:

```json
{
  "version": 1,
  "folders": [
    {
      "id": "fav-tools",
      "name": "Favorite Tools",
      "color_rgb": [167, 139, 250],
      "glyph": "★",
      "members": [
        { "kind": "Nro", "path": "sdmc:/switch/sys-clk/sys-clk.nro" },
        { "kind": "Application", "app_id": "0100000000010000" },
        { "kind": "Special", "subtype": "SpecialEntryAlbum" }
      ]
    }
  ]
}
```

### Folder semantics

1. A folder is a **virtual entry** in the desktop grid — it has a name, a glyph, a
   color, and an icon (composited from the first 4 member icons in a 2×2 thumbnail).
2. Tapping a folder on the desktop opens a **sheet** showing only its members (full-
   screen overlay, similar layout to Launchpad but filtered).
3. An entry can be in **at most one folder**. Adding to a second folder removes from
   the first. (Mirrors iOS behavior. Avoids the conflict of "which folder owns this
   icon's grid position?")
4. Folders are **separate from categories**. A folder named "Stuff I use daily" can
   contain one Nintendo title + two homebrew tools — categories are auto, folders are
   manual.
5. The **Launchpad** ignores folders for sorting purposes (it shows all entries flat,
   sectioned by category). Folders only affect the desktop grid.

### Folder creation flow (gated on K+3 long-press)

1. Long-press an entry on the desktop → enters edit mode (K+3).
2. Drag entry on top of another entry → prompts "Create folder?" with auto-name from
   common category ("Homebrew" if both Nro, "Apps" if both Application, etc.).
3. User confirms → both entries removed from grid, new folder entry inserted at the
   first entry's grid position.
4. Drag an existing entry onto an existing folder → adds entry to folder.
5. Long-press a folder → option to rename / change color / delete (deletion preserves
   member entries, returns them to the grid).

### Folder persistence

`folders.json` is read once at boot in `QdDesktopIconsElement::ScanIcons()` and applied
as a post-pass: members are removed from the flat scan list and a synthetic
`NroEntry { kind = IconKind::Folder, ... }` is inserted at the folder's grid position.

A new `IconKind::Folder = 4` enum value is added. The Launchpad sees these as
`IconKind::Folder` and inflates them inline (folder members appear in the appropriate
category section, NOT under a "Folders" header — the Launchpad is for finding apps,
not navigating folder structure).

---

## Migration path

### Phase 1 (this design doc — K+1.0)
- Add `IconCategory` enum + `category` field to `NroEntry`.
- Add `ClassifyEntry()` + `IsNintendoPublisher()` + persistent cache.
- Update `QdLaunchpadElement::Open()` to populate `LpItem::category` from `NroEntry::category`.
- Extend `LpSortKind` → 4 values (Nintendo, Homebrew, Extras, Builtin) instead of 3.
- Update `SectionLabel()` to return the new strings.
- Update sort comparator to order by category first.
- Desktop grid still flat — categories ONLY surface in Launchpad for K+1.0.

### Phase 2 (K+1.1)
- Add `IconKind::Folder` enum value.
- Add `folders.json` reader/writer.
- Add `QdFolderSheet` overlay element (similar to QdLaunchpad but filtered).
- Desktop grid renders folder entries with composite 2×2 thumbnail.
- Folder open/close gestures (tap = open sheet, B = close).

### Phase 3 (K+1.2 — gated on K+3 edit mode)
- Long-press → drag-to-create-folder gesture.
- Edit mode UI for renaming/recoloring/deleting folders.

---

## Anti-stub gates

Per global rule R42 (no-stubs-no-scaffolds):

1. **`IconCategory::Payloads`** is allocated in the enum but the corresponding
   classifier returns nothing for it in K+1.0. This is permitted ONLY because the
   enum value is used unconditionally (the section header renders empty if no entries
   match). It is not a stub — it's a complete-and-correct empty section. Convert to a
   real Hekate-payload scanner in K+6 or remove the enum value.

2. **`IsNintendoPublisher` cache file** must be created and invalidated correctly on
   first boot. Cannot ship the function without the cache write path implemented.

3. **`folders.json`** must round-trip (read → modify → write → re-read produces same
   bytes). Schema versioning matters; do not ship without a version migration test.

---

## Open questions for creator

1. **Category taxonomy** — does Nintendo / Homebrew / Extras / Payloads match your mental
   model, or should we name them differently? (E.g. "First-Party / Homebrew / Other / Payloads"?)
2. **Folder color picker** — fixed 8-color palette (matching Q OS brand) or full RGB?
3. **Folder thumbnail** — 2×2 composite of first 4 members, or single-color block with
   folder name centered?
4. **Implementation order** — do Phase 1 (categories) first and stabilize, or wait until
   K+3 lands so Phase 2 + Phase 3 ship together as "the Folders feature"?
5. **K+1 ships before K+5 (test rig) or after?** — K+5 is currently dispatched in parallel
   to a Sonnet agent; if K+1 lands first the harness will need to know about
   `IconCategory` to test it.

---

## What this doc does NOT cover

- **Filters** (search-by-category, "show only Homebrew") — that's K+2 (Settings + Filter chain).
- **Recents** (LRU-tracked recent apps) — that's K+4.
- **Drag-reorder of grid positions** — that's K+3.

Each of those is a separate design doc when its turn comes. K+1 is **only**
categories + folders.

---

## Cross-references

- `qd_DesktopIcons.hpp` — IconKind enum, NroEntry struct (extend here)
- `qd_Launchpad.cpp` — LpSortKind, RebuildFilter, SectionLabel (extend here)
- `qd_Launchpad.hpp` — LpItem (extend here)
- `QOS-REBRAND-ASSET-INVENTORY.md` — folder color palette aligns with brand
- `46_Stabilization_Handoff.md` — current desktop grid layout constants
- `47_Integratable_Tasks_Catalog.md` — K+1 entry in the K-cycle catalog
