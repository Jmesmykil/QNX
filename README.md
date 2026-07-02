<div align="center">

![Q OS uMenu](https://raw.githubusercontent.com/Jmesmykil/QNX/main/assets/branding/v3-hero-banner.png)

# Q OS uMenu

**A windowed desktop OS, built on top of the Nintendo Switch home menu.**

[![License](https://img.shields.io/badge/license-GPLv2-00E5FF?style=for-the-badge)](LICENSE)
[![Download — latest release](https://img.shields.io/badge/download-latest%20release-D946EF?style=for-the-badge&logo=github)](https://github.com/Jmesmykil/QNX/releases/latest)
![HW-verified — Switch Erista](https://img.shields.io/badge/HW--verified-Switch%20Erista-22C55E?style=for-the-badge)
![Built on Atmosphère](https://img.shields.io/badge/built%20on-Atmosph%C3%A8re-A78BFA?style=for-the-badge)

[What it is](#what-it-is) · [Features](#features) · [Themes](#themes) · [Install](#install) · [Controls](#controls) · [Build](#build-from-source) · [Architecture](#architecture) · [Credits](#credits) · [License](#license)

</div>

---

## What it is

A Nintendo Switch homebrew launcher that **acts like a real desktop OS**. Drag-and-resize windows with themed frames. Pin a process monitor to the corner while you play. Tap dock tiles like a taskbar. Right-click any icon (ZL or long-press). Swappable themes that change the icon pack, wallpaper, palette, window chrome, and hot-corner emblem all at once.

It replaces `qlaunch` in Atmosphère's SystemApplet slot (`0100000000001000`) and runs alongside Hekate. Pull the bundle out of `atmosphere/contents/` and you're back to the stock Nintendo home menu with zero side effects.

Forked from [XorTroll's uLaunch](https://github.com/XorTroll/uLaunch). It is **not** an OS, **not** a kernel, **not** a CFW — it is the GUI layer that sits inside the CFW stack you already run. See [Architecture](#architecture) for exactly where it fits.

> **Status:** HW-verified on an OG Switch Erista (T210) running Atmosphère + Hekate, current HOS. Every feature below is what the device actually does when it boots. Per-release history lives in [CHANGELOG.md](CHANGELOG.md); the plan ahead lives in [ROADMAP.md](ROADMAP.md).

<div align="center">

![The Q OS windowed desktop](https://raw.githubusercontent.com/Jmesmykil/QNX/main/assets/screenshots/desktop-q-os.png)

</div>

## Features

![Q OS theme icon-pack grid — every theme is its own shape vocabulary](https://raw.githubusercontent.com/Jmesmykil/QNX/main/assets/branding/v3-icon-pack-grid.png)

Every theme is its own icon pack — not the same shapes recolored. Glass uses smooth filled silhouettes; Neon uses outlined arcade shapes with a glow; Retro draws a NES pad and a cartridge; Pixel is chunky 16×16 pixel art; Blueprint is technical schematic strokes; Pastel is rounded blobs. Pick one and the whole desktop swaps to that vocabulary.

### Desktop

- **Multi-window UI with themed chrome** — each window frame is a per-theme SVG master, rasterized and drawn as a nine-patch so corners stay crisp at any size. Drop shadow, drag-to-move, and four corner buttons, each with its own colour and glyph so the action is unmistakable: **× close (top-left)**, **□ maximize (top-right)**, **– minimize (bottom-left)**, **↗ resize grip (bottom-right)**. A bottom-bar instruction strip lives in the chrome itself, so the controls are never a guess. Windows default to 1280×800 (about ⅔ of the screen) and content fills the viewport with no dead side margins.

![Q OS window chrome — four corner buttons, each its own colour and glyph](https://raw.githubusercontent.com/Jmesmykil/QNX/main/assets/branding/v3-window-chrome.png)

- **Six desktop folder tiles** (Games / Emulators / Tools / System / Q OS / Other) auto-classify every NRO on your SD card. Tap a tile and the folder opens as a category-filtered launchpad window. The glyph on each tile follows the active theme.
- **Top-left hot corner** carries the theme's identity emblem (Q for Glass, lightning for Neon, Pac-wedge for Retro, heart for Pastel, flame for Dark, prism for Gradient, compass for Blueprint, 8-bit star for Pixel). Tap it for the app / recent / search dropdown.
- **Top-right hot corner** shows live system status (battery, charger type, time, date, network, output volume) plus quick actions — Sleep, Restart, **Reboot to Hekate**, Lock — and a Dev section (nxlink, USB serial, log flush).
- **Dock band** packs minimized windows right-to-left with live snapshot textures, taskbar-style.

### Built-in windowed apps

- **Vault** — the file manager, and the HBMenu replacement. 12 sidebar shortcuts (Homebrew / Saves / NSP-NCA / Payloads / Atmosphère / Themes / Q OS / Logs / Bootloader / Nintendo / Switch deep / SD root) plus a grid pane with a scrollbar for long lists. Inline text and image viewers preview files without leaving the desktop. "Launch as application" for retail-eligible NROs. Per-file context menu with "Edit Pokémon save" and "View Cheats".
- **Settings** — tabbed (System, Network, Audio, Display, Account, About, Folders, Themes). Live libnx data; battery / clock / network refresh once per second.
- **About Q OS** — card layout with logo, version, build timestamp, and live info rows across Build, Hardware, System, and Power (firmware, serial, model, region, emuMMC, NFC, USB 3.0).
- **Monitor** — a real task manager. **L/R** toggles **Stats** (CPU/GPU MHz, SoC + PCB temp, RAM, FPS, uptime, battery, network, Bluetooth) and **Resources** (live ledger of textures, services, sessions, threads, file handles, windows, snapshots — counts and bytes per kind). **Y** dumps the ledger to the log; **X** toggles per-frame perf logging to CSV.
- **Saves** — Pokémon save autoscan + editor surface. Walks `atmosphere/contents/<TID>/save/`, JKSV, and Checkpoint backups for the mainline Pokémon titles, reports what it found per game, and **Y** rescans.
- **Cheats** — an Atmosphère cheats browser + toggle. Walks `atmosphere/contents/<TID>/cheats/*.txt`, lists titles with cheats, and **A toggles** a cheat by commenting it out so it doesn't apply at launch. The Master Code is always preserved; a `.qos-backup` safety copy is written on first change.
- **Task Manager** — `pmdmnt`-backed list of running NRO processes plus open windows and dock-minimized entries, with per-row Focus / Close / Minimize. Suspended retail titles show their real title name and resume on tap.
- **Nintendo Apps** — launchers wrapped over libnx library applets: Album, Controllers, Mii Edit, Profile, Web, Error Display, Software Keyboard, and System Settings. These open full-screen because they are firmware-rendered sysmodule processes.

### Window manager

- **Pin to corner** — drag a window's titlebar into a screen corner to snap, including over the system top bar.
- **Dock-zone bypass** — dock tiles and both hot corners stay reachable while a window is focused.
- **Singleton windows** — tapping a dock tile twice focuses the existing window instead of spawning a duplicate.
- **B / +** closes any windowed layout; drag a titlebar into the dock band to minimize.

### Networking

- **Network NRO push** — run `nxlink -s <switch-ip> -r my.nro` from your dev machine; uMenu parks it under `sdmc:/switch/` and hands it straight to Launchpad.
- **Telnet DevConsole** — an optional headless debug shell, **off by default**. Treat it as trusted-LAN-only and leave it off unless you are actively debugging; see [the security note](#security) below.
- **Reboot to Hekate** — the top-right hot-corner action chainloads Hekate cleanly via the community NRO rather than reimplementing the reboot chain (see [The composition principle](#the-composition-principle)).

## Themes

Themes ship in the box. Apply them in **Settings → Themes**; the wallpaper, palette, icon pack, window chrome, hot-corner emblem, and loading-fade colours all swap together, no reboot.

![Q OS themes — all ten, each with its own wallpaper and window chrome](https://raw.githubusercontent.com/Jmesmykil/QNX/main/assets/branding/v3-themes-gallery.png)

| Theme | Accent | Style |
|---|---|---|
| **Q OS / Liquid Glass** | Cyan | Smooth filled silhouettes (default) |
| **Neon** | Magenta | Outlined arcade shapes with glow |
| **Minimal** | Warm beige | Thin-line / single-dot vocabulary |
| **Retro** | Amber | Pixel-art, NES / cartridge motifs |
| **Cards** | Warm amber | Playing-card-suit motifs |
| **Pastel** | Powder pink | Rounded "cute" blob forms |
| **Dark** | Ember orange | Heavy slab forms, brutalist edges |
| **Gradient** | Violet | Flowing curves, orbital motifs |
| **Blueprint** | Tech cyan | Technical-drawing strokes + dimension marks |
| **Pixel** | Yellow | Strict 16×16 chunky pixel art |

Custom `.ultheme` packs live under `sdmc:/ulaunch/themes/`. When you upgrade Q OS, bundled themes auto-refresh from `romfs`; displaced files are renamed `<name>.qos-prev.<size>.bak` so nothing is lost.

## Install

**You need:**
- A Nintendo Switch with **Atmosphère CFW** already working — the [Atmosphère README](https://github.com/Atmosphere-NX/Atmosphere) is the source of truth for that setup. Tested against current HOS (20.0.0).
- **Hekate** as your bootloader (recommended; required for the hot-corner Reboot-to-Hekate).
- **Tomvita's `reboot_to_hekate.nro`** under `sdmc:/switch/` (the hot-corner Hekate action delegates to it).
- Your SD card mounted on a computer.

**Steps:** download the [latest release](https://github.com/Jmesmykil/QNX/releases/latest) and drop its contents onto your SD card root. The layout is what Atmosphère expects:

```
sdmc:/atmosphere/contents/0100000000001000/exefs.nsp   ← uSystem (replaces qlaunch)
sdmc:/ulaunch/bin/uMenu/main                           ← Q OS menu binary
sdmc:/ulaunch/bin/uMenu/main.npdm
sdmc:/ulaunch/bin/uMenu/romfs.bin                      ← theme assets + bundled .ultheme packs
sdmc:/ulaunch/bin/uManager/                            ← management NRO assets
sdmc:/ulaunch/bin/uLoader/                             ← hbloader replacement
sdmc:/switch/uManager.nro                              ← Q OS manager NRO
sdmc:/switch/reboot_to_hekate.nro                      ← Tomvita NRO (required for hot-corner Hekate reboot)
```

Eject the SD card properly, boot Hekate, launch Atmosphère. Q OS uMenu loads instead of the stock home menu.

- **Upgrading:** drop the new files over the top. First boot refreshes the theme bundles on your SD and re-extracts the active theme against the new packs. Your settings, favourites, and custom themes are preserved.
- **Uninstall:** delete `sdmc:/atmosphere/contents/0100000000001000/exefs.nsp` and reboot. The stock home menu returns; saves, titles, and the rest of your CFW config are untouched.

## Controls

| Input | Action |
|---|---|
| **Touch** | Primary interaction — tap tiles, drag titlebars, hit corner buttons |
| **ZR** (hold) | Cursor mode — move a pointer with the stick for precise hits |
| **ZL** / long-press | Right-click — opens the per-icon context menu |
| **A** | Activate / launch / toggle the focused item |
| **B** / **+** | Close the focused window |
| **L / R** | Switch sub-views (e.g. Monitor Stats ↔ Resources) |
| **Y** | Context action per app (rescan in Saves, view hex in Cheats, dump ledger in Monitor) |
| **X** | Per-frame perf logging toggle (Monitor) |
| Drag titlebar → corner | Pin / snap the window |
| Drag titlebar → dock | Minimize |

## Build from source

**You need:** macOS or Linux, **devkitPro** with **devkitA64** at `/opt/devkitpro`, the Switch packages `switch-sdl2 switch-freetype switch-glad switch-libdrm_nouveau switch-sdl2_gfx switch-sdl2_image switch-sdl2_ttf switch-sdl2_mixer build_romfs`, Python 3 + Pillow (for the icon generator), and the submodules (`git submodule update --init --recursive` inside `src/`).

```bash
cd src
export DEVKITPRO=/opt/devkitpro

make package      # full bundle → src/SdOut/ (zips up as the release archive)
make umenu        # just the menu binary → src/SdOut/ulaunch/bin/uMenu/{main,romfs.bin}
```

Regenerate the per-theme icon packs after editing the generator:

```bash
python3 scripts/generate-qos-theme-icons.py
cd src && make umenu     # repacks romfs.bin with the new PNGs
```

Architecture and design notes live under [`docs/`](docs/).

## Architecture

```
Hekate (bootloader, CTCaer)
  └─ chainloads → Atmosphère (CFW)
        └─ launches uSystem in SystemApplet slot 0100000000001000
              ├─ replaces stock qlaunch
              ├─ hosts uMenu (this fork)  ← the desktop you see
              ├─ sends SMI messages
              └─ dispatches HomebrewLibraryApplets
```

Q OS uMenu is the GUI layer inside Atmosphère's SystemApplet slot. It composes the existing stack rather than replacing it:

- **Hekate** — bootloader chainload.
- **Atmosphère** — CFW orchestration + service patches.
- **libnx** — the syscall + service ABI.
- **libstratosphere** — sysmodule scaffolding, IPC, result codes.
- **Plutonium** — the UI primitives.
- **Q OS uMenu** — layout, windowing, hot corners, desktop, themes, icon packs.

### The composition principle

Q OS composes existing community projects into a new product. It **does not reinvent** primitives that Atmosphère / libnx / Plutonium / community NROs already provide.

Reboot-to-Hekate is the canonical example. Reimplementing the `bpc:ams` payload-set + reboot chain inside uMenu doesn't work reliably across HOS versions, because the chainload depends on dispatch-table behaviour Atmosphère adjusts release to release. The right approach is to **launch Tomvita's `reboot_to_hekate.nro`** via `smi::LaunchHomebrewLibraryApplet` — three lines of glue that replaced ~200 lines of failed reinvention and survive firmware changes. Every "let me just write that myself" instinct gets checked against "is there already a community NRO for this?" first.

### Security

The Telnet DevConsole is a debugging convenience, not a hardened service. It is **off by default** and should only ever be enabled on a LAN you trust, then turned back off. Hardening work (PIN auth, path-prefix guards, bind-to-local-IP) is tracked in [ROADMAP.md](ROADMAP.md) and [CHANGELOG.md](CHANGELOG.md). Until you see it marked done there, treat the DevConsole as trusted-LAN-only.

## Credits

This project would not exist without these people. Read this part — they earned every line.

- **[XorTroll](https://github.com/XorTroll)** built **uLaunch**, the **Plutonium** UI framework, the libnx-ext extensions, the arc result-code generator, and the original uLoader hbloader replacement. Almost every system call here traces back to code XorTroll wrote — the architecture, IPC patterns, theme system, message queue, and menu state machine. This fork reskins and extends that foundation; it did not invent it.
- **Stary2001** — XorTroll's long-time uLaunch collaborator, with substantial contributions to the upstream codebase this forks.
- **The [Atmosphère-NX](https://github.com/Atmosphere-NX) team** — SciresM, TuxSH, hexkyz, fincs, and the whole crew — ship the CFW everything runs on, and libstratosphere for sysmodule entry points, message queues, and result codes.
- **The [switchbrew](https://github.com/switchbrew) team** — fincs, plutoo, yellows8, WinterMute, shchmue, and many more — maintain **libnx**, the C library behind every Switch service call here.
- **WinterMute** and the **[devkitPro](https://github.com/devkitPro)** maintainers ship devkitA64, the toolchain this compiles against.
- **Tomvita** maintains the community NRO collection — including **reboot_to_hekate.nro** — that makes the composition strategy possible.
- **The [Sphaira](https://github.com/ITotalJustice/sphaira) team** (TotalJustice and contributors) ship the homebrew app store and the ForTheUsers distribution infrastructure.
- **The [Hekate](https://github.com/CTCaer/hekate) team** (CTCaer and contributors) ship the bootloader chain the whole boot path depends on.
- **Dear ImGui** (ocornut), **stb** (Sean T. Barrett), **nlohmann/json**, and **kuba--/zip** live in the libs tree and drive the legacy designer tool, image/font primitives, theme JSON, and `.ultheme` packs.

If your name should be here and isn't, open an issue and it gets added. The full chain is in [CREDITS.md](CREDITS.md); the GPLv2 propagation is documented in [LICENSE-AUDIT.md](LICENSE-AUDIT.md).

## Contributing

Open issues. Open pull requests. They get answered. The codebase is GPLv2 and any contribution ships under the same terms. If you want to fork this and ship your own thing — that's what GPLv2 is for. Keep the credit chain intact and open-source your changes.

## License

**GPLv2.** Plutonium and Atmosphère-libs are GPLv2 and propagate through static linking, so the whole project is GPLv2 — the full chain of why is in [LICENSE-AUDIT.md](LICENSE-AUDIT.md). The art assets (the Q OS originals in `romfs/default/ui/` and the per-theme icon packs in the `.ultheme` bundles) are released under GPLv2 too, to keep the bundle's license consistent.

## Links

- **This repo (public):** <https://github.com/Jmesmykil/QNX>
- **Q OS umbrella project:** <https://github.com/Jmesmykil/QOS>
- **Upstream uLaunch** (read this to understand the architecture): <https://github.com/XorTroll/uLaunch>

<div align="center">

Built with respect for everyone whose code this stands on.

</div>
