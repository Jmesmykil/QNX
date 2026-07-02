# Q OS — Overhaul Spec: Target Design System + Per-Asset Specs

> Companion to `00-BRIEF.md` and `01-CURRENT-STATE.md`. This is the **target**: the unified design
> tokens, then a concrete spec for each asset (selection outline, cursor, icons, glyphs, wallpapers,
> font). Every spec says **(a) requirements, (b) exact Q OS integration — format/size/load path,
> (c) per-theme variation rules.** Exact output filenames + drop locations are in
> `03-DELIVERABLES-AND-PIPELINE.md`.
>
> **Grounding rule:** the integration facts here are read from the source (see `01`). Do not invent
> new formats or load paths — design *into* the ones documented.

---

## PART I — THE UNIFIED DESIGN TOKEN SYSTEM

Q OS already has an 18-role color schema parsed from `ui/QdPalette.json` (see `01` §1a). The
overhaul **keeps that schema as the SSOT** (so handed-back palettes drop straight into existing
`.ultheme` bundles and the C++ factories) and **adds** the missing non-color tokens (type, spacing,
radius, elevation, motion) as a *documentation layer* the designer maintains and the engineers
implement against. Don't rename the 18 color roles — code reads them by name.

### T1. Color roles (KEEP these 18 names; retune values)
`desktop_bg, surface_glass, topbar_bg, dock_bg, accent, text_primary, text_secondary, focus_ring,
button_close, button_minimize, button_maximize, button_restore, cursor_fill, cursor_outline,
cursor_right_click, titlebar_inactive, grid_line` + the string `wallpaper_pack`.

**Retuning rules (resolve the `01` §1b drift):**
- **Contrast floor:** `focus_ring` vs `desktop_bg` and vs `surface_glass` must clear a *consistent*
  contrast target across all 10 themes, so the selection outline reads with the *same prominence*
  everywhere (today Pixel screams, Minimal whispers). Pick a target (e.g. focus_ring ≥ 3:1 vs the
  surface it rings) and tune each theme to it.
- **`text_primary` is off-white/near-tonal** in every theme. **Decision needed on Retro:** today
  Retro's primary is saturated CRT green `#6AFF82`. Recommended: move the green to a *secondary/
  accent* role and give Retro a high-legibility near-white primary, OR formally bless "Retro uses a
  phosphor-green primary" as a one-theme exception and document it. Don't leave it accidental.
- **`button_minimize` must be visually distinct from `accent`** (today they collide in Retro/Pixel).
- **`button_restore == button_maximize`** is a deliberate rule (restore is the maximize button's
  alternate state) — formalize it; you only need to deliver one maximize/restore hue per theme.
- **`grid_line` and `titlebar_inactive` are distinct roles** — keep them at least slightly
  differentiated (today they collide in Dark/Minimal).
- Provide every value as `#RRGGBB` (or `#RRGGBBAA` where alpha matters). Keep the 10 themes'
  **personalities locked** (see `00` §5) — retune for cohesion, don't recolor the identities.

> **Optional extension (propose, don't assume):** if the selection/cursor specs below need roles
> that don't exist yet (e.g. a dedicated `selection_glow` or `accent_muted`), you MAY propose ADDING
> new optional keys to `QdPalette.json`. Mark them clearly as *new keys* in `03` so engineering wires
> the parser (`_QD_PALETTE_APPLY` in `qd_Theme.cpp`) + adds the struct field. Prefer reusing the 18
> existing roles first; every new token is engineering work.

### T2. Type scale
Q OS renders text via Plutonium at named sizes; the codebase uses `Small / Medium / MediumLarge /
Large` plus registered extras at **22 / 18 / 14 px** (`main.cpp:637`). Plutonium's built-in "Small"
floor is ~27 px. Deliver a **type scale mapped to these buckets**, designed for **1920×1080 rendered,
viewed at 720p/TV**:

| Token | Use | Approx px @1080 |
|-------|-----|-----------------|
| `type/display` | brand splash, lock-screen clock | ~96–120 |
| `type/title` (Large) | window titles, big headers | ~40–48 |
| `type/headline` (MediumLarge) | section headers, help overlay | ~30–34 |
| `type/body` (Medium) | primary UI text, toasts | ~24–27 |
| `type/label` (Small) | icon labels, status line, captions | ~18–22 |
| `type/caption` (extra) | dense secondary text | ~14–16 |

Specify family, weight, tracking, and min legible size per bucket (the font lives in §F). Icon labels
truncate to ~14 chars w/ ellipsis at 168 px cell width (`qd_Launchpad.cpp`), so the label face must
be compact and legible small.

### T3. Spacing scale
Reflect the live geometry (multiples of ~6/12; see `01` §2): dock gap 18, grid gap 12, padding 12.
Recommend a token ramp: **4 / 8 / 12 / 18 / 24 / 36 / 48** (px @1080). Window stagger is 36; corner
hit-zones 48; topbar 48; dock 148. Keep new spacing on this ramp so it composes with existing layout.

### T4. Corner radius
Today: **window body = 8 px**; everything else (selection rects, tiles, buttons-as-discs) is either
hard-cornered or a full circle. **Define a radius ramp and apply it consistently**:

| Token | Value (@1080) | Applies to |
|-------|---------------|-----------|
| `radius/sm` | 6–8 px | selection outline corners, small tiles, badges |
| `radius/md` | 10–14 px | window bodies (align with the existing 8 → propose final), panels, folder tiles |
| `radius/lg` | 18–24 px | large surfaces / dock container if rounded |
| `radius/pill` | full | window traffic-light buttons (stay circular), toggle pills |

The biggest cohesion win: **give the selection outline the same corner radius as the tiles/windows
it surrounds** (kills the hard-corner-on-rounded-content clash, `01` §3 audit #1).

### T5. Elevation / shadow
Today: a single window drop shadow (offset **+6,+6**, black @ alpha **0x80**, no blur — it's a hard
offset rect; `qd_Window.cpp:60,263`). Define an elevation ramp the engine can approximate cheaply
(offset + alpha, optionally a 1–2 px soft edge):

| Token | Offset | Alpha | Use |
|-------|--------|-------|-----|
| `elev/0` | none | — | flat on desktop |
| `elev/1` | +2,+2 | ~0x40 | tiles, dock icons (subtle lift) |
| `elev/2` | +6,+6 | ~0x80 | windows (matches today) |
| `elev/focus` | glow, not offset | accent/focus_ring @ low alpha | the *focused* state (selection) |

Keep shadows **neutral black** (universal), as the code already does — don't tint per theme. A soft
glow for focus may be tinted (focus_ring color).

### T6. Motion
**Budget is tiny** (`00` §4.5). Define motion as **discrete state changes**, not continuous tweens:
- **Selection focus:** instantaneous ring + fill-brighten on focus (today: +40 brightness). You MAY
  spec a 1–2-frame "pop" but it must be optional/cheap.
- **Window minimize/restore:** the engine already animates this over a few frames
  (`AdvanceAnimation`). You can spec timing/easing as *guidance*.
- **Theme transition:** there's a one-shot themed splash frame on theme change
  (`DrawThemeTransitionFrame`: gradient + Q emblem + 3 loading dots). You may art-direct this splash.
- Do **not** spec hover/idle continuous animation for the cursor or selection.

---

## PART II — PER-ASSET SPECS

### A. Selection outline (the focus highlight) — TOP PRIORITY 🔴

**Context:** drawn around the focused desktop icon (168×168 art in a 180×180 cell), folder tabs,
window bodies, and (different metaphor) list rows. Today: hard-cornered 2×1px rects in `focus_ring`.
See `01` §3 for all sites.

**(a) Requirements**
- ONE unified selection language across icon grid, tiles, dock, and (reconciled) lists.
- **Rounded corners** matching the rung the element sits on (`radius/sm` for tiles/icons).
- Three states, visually distinct but clearly related:
  - **Focused** (keyboard/D-pad/pointer is on it, not yet chosen): the prominent ring.
  - **Selected** (toggled-on / multi-select): a persistent marker (e.g. filled corner check or a
    second ring) distinct from focus.
  - **Hover** (pointer over, in mouse mode): a lighter version of focus.
- **Consistent prominence across all 10 themes** (resolve the contrast spread). Use `focus_ring`
  for the ring; if a glow is used, tint it with `focus_ring`/`accent`.
- Preserve the existing "focused tile also brightens its fill" idea (it reads well) — or replace it
  deliberately.
- Must read at 720p / TV distance: ring thickness should be **≥ 3 px @1080** effective.
- Reconcile the **two systems**: deprecate/replace the legacy `Selected.png` (416×416) halo so there
  is one selection treatment, OR redesign `Selected.png` to match the new ring exactly (the legacy
  `ui/` menu still blits it).

**(b) Q OS integration (verified)**
- The grid/tile/window rings are **drawn in code with SDL** (`SDL_RenderDrawRect` /
  `DrawRoundedRect`) using the `focus_ring` color — so the *primary deliverable here is a SPEC*
  (geometry: corner radius, thickness, inset, glow params, per-state diff) the engineer implements,
  **plus** optional **9-slice corner/edge PNG assets** if the design needs a gradient/glow ring that's
  cheaper to blit than to draw (a 9-slice "selection frame" with transparent center).
- If you supply a 9-slice: deliver as a transparent PNG sized to the corner radius (e.g. a
  `selection_frame.9.png`-style asset, ~64×64 corner). Center stays empty (icon shows through).
- The legacy halo, if kept, is **`romfs/default/ui/Main/OverIcon/Selected.png`, 416×416, transparent
  PNG** (must tint/scale to look identical to the new ring).

**(c) Per-theme variation**
- Color comes from each theme's `focus_ring` (already per-theme). **You don't ship 10 ring assets** —
  you ship ONE geometry spec + (optionally) ONE tintable/grayscale 9-slice that the engine colors per
  theme, OR a neutral-white glow PNG multiplied by `focus_ring`. State the tinting model explicitly.
- Pixel theme is the one place a *hard-cornered* ring might be on-brand (8-bit). Decide: either allow
  a `radius/sm = 0` override for Pixel only (documented exception) or keep it rounded for cohesion.

---

### B. Mouse cursor — TOP PRIORITY 🔴

**Context:** a 44×44 procedural SDL texture, hotspot at center (22,22), driven by touch. Hardcoded
cyan today; ignores the `cursor_fill/outline/right_click` tokens. See `01` §4.

**(a) Requirements**
- A cursor that is **legible on ANY background** (the current black-ringed white center dot achieves
  this — preserve that principle: a high-contrast core that survives both light and dark surfaces).
- **Theme-aware:** body uses `cursor_fill`, outline uses `cursor_outline`.
- A **distinct right-click state** using `cursor_right_click` (e.g. the ring turns red/accent, or a
  small badge appears) — the token exists in all 10 themes and is currently unused.
- Optional: a **busy/loading** variant if cheap.
- Hotspot: clearly documented. Today the click point is the **center** of the bubble (not a tip). You
  may keep center-hotspot (good for a ring cursor) or move to a classic tip — **if you change it, say
  so explicitly** so the blit offset is updated.
- Keep it small (≈36–48 px visual) and readable at 720p.

**(b) Q OS integration (verified)**
- Two viable paths — **pick one and state it**:
  1. **Spec-driven procedural (matches today):** deliver a precise spec (radii, alphas, colors-by-
     token, right-click diff, hotspot) and the engineer reproduces it in `qd_Cursor.cpp`, swapping
     the hardcoded `BRAND_CYAN_*` for `g_QdTheme.cursor_fill/outline/right_click`. Cheapest; no asset
     file. **Recommended** unless the design needs raster detail.
  2. **PNG cursor:** deliver transparent PNG(s) the engine blits. Sizes: **base 44×44** (to match the
     existing texture footprint/hotspot math) — and provide **88×88 @2x** masters for crispness.
     If theme-tinting a grayscale cursor isn't feasible, you'd need **per-theme cursor PNGs** (10 ×
     {pointer, right-click}) — heavier, so prefer a tintable design or path 1.
- There is **no hardware mouse**; the cursor follows touch in 1920×1080 space — design for a
  "floating pointer," not an arrow anchored to a screen corner.

**(c) Per-theme variation**
- Colors from `cursor_fill` / `cursor_outline` / `cursor_right_click` per theme (already defined).
- Keep the silhouette identical across themes (cohesion); only color changes. The contrast-survival
  core (the white-dot-in-black-ring idea) should stay roughly constant so it's always findable.

---

### C. Icon system + icon packs 🟡

**Context:** 192×192 PNGs; base set in `romfs/default/ui/Main/EntryIcon/`, per-theme overrides inside
each `.ultheme`. 17 named per-theme icons + system/special icons + fallbacks. Drawn into a 168px
cell. See `01` §6 and `docs/QOS-REBRAND-ASSET-INVENTORY.md` for the replacement list.

**(a) Requirements**
- A **coherent icon family**: define the grid (e.g. 192×192 canvas with a 24px safe padding → ~144px
  live area), **stroke weight**, **corner radius** (align to `radius/sm`), perspective (flat vs
  isometric — recommend flat for legibility), and the **two-color rule** (silhouette + accent) so
  icons stay readable at dock size (~84px) and TV distance.
- The **named system/dock/folder icons** (the 17 in each theme + the special-feature icons from the
  rebrand inventory): Settings, Album (generic photo card), Themes (palette), Controllers (generic
  gamepad — **no Joy-Con**), MiiEdit (generic avatar — **non-Mii**), WebBrowser (globe), Amiibo
  (generic NFC waves), plus DockVault/Monitor/About/AllPrograms/Control/Tasks, Folder + folder
  categories (Games/Emulators/Tools/System/QOS/Other), DefaultApplication, DefaultHomebrew, Empty
  (dotted-rounded slot placeholder), HotCornerQ (the brand Q).
- **Fallbacks** must be neutral/branded (DefaultApplication = branded grid square; DefaultHomebrew =
  hammer/wrench-style mark; Empty = dotted placeholder).
- **NO Nintendo/trademarked IP** (`00` §4.8).
- A documented **"icon-pack recipe"**: the rules + a template so new packs (now and over time) are
  consistent. A pack = a named visual treatment (line / glow / flat / pixel) applied to the full
  named-icon set.

**(b) Q OS integration (verified)**
- **Format:** transparent **PNG, 192×192** (matches every existing icon). Provide **@2x 384×384**
  vector-exported masters for future-proofing; the repo uses the 192 size.
- **Base set drop:** `romfs/default/ui/Main/EntryIcon/<Name>.png` (theme-0 + universal fallback).
- **Per-theme pack drop:** inside each `.ultheme` at `ui/Main/EntryIcon/<Name>.png` (overrides base
  when active). Filenames must match the existing names exactly (see `03` for the list).
- Other icon dirs that exist and may need originals: `PayloadIcon/` (Hekate, Daybreak, etc. — small
  payload launcher icons), `PowerIcon/` (Sleep/Restart/Shutdown/Hekate), `OverIcon/` (Selected,
  Suspended, Corrupted, Gamecard, NeedsUpdate, NotLaunchable…), `TopIcon/` (Bluetooth…). Treat these
  as part of the system family.

**(c) Per-theme variation**
- One **icon-pack treatment per theme** (10), each applying that theme's material language (e.g.
  Neon = glow, Minimal = thin line, Pixel = 8-bit, Blueprint = white technical line on blue) to the
  full named set, sized 192×192. Keep silhouettes/metaphors identical across packs (a "Settings" gear
  is the same gear everywhere) — only the *treatment* changes. This is the cohesion contract.

---

### D. Glyph set 🟡

**Context:** window-button symbols are primitive SDL shapes; brand "Q" is 5 rects; star/status use
font glyphs. No unified family. See `01` §5.

**(a) Requirements** — one coherent glyph family, consistent stroke/metrics/corner:
- **Window-button glyphs:** Close (X), Maximize (square), Minimize (dash), Restore (overlap squares),
  Resize (↗). Sized to sit inside a **32px disc** (live ~58% of disc radius ≈ 18px), in a dark
  contrast color over bright button fills.
- **Brand "Q" mark:** the recurring emblem (today an open square + tail). Redesign as a clean,
  scalable mark used at 36px (hot corner), ~180px (transition splash), and 256px (`Logo.png`).
- **Favorites star** (replaces the `★` font glyph), **badges** (count badge, NeedsUpdate, Suspended,
  Corrupted overlays in `OverIcon/`), **status marks** (connection, battery — though pure system art
  like battery/signal may stay functional).
- Keep them **monochrome + tintable** (engine colors them per state/theme).

**(b) Q OS integration**
- Window-button + brand glyphs are mostly **drawn in code** → **deliver a SPEC + SVG masters**; small
  ones the engine can keep procedural, but the *design* (proportions, stroke, corner) comes from you.
- The **brand Q** also needs raster: **`romfs/Logo.png` 256×256** transparent PNG, and a 1×1→real
  **`theme/Icon.png`** thumbnail per theme (today a 1×1 placeholder — see §thumbnails below).
- Overlay glyphs (`OverIcon/*`) are **transparent PNGs** blitted over icons — size to the 168px cell
  (the existing `Selected.png` is 416×416; smaller overlays like Suspended/Corrupted are icon-sized).

**(c) Per-theme variation**
- Glyph shapes are **constant across themes** (an X is an X). Color is applied by the engine from the
  relevant token (button glyphs use the dark contrast color; the Q uses `accent`). The Pixel theme
  may warrant a pixel-grid glyph variant (optional, documented).

**Theme thumbnails:** also deliver a real **`theme/Icon.png`** per theme (currently 1×1) so the
theme picker shows a preview. Recommend a small swatch/preview tile, e.g. **256×256** (engine scales)
— confirm final size in `03`.

---

### E. Wallpapers (+ future wallpaper packs) 🟡

**Context:** 10 procedural wallpapers in C++ at 1280×720 → stretched to 1920×1080. An image path
exists (`QdImageWallpaperElement` → `ui/Background.png` in the `.ultheme`, any SDL_image format,
scaled to 1280×720) but isn't wired into boot yet. See `01` §7.

**(a) Requirements**
- **One art-directed wallpaper per theme (10)**, each expressing that theme's identity (the current
  procedural intents are good briefs: Q OS = "Cold Plasma Cascade"; Neon = 4 horizontal neon bands;
  Retro = amber band + scanlines; Pastel = four pastel circles; Gradient = 3-stop vertical; Blueprint
  = drafting grid; Pixel = 32×32 checkerboard; Dark = embers in void; Minimal = quiet warm; Cards =
  layered cards). Modernize/clean them, keep the concept.
- **Full-bleed 16:9.** Keep critical/branded content **inside the central ~92%** (TV overscan safety,
  `00` §4.3). The image is stretched, so author at the true target ratio.
- Must not fight the chrome: the top bar (48px), dock (148px), and bright `focus_ring`/`accent` UI sit
  on top — keep wallpaper contrast moderate behind those zones (don't put busy detail under the dock).
- **Wallpaper-pack recipe (future):** define what a "wallpaper pack" is — a named set of 1280×720
  images shipped together (e.g. a seasonal pack, an abstract pack) and how they map to themes or stand
  alone. Provide the template + naming.

**(b) Q OS integration (verified)**
- **Format/size:** author/export at **1280×720** (the bake resolution). PNG preferred (JPEG/BMP/WebP
  also load). Keep file size modest (GPU budget ~3.5 MB texture; source is scaled down at load).
- **Drop path:** package as **`ui/Background.png` inside each `.ultheme`**. When present in the active
  theme cache (`sdmc:/ulaunch/cache/active/ui/Background.png`), the image wallpaper element renders it.
- ⚠️ **Integration dependency (flag to engineering):** the image path is **built but not yet wired
  into boot** — the active layouts currently always instantiate the *procedural* `QdWallpaperElement`
  (`ui_MainMenuLayout.cpp:738`, `ui_StartupMenuLayout.cpp:69`, `qd_LockscreenLayout.cpp:331`). The
  header says Phase B will "swap the procedural element for this one when Background.png is present."
  **So shipping image wallpapers requires that one-time wire-up.** This is noted in `03` as a
  prerequisite task; the *assets* are still authored to the `ui/Background.png` contract.
- A higher-res master (1920×1080) is fine to keep as the editable source, but the **shipped file is
  1280×720** to match the bake and save VRAM.

**(c) Per-theme variation**
- 10 wallpapers, one per theme, each in that theme's palette. The `wallpaper_pack` string in
  `QdPalette.json` already names which wallpaper a theme wants — image wallpapers simply replace the
  procedural one for that theme when `Background.png` ships in its bundle.

---

### F. Custom font — HIGH LEVERAGE 🟡

**Context:** `ui/Font.ttf` in the active theme cache becomes the default UI face; else Nintendo's
shared font. No theme ships one today. See `01` §8.

**(a) Requirements**
- A **signature OS typeface** that says "Q OS" — modern, clean, technical-but-friendly to match the
  "liquid glass" flagship. Likely a **geometric/humanist sans** with excellent small-size legibility
  (icon labels at ~18px @1080, viewed at 720p/TV).
- **Coverage:** full **Basic Latin + Latin-1 Supplement** at minimum (ASCII + accented Latin). It does
  **not** need CJK/emoji/Nintendo-button glyphs — Q OS keeps loading the Nintendo shared font for
  those alongside it (so the custom font is the Latin face; the rest fall back). **It must not crash
  or render tofu when it's the primary face** for Latin text.
- **Weights:** at least **Regular + Medium/Semibold + Bold** (titles want weight; labels want
  regular). A condensed/compact cut for tight icon labels is a bonus.
- Number legibility matters (clock, battery %, counts) — clear tabular figures preferred.
- **License must permit embedding/redistribution in an open-source homebrew binary** — OFL (SIL Open
  Font License) is ideal. **State the license + source explicitly.** (Original design, or an
  appropriately-licensed existing face — no proprietary fonts.)

**(b) Q OS integration (verified)**
- **Format:** a real **`.ttf`** (preferred; `.otf` also works) loadable by **SDL_ttf** via Plutonium.
- **Drop path:** **`ui/Font.ttf` inside the `.ultheme`** (resolves to the active theme cache;
  `main.cpp:616` picks it up automatically — **no code change needed** to *use* a font, just ship the
  file in the bundle). If you want the font to be OS-wide regardless of theme, ship it in **all 10**
  bundles (or in `romfs/default/` + a small loader tweak — note in `03`).
- Sizes are driven by Plutonium buckets + the registered extras (22/18/14). Ensure the face is
  legible at **14 px @1080** (it can be shown at ~9–10px effective on a 720p handheld panel).

**(c) Per-theme variation**
- **One font for the whole OS** is the recommended default (cohesion). The system *allows* a different
  `Font.ttf` per theme (it's read from the active theme), so you MAY ship a thematic alt for special
  themes (e.g. a pixel font for the **Pixel** theme, a mono/technical face for **Blueprint**, a CRT
  face for **Retro**) — but treat those as optional flourishes layered on one primary OS font, and
  confirm each still has full Latin coverage + graceful fallback.

---

## PART III — CROSS-ASSET CONSISTENCY CHECKLIST

The whole point is cohesion. Before delivering, verify:
1. **One corner-radius language** — selection outline, tiles, windows, badges all reference `radius/*`.
2. **One selection treatment** — grid, tabs, dock, lists, and the legacy `Selected.png` all read as
   the same "this is focused/selected" idea (no outline-vs-fill split, no two systems).
3. **Theme-aware everywhere** — cursor uses `cursor_*`; nothing hardcodes a brand color that should be
   a token (kill the `#00E5FF` cursor and `#0080AA` search-ring leaks).
4. **Consistent prominence across 10 palettes** — focus_ring contrast tuned to one target; icons
   readable at dock size in every pack.
5. **Same silhouettes across themes** — icons/glyphs keep identical shapes; only treatment/color vary.
6. **Personalities intact** — Neon still glows, Retro still scanlines, Pixel still 8-bit, Blueprint
   still drafting paper (`00` §5 matrix).
7. **Platform-safe** — every raster at the exact sizes in `03`; wallpapers 1280×720; font OFL-ish TTF
   with Latin coverage; nothing relies on motion/VRAM the Switch can't give.

Proceed to `03-DELIVERABLES-AND-PIPELINE.md` for the exact file manifest + drop locations.
