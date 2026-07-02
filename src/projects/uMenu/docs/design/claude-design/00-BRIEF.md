# Q OS — Visual Overhaul Master Brief (for Claude Design)

> **Audience:** Claude Design (Anthropic's design-system tool) + any human designer.
> **Status:** Design brief only. NOTHING in the Q OS codebase has changed. This package
> tells you what to make and exactly where it plugs back in.
> **Date authored:** 2026-06-14.
> **Read order:** this file → `01-CURRENT-STATE.md` (what exists today) →
> `02-OVERHAUL-SPEC.md` (the target system + per-asset specs) →
> `03-DELIVERABLES-AND-PIPELINE.md` (the exact files to hand back and where they drop).

---

## 0. TL;DR — the one-paragraph ask

Q OS is a **desktop operating system that runs on a Nintendo Switch**. It already has 10
themed variants and a working windowed desktop, but the visual language is *programmer-drawn*:
hard-cornered 2-pixel selection rectangles, a cursor that ignores the theme, procedurally
generated wallpapers, and Nintendo's stock system font. We want a **cohesive, modern, "real OS"
visual overhaul** that (a) **tightens the 10 existing themes** so they read as one family with
ten personalities, and (b) gives us a **repeatable asset pipeline** — selection outline, mouse
cursor, icons + icon packs, glyphs, wallpapers (and future wallpaper packs), and a custom font —
that Claude Design produces now and keeps producing over time. Every asset must respect the hard
constraints of the platform (see §4).

---

## 1. What Q OS is

- **Q OS** is a **homebrew desktop OS for the Nintendo Switch (Erista / 2017 model)**. It is a
  clean-room, C++20 fork of XorTroll/uLaunch (the open-source Switch launcher), turned into a
  full **windowed desktop environment**: a top bar ("the Bridge"), a dock ("the Deck"), a desktop
  icon grid, draggable/resizable windows, a file manager, a save manager, and more.
- It renders with **SDL2 + Plutonium** (a Switch UI toolkit). There is **no web layer, no CSS,
  no HTML** — everything is drawn with SDL2 primitives (filled rects, circles, lines) and blitted
  textures (PNG/JPEG icons + TTF text). This is important: **your deliverables are flat raster
  and vector source assets + color/spec values, not CSS/components.**
- The desktop layer is internally called **"qdesktop"** (source under
  `source/ul/menu/qdesktop/`, headers under `include/ul/menu/qdesktop/`).
- It ships **10 built-in themes**, each one a *palette + wallpaper + folder-icon pack* selected
  together as a unit (index 0–9): **Q OS** (the cyan-on-navy "glass" default), **Neon, Minimal,
  Retro, Cards, Pastel, Dark, Gradient, Blueprint, Pixel**.

### Target screen / canvas facts (memorize these)
- **Docked output: 1920×1080.** The entire UI is laid out in a **1920×1080 coordinate space**.
- **Handheld output: 1280×720** (the same layout, downscaled). Wallpapers are authored/baked at
  **1280×720** and stretched to 1920×1080 at blit time (a GPU-memory constraint — see §4).
- The Switch is held **~30–60 cm from the eyes in handheld** and shown **on a TV across a room
  when docked.** Designs must be legible at both distances. Favor bold, high-contrast shapes;
  avoid hairline detail that vanishes at 720p or from the couch.

---

## 2. The target aesthetic / vibe

**"A clean, modern, cohesive desktop OS — where each themed variant keeps its own personality."**

Distill the vibe to these adjectives, in priority order:

1. **Cohesive** — all 10 themes should feel like the *same operating system* wearing 10 outfits.
   Same shapes, same corner radii, same selection behavior, same cursor silhouette, same icon
   grid metaphor. Only color, texture, and "material feel" change between themes.
2. **Modern / "liquid glass"** — the flagship theme ("Q OS", index 0) is **dark, glassy, cyan-on-
   deep-navy** with soft glows. That is the north-star material language: translucent surfaces,
   soft elevation, restrained neon accents. Other themes reinterpret that material (warm, pastel,
   CRT, blueprint, 8-bit) without abandoning the underlying structure.
3. **Crisp and legible** — this runs on a games console at TV distance. Generous hit targets,
   strong figure/ground contrast, bold iconography. Clean, not busy.
4. **Characterful, not generic** — Q OS is a passion project ("a love letter"), not a corporate
   shell. Each theme should have a memorable identity (Retro = amber CRT scanlines; Pixel = NES
   primaries; Blueprint = drafting paper). The overhaul tightens the *system*, it does not
   sand off the personalities.

**Anti-goals (do NOT do these):**
- Do **not** clone the Nintendo Switch home menu, macOS, or Windows 1:1. Q OS borrows the
  *traffic-light window button* metaphor and a dock, but it is its own brand.
- Do **not** introduce a web/Material/Fluent component kit. There are no buttons-as-DOM here.
- Do **not** make the 10 themes more similar than they are today by flattening them to one accent;
  the 2026 "distinctness rework" deliberately pushed them apart (Minimal=amber, Dark=ember,
  Cards=amber, etc.). Keep them distinct — just make them *coherent*.
- Do **not** rely on motion you can't ship: animation budget is tiny (see §4).

---

## 3. The overhaul goals (what "done" looks like)

| # | Goal | Why it matters |
|---|------|----------------|
| G1 | **Tighten the 10 theme palettes** into one documented token system with consistent roles, contrast, and naming. | Today palettes are hand-tuned per theme with drift (see `01-CURRENT-STATE.md` §audit). |
| G2 | **Redesign the selection outline** (the focus highlight on icons/tiles/buttons). | Creator-called-out. Today it's a hard-cornered 2px double rectangle — the single most-seen, least-polished element. |
| G3 | **Redesign the mouse cursor** and make it theme-aware. | Creator-called-out. Today the cursor is **hardcoded cyan** and ignores every theme's cursor colors. |
| G4 | **Design a coherent icon system** + a pipeline for **custom icon packs**. | Today icons are a mix of bespoke + leftover upstream art at inconsistent sizes. |
| G5 | **Design a glyph set** (the small symbols: window buttons, status, hot-corner "Q", favorites star, badges). | Today glyphs are primitive SDL shapes or single Unicode chars in the system font. |
| G6 | **Design per-theme wallpapers** (and a **future wallpaper-pack** pipeline). | Today wallpapers are procedurally generated in C++ at runtime; we want art-directed images with a drop-in path. |
| G7 | **Design a custom font** for the OS. | Today Q OS uses Nintendo's stock shared font. A signature typeface is the biggest single lever on "feels like its own OS". |
| G8 | **Define the asset pipeline** so handed-back files plug straight into the repo with no guesswork. | This is what makes Claude Design a *repeatable* asset source, not a one-off. |

---

## 4. Hard constraints (read before designing ANYTHING)

These are platform realities, not preferences. Violating them means the asset cannot ship.

1. **No vector/CSS rendering at runtime.** The OS draws SDL2 primitives and blits raster textures.
   - **Deliver vector source (SVG/Figma) AND exported raster (PNG, transparent)** at the exact
     pixel sizes listed in `03-DELIVERABLES-AND-PIPELINE.md`. The repo consumes the PNGs; the
     vectors are our editable masters for future re-exports.
2. **Color is delivered as hex tokens**, parsed from JSON (`#RRGGBB` or `#RRGGBBAA`). See the
   token list in `02-OVERHAUL-SPEC.md`. Eighteen color roles per theme, today.
3. **Wallpapers are authored at 1280×720** (16:9), full-bleed, and will be stretched to
   1920×1080. Don't put critical content in the outer ~4% (overscan-ish safety on TVs).
4. **GPU memory is extremely tight.** The Switch GPU pool barely fits Plutonium's framebuffers +
   one wallpaper texture (~3.5 MB). So:
   - One full-screen wallpaper texture max. No huge image atlases.
   - Icons are small textures (≤192×192). Keep packs lean.
5. **Tiny animation budget.** ~60 fps target with a per-frame budget already mostly spent on the
   UI. Selection/cursor/hover effects should be **static or 1–2 frame state changes**, not
   continuous animations. If you spec motion, spec it as "cheap state change" (e.g. a brighter
   ring on focus), and mark anything aspirational clearly.
6. **Font must be a real `.ttf` (or `.otf`) file** that SDL_ttf can load, with full Latin
   coverage (ASCII + common Latin-1) at minimum. **It must degrade gracefully:** Q OS falls back
   to Nintendo's shared font for CJK/emoji/Nintendo-button glyphs, so your font does NOT need to
   cover CJK — but it must not break when it's the primary face. License must permit
   embedding/redistribution in an open-source homebrew binary (OFL or similar — **state the
   license**).
7. **Legibility at 720p + TV distance** beats fine detail everywhere.
8. **No trademarked/Nintendo IP.** No Mario, no Joy-Con likenesses, no Nintendo logos, no Switch
   trademark. Icons for system features (Album, Controllers, Mii edit, Amiibo, Web) must be
   *generic originals* (photo card, generic gamepad silhouette, generic avatar, NFC waves, globe).
   See `docs/QOS-REBRAND-ASSET-INVENTORY.md` for the exact upstream-art replacement list.
9. **Keep each theme's personality** (see §2 anti-goals).

---

## 5. The 10 themes at a glance (your variation matrix)

Every asset that varies by theme must have a variant per row. Personalities are LOCKED — match
them, don't redesign them. (Full palettes + hex in `01-CURRENT-STATE.md`.)

| Idx | Name | Personality (one line) | Accent | Base |
|----:|------|------------------------|--------|------|
| 0 | **Q OS** | Dark "liquid glass," the flagship | Sky cyan `#7DD3FC` | Deep navy `#0A0A14` |
| 1 | **Neon** | Black + hot-magenta + electric-cyan + lime | Hot magenta `#FF2AD0` | Near-black violet `#050010` |
| 2 | **Minimal** | Warm stone gray + dusty amber, quiet | Warm off-white `#D4C8B4` | Warm gray `#1A1816` |
| 3 | **Retro** | Amber CRT + green phosphor text + scanlines | Amber `#FFA83A` | Navy `#0A1420` |
| 4 | **Cards** | Warm amber on blue slate, layered cards | Warm amber `#FF9A3C` | Blue slate `#1C2030` |
| 5 | **Pastel** | Soft slate + powder pink + mint + lavender | Powder pink `#FBC6E4` | Soft slate `#1E1E28` |
| 6 | **Dark** | Embers in a void, pure black + ember orange | Ember orange `#FF6040` | Pure black `#000004` |
| 7 | **Gradient** | Indigo→violet→cyan smooth transition | Violet `#A070FF` | Indigo `#100522` |
| 8 | **Blueprint** | Drafting paper, cyan lines on blueprint blue | Cyan `#7AE0FF` | Blueprint blue `#051832` |
| 9 | **Pixel** | NES limited palette, bold primaries | NES yellow `#FFCC00` | Dark navy `#000018` |

---

## 6. How to use this package with the Claude Design setup form

Claude Design's intake asks for a company/product blurb, freeform notes, and attachments, then
returns a design system + assets. Here's exactly what to enter so the output lands on-target.

### 6a. "Company name and blurb" field
Paste this:

> **Q OS** — a clean, modern desktop operating system that runs on a Nintendo Switch. It's a
> homebrew console shell with a real windowed desktop: top bar, dock, draggable windows, a desktop
> icon grid, and 10 swappable themes (a dark "liquid glass" flagship plus Neon, Minimal, Retro,
> Cards, Pastel, Dark, Gradient, Blueprint, and Pixel). The aesthetic is cohesive and characterful:
> one operating system wearing ten outfits. Everything renders at 1920×1080 (docked) / 1280×720
> (handheld) with SDL2 — so we need flat raster + vector source assets and hex color tokens, not a
> web component kit. We're overhauling the whole visual language: theme palettes, the selection
> outline, the mouse cursor, icons + icon packs, glyphs, wallpapers, and a custom OS font.

### 6b. Notes / instructions field
Paste this:

> Please treat the attached brief as the source of truth (start with 00-BRIEF, then 02-OVERHAUL-SPEC
> for per-asset specs and 03-DELIVERABLES for exact output formats/sizes). Hard constraints:
> (1) deliver editable vector masters AND exported transparent PNGs at the exact pixel sizes in the
> deliverables doc; (2) colors as `#RRGGBB`/`#RRGGBBAA` hex per the 18 named roles; (3) wallpapers
> at 1280×720 full-bleed; (4) tiny GPU + animation budget — static or 1–2-frame states only;
> (5) custom font as an OFL-or-similar `.ttf` with full Latin coverage that degrades gracefully;
> (6) no Nintendo/trademarked IP; (7) must read at 720p and TV distance; (8) keep each of the 10
> themes' distinct personalities — tighten the system, don't homogenize the variants. Top priorities,
> in order: the selection outline, the theme-aware cursor, then the icon system, glyphs, wallpapers,
> and the font. Deliver one master design system (tokens + the variation rules) plus per-theme
> variants for everything that varies by theme.

### 6c. What to attach
Attach all four markdown files in this folder:
- `00-BRIEF.md` (this file)
- `01-CURRENT-STATE.md`
- `02-OVERHAUL-SPEC.md`
- `03-DELIVERABLES-AND-PIPELINE.md`

If the form also accepts reference images, attach (optional but ideal):
- The 10 procedural wallpaper screenshots (capture from a running Q OS, one per theme) so the
  designer sees the current texture language they're replacing.
- A screenshot of the desktop icon grid with a focused/selected tile (shows the current selection
  rectangle + cursor in context).
- The current `romfs/Logo.png` (256×256) and one theme's `theme/Icon.png` for the brand mark.

> ⚠️ **Mirror-vs-local note for Claude Design:** the public mirror Claude Design can browse
> (`github.com/Jmesmykil/QNX.git`) is **~30 versions behind** the local working tree. This brief
> is written around the **design *language and intent*** (palettes, token roles, integration
> contracts, file paths) — all of which are stable across that gap — **not** around exact file
> revisions. Where the mirror's code differs in detail, trust **this brief's described contracts**
> (theme = ZIP with `theme/Manifest.json` + `ui/QdPalette.json`; cursor/selection drawn in
> `qdesktop`; wallpaper via `ui/Background.png` or procedural pack; font via `ui/Font.ttf`), and
> flag any contract that looks materially different in the mirror so we can reconcile against local.

### 6d. What to expect back (deliverables you should request)
1. A **Q OS design-system doc**: the unified token set (color roles, type scale, spacing, radius,
   elevation, motion) + the per-theme palette table.
2. **Selection-outline** spec + exported corner/ring assets (per `03-DELIVERABLES`).
3. **Cursor** set: pointer + right-click states, per-theme, exported at the required sizes,
   with hotspot documented.
4. **Icon system**: the core system/dock/folder icon set as a coherent family, plus the rules
   (grid, stroke, corner, padding) for spinning up new **icon packs**, plus at least the 10
   per-theme folder-icon variants.
5. **Glyph set**: window-button glyphs, status glyphs, the "Q" brand mark, favorites star, badges.
6. **Wallpapers**: one art-directed 1280×720 wallpaper per theme (10), plus the recipe for future
   **wallpaper packs**.
7. **Custom font**: the `.ttf`/`.otf` + a specimen + license, with weights/usage documented.
8. **Editable vector masters** (Figma/SVG) for everything raster, so we can re-export later.

---

## 7. Where everything lives (quick map for the designer's mental model)

| Concern | Repo location | Format today |
|---------|---------------|--------------|
| Theme bundles (10) | `romfs/themes/q-os-{0..9}-*.ultheme` | ZIP archives |
| Base/default theme assets | `romfs/default/ui/**`, `romfs/default/theme/Manifest.json` | PNG + JSON |
| Per-theme palette (18 roles) | inside each `.ultheme` → `ui/QdPalette.json` | JSON hex |
| Palette also hardcoded in C++ | `include/ul/menu/qdesktop/qd_Theme.hpp` (10 factories) | C++ structs |
| Selection outline / focus ring | `source/ul/menu/qdesktop/qd_Launchpad.cpp` `PaintCell()` (icon grid); `qd_Window.cpp` (windows) | SDL rects |
| Mouse cursor | `source/ul/menu/qdesktop/qd_Cursor.cpp` (+ `.hpp`) | procedural SDL texture |
| Window chrome / buttons | `source/ul/menu/qdesktop/qd_Window.cpp`; geometry in `qd_WmConstants.hpp` | SDL primitives |
| Wallpapers (procedural) | `qd_Wallpaper.cpp` (pack 0) + `qd_WallpaperPacks.cpp` (packs 1–9) | runtime pixels |
| Wallpapers (image path) | `QdImageWallpaperElement` reads `ui/Background.png` from theme cache | PNG/JPEG |
| Icons (system/dock/folder) | `romfs/default/ui/Main/EntryIcon/*.png` + per-theme inside `.ultheme` | PNG |
| Glyphs | mostly primitive SDL shapes + Unicode chars in font; star ★ via RenderText | code / font |
| Font | `ui/Font.ttf` in active theme cache (none ship one today) → falls back to Nintendo shared font | TTF |
| Brand mark "Q" | drawn as primitive rects (`DrawThemeTransitionFrame`, hot-corner) + `romfs/Logo.png` 256² | code + PNG |
| Upstream-art replacement list | `docs/QOS-REBRAND-ASSET-INVENTORY.md` | doc |

Proceed to `01-CURRENT-STATE.md` for the full, file-cited inventory and the honest audit of
what's inconsistent (the "tighten-up" targets), then `02-OVERHAUL-SPEC.md` for the spec.
