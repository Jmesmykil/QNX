# Q OS uMenu — Roadmap

> Forward-looking plan. Status of past releases lives in [CHANGELOG.md](./CHANGELOG.md).
> Old internal version-chain notes from the v0.x / v1.x bring-up phase are archived under [`archive/`](./archive/).

---

## Where we are

**v3.4.0 — shipped.** HW-verified GREEN on OG Switch Erista, Atmosphère 1.8.x + Hekate, HOS 20.0.0.

v3.x cumulative deliverables:
- Ten per-theme icon packs (170 distinct PNGs).
- Multi-window desktop UI (drag / resize / minimize / maximize / pin-to-corner / dock-zone bypass / singleton windows).
- HBMenu absorbed → Vault file manager with 12 filesystem shortcuts and per-file ctx menus.
- Resource ledger + perf logger + windowed Monitor (true task manager).
- Pokémon save autoscan + read-only SwSh save viewer (W13-PARSER, v3.4).
- Atmosphère cheats browser + in-OS HTTPS installer filtered by installed games (W14-INSTALLER, v3.4–v3.5).
- 7-tile dock: Vault / Monitor / AllPrograms / Tasks / Nintendo / Saves / Cheats.

---

## Product direction

**Q OS uMenu is becoming the unified clean-room desktop OS for the Switch homebrew ecosystem.** Every release absorbs another well-loved community tool into the OS proper, until users no longer need to juggle a dozen separate NROs. We are not adding novelty features for their own sake — every absorption replaces a real, in-the-wild homebrew workflow with a polished, integrated version.

**Three rules that govern absorption:**

1. **Clean-room reimplementation.** We do not link or vendor closed-source community code. Every absorbed feature is rewritten from public specs / open-source reference (PKHeX, sphaira, EdiZon-Reborn, HamletDuFromage/switch-cheats-db, etc.) under GPL-2.0 compatible terms.
2. **Bulletproof the existing surface before adding new ones.** Each release locks down what's already shipped before stacking new features. The W15 4-agent audit gate is the new normal.
3. **Don't promise what hardware can't deliver.** Windowed external NROs (v3.1 headline) was researched and ruled infeasible without an Atmosphère extension nobody has built. Marked **deferred indefinitely** below until the broader absorption work makes it tractable.

---

## Deferred indefinitely

### Windowed external homebrew

The v3.1 plan was to redirect `hbloader`'s nv-gfx output into a Q OS window. After hardware analysis (`docs/L-CYCLE-WINDOW-MANAGER-DESIGN.md §3.1–3.2`) and the W13 absorption survey, this is not feasible without an Atmosphère extension that doesn't exist yet. The pragmatic substitute that already ships:

- **L+2 window-state persistence** — open windows survive NRO round-trips. Launch an NRO from a Q OS launchpad inside a folder window, exit the NRO, the same launchpad reopens at the same scroll + focus.
- **Vault homebrew launch from any sidebar shortcut** — keeps the folder context.

We will revisit windowed external NROs when (a) Atmosphère exposes a framebuffer-redirect extension, or (b) enough community functionality has been absorbed into Q OS proper that external NROs become rare.

---

## v3.5 — Stabilize + absorb sprint (in progress)

Three sub-waves landing now:

- **W14-CHEATS-INSTALLER** ✅ — `nsListApplicationRecord` → GitHub releases API → libnx ssl → libminizip filtered extract. Only installs cheats for installed games. Eliminates the bulk-install boot hang.
- **W13-SAVE-PARSER** ✅ — verbatim PKHeX SwishCrypto XOR pad + SHA-256 + correct chunk advance (0x7F). PK8 decode (species / nickname / level / item / shiny / IVs) for Sword/Shield. PartyBox shows 6 real Pokémon. **Read-only**; write-back is v3.6.
- **W15 stabilization** ✅ — TaskManager ns refcount fix (P1 session-corruption), Monitor psm/nifm refcount fix, capture-buffer leak fix (`unique_ptr`), WindowManager ledger UNTRACK on teardown, SaveAutoscan entry cap=64, CheatsLayout detail-pane shadow-cached.

**Still landing before v3.5.0 ships:**

- **W16-TELNET-HARDEN** — port 9999 `CmdLaunch` has carried INADDR_ANY + no path validation + no auth for 5 releases. Three fixes: path-prefix guard, bind to `nifmGetCurrentIpAddress()`, 6-digit PIN required as first line. **Security debt; no more carries.**
- **W16-THEMES-AUDIT** — bulletproof the 10 existing themes. Audit every theme's wallpaper + palette + folder PNG pack + hot-corner emblem. Find broken / inconsistent assets. Lock down the theme system as the user-visible polish anchor.

---

## v3.6 — Footprint phase 2 + absorb wave 1

- **Lazy theme loading** (slipped twice — committed for v3.6). Target: −40–60 MiB resident set when only one theme active.
- **SwSh save write-back** (Q OS makes its first real save modification). Test on backup-restored saves first; never on the live `main`.
- **Async NS title resolver.** Cheats UI shows real game names instead of `"TID 0x<hex>"`.
- **JKSV-style save backup operation.** Today we autoscan JKSV's directory format read-only; v3.6 adds the actual "back this save up" button.
- **Tier-A render churn cleanup** (NintendoAppsLayout, LockscreenLayout, Launchpad display_text, VaultLayout sidebar color cache).
- **Drop `restore.sh` dual-layout polish** carried since v3.0.x. Add a v3.6 patch lane for it.

---

## v3.7 — Absorb wave 2

- **Goldleaf-style NSP installer** — local-SD-only install of `.nsp` / `.nsz` / `.xci`. No title-key dumping; legal-clean path.
- **sys-clk profile UI** — read sys-clk's existing daemon state via its CSV file API; don't replace the daemon, just give it a UI surface in Monitor's Stats view.
- **Album browser via `caps:a`** — grid thumbnails of screenshots; tap to view fullscreen.
- **FTP server** — port the BSD-socket-based pattern from sphaira. Read-only first; write requires HID-confirmation toast.
- **Overlay manager** — install / enable / disable `.ovl` files in `sdmc:/switch/.overlays/`. Tesla overlay re-enable can ride along once the manager is in place.

---

## v3.8+ — Absorb wave 3 (research-level)

- **HDR enabler** — Atmosphère config write to enable HDR on supported titles.
- **Atmosphère config tweaks UI** — sys-clk-style overlay config surface for the most common Atmosphère options.
- **Save data cloud backup** — opt-in upload of save .zip to a user-specified endpoint (no built-in cloud).
- **Multi-user account switching from the dock.** Needs libstratosphere IPC work that may not be exposed yet.
- **Q OS-native sysmodules.** Move theme cache management, NRO scanner, window manager into sysmodule services so Q OS-aware NROs can call into them.
- **Update channel.** In-app "check for updates" against the public QNX release feed with cryptographic signature verification.

---

## Process rules

Four non-negotiables learned from the v0.x → v3.4.x grind:

1. **One change per release.** Two simultaneous changes = two releases. Mixing them makes the inevitable HW regression impossible to bisect.
2. **Never build on a crashing base.** If `vN` crashes on hardware, revert to `vN-1` or fix as a targeted patch (`vN.1`). Never add features on top of a fatal.
3. **Composition over reinvention.** Before reimplementing any low-level system call, IPC stub, or kernel chainload glue, check whether an existing community NRO already solves it. The hot-corner Reboot-to-Hekate path is the canonical example — three lines composing `reboot_to_hekate.nro` replaced ~200 lines of failed direct-reimplementation attempts.
4. **W15 audit gate before every minor release.** Four parallel read-only agents (boot+services / render / memory / roadmap drift). Zero code FAILs before tagging stable.

Design notes for the items above live under [`docs/`](./docs/). The full history of the version chain is in [CHANGELOG.md](./CHANGELOG.md).
