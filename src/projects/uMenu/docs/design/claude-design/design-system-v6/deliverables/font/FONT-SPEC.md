# Q OS — Font Spec  (overhaul 🟡, high leverage)

The `ui/Font.ttf` slot exists and works (`main.cpp:616` — a theme's `Font.ttf`
becomes the default UI face; else Nintendo's shared font). No theme ships one
today, so the whole OS reads as "stock Switch." Shipping a signature face is the
single biggest "this is its own OS" lever. **No code change needed to use one —
just ship the file in the bundle.**

> I can't mint a `.ttf` binary in this tool, so this is a **sourcing
> recommendation + integration spec**, not a generated font. All options below
> are **SIL Open Font License 1.1 (OFL)** — free to embed/redistribute in an
> open-source homebrew binary. Download the static `.ttf` weights and drop them
> in per §Integration.

## Primary recommendation — **Space Grotesk**  (OFL 1.1)

- **Why it fits:** a geometric grotesque with a technical, slightly mechanical
  character — modern and distinctive without being cold. Pairs naturally with
  the "liquid glass" flagship; reads as a *console OS*, not a corporate web app.
- **Source:** github.com/floriankarsten/space-grotesk · Google Fonts "Space Grotesk".
- **Coverage:** full Basic Latin + Latin-1 Supplement (ASCII + accented Latin).
  Degrades gracefully — Q OS keeps loading the Nintendo shared font for
  CJK / emoji / button glyphs alongside it.
- **Weights to ship:** 400 Regular, 500 Medium, 600 SemiBold, 700 Bold.
- **Figures:** derived from a monospace — even, **tabular-friendly** numerals
  (good for clock / battery % / counts). Legible at 14px @1080.

### Type scale mapping (see tokens.json → type)
| Token | px @1080 | Weight | Face use |
|---|---|---|---|
| display | 104 | 600 | brand splash, lock clock |
| title (Large) | 44 | 600 | window titles |
| headline (MediumLarge) | 32 | 600 | section headers |
| body (Medium) | 26 | 400 | UI text, toasts |
| label (Small/22) | 20 | 500 | icon labels, status |
| caption (extra/14) | 15 | 400 | dense secondary (min legible 14) |

Pair with **IBM Plex Mono** (OFL) for paths / hex cheat codes / version strings
in surfaces that show technical text (it's the design-system mono throughout).

### Conservative alternative
If small-label legibility at TV distance needs more humanist warmth, **Hanken
Grotesk** or **Lexend** (both OFL, both legibility-tuned) are drop-in swaps with
wider weight ranges. Space Grotesk is the *signature* pick; these are the *safe*
pick.

## Optional thematic alternates (per-theme `ui/Font.ttf`, all OFL, all full Latin)
- **Pixel** → **Pixelify Sans** (OFL) — chunky pixel face, on-brand 8-bit.
- **Blueprint** → **IBM Plex Mono** (OFL) — technical drafting feel.
- **Retro** → **VT323** (OFL) — CRT terminal face (use sparingly; check label
  legibility at 14px).

Treat these as flourishes over the one primary OS font — ship only in those
themes' bundles.

## Integration
- **OS-wide (recommended for cohesion):** ship the **same** `Space-Grotesk` TTF
  as `ui/Font.ttf` in **all 10** `.ultheme` bundles, OR place it once in
  `romfs/default/` and point the `TryGetActiveThemeResource("ui/Font.ttf")`
  fallback at it (small loader tweak in `main.cpp`).
- **Per-theme alt:** drop the alternate `ui/Font.ttf` only in that theme's
  bundle — `main.cpp:616` picks it up automatically when the theme is active.
- Ship the **`LICENSE`** (the font's OFL text) alongside, per OFL redistribution
  terms. (`font/LICENSE` in the handback.)

## Deliverables in this folder
- `FONT-SPEC.md` (this file).
- `LICENSE` — paste the chosen font's OFL 1.1 text here when you drop the TTF.
- *(you add)* `Font.ttf` — the downloaded static weight(s) of Space Grotesk.
