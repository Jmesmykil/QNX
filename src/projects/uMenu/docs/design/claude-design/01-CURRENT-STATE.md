# Q OS — Current Visual System: Inventory + Honest Audit

> Companion to `00-BRIEF.md`. This is the *ground truth* of what exists today, read straight from
> the source. Every claim below cites the file it came from so a designer can trust it without
> opening the repo. The **AUDIT** call-outs (🔴 = high priority, 🟡 = medium) are the "tighten-up" targets.
>
> All paths are relative to `src/projects/uMenu/` unless absolute.

---

## 1. The theme system — how a "theme" actually works

A Q OS theme is a **`.ultheme` file, which is a ZIP archive** (verified: `file` reports
"Zip archive data"). The 10 bundled themes live at `romfs/themes/q-os-{0..9}-*.ultheme`.

Each archive contains, at minimum:
- `theme/Manifest.json` — metadata (name, version, author, description, `format_version: 3`).
- `theme/Icon.png` — a theme thumbnail. **Currently a 1×1 placeholder** (72 bytes) in every
  bundle. 🟡
- `ui/QdPalette.json` — **the palette** (the heart of the theme): 18 named color roles as hex.
- *(themes 1–9 only)* `ui/Main/EntryIcon/*.png` — 17 per-theme **folder/dock icon** PNGs
  (DockVault, DockMonitor, DockAbout, DockAllPrograms, DockControl, DockTasks, Folder,
  DefaultApplication, DefaultHomebrew, Empty, FolderGames, FolderEmulators, FolderTools,
  FolderSystem, FolderQOS, FolderOther, HotCornerQ).
- *(theme 0 only)* ships **no** EntryIcon PNGs — it relies entirely on `romfs/default/ui/...`.

Theme 0 ("Q OS") archive is tiny (816 B, 3 files: Manifest + 1×1 Icon + palette). Themes 1–9 are
20 files each, dominated by their EntryIcon PNG sets (Neon is the heaviest at ~217 KB because its
icons are large glowy PNGs; Minimal/Cards icons are ~10 KB total because they're tiny line glyphs).

**Loading path** (`source/ul/menu/qdesktop/qd_Theme.cpp`):
- On theme apply, the `.ultheme` ZIP is extracted to a cache dir (e.g.
  `sdmc:/ulaunch/cache/active/`), then `LoadThemeFromCache()` reads `ui/QdPalette.json` and
  overlays any present keys onto the live palette (`g_QdTheme`). Absent keys keep the prior
  theme's value. Hex is parsed by `ParseHexColor()` accepting `#RRGGBB` or `#RRGGBBAA`.
- The same 10 palettes are **also hardcoded as C++ factories** in
  `include/ul/menu/qdesktop/qd_Theme.hpp` (`QdTheme::Glass()`, `Neon()`, … `Pixel()`), used as
  the in-binary defaults. `SetActivePalettePack(idx)` switches palette; `SetActiveWallpaperPack(idx)`
  switches wallpaper; `SetActiveThemePack(idx)` does both.
- `QdPalette.json` also carries a Q-OS-specific **`"wallpaper_pack"`** string (e.g. `"Neon"`)
  that maps to the procedural wallpaper index.

> 🔴 **AUDIT — palette defined in two places.** Every palette exists *both* as hex in
> `ui/QdPalette.json` *and* as `Rgb(...)` literals in `qd_Theme.hpp`. They are kept in sync by
> hand. (Spot-check: they currently match — e.g. Glass accent `#7DD3FC` == `Rgb(0x7D,0xD3,0xFC)`.)
> A redesign that changes palettes must change **both** representations or the in-binary default
> and the bundled `.ultheme` will disagree. *Recommendation in `02`: treat `QdPalette.json` as the
> SSOT and have the brief output a single hex table that maps cleanly to both.*

### 1a. The 18 color roles (the token schema)
Read from `ui/QdPalette.json` in every theme. (Header `qd_Theme.hpp` comments say "17 tokens" — it
predates `grid_line`; the JSON and struct actually carry **18** including `wallpaper_pack`, or 17
colors + 1 string.) Roles:

| Role | What it colors |
|------|----------------|
| `desktop_bg` | the desktop backdrop base (behind the wallpaper math) |
| `surface_glass` | window bodies, panels, tiles, "glass" surfaces |
| `topbar_bg` | the top bar ("the Bridge") |
| `dock_bg` | the dock ("the Deck") |
| `accent` | primary brand/action color (active tile borders, emphasis) |
| `text_primary` | main text |
| `text_secondary` | dimmed/secondary text, inactive ring |
| `focus_ring` | **the selection / focus outline** (see §3) |
| `button_close` | window close button (TL) |
| `button_minimize` | window minimize button (BL) |
| `button_maximize` | window maximize button (TR) |
| `button_restore` | window restore (post-maximize) |
| `cursor_fill` | **intended** cursor body color (see §4 — currently ignored!) |
| `cursor_outline` | **intended** cursor outline color (currently ignored!) |
| `cursor_right_click` | **intended** right-click cursor accent (currently ignored!) |
| `titlebar_inactive` | unfocused window titlebar |
| `grid_line` | desktop grid lines + window content border |
| `wallpaper_pack` | *(string)* which procedural wallpaper to use |

### 1b. Full palettes — all 10 themes (verbatim from each `QdPalette.json`)

**0 — Q OS** (flagship; Manifest: "Dark Liquid Glass. Cyan accent on deep navy, Cold Plasma Cascade wallpaper")
```
desktop_bg #0A0A14  surface_glass #12122A  topbar_bg #0C0C20  dock_bg #10102A
accent #7DD3FC  text_primary #E0E0F0  text_secondary #8888AA  focus_ring #7CC5FF
button_close #F87171  button_minimize #FBBF24  button_maximize #4ADE80  button_restore #4ADE80
cursor_fill #F5F5FF  cursor_outline #050510  cursor_right_click #E54B4B
titlebar_inactive #181830  grid_line #181832  wallpaper_pack "Q OS"
```

**1 — Neon** ("Black base + hot magenta + electric cyan + lime")
```
desktop_bg #050010  surface_glass #140522  topbar_bg #0A0018  dock_bg #100520
accent #FF2AD0  text_primary #F0FFF0  text_secondary #A870C8  focus_ring #2AFFE5
button_close #FF326E  button_minimize #F5E02A  button_maximize #6AFF50  button_restore #6AFF50
cursor_fill #FFF0FF  cursor_outline #100018  cursor_right_click #FF2AD0
titlebar_inactive #220A32  grid_line #2A103A  wallpaper_pack "Neon"
```

**2 — Minimal** ("Warm stone gray + dusty amber accent")
```
desktop_bg #1A1816  surface_glass #262422  topbar_bg #1E1C1A  dock_bg #22201E
accent #D4C8B4  text_primary #F2EEE6  text_secondary #90887C  focus_ring #C8A86A
button_close #E06060  button_minimize #E0C040  button_maximize #40C870  button_restore #40C870
cursor_fill #FAF6EE  cursor_outline #1A1816  cursor_right_click #E06060
titlebar_inactive #2E2A26  grid_line #2C2824  wallpaper_pack "Minimal"
```

**3 — Retro** ("Deep navy + amber + CRT green. Amber band + scanline wallpaper")
```
desktop_bg #0A1420  surface_glass #141C2A  topbar_bg #101826  dock_bg #141C2C
accent #FFA83A  text_primary #6AFF82  text_secondary #4AB060  focus_ring #FFC860
button_close #E85A4C  button_minimize #FFA83A  button_maximize #6AFF82  button_restore #6AFF82
cursor_fill #6AFF82  cursor_outline #0A1420  cursor_right_click #E85A4C
titlebar_inactive #1A2436  grid_line #222E42  wallpaper_pack "Retro"
```

**4 — Cards** ("Warm amber on blue slate. Layered cards wallpaper")
```
desktop_bg #1C2030  surface_glass #262C40  topbar_bg #202436  dock_bg #24283A
accent #FF9A3C  text_primary #F0ECE4  text_secondary #B0A896  focus_ring #FF7AD0
button_close #FF7A82  button_minimize #FFCC4A  button_maximize #60E89C  button_restore #60E89C
cursor_fill #FFF5E6  cursor_outline #10141E  cursor_right_click #FF7AD0
titlebar_inactive #2A3044  grid_line #2E3448  wallpaper_pack "Cards"
```

**5 — Pastel** ("Soft slate + powder pink + mint + lavender. Four pastel circles wallpaper")
```
desktop_bg #1E1E28  surface_glass #2A2A38  topbar_bg #24222E  dock_bg #262432
accent #FBC6E4  text_primary #F4ECF4  text_secondary #B8A8C2  focus_ring #C9BBF0
button_close #F5A8AE  button_minimize #FAE3A4  button_maximize #B0E8C0  button_restore #B0E8C0
cursor_fill #FDF2F5  cursor_outline #1E1828  cursor_right_click #F5A8AE
titlebar_inactive #322C3C  grid_line #363040  wallpaper_pack "Pastel"
```

**6 — Dark** ("Embers in a void — pure black + ember orange accent")
```
desktop_bg #000004  surface_glass #120A06  topbar_bg #080402  dock_bg #0C0604
accent #FF6040  text_primary #ECE6E0  text_secondary #807066  focus_ring #FF8050
button_close #E84040  button_minimize #E8A030  button_maximize #60C070  button_restore #60C070
cursor_fill #FFE6D8  cursor_outline #000000  cursor_right_click #E84040
titlebar_inactive #1A0E08  grid_line #1A0E08  wallpaper_pack "Dark"
```

**7 — Gradient** ("Deep indigo + violet + cyan accent. 3-stop vertical gradient wallpaper")
```
desktop_bg #100522  surface_glass #1A1232  topbar_bg #140828  dock_bg #180C30
accent #A070FF  text_primary #ECE8FA  text_secondary #9A88C0  focus_ring #7AE0FF
button_close #F86A9A  button_minimize #FFC470  button_maximize #70E0B8  button_restore #70E0B8
cursor_fill #F5EFFF  cursor_outline #0A0518  cursor_right_click #F86A9A
titlebar_inactive #221838  grid_line #261C3C  wallpaper_pack "Gradient"
```

**8 — Blueprint** ("Deep blueprint blue + cyan accents + white technical text. Drafting-grid wallpaper")
```
desktop_bg #051832  surface_glass #0A2240  topbar_bg #061C36  dock_bg #081F3A
accent #7AE0FF  text_primary #E0F0FF  text_secondary #70A8D0  focus_ring #A8E8FF
button_close #E87A7A  button_minimize #F0C86A  button_maximize #6AE8B0  button_restore #6AE8B0
cursor_fill #F0F8FF  cursor_outline #051224  cursor_right_click #E87A7A
titlebar_inactive #0C2646  grid_line #123252  wallpaper_pack "Blueprint"
```

**9 — Pixel** ("NES limited palette + bold primaries on dark navy. 32×32 colorful checkerboard wallpaper")
```
desktop_bg #000018  surface_glass #101030  topbar_bg #080820  dock_bg #0C0C28
accent #FFCC00  text_primary #F8F8F8  text_secondary #8080C0  focus_ring #00E800
button_close #E80000  button_minimize #FFCC00  button_maximize #00E800  button_restore #00E800
cursor_fill #FFFFFF  cursor_outline #000000  cursor_right_click #E80000
titlebar_inactive #181838  grid_line #202040  wallpaper_pack "Pixel"
```

> 🟡 **AUDIT — palette inconsistencies / drift to fix:**
> - **Retro uses `text_primary` = CRT green `#6AFF82`** while every other theme uses an
>   off-white primary. That's intentional personality, BUT it means Retro's "primary text" is a
>   saturated hue — a cohesion outlier the redesign should *decide on deliberately* (keep as a
>   signature, or move green to accent and use it consistently).
> - **`button_minimize` doubles as / equals `accent`** in Retro (`#FFA83A`) and Pixel (`#FFCC00`),
>   so the minimize button vanishes into accent-colored chrome. Inconsistent across the set.
> - **`button_restore` always equals `button_maximize`** (every theme). Fine, but undocumented —
>   formalize it (restore = maximize is a deliberate rule, not coincidence).
> - **`grid_line` ≈ `titlebar_inactive`** in several themes (Dark: both `#1A0E08`; Minimal very
>   close). They're different *roles* (content border vs inactive titlebar) that happen to collide;
>   decide whether they should be distinct.
> - **Contrast varies wildly:** Pixel's `focus_ring #00E800` on `desktop_bg #000018` is screaming;
>   Minimal's `focus_ring #C8A86A` on `#1A1816` is subtle. The selection outline therefore has
>   *very* different prominence per theme — a key cohesion problem (see §3 audit).
> - **`cursor_*` tokens exist in all 10 themes but are dead** (see §4) 🔴.

---

## 2. Layout geometry (the canvas the assets live in)

From `include/ul/menu/qdesktop/qd_WmConstants.hpp` and `qd_Launchpad.hpp`. **All px are in the
1920×1080 layout space** (handheld 1280×720 is a downscale).

**Screen:** `SCREEN_W 1920 × SCREEN_H 1080`.

**Top bar ("the Bridge"):** `TOPBAR_H 48`.

**Dock ("the Deck"):** `DOCK_H 148`, `DOCK_SLOT_SIZE 84`, `DOCK_SLOT_GAP 18`,
`DOCK_SLOT_COUNT 6`, `DOCK_PADDING_BOTTOM 12`. Minimized-window snapshots: `SNAP_W 108 × SNAP_H 60`.

**Desktop icon grid (Launchpad):**
- `LP_COLS 10`, `LP_ROWS` derived; `LP_ITEMS_PER_PAGE 40`.
- **Cell:** `LP_CELL_W 180 × LP_CELL_H 180` (icon art + label below).
- **Icon art within cell:** `LP_ICON_W 168 × LP_ICON_H 168` (square, centered; label under it).
- Grid origin `LP_GRID_X 12`, `LP_GRID_Y 192`; `LP_GAP_X 12`, `LP_GAP_Y 12`; `LP_FOOTER_H 48`.
- Search bar: `LP_SEARCH_BAR_X 300, _Y 84, _W 1320, _H 48`.

**Windows:** `TITLEBAR_H 42`, `BOTTOM_BAR_H 42`, `WIN_MIN_W 300 × WIN_MIN_H 180`,
default size **1280×800**. Corner buttons: `CORNER_BTN_SIZE 48` hit-zone, `CORNER_BTN_INNER_SIZE 32`
visible icon. `GRIP_SIZE 18`. Window corner radius **8 px** (`DrawRoundedRect`, `qd_Window.cpp`).
Drop shadow: offset **+6,+6**, black `#000000` @ alpha `0x80` (`kShadow`, `qd_Window.cpp:60`).
Focus ring thickness `kFocusRing 3` (`qd_Window.hpp:328`).

> These numbers tell you the *art sizes* and *proportions* to design to: e.g. dock icons want a
> ~84 px footprint, desktop icons a 168 px square, window buttons a 32 px disc inside a 48 px zone.

---

## 3. The selection outline / focus highlight (CREATOR-CALLED-OUT) 🔴

This is the box that surrounds the **focused/selected desktop icon or tile**. There are several
rendering sites; the canonical one for the desktop icon grid is `PaintCell()`.

**Desktop icon grid — `source/ul/menu/qdesktop/qd_Launchpad.cpp` → `QdLaunchpadElement::PaintCell()`
(focus ring section, ~line 2674):**
```cpp
// ── 5. Focus ring ──
if (is_focused) {
    SDL_SetRenderDrawColor(r, theme_.focus_ring.r, theme_.focus_ring.g, theme_.focus_ring.b, 0xFFu);
    SDL_Rect ring  { icon_x - 2, icon_y - 2, LP_ICON_W + 4, LP_ICON_H + 4 };
    SDL_RenderDrawRect(r, &ring);                 // 1px hard-cornered rectangle
    SDL_Rect ring2 { icon_x - 1, icon_y - 1, LP_ICON_W + 2, LP_ICON_H + 2 };
    SDL_RenderDrawRect(r, &ring2);                // 2nd 1px rect, 1px inside
}
```
So today the selection outline is: **two 1-pixel, hard-cornered (90°) rectangles**, drawn 2px and
1px outside the 168×168 icon, in the theme `focus_ring` color at full opacity. There is **no corner
radius, no glow, no fill, no animation, no elevation.** When focused, the icon's background fill is
also brightened by +40 per channel (`PaintCell` step 1).

**Auto-folder tab tiles** (`qd_Launchpad.cpp` ~line 1996): the focused tab gets the same treatment —
outer rect in `focus_ring`, plus an inner rect in `accent/2` (a half-value "shadow ring"). Active
(not focused) tab uses an `accent`-colored 1px border (`PaintFolderTile`, ~line 2196).

**Search bar focus** (`qd_Launchpad.cpp` ~line 1901): outer rect in `focus_ring`, inner rect in a
**hardcoded** `#0080AA`. 🟡 (hardcoded, not theme-derived.)

**Windows** (`qd_Window.cpp` ~line 292): focused window gets `kFocusRing 3` nested 1px rects in
`focus_ring` color, plus the window body has a 1px `focus_ring` outer ring at all times
(`qd_Window.cpp:278`). Hard-cornered on the straight edges; the rounded-rect body is drawn
separately with 8px corners via `DrawRoundedRect`.

**Other selection styles (for consistency awareness):**
- Lists (Task manager, Home mini-menu, Cheats, Save editor): a **filled** highlight rect in
  `accent`/`focus_ring` at partial alpha + a border. Different metaphor (fill) than the grid
  (outline-only). 🟡
- The legacy (non-qdesktop) `ui/` menu uses a PNG overlay **`romfs/default/ui/Main/OverIcon/Selected.png`
  (416×416)** as a selection halo — a *different mechanism entirely* from the qdesktop SDL rects.

> 🔴 **AUDIT — the selection outline is the #1 tighten-up target:**
> 1. **Hard 90° corners** on the grid/tile/window rects clash with the **8px rounded** window
>    bodies and rounded tiles — inconsistent corner language.
> 2. **Outline-only on the grid, but filled on lists** — two different "this is selected" visual
>    metaphors coexist.
> 3. **Prominence is theme-dependent and uneven** (Pixel screams, Minimal whispers) because it's
>    just `focus_ring` at flat alpha with no consistent contrast treatment.
> 4. **Two parallel systems**: qdesktop SDL rects vs the legacy `Selected.png` halo (416×416).
> 5. **One hardcoded color leak** (`#0080AA` on the search bar) bypasses the theme.
> The redesign should define ONE selection treatment (corner radius, thickness, glow/elevation,
> focused-vs-selected-vs-hover states) that reads consistently across all 10 palettes — see `02` §A.

---

## 4. The mouse cursor (CREATOR-CALLED-OUT) 🔴

**File:** `source/ul/menu/qdesktop/qd_Cursor.cpp` (+ `include/.../qd_Cursor.hpp`).

The cursor is **drawn procedurally** into a **44×44 ABGR8888 SDL texture**, built once at startup
(`BuildCursorTexture`). **No PNG asset is loaded for the cursor.** Design ("Liquid Glass Bubble v3"):
- 18 px radius glass bubble; texture is 44×44 with the **hotspot at the center (22,22)** — i.e. the
  click point is the middle of the bubble, not a tip. Blit offset = `(cursor_x − 22, cursor_y − 22)`.
- Body: filled circle, alpha ~110 (~43% — see-through "glass").
- Outline: 2-pass anti-aliased ring (radius+1 @ alpha 80 soft halo; radius @ alpha 255 crisp edge).
- Center crosshair: 5px black disc + 2px white dot on top (a white tip ringed by black so it's
  visible on any background).
- It is driven by **touch** (`OnInput` reads `TouchPoint`); there is no hardware mouse — the cursor
  follows the finger / pointer in the 1920×1080 space.

> 🔴 **AUDIT — the cursor is hardcoded and ignores the theme.** The color is **brand cyan
> `#00E5FF`**, defined as `BRAND_CYAN_R/G/B` constants right in `qd_Cursor.cpp:32` with the comment
> *"No matching token in QdTheme … so they are hardcoded here."* **This is factually stale:** the
> palette HAS had `cursor_fill`, `cursor_outline`, and `cursor_right_click` tokens in all 10 themes
> the whole time (see §1b). So:
> - On the **Retro** theme (green cursor intended, `cursor_fill #6AFF82`) you still get a **cyan**
>   cursor. Same mismatch on Dark (warm `#FFE6D8` intended), Pixel (`#FFFFFF`), etc.
> - There is **no distinct right-click cursor state** even though `cursor_right_click` exists in
>   every palette.
> The redesign should (a) define a cursor whose fill/outline come from `cursor_fill`/`cursor_outline`,
> (b) add a right-click variant using `cursor_right_click`, and (c) keep the "readable on any
> background" property (the black-ringed white tip is good — preserve that idea). See `02` §B.

---

## 5. Window chrome + glyphs

**File:** `source/ul/menu/qdesktop/qd_Window.cpp` (geometry `qd_WmConstants.hpp`).

- Windows are **rounded rects (8px corners)** with a 1px outer ring (`focus_ring`) + inner fill
  (`desktop_bg`), a 42px titlebar (`titlebar_inactive` unfocused / `surface_glass` focused), a
  42px bottom bar, a content border (`grid_line`), and a +6,+6 black drop shadow @ 0x80.
- **Window buttons = macOS-style "traffic lights" but in the four corners**: TL=close, TR=maximize,
  BL=minimize, BR=resize. Each is a **32px filled circle in a 48px hit zone**, colored by
  `button_close`/`button_maximize`/`button_minimize`/`accent` (resize). When inactive they dim to
  gray `#606060` (`kCornerDim`). Hover overlay is white @ 0x40.
- Each button has a **dark inner glyph** (`#101018` @ ~230 alpha) drawn as a primitive shape:
  X (close), square (maximize), dash (minimize), arrow ↗ (resize). Glyph reaches ~58% of disc
  radius. These are **drawn with SDL primitives, not font glyphs or PNGs.** 🟡

**Other glyphs in the system today:**
- **Brand "Q" mark**: drawn as **5 filled rects** (an open square + a tail) — see
  `qd_Theme.cpp::DrawThemeTransitionFrame` (180px version) and the hot-corner overlay (36px). It's
  the recurring brand emblem, rendered procedurally, scaled per use. 🟡
- **Favorites star**: a single `★` (U+2605) rendered via `RenderText` in the font, blitted
  top-right of favorited icons (`qd_Launchpad.cpp` ~line 2685).
- **Status / counts**: text via the font.
- The icon-grid **glyph fallback** (when an app has no icon art) renders a single character via
  `RenderText` centered in the tile (`qd_Launchpad.cpp` ~line 2596).

> 🟡 **AUDIT — glyphs are a grab-bag.** Window-button symbols are primitive SDL shapes; the brand
> "Q" is 5 rects; the star and fallbacks lean on the font's glyph; status uses the font. There's no
> single glyph family with consistent stroke weight / corner / metrics. The redesign should define a
> small **glyph set** (window buttons, brand Q, star, badges, status marks) as one coherent family —
> see `02` §D.

---

## 6. Icons

**Two icon sources:**
1. **Base/default set** — `romfs/default/ui/Main/EntryIcon/*.png` (used by theme 0 and as fallback
   for all themes). **Verified dimensions: 192×192** (e.g. `Folder.png`, `DockVault.png`). There's
   also `PayloadIcon/`, `OverIcon/`, `PowerIcon/`, `TopIcon/`, and Settings chrome PNGs.
2. **Per-theme set** — inside each `.ultheme` at `ui/Main/EntryIcon/*.png`. **17 named icons** per
   theme (listed in §1). These override the base set when that theme is active. Verified per-theme
   `Folder.png` is also **192×192** (sizes are consistent; visual styles are wildly different — Neon
   = big glowy PNGs, Minimal = tiny line glyphs).

**App icons** (Switch games / NRO homebrew) are loaded at runtime from the title's own NACP icon
(JPEG), cached as BGRA, drawn into the 168px cell. Those are *not* ours to design — but our **default
fallbacks** (`DefaultApplication.png`, `DefaultHomebrew.png`, `Empty.png`) are.

**Brand mark file:** `romfs/Logo.png` — **256×256**. `romfs/default/ui/Background.png` —
**1920×1080** (the legacy static wallpaper, mostly superseded by the procedural system).

The full list of **upstream-borrowed PNGs that must be replaced** for the public hard-fork is
already inventoried in **`docs/QOS-REBRAND-ASSET-INVENTORY.md`** (56 PNGs, prioritized). Use that as
the authoritative "icons to originate" checklist; this brief's job is to give them a *coherent system*.

> 🟡 **AUDIT — icons lack a system.** Sizes are consistent (192px) but the *visual language* is not:
> theme icon sets were each drawn to their own taste (glow vs line vs flat), and several base icons
> are still upstream uLaunch art (per the rebrand inventory). There's no documented grid, stroke
> weight, corner radius, padding, or "how to make a new pack" recipe. See `02` §C.

---

## 7. Wallpapers

**Two mechanisms exist; only the procedural one is wired into boot today.**

1. **Procedural (active):** `source/ul/menu/qdesktop/qd_Wallpaper.cpp` (pack 0 "Glass / Cold Plasma
   Cascade") + `qd_WallpaperPacks.cpp` (packs 1–9). Each `RenderWallpaperPack{N}_{Name}()` writes
   RGBA8888 pixels **at 1280×720** (`WP_W 1280 × WP_H 720`) into a buffer at runtime, which is then
   stretched to 1920×1080 at blit (`WP_BLIT_W/H`). This is a **GPU-memory decision**: a 1280×720
   texture is ~3.5 MB; a native 1920×1080 one (~7.9 MB) won't fit alongside Plutonium's
   framebuffers. The active layouts (`ui_MainMenuLayout.cpp:738`, `ui_StartupMenuLayout.cpp:69`,
   `qd_LockscreenLayout.cpp:331`) all instantiate `QdWallpaperElement::New(...)` (the procedural one).
   `g_wallpaper_dirty` (atomic flag) triggers a re-bake on theme change.

2. **Image (built but not yet wired into boot):** `QdImageWallpaperElement`
   (`include/.../qd_ImageWallpaperElement.hpp`) loads a **static image** from a runtime path —
   typically **`sdmc:/ulaunch/cache/active/ui/Background.png`** (i.e. a `Background.png` packed
   inside the active `.ultheme`). It accepts **any SDL_image format (PNG, JPEG, BMP, WebP)**, scales
   the source **down to 1280×720** at load (`SDL_BlitScaled`), and blits full-screen. The header
   notes it's a *sibling* of the procedural element and that "Phase B wire-up (future):
   StartupMenuLayout will swap the procedural element for this one **when Background.png is present**
   in the active theme cache." **No theme currently ships a `ui/Background.png`,** so this path is
   dormant.

> 🟡 **AUDIT — wallpapers are code, not art.** All 10 wallpapers are algorithms (plasma, neon bands,
> scanlines, checkerboards). They're palette-reactive but not art-directed, and changing one means
> editing C++. There IS a clean drop-in for real images (`ui/Background.png` in the `.ultheme`,
> 1280×720), but it needs the one-line boot wire-up to activate. The redesign should deliver
> **art-directed 1280×720 wallpapers** that drop into that path, and a **wallpaper-pack recipe** for
> the future. See `02` §E + `03` for the integration note.

---

## 8. Font

**File:** `source/main.cpp:616`.
```cpp
const auto default_font_path = ul::menu::ui::TryGetActiveThemeResource("ui/Font.ttf");
if (!default_font_path.empty())  renderer_opts.AddDefaultFontPath(default_font_path);
else { renderer_opts.AddDefaultSharedFont(PlSharedFontType_Standard); ... }
```
So Q OS **already supports a custom font**: if the active theme ships **`ui/Font.ttf`**, that TTF
becomes the default UI face. Otherwise it falls back to **Nintendo's shared system fonts**
(`PlSharedFontType_Standard` + CJK/KO). It always *additionally* loads
`PlSharedFontType_NintendoExt` (the Nintendo button-glyph font) and registers extra small sizes
(22/18/14 px) so long labels can shrink. Text is rendered via Plutonium `RenderText` at
`DefaultFontSize::{Small, Medium, MediumLarge, Large}`.

**Verified: no `.ultheme` ships a `Font.ttf`** (checked all 10 — zero `Font.ttf` entries). So today
the whole OS is set in **Nintendo's stock font**.

> 🟡 **AUDIT — no signature typeface.** The `ui/Font.ttf` slot exists and works, but is empty, so Q
> OS reads visually as "stock Switch." A custom font is the highest-leverage single change for "this
> is its own OS." Constraints: real `.ttf`/`.otf`, SDL_ttf-loadable, full Latin coverage, graceful
> fallback (CJK/buttons stay on the shared font), redistributable license. See `02` §F.

---

## 9. Audit summary — the prioritized "tighten-up" list

| Pri | Finding | Where |
|----:|---------|-------|
| 🔴 1 | **Selection outline** = hard-cornered 2px double rect; clashes with 8px rounded windows; outline-vs-filled metaphors differ; prominence uneven across themes; a 2nd legacy `Selected.png` system coexists; one hardcoded `#0080AA`. | `qd_Launchpad.cpp PaintCell`, `qd_Window.cpp` |
| 🔴 2 | **Cursor ignores the theme** — hardcoded cyan `#00E5FF`; the `cursor_fill/outline/right_click` tokens are dead in all 10 themes; no right-click variant. | `qd_Cursor.cpp` |
| 🔴 3 | **Palette defined twice** (JSON + C++ factories), kept in sync by hand. | `QdPalette.json` + `qd_Theme.hpp` |
| 🟡 4 | **No custom font** — `ui/Font.ttf` slot empty; whole OS is stock Nintendo font. | `main.cpp:616` |
| 🟡 5 | **Wallpapers are procedural code**, not art; image path (`ui/Background.png`, 1280×720) exists but unwired. | `qd_Wallpaper*.cpp`, `QdImageWallpaperElement` |
| 🟡 6 | **Icons lack a system** (per-theme styles drawn ad hoc; some still upstream art). | `romfs/.../EntryIcon`, rebrand inventory |
| 🟡 7 | **Glyphs are a grab-bag** (primitive shapes + font chars + 5-rect "Q"); no unified family. | `qd_Window.cpp`, `qd_Theme.cpp` |
| 🟡 8 | **Palette drift**: Retro green primary outlier; minimize==accent in Retro/Pixel; grid_line≈titlebar_inactive collisions; contrast spread. | `01` §1b audit |
| 🟡 9 | **Theme thumbnails (`theme/Icon.png`) are 1×1 placeholders** in all 10. | each `.ultheme` |

Proceed to `02-OVERHAUL-SPEC.md` for the target token system and the per-asset specs that resolve
each of these.
