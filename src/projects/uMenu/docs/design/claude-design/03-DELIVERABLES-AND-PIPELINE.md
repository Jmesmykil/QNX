# Q OS — Deliverables Manifest + Repo Drop Pipeline

> Companion to `00`–`02`. This is the concrete checklist: **exactly what files Claude Design should
> output, in what format/size, with what name, and where each one drops back into the repo** so a
> handed-back package plugs straight in. Filenames below match the real assets in the tree
> (enumerated from `romfs/`).
>
> Repo root for all paths: `src/projects/uMenu/` (i.e.
> `/Users/astral/QOS/tools/qos-ulaunch-fork/src/projects/uMenu/`).
>
> **Two integration prerequisites** (engineering tasks, not design tasks) are flagged ⚙️ — assets are
> still authored to the documented contract; they just need a one-time code wire-up to go live.

---

## 0. Output structure Claude Design should hand back

Deliver a single package mirroring this shape (so it maps 1:1 onto the repo):

```
qos-design-handback/
├─ design-system/
│  ├─ tokens.json            # the unified token set (colors + type/space/radius/elev/motion)
│  ├─ palettes.json          # all 10 retuned palettes in the QdPalette schema (see §1)
│  ├─ Q-OS-DESIGN-SYSTEM.pdf # the human-readable system doc (specimen, rules, do/don't)
│  └─ figma-or-svg-masters/  # editable vector source for everything raster
├─ selection/                # selection-outline spec + optional 9-slice (see §2)
├─ cursor/                   # cursor spec + optional PNGs (see §3)
├─ icons/
│  ├─ base/                  # the default/base icon set → romfs/default/... (see §4)
│  └─ packs/q-os-{0..9}/     # per-theme icon packs (see §4)
├─ glyphs/                   # glyph SVG masters + Logo.png + theme Icon.png (see §5)
├─ wallpapers/q-os-{0..9}/   # one 1280×720 Background.png per theme (see §6)
└─ font/                     # Font.ttf + specimen + LICENSE (see §7)
```

---

## 1. Design tokens + palettes

| Deliverable | Format | Notes |
|-------------|--------|-------|
| `tokens.json` | JSON | The non-color tokens from `02` Part I: `type/*`, `space/*`, `radius/*`, `elev/*`, `motion/*`. Documentation/contract for engineers. |
| `palettes.json` | JSON | All **10** retuned palettes, **using the exact 18-role `QdPalette.json` schema** (`02` T1). One object per theme keyed by index/name. |
| `Q-OS-DESIGN-SYSTEM.pdf` (or .md) | doc | The system: token tables, palette table, type specimen, selection/cursor/icon/glyph rules, do/don't. |

**Drop / use:**
- Each theme's palette object → replaces the contents of **`romfs/themes/q-os-{idx}-*.ultheme` →
  `ui/QdPalette.json`** (the `.ultheme` is a ZIP; engineers regenerate the bundles, often via the
  existing `scripts/generate-qos-ultheme-bundles.py` mentioned in project memory).
- ⚙️ **Prerequisite #1 — palette is mirrored in C++.** The same 10 palettes are hardcoded as
  factories in **`include/ul/menu/qdesktop/qd_Theme.hpp`** (`QdTheme::Glass()` … `Pixel()`). Any
  retuned hex must be applied **there too** (or the in-binary default diverges from the bundle). Make
  the hex copy-paste-able (one table) so this is mechanical. *(Treat `QdPalette.json` as SSOT — see
  `01` audit 🔴3.)*
- If you propose any **new** color keys (e.g. `selection_glow`), list them explicitly here so
  engineering adds the struct field + a `_QD_PALETTE_APPLY(... )` line in `qd_Theme.cpp`. Prefer
  reusing the existing 18.

---

## 2. Selection outline 🔴

| Deliverable | Format | Size | Notes |
|-------------|--------|------|-------|
| `selection/SELECTION-SPEC.md` | doc | — | Geometry: corner radius (→ `radius/sm`), ring thickness (≥3px @1080), inset, glow params, and the **focused / selected / hover** state diffs. The primary deliverable (the ring is code-drawn). |
| `selection/selection_frame.png` *(optional)* | transparent PNG, 9-slice | ~64×64 corner tile | Only if the design needs a gradient/glow ring cheaper to blit than draw. Center transparent (icon shows through). Grayscale/tintable. |
| `selection/Selected.png` *(if keeping legacy halo)* | transparent PNG | **416×416** | Redesigned legacy halo, matching the new ring exactly. |

**Drop / use:**
- Spec → engineers update the ring draw in **`source/ul/menu/qdesktop/qd_Launchpad.cpp`**
  (`PaintCell()` focus-ring block, ~line 2674; folder-tab ring ~line 1996; search-bar ring ~line
  1901 — also **remove the hardcoded `#0080AA`** there) and **`qd_Window.cpp`** (window focus ring
  ~line 292). Color stays `g_QdTheme.focus_ring` (per-theme, already wired).
- 9-slice (if any) → a new `romfs/default/ui/` path (engineers name it; suggest
  `ui/Main/Selection/Frame.png`).
- `Selected.png` → **`romfs/default/ui/Main/OverIcon/Selected.png`** (overwrites the 416×416 legacy
  halo used by the non-qdesktop `ui/` menu).

---

## 3. Cursor 🔴

State your chosen path (spec-driven procedural **recommended**, or PNG) at the top of the spec.

| Deliverable | Format | Size | Notes |
|-------------|--------|------|-------|
| `cursor/CURSOR-SPEC.md` | doc | — | Silhouette, radii/alphas, **hotspot** (today = center (22,22) of a 44×44 texture — say if you change it), the **right-click** state diff, contrast-survival core. Colors mapped to `cursor_fill` / `cursor_outline` / `cursor_right_click`. |
| `cursor/pointer.png` *(only if PNG path)* | transparent PNG | **44×44** (+ `@2x` 88×88 master) | Base pointer. Footprint must match the existing 44×44 / center-hotspot math (or document the new offset). |
| `cursor/right_click.png` *(only if PNG path)* | transparent PNG | 44×44 (+@2x) | Right-click variant. |

**Drop / use:**
- **Procedural path (recommended):** spec → engineers edit **`source/ul/menu/qdesktop/qd_Cursor.cpp`**
  to (a) replace the hardcoded `BRAND_CYAN_R/G/B` (line ~32) with `g_QdTheme.cursor_fill` /
  `cursor_outline`, and (b) add the `cursor_right_click` state. No asset file ships. **This also
  fixes `01` audit 🔴2** (cursor finally honors the theme).
- **PNG path:** cursor PNG(s) → engineers add a load path (suggest `romfs/default/ui/Main/Cursor/…`).
  If a tintable grayscale cursor isn't feasible you must ship per-theme PNGs (10×{pointer,right_click})
  — avoid if possible (VRAM + churn).

---

## 4. Icons + icon packs 🟡

**Format for ALL icons:** transparent **PNG, 192×192** (matches every existing icon). Provide **@2x
384×384** vector-exported masters in `figma-or-svg-masters/`. Honor `02` §C grid/stroke/corner rules.
**No Nintendo/trademarked IP.**

### 4a. Base / default set → `romfs/default/ui/Main/EntryIcon/`
Deliver all **24** (these are theme-0's set + the universal fallbacks + the special-feature icons all
themes fall back to). Names must match **exactly**:

```
Album.png  Amiibo.png  Controllers.png  DefaultApplication.png  DefaultHomebrew.png
DockAbout.png  DockAllPrograms.png  DockControl.png  DockMonitor.png  DockTasks.png  DockVault.png
Empty.png  Folder.png  FolderEmulators.png  FolderGames.png  FolderOther.png  FolderQOS.png
FolderSystem.png  FolderTools.png  HotCornerQ.png  MiiEdit.png  Settings.png  Themes.png  WebBrowser.png
```
Drop → **`romfs/default/ui/Main/EntryIcon/<Name>.png`** (overwrite). Design notes per `02` §C
(Album=photo card, Controllers=generic gamepad, MiiEdit=generic avatar, Amiibo=NFC waves, WebBrowser=
globe, Empty=dotted placeholder, Default*=branded fallbacks).

### 4b. Per-theme packs → inside each `.ultheme`
Each of the 10 packs delivers the **17** dock/folder icons (the special-feature icons fall back to
base, so packs don't ship them). Names must match exactly:

```
DefaultApplication.png  DefaultHomebrew.png  DockAbout.png  DockAllPrograms.png  DockControl.png
DockMonitor.png  DockTasks.png  DockVault.png  Empty.png  Folder.png  FolderEmulators.png
FolderGames.png  FolderOther.png  FolderQOS.png  FolderSystem.png  FolderTools.png  HotCornerQ.png
```
Drop → packaged into **`romfs/themes/q-os-{idx}-*.ultheme` → `ui/Main/EntryIcon/<Name>.png`**. One
treatment per theme (`02` §C(c)) — same silhouettes, theme material applied. *(Theme 0 "Q OS" ships
no per-theme EntryIcons today and uses the base set — keep that, or add a pack for it; state which.)*

### 4c. System icon dirs (part of the family) → `romfs/default/ui/Main/`
Originate these too (currently base/upstream art):
- **`OverIcon/`** (transparent PNGs, icon-cell-sized): `Corrupted.png  Gamecard.png
  HomebrewTakeoverApplication.png  NeedsUpdate.png  NotLaunchable.png  Suspended.png` (+ `Selected.png`
  in §2). These overlay app icons.
- **`PowerIcon/`**: `Hekate.png  Restart.png  Shutdown.png  Sleep.png`.
- **`TopIcon/`**: `Bluetooth.png`.
- **`PayloadIcon/`** (small launcher icons): `icon_hekate.png  icon_daybreak.png  icon_lockpick_rcm.png
  icon_tegraexplorer.png  icon_biskeydump.png  icon_choi_dujour_nx.png  icon_quick_reboot.png
  icon_reboot_to_hekate.png  icon_reboot_to_payload.png  icon_payload_generic.png`. *(These name
  third-party homebrew tools — make them generic/recognizable without copying logos.)*

> Cross-check everything in 4a–4c against **`docs/QOS-REBRAND-ASSET-INVENTORY.md`** (the authoritative
> "upstream art to replace" list, 56 PNGs, prioritized). It's the shipping gate for the public fork.

### 4d. Icon-pack recipe → `icons/ICON-PACK-RECIPE.md`
The template + rules so new packs (now and over time) stay consistent: canvas/safe-area, stroke,
corner, color usage, the full required filename list, and an export checklist. This is what makes
icon packs *repeatable*.

---

## 5. Glyphs + brand mark 🟡

| Deliverable | Format | Size | Drop / use |
|-------------|--------|------|-----------|
| `glyphs/GLYPH-SPEC.md` + SVG masters | doc + SVG | — | Window-button glyphs (Close/Max/Min/Restore/Resize, live ~18px in a 32px disc), brand **Q**, favorites star, badges, status marks. Monochrome/tintable. Window/brand glyphs are code-drawn → spec drives `qd_Window.cpp` + `qd_Theme.cpp`. |
| `glyphs/Logo.png` | transparent PNG | **256×256** | → **`romfs/Logo.png`** (overwrite). The master brand mark. |
| `glyphs/theme-icons/q-os-{0..9}.png` | transparent PNG | **256×256** (engine scales) | → each **`.ultheme` → `theme/Icon.png`** (replaces the 1×1 placeholder; theme picker preview). Confirm final size if engine wants smaller. |
| `glyphs/star.png` *(optional)* | transparent PNG | ~32×32 | If replacing the `★` font glyph with raster; else the new font's star is fine. Drop under `romfs/default/ui/Main/` (engineers name). |

Keep glyph shapes constant across themes (color applied by engine). A Pixel-grid glyph variant is an
optional documented flourish.

---

## 6. Wallpapers 🟡

| Deliverable | Format | Size | Notes |
|-------------|--------|------|-------|
| `wallpapers/q-os-{0..9}/Background.png` | PNG (JPEG/WebP/BMP also load) | **1280×720** | One per theme, full-bleed 16:9, central ~92% safe, moderate contrast under top bar (48px) + dock (148px). Keep file size modest. |
| `wallpapers/WALLPAPER-PACK-RECIPE.md` | doc | — | Defines a "wallpaper pack" (named set of 1280×720 images), naming, and how packs map to themes / stand alone — for future packs. |
| `wallpapers/masters/` *(optional)* | 1920×1080 source | — | Editable hi-res master; shipped file stays 1280×720 (bake res + VRAM). |

**Drop / use:**
- Each `Background.png` → packaged into **`romfs/themes/q-os-{idx}-*.ultheme` → `ui/Background.png`**.
- ⚙️ **Prerequisite #2 — wire the image wallpaper into boot.** Today the active layouts always create
  the *procedural* `QdWallpaperElement` (`ui_MainMenuLayout.cpp:738`, `ui_StartupMenuLayout.cpp:69`,
  `qd_LockscreenLayout.cpp:331`). To use the shipped images, engineers must instantiate
  `QdImageWallpaperElement::New(ul::cfg::GetActiveThemeResource("ui/Background.png"))` when a
  `Background.png` is present in the active theme cache (the "Phase B wire-up" the header already
  anticipates). The element + load path already exist (`01` §7); the assets are authored to its
  contract regardless.

---

## 7. Custom font 🟡

| Deliverable | Format | Notes |
|-------------|--------|-------|
| `font/Font.ttf` (+ weights) | **.ttf** (or .otf) | SDL_ttf-loadable. Full **Basic Latin + Latin-1** coverage. Regular + Medium/Semibold + Bold (+ optional condensed). Clear tabular figures. |
| `font/SPECIMEN.pdf` | doc | Weights, sizes, the type scale (`02` T2), legibility at 14px @1080. |
| `font/LICENSE` | text | **OFL or similar redistributable license — state it explicitly.** Required to ship in the open-source binary. |

**Drop / use:**
- Per-theme: package **`Font.ttf` inside each `.ultheme` → `ui/Font.ttf`**. `main.cpp:616` picks it up
  automatically when the theme is active — **no code change needed to *use* it**, just ship the file.
- OS-wide (recommended for cohesion): ship the **same** `Font.ttf` in **all 10** bundles, OR place it
  once in **`romfs/default/`** and have engineers point `TryGetActiveThemeResource` fallback at it
  (small loader tweak — note for engineering). State which model you intend.
- Optional thematic alternates (pixel font for Pixel, mono for Blueprint, CRT for Retro) ship in just
  those bundles' `ui/Font.ttf` — each must still have full Latin coverage.

---

## 8. Integration summary — where each handback file lands

| Handback | Repo destination |
|----------|------------------|
| `palettes.json` (per theme) | `romfs/themes/q-os-{idx}-*.ultheme :: ui/QdPalette.json` **+** `include/ul/menu/qdesktop/qd_Theme.hpp` factories ⚙️ |
| `tokens.json`, design-system PDF | `docs/design/claude-design/` (alongside this brief) — engineering reference |
| Selection spec / 9-slice / `Selected.png` | code (`qd_Launchpad.cpp`, `qd_Window.cpp`) / `romfs/default/ui/Main/{Selection,OverIcon}/` |
| Cursor spec / PNGs | code (`qd_Cursor.cpp`) / `romfs/default/ui/Main/Cursor/` (if PNG) |
| Base icons (24) | `romfs/default/ui/Main/EntryIcon/<Name>.png` |
| Per-theme icon packs (17 × 10) | `romfs/themes/q-os-{idx}-*.ultheme :: ui/Main/EntryIcon/<Name>.png` |
| System icons (OverIcon/PowerIcon/TopIcon/PayloadIcon) | `romfs/default/ui/Main/<dir>/<Name>.png` |
| `Logo.png` | `romfs/Logo.png` |
| Theme thumbnails (`theme/Icon.png` × 10) | `romfs/themes/q-os-{idx}-*.ultheme :: theme/Icon.png` |
| Wallpapers (10) | `romfs/themes/q-os-{idx}-*.ultheme :: ui/Background.png` ⚙️ |
| `Font.ttf` | `romfs/themes/q-os-{idx}-*.ultheme :: ui/Font.ttf` (+ optional `romfs/default/`) |
| Glyph/icon SVG masters, font sources | `docs/design/claude-design/masters/` (editable, not shipped to device) |

**The `.ultheme` bundles are ZIP files** — engineers (re)build them from the loose assets, almost
certainly via the existing `scripts/generate-qos-ultheme-bundles.py` (referenced in project memory),
so hand back **loose files in the per-theme folder structure** above; do not try to author the ZIPs.

**Two ⚙️ engineering prerequisites recap (not design work, but gate go-live):**
1. Apply retuned palettes to the **C++ factories** in `qd_Theme.hpp` (mirror of `QdPalette.json`).
2. **Wire `QdImageWallpaperElement`** into the boot layouts so shipped `Background.png` images render
   (the element + path already exist).

Everything else (selection, cursor, icons, glyphs, font) drops in via existing, verified load paths.
