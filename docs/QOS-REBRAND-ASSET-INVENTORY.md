# Q OS Rebrand Asset Inventory

> **Authoring SSOT:** `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uMenu/romfs/default/`
> **Generated:** 2026-04-25T14:00:00Z (J-tweak2 deploy)
> **Purpose:** Track every `.png` asset borrowed from upstream uLaunch (XorTroll/Stary2001) that
> must be replaced with a Q OS-original asset before the public hard-fork release.
>
> Per creator directive: *"This is a loveletter to all of them"* — upstream theme art is left in
> the historical record (LICENSE-AUDIT.md / UPSTREAM-ANALYSIS.md / commit history) but the
> shipping default theme must be Q OS-original to avoid trademark / brand-confusion concerns and
> to give the rebrand a clean visual identity. Functional PNGs that are pure Switch system art
> (battery, connection ladder) may be retained or recreated.

---

## Total surface

- **56 PNGs** under `romfs/default/ui/`
- 1 hero asset (Background, 200 KB)
- 35 bespoke icons (EntryIcon / QuickIcon / OverIcon / TopMenuBackground / EntryMenu*)
- 11 battery state icons (10..100, Charging)
- 5 connection-strength ladder icons
- 4 Settings UI chrome PNGs

---

## Priority 1 — Hero / desktop-visible (replace FIRST)

| Path | Bytes | Origin | Replacement target |
|---|---:|---|---|
| `ui/Background.png` | 204 015 | Upstream desktop wallpaper | Q OS original wallpaper, 1920×1080, brand palette (deep navy / cyan glow) |
| `ui/Main/EntryMenuBackground.png` | 9 753 | Upstream entry-menu chrome | Q OS rounded-rect chrome with brand stroke |
| `ui/Main/InputBarBackground.png` | — | Upstream bottom-bar chrome | Q OS dock bar chrome |
| `ui/Main/OverIcon/Cursor.png` | 20 257 | Upstream cursor reticle | Q OS cursor — circular brand-cyan ring |
| `ui/Main/OverIcon/Selected.png` | — | Upstream selection halo | Q OS selection halo |

---

## Priority 2 — Special-icon glyphs (replace before K-cycle ships)

These are loaded by `qd_DesktopIcons.cpp` for the 8 SpecialEntry* IDs (Settings/Album/Themes/
Controllers/MiiEdit/WebBrowser/UserPage/Amiibo). They're upstream uLaunch art and currently
the desktop's most prominent borrowed pixels.

| Path | Bytes | EntryType | Replacement |
|---|---:|---|---|
| `ui/Main/EntryIcon/Settings.png` | 12 103 | SpecialEntrySettings | Q OS gear, brand-cyan accent |
| `ui/Main/EntryIcon/Album.png` | — | SpecialEntryAlbum | Q OS album — stylized photo card |
| `ui/Main/EntryIcon/Themes.png` | 10 414 | SpecialEntryThemes | Q OS theme palette |
| `ui/Main/EntryIcon/Controllers.png` | — | SpecialEntryControllers | Q OS controller silhouette |
| `ui/Main/EntryIcon/MiiEdit.png` | 17 844 | SpecialEntryMiiEdit | Q OS avatar — non-Mii style |
| `ui/Main/EntryIcon/WebBrowser.png` | 19 710 | SpecialEntryWebBrowser | Q OS globe |
| `ui/Main/EntryIcon/Amiibo.png` | 12 632 | SpecialEntryAmiibo | Q OS NFC waves — generic, non-trademark |
| `ui/Main/EntryIcon/Empty.png` | — | empty slot | Q OS dotted-rounded slot placeholder |

**Plus the QuickIcon mirrors** (same 8 entries, smaller variant) — if used post-K-cycle, replace.

---

## Priority 3 — Defaults + decorative

| Path | Bytes | Notes |
|---|---:|---|
| `ui/Main/EntryIcon/DefaultApplication.png` | — | Fallback icon for an Application entry with no JPEG. Replace with Q OS-branded grid square. |
| `ui/Main/EntryIcon/DefaultHomebrew.png` | — | Fallback for an NRO entry without an embedded NACP icon. Q OS hammer/wrench glyph. |
| `ui/Main/EntryIcon/Folder.png` | 8 913 | Folder badge for grouped entries (K+1 surfaces this). Q OS folder. |
| `ui/Main/TopMenuBackground/{Default,Application,Folder,Homebrew}.png` | — | Top-area chrome variants per context. Q OS dark gradient. |
| `ui/Main/EntryMenuLeftIcon.png` / `…RightIcon.png` | — | Entry-menu side glyphs. Replace if the entry menu remains in K-cycle. |

---

## Priority 4 — Status overlays (small, pixel-perfect)

These need pixel-accurate redesign because they render at exact small sizes on top of icons.

| Path | Notes |
|---|---|
| `ui/Main/OverIcon/Border.png` | Hover/focus border |
| `ui/Main/OverIcon/Suspended.png` | App is suspended in background |
| `ui/Main/OverIcon/Corrupted.png` | App data corrupted badge |
| `ui/Main/OverIcon/Gamecard.png` | Game card insertion badge |
| `ui/Main/OverIcon/NeedsUpdate.png` | Update available |
| `ui/Main/OverIcon/NotLaunchable.png` | Cannot launch (DLC, missing parent) |
| `ui/Main/OverIcon/HomebrewTakeoverApplication.png` | HBL-launched native title |

---

## Priority 5 — System-symbol PNGs (consider keeping)

These are functional Switch system iconography. Recreating them in original style is fine but
pure-functional retention is acceptable. Decide at packaging time.

| Path | Count |
|---|---:|
| `ui/Main/TopIcon/Battery/{10..100,Charging}.png` | 11 |
| `ui/Main/TopIcon/Connection/{0..3,None}.png` | 5 |
| `ui/Main/QuickIcon/Power.png` | 1 |

---

## Priority 6 — Settings UI chrome

| Path | Notes |
|---|---|
| `ui/Settings/InputBarBackground.png` | Settings bottom bar |
| `ui/Settings/SettingEditableIcon.png` | Editable-row pencil glyph |
| `ui/Settings/SettingNonEditableIcon.png` | Locked-row glyph |

These ship with Track A's `qd_SettingsLayout` once promoted. Re-skin in Q OS palette.

---

## Brand palette reference

> Cyan `#00E5FF` + deep navy `#0E1A33` + magenta accent `#D946EF`.
> Logo: Q OS wordmark, SF Pro Display Bold for desktop, JetBrains Mono for monospace surfaces.
> Cross-ref: `feedback_filament_brand_palette` (sister project) — Q OS borrows the Filament
> palette as a starting point, then forks toward a colder navy base.

---

## Workflow for rebrand pass

1. Generate originals via Photoshop MCP (`mcp__photoshop__*`) at the documented dimensions
   (use `sips -g pixelWidth -g pixelHeight <path>` to read upstream dimensions before sourcing).
2. Drop each new PNG into `assets/qos-rebrand/` (NEW dir — keep upstream PNGs untouched until
   the swap commit).
3. Single atomic commit: `git mv` upstream PNG → `archive/upstream-art-<sha>/` then `cp` Q OS
   original into the original path. Build, deploy, visual-verify on hardware.
4. Confirm `LICENSE-AUDIT.md` "art assets" row updates from "GPLv2 (upstream)" to
   "CC-BY-SA-4.0 (Q OS originals)" or "All rights reserved (Q OS originals)" per creator's
   licensing decision.

---

## Status

- ☑ P1 (5 hero assets) — **DONE 2026-04-25T16:33:00Z** — ImageMagick 7.1.2-17, Q OS brand palette.
  Placed in both `src/default-theme/ui/` and `src/projects/uMenu/romfs/default/ui/`.
  Staged originals in `assets/qos-rebrand/`.
  | Asset | Old bytes | New bytes | Dimensions |
  |---|---:|---:|---|
  | `ui/Background.png` | 204 015 | 17 284 | 1920×1080 |
  | `ui/Main/EntryMenuBackground.png` | 9 753 | 13 498 | 1920×837 |
  | `ui/Main/InputBarBackground.png` | 1 062 | 3 230 | 1797×60 |
  | `ui/Main/OverIcon/Cursor.png` | 20 257 | 11 494 | 444×444 |
  | `ui/Main/OverIcon/Selected.png` | 4 306 | 1 787 | 416×416 |
- ☑ P2 (8 Special icons) — **DONE 2026-04-25T17:15:00Z** — ImageMagick 7.1.2-17, Q OS brand palette.
  Placed in both `src/default-theme/ui/Main/EntryIcon/` and `src/projects/uMenu/romfs/default/ui/Main/EntryIcon/`.
  Archived upstream PNGs to `archive/upstream-art-p2/`.
  | Asset | Old bytes | New bytes |
  |---|---:|---:|
  | `Settings.png` | 12 103 | 15 930 |
  | `Album.png` | 7 564 | 11 442 |
  | `Themes.png` | 10 414 | 16 165 |
  | `Controllers.png` | 7 629 | 6 891 |
  | `MiiEdit.png` | 17 844 | 18 527 |
  | `WebBrowser.png` | 19 710 | 39 684 |
  | `Amiibo.png` | 12 632 | 16 631 |
  | `Empty.png` | 1 801 | 2 448 |
- ☑ P3 (defaults + chrome) — **DONE 2026-04-25T17:45:00Z** — ImageMagick 7.1.2-17, Q OS brand palette.
  Placed in both `src/default-theme/ui/Main/` and `src/projects/uMenu/romfs/default/ui/Main/`.
  Archived upstream PNGs to `archive/upstream-art-p3/`.
  | Asset | Upstream bytes | New bytes | Dimensions | New SHA8 |
  |---|---:|---:|---|---|
  | `EntryIcon/DefaultApplication.png` | 6 876 | 5 753 | 384×384 | 06be33c9 |
  | `EntryIcon/DefaultHomebrew.png` | 6 711 | 14 557 | 384×384 | 1f45aaeb |
  | `EntryIcon/Folder.png` | 8 913 | 2 960 | 384×384 | 492ac869 |
  | `TopMenuBackground/Default.png` | 1 958 | 2 307 | 1797×136 | a25613e3 |
  | `TopMenuBackground/Application.png` | 1 957 | 2 314 | 1797×136 | 46c1339f |
  | `TopMenuBackground/Folder.png` | 1 950 | 2 318 | 1797×136 | 7c095dec |
  | `TopMenuBackground/Homebrew.png` | 1 934 | 2 317 | 1797×136 | 04d0dce9 |
  | `EntryMenuLeftIcon.png` | 1 665 | 871 | 90×837 | 63439ab8 |
  | `EntryMenuRightIcon.png` | 1 642 | 915 | 90×837 | f126b5db |
- ☑ P4 (status overlays) — **DONE 2026-04-25T17:23:00Z** — ImageMagick 7.1.2-17, Q OS brand palette.
  Placed in both `src/default-theme/ui/Main/OverIcon/` and `src/projects/uMenu/romfs/default/ui/Main/OverIcon/`.
  Archived upstream PNGs to `archive/upstream-art-p4/`.
  | Asset | Old bytes | New bytes | Dimensions | New SHA8 |
  |---|---:|---:|---|---|
  | `OverIcon/Border.png` | 2 383 | 5 965 | 444×444 | ca99fc55 |
  | `OverIcon/Suspended.png` | 3 859 | 1 903 | 416×416 | d5a6d978 |
  | `OverIcon/Corrupted.png` | 1 972 | 13 868 | 384×384 | 5352671d |
  | `OverIcon/Gamecard.png` | 3 716 | 5 426 | 488×488 | acd60b64 |
  | `OverIcon/NeedsUpdate.png` | 2 410 | 11 622 | 384×384 | f92e2f15 |
  | `OverIcon/NotLaunchable.png` | 3 185 | 25 262 | 384×384 | d2573744 |
  | `OverIcon/HomebrewTakeoverApplication.png` | 2 128 | 4 955 | 384×384 | 093e9c95 |
- ☐ P5 (system symbols) — keep-or-rebuild decision pending
- ☐ P6 (Settings chrome) — blocked on Track A promotion completing

Updated: 2026-04-25T17:23:00Z
