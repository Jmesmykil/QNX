---
name: q-os-design
description: Use this skill to generate well-branded interfaces and assets for Q OS uMenu (a windowed desktop OS for Nintendo Switch homebrew), either for production or throwaway prototypes/mocks/etc. Contains essential design guidelines, colors, type, fonts, assets, and UI kit components for prototyping.
user-invocable: true
---

Read the README.md file within this skill, and explore the other available files.

If creating visual artifacts (slides, mocks, throwaway prototypes, etc), copy assets out and create static HTML files for the user to view. If working on production code, you can copy assets and read the rules here to become an expert in designing with this brand.

If the user invokes this skill without any other guidance, ask them what they want to build or design, ask some questions, and act as an expert designer who outputs HTML artifacts _or_ production code, depending on the need.

Key facts to anchor on:
- Q OS uMenu is a homebrew Switch launcher styled as a windowed desktop OS. Unaffiliated with BlackBerry's QNX.
- The look: a deep navy "void" gradient, electric-cyan accent (#7DD3FC in-product / #00E5FF brand), magenta + lavender pops, four-color window controls (close red / minimize amber / maximize green).
- Type: Newsreader italic (serif wordmark), IBM Plex Sans (UI), IBM Plex Mono (paths/hex/version/commands).
- 10 swappable themes live under `tokens/themes.css` as `[data-theme="…"]` scopes — selecting one swaps palette + wallpaper + icon pack together.
- Voice: first-person, honest, technical, spec-driven; no marketing hype; effectively no emoji.
- Components compile into `_ds_bundle.js`; link `styles.css` and read components off `window.<Namespace>` (run the project's design-system check to get the exact namespace).
- The `ui_kits/desktop/` recreation is the reference for how surfaces compose.
