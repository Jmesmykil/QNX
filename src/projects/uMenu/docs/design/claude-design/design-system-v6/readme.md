# Q OS Design System

A design system for **Q OS uMenu** — *a windowed desktop OS, built on top of the
Nintendo Switch home menu.* Q OS uMenu is a homebrew launcher (a `qlaunch`
replacement for Atmosphère CFW, forked from XorTroll's uLaunch) that behaves
like a real desktop: drag-and-resize windows, a taskbar dock, hot corners, a
file-manager Vault, a process Monitor, and **ten swappable themes** that each
ship their own icon pack, wallpaper, palette, and corner emblem.

This system captures that identity — the deep navy void, the electric-cyan
accent, the four-color window controls, the mono technical texture — as tokens,
components, and a full interactive recreation of the desktop, so any agent can
design on-brand Q OS surfaces.

> ⚠️ **Unaffiliated with BlackBerry's QNX.** "Q OS" here is the homebrew Switch
> project Q OS uMenu.

---

## Sources

Everything here was reverse-engineered from the project's own code and assets.
The reader may not have access, but for the record:

- **GitHub (this fork):** <https://github.com/Jmesmykil/QNX> — *Q OS uMenu*, the
  public mirror. Tokens were lifted verbatim from
  `src/projects/uMenu/include/ul/menu/qdesktop/qd_Theme.hpp` (the 10 `QdTheme`
  factories); chrome geometry from `qd_GlobalChrome.cpp` /
  `qd_HotCornerRightDropdown.cpp`; category colors from `qd_FolderTheme.cpp`;
  the icon vocabulary from `scripts/generate-qos-theme-icons.py`; brand palette
  from the README shields + `assets/branding/`.
- **Q OS umbrella project:** <https://github.com/Jmesmykil/QOS>
- **Upstream uLaunch** (architecture source of truth): <https://github.com/XorTroll/uLaunch>

Explore the QNX repo to design with deeper fidelity — the `docs/` tree and
`README.md` describe every window, dropdown, and theme in detail.

---

## CONTENT FUNDAMENTALS

**Voice — a developer talking straight to other tinkerers.** The README is
first-person and unvarnished: *"This is the HBMenu replacement,"* *"I develop on
macOS,"* *"Until then, only run uMenu on a LAN you trust."* It earns trust by
being honest about limits, not by selling.

- **Person:** First-person singular for the author's decisions ("I forked it,
  reskinned it"), second-person for the user ("pull the bundle out of
  `atmosphere/contents/` and you're back to stock"). Never corporate "we."
- **Casing:** Sentence case for prose. `UPPER` only for status badges and mono
  micro-labels (`HW-VERIFIED`, `BATTERY 87%`). Product names keep their own
  casing: *Q OS*, *uMenu*, *Atmosphère*, *Hekate*, *libnx*, *NRO*.
- **Tone:** Precise, technical, dry-honest. States facts and tradeoffs:
  *"Three lines of glue replaced ~200 lines of failed reinvention."* Owns bugs
  openly ("Known issues" calls out a security gap by name). Confidence without
  hype.
- **Numbers & specifics carry the weight.** Real values, not vibes — `780×480 →
  1280×800`, `port 9999`, `10 Hz cap; 5 MiB rotation`, `HOS 20.0.0`. When you
  write Q OS copy, prefer the concrete spec over the adjective.
- **Composition principle as a mantra:** *"compose existing community projects,
  don't reinvent."* Credit is load-bearing — the README spends a whole section
  thanking upstream authors by name. Respect that ethic in any copy.
- **Emoji:** Effectively none in product/UI. The README uses a couple of
  sparing inline markers (⚠️) for warnings only. **Do not** decorate Q OS
  surfaces with emoji. Glyphs are drawn icons or mono symbols, not emoji.
- **Micro-copy examples:** instruction strips read like controller hints —
  `B / + Close`, `A Launch · Y Edit save · ZL Menu`, `L / R Switch view`.
  Always button-glyph + verb, mono, terse.

---

## VISUAL FOUNDATIONS

**The void.** Every surface sits on a deep navy-to-violet gradient
(`#0A0E1A → #0A0A14 → #140A26`) lit from the top-left in cyan and the
bottom-right in magenta — the glow of the hero "Q" bubble. Backgrounds are
dark, near-black, never flat: there's always a faint radial light. Use
`.qos-void`.

**Color.** One electric accent on a navy field. In-product chrome uses a soft
sky cyan `#7DD3FC` (`--accent`); branding/hero moments use a punchier
`#00E5FF` (`--brand-cyan`). Magenta `#D946EF` and lavender `#A78BFA` are pops,
never primary. The four window controls are color-coded and unmistakable: close
**red**, minimize **amber**, maximize/restore **green**. Auto-folder tiles tint
by category (games blue, homebrew green, system purple, payloads orange,
builtin lavender, custom cyan). Ten full theme palettes ship — selecting one
swaps palette + wallpaper + icon pack atomically.

**Type.** Three families. **Newsreader italic** is the elegant serif wordmark
("Q OS uMenu") — premium framing over homebrew, used only for brand/hero
moments. **IBM Plex Sans** is the UI and body face. **IBM Plex Mono** carries
the technical texture: file paths, hex cheat codes, version strings, telnet
commands, status labels — it's everywhere a developer's eye lands.

**Surfaces & cards.** Translucent "glass": a navy fill at ~92% opacity, a 1px
cyan hairline (`rgba(125,211,252,0.16–0.45)`), 12px radius, soft drop shadow,
and a backdrop blur. Active windows brighten the hairline to the full accent;
inactive ones fall back to `--titlebar-inactive`. Dropdown panels use an 8px
radius with a *full* accent border ring (two-pass paint: accent rect, navy
inset).

**Borders & rings.** 1px accent hairlines mark every chrome edge — the top bar
bottom, the dock top, panel outlines — usually at low alpha (0.16–0.5). Folder
tiles get a 2px *solid* category-color ring. Focus is a brighter ring, not a
glow alone.

**Shadows & glow.** Windows cast a soft, large, downward shadow on the void
(`--shadow-window`). Glows are restrained — a cyan ring-glow on hover
(`--glow-accent`), a magenta bloom for emphasis (`--glow-magenta`). This is dark
UI, not a lightshow; one glowing element at a time.

**Radii.** Controls 6–8px, cards/windows 12px, large surfaces 18px (folder
tiles), and fully-round (999px) for dock dots, the cursor, badges, and corner
buttons.

**Corner radii vibe:** soft but not pill-soft. Sharp-cornered slabs belong to
the Dark/Pixel themes only; the default Glass is gently rounded.

**Motion.** Quick and physical. Hover lifts tiles `translateY(-3 to -4px)` with
a `cubic-bezier(.2,.8,.2,1)` ease; press shrinks to `scale(0.97)`; transitions
are 80–180ms. Theme transitions paint one bridging frame in the destination
palette (no jarring flash). No infinite decorative loops, no bounces on content.

**Hover / press states.** Hover: brighten background tint (+ accent ring glow on
interactive tiles), lift. Press: scale down slightly. Active/focused: accent
fill or border + a state dot. Disabled: 40% opacity, `not-allowed`.

**Transparency & blur.** Used deliberately for chrome that floats over content:
the top bar (75% opacity), dock (63%), dropdowns (92%) all use `backdrop-filter`
blur. Window bodies are mostly opaque glass. The alpha levels mirror the SDL
byte values in the code (`--alpha-topbar/dock/panel`).

**Imagery.** The only photographic-style assets are the wallpaper gradients —
dark, cool, with a subtle violet floor and cyan/magenta point-lights. No
photos, no stock illustration. Brand imagery is the glowing Q bubble and the
icon-pack grid.

**Layout rules.** The desktop is a fixed `1920×1080` canvas. Persistent chrome
is pinned: top bar (48px) at `y=0`, dock (148px) at the bottom, hot corners at
`96×72`. Content (folder grid, windows) lives between them. Windows default to
`1280×800` and never letterbox their content.

---

## ICONOGRAPHY

Q OS has a **per-theme icon-pack system** — its single most distinctive visual
idea. There are 17 glyph roles (6 desktop categories, 10 launchpad roles, 1 hot
-corner emblem) and **each of the 10 themes draws them in its own shape
vocabulary, not a recolor**: Glass is smooth filled silhouettes, Neon is glowing
outlined arcade shapes, Minimal is thin line art, Retro/Pixel are chunky pixel
art, Blueprint is technical schematic strokes, Pastel is rounded blobs with
eye-circles, Cards uses playing-card suits. 170 PNGs total, generated offline by
`scripts/generate-qos-theme-icons.py`. See `assets/branding/v3-icon-pack-grid.png`
for the full matrix.

**How icons are produced in the real product:** procedural SDL draws (filled
rects, arcs, polygons) and pre-baked PNGs shipped inside each `.ultheme` bundle
— *no runtime emoji, no icon font.* The Glass folder pack literally renders the
**category letter** (G/H/S/P/B/F) inside a ringed glass tile.

**In this design system:** we did **not** redraw the 170-glyph pack. Component
glyphs use mono Unicode symbols (`▤ ◷ ⚙ ◳ ⌗ ⓘ`) and the authentic category
letters as lightweight stand-ins — readable and on-brand without faking the
pixel-perfect art. The hot-corner emblem uses the serif **Q** (and per-theme
swaps: ⚡ Neon, 🔥 Dark, ♥ Pastel). **When building a real Q OS surface, prefer
copying the actual PNGs from a `.ultheme` bundle** over Unicode. The brand
*assets* (logos, hero, icon grid) live in `assets/` and should be used directly.

**Emoji:** not part of the product. Only the theme-emblem stand-ins (🔥 ♥) and a
couple of dock symbols use Unicode for convenience here; production surfaces
should use drawn glyphs.

---

## Index / manifest

**Root**
- `styles.css` — the single entry point consumers link (`@import` manifest only).
- `base.css` — element defaults + `.qos-void`, `.qos-glass`, `.qos-wordmark`, `.qos-eyebrow` helpers.
- `fonts.css` — Newsreader + IBM Plex Sans/Mono (Google Fonts).
- `SKILL.md` — Agent-Skill front matter for use in Claude Code.

**`tokens/`**
- `colors.css` — brand palette, Glass surfaces, accent, text, window buttons, category + status colors, alpha helpers.
- `typography.css` — font tokens + type scale, weights, leading, tracking.
- `spacing.css` — spacing scale, radii, borders, shadows/glows, desktop chrome geometry.
- `themes.css` — all 10 theme palettes as `[data-theme="…"]` scopes.

**`components/`** (React primitives — `import { X } from window.<Namespace>`)
- `core/` — **Button** (primary/secondary/ghost/danger), **Badge** (mono status pill), **Card** (glass surface).
- `window/` — **WindowFrame** (the signature windowed-OS chrome; wireable corner buttons + drag).
- `desktop/` — **FolderTile** (category folder), **DockTile** (dock entry).
- `menu/` — **MenuPanel** (dropdown surface), **StatusRow** (status/action row), **HotCornerWidget** (corner emblem).

**`ui_kits/`**
- `desktop/` — full interactive recreation of the Q OS desktop (lock screen, folder grid, draggable windows, hot-corner dropdowns, live theme switching). Entry: `index.html`.

**`guidelines/`** — foundation specimen cards (Colors / Type / Spacing / Brand) shown in the Design System tab, plus the **Overhaul** group (selection outline, theme-aware cursor, glyph family, wallpapers, the 10-theme showcase).

**`deliverables/`** — the **visual-overhaul handback** (repo-ready, see `deliverables/README.md`): retuned `palettes.json` + `tokens.json`, `SELECTION-SPEC.md`, `CURSOR-SPEC.md`, `GLYPH-SPEC.md` (+ SVG masters), `ICON-PACK-RECIPE.md`, `WALLPAPER-PACK-RECIPE.md`, `FONT-SPEC.md`, and 10 art-directed 1280×720 `Background.png` wallpapers.

**`assets/`**
- `branding/` — hero banners, icon-pack grid, brand palette, icons row.
- `qos-rebrand/` — desktop chrome textures (background, cursor, entry-menu bg, selected).
- `qos-icon-256.jpg` — app icon.

---

## Using it

Link `styles.css`, then build on the tokens and components. Default `:root` is
the **Glass** theme; wrap any subtree in `data-theme="neon"` (etc.) to switch.
The compiler bundles every component into `_ds_bundle.js` under the namespace
reported by `check_design_system` — load that script and read components off
`window.<Namespace>`.
