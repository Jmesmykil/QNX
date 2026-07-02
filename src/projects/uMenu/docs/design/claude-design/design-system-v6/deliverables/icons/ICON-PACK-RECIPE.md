# Q OS — Icon-Pack Recipe

How to spin up a new icon pack (now and over time) so every pack stays coherent.
A **pack** = one named visual *treatment* applied to the full named-icon set.
Silhouettes/metaphors are **constant across packs** (a Settings gear is the same
gear everywhere); only the treatment + color change. That is the cohesion
contract.

## 1. Canvas & grid
- **192×192** transparent PNG (matches every existing icon). Provide **384×384
  @2x** vector masters in `icons/masters/`.
- **24px safe padding** → ~144px live area, centered. Optical-center, don't just
  bounding-box center.
- Flat (no isometric/perspective) for legibility at dock size (~84px) and TV
  distance.

## 2. Stroke / corner / color
- Line-treatment packs: stroke ≈ **12–14px** @192 (scales from the 2.2u @24
  family). Round joins + caps (Pixel pack: square/miter, crisp).
- Corners: `radius/sm` family feel.
- **Two values max:** silhouette + one accent detail. Color from the active
  theme (engine tints) — author masters monochrome/grayscale where the pack is
  tintable, or bake the theme color for fixed packs.

## 3. The 17 per-theme names (exact filenames — must match)
```
DefaultApplication  DefaultHomebrew  DockAbout  DockAllPrograms  DockControl
DockMonitor  DockTasks  DockVault  Empty  Folder  FolderEmulators
FolderGames  FolderOther  FolderQOS  FolderSystem  FolderTools  HotCornerQ
```
Drop → `romfs/themes/q-os-{idx}-*.ultheme :: ui/Main/EntryIcon/<Name>.png`.

## 4. The base / universal set (24) → `romfs/default/ui/Main/EntryIcon/`
The 17 above **plus** the special-feature icons all themes fall back to:
`Album Amiibo Controllers MiiEdit Settings Themes WebBrowser` — all **generic
originals, NO Nintendo IP**: Album = photo card, Controllers = generic gamepad
(no Joy-Con), MiiEdit = generic avatar (non-Mii), Amiibo = NFC waves,
WebBrowser = globe, Empty = dotted-rounded placeholder, Default* = branded
fallbacks.

## 5. System dirs (same family) → `romfs/default/ui/Main/`
`OverIcon/` (Corrupted, Gamecard, HomebrewTakeover, NeedsUpdate, NotLaunchable,
Suspended, Selected), `PowerIcon/` (Hekate, Restart, Shutdown, Sleep),
`TopIcon/` (Bluetooth), `PayloadIcon/` (generic launcher icons). Cross-check the
shipping gate: `docs/QOS-REBRAND-ASSET-INVENTORY.md`.

## 6. The ten treatments (locked personalities — see Icon-pack system card)
| Pack | Treatment |
|---|---|
| Glass | smooth filled silhouette, accent |
| Neon | outline + glow |
| Minimal | thin line |
| Retro | pixel / NES, amber |
| Cards | filled, playing-card-suit motifs |
| Pastel | rounded blobs w/ eye-dots |
| Dark | heavy slab |
| Gradient | flowing curves, violet→cyan gradient stroke |
| Blueprint | thin white technical line + dimension ticks |
| Pixel | strict pixel grid, NES primaries |

## 7. Per-icon vocabulary
The icon system defines distinct shapes per theme for the **folder/category +
emblem** roles (a vault in Glass ≠ a vault in Pixel) but a **constant metaphor**
for status/dock roles (Wi-Fi is Wi-Fi). Use the **Icon-pack system** specimen
card as the visual source of truth; `glyphs/masters/*.svg` for the constant
family marks.

## 8. Export checklist
- [ ] 192×192, transparent, centered, 24px safe pad.
- [ ] All 17 names present (packs) / 24 (base).
- [ ] @2x master saved to `icons/masters/`.
- [ ] No Nintendo/trademarked IP.
- [ ] Reads at 84px (dock) and on a TV from the couch.
- [ ] Treatment matches the pack's row in §6.
- [ ] Hand back loose files in the per-theme folder structure (engineers ZIP the
      `.ultheme` via `scripts/generate-qos-ultheme-bundles.py`).
