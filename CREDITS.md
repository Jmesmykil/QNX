# Credits

This file is the long form thank you list. The short form lives in the README. If your name should be here and is missing, open an issue and we will fix it.

## The people whose code we are standing on

### XorTroll
The original author of uLaunch and the entire family of code we forked from. This includes:

* **uLaunch** — the Atmosphere sysmodule that replaces qlaunch (Switch home menu) with a custom launcher. We forked it. This whole project is downstream of uLaunch.
* **Plutonium** — the C++ UI framework that draws every pixel of the menu. SDL2 wrapper plus a layout system plus an element tree. We did not write any of it.
* **libnx-ext** — extensions on top of libnx that add functionality the upstream does not expose (NS application control data wrappers and others)
* **arc** — Python tooling for generating C++ result code definitions from a manifest
* **uLoader** — the custom homebrew loader that replaces nx-hbloader. Lets uLaunch chainload NRO files as either applet or application context.
* **uManager** — the installer NRO that creator uses to install and uninstall the sysmodule from inside Switch homebrew menu
* **uScreen** — the Java desktop companion that displays Switch screen over USB. Q OS is replacing this with a native Swift QOS Mirror.app eventually.
* **uDesigner** — the WebAssembly theme editor. Q OS is replacing this with a native Swift QOS Theme Designer.app eventually.

XorTroll's GitHub: https://github.com/XorTroll
uLaunch repo: https://github.com/XorTroll/uLaunch
Plutonium repo: https://github.com/XorTroll/Plutonium

### Stary2001
Co-maintainer and longtime contributor on uLaunch. The upstream credit string in `src/projects/uMenu/source/main.cpp` reads "uLaunch by XorTroll and Stary2001". This is a real credit, not a courtesy mention. Stary2001's contributions are all over the codebase we forked.

GitHub: https://github.com/Stary2001

### The Atmosphere-NX team
SciresM, TuxSH, hexkyz, fincs, lioncash, misson20000, and every other contributor. They built the custom firmware that everything else depends on.

* **Atmosphere** itself: https://github.com/Atmosphere-NX/Atmosphere
* **Atmosphere-libs** (libstratosphere): the C++ template library for sysmodules that we use for the Q OS sysmodule entry point, the message queue, the result code system. Statically linked. GPLv2.

### The switchbrew team
fincs, plutoo, yellows8, WinterMute, shchmue, derrek, hexkyz, naehrwert, motezazer, and many more contributors over many years. They reverse engineered the Switch hardware and operating system enough that everything we do is possible.

* **libnx**: https://github.com/switchbrew/libnx
* **switch-tools**: the tooling we use to package NSPs and NSOs

### WinterMute and devkitPro
The toolchain. devkitA64 is the GCC fork plus Switch headers plus Makefile templates that turn our C++ into Switch executable code. There is no realistic alternative; almost every Switch homebrew project on Earth uses devkitPro.

GitHub: https://github.com/devkitPro

### The Sphaira team (ITotalJustice and contributors)
The homebrew app store we plan to publish through. They built the infrastructure that makes homebrew distribution actually work.

GitHub: https://github.com/ITotalJustice/sphaira
ForTheUsers CDN: https://fortheusers.org/

### The Hekate team (CTCaer and contributors)
The Switch bootloader chain. Hekate launches Atmosphere CFW which launches our sysmodule. Without Hekate the whole homebrew CFW boot story does not exist.

GitHub: https://github.com/CTCaer/hekate

## Library authors

### Dear ImGui (ocornut)
The immediate mode GUI library. Bundled in `libs/imgui/`. Used by the upstream uDesigner WebAssembly tool we inherited (kept as a future rebuild reference, not in our shipping build).

GitHub: https://github.com/ocornut/imgui

### nlohmann/json (Niels Lohmann)
The single header JSON library that drives the theme manifest system. Every `Manifest.json` and every `UI.json` file gets parsed by this library.

GitHub: https://github.com/nlohmann/json

### stb (Sean T. Barrett)
The single file image and font libraries. We use these for image decoding and texture loading inside the menu.

GitHub: https://github.com/nothings/stb

### kuba--/zip
The C zip library used for theme pack extraction (.ultheme files). Used by uDesigner originally.

GitHub: https://github.com/kuba--/zip

## Build dependencies (devkitPro pacman packages)

These ship with devkitPro and we link against them. None of them are forked into our tree.

* `switch-sdl2` — SDL2 port for Switch
* `switch-sdl2_gfx`, `switch-sdl2_image`, `switch-sdl2_ttf`, `switch-sdl2_mixer` — SDL2 helper libraries
* `switch-freetype` — font rasterization
* `switch-glad` — OpenGL loader
* `switch-libdrm_nouveau` — DRM driver for Tegra X1 GPU
* `build_romfs` — devkitPro tool for building the read only filesystem image embedded in the NSO

## Q OS contributors

Currently this fork is maintained by **Jamesmykil** (creator of Q OS). Implementation help comes from a coordinated set of agents working in parallel on art, code, build, and design. The agents are tools, not authors. Every line of decision making in this fork traces back to Jamesmykil.

If you contribute via pull request you go on this list with your GitHub handle and what you contributed.

## Asset attribution

### Q OS originals (29 PNGs as of v1.2.3)
All visible art in this release is original Q OS work, not upstream. See [docs/QOS-REBRAND-ASSET-INVENTORY.md](./docs/QOS-REBRAND-ASSET-INVENTORY.md) for the full list with dimensions, byte counts, and SHA8 hashes.

* **P1** Hero assets (5): Background.png, EntryMenuBackground.png, InputBarBackground.png, OverIcon/Cursor.png, OverIcon/Selected.png
* **P2** SpecialEntry icons (8): Settings, Album, Themes, Controllers, MiiEdit, WebBrowser, Amiibo, Empty
* **P3** Defaults and chrome (9): DefaultApplication, DefaultHomebrew, Folder, four TopMenuBackground variants, EntryMenuLeftIcon, EntryMenuRightIcon
* **P4** Status overlays (7): Border, Suspended, Corrupted, Gamecard, NeedsUpdate, NotLaunchable, HomebrewTakeoverApplication

Generated with ImageMagick 7.1.2-17 in the Q OS brand palette (#00E5FF cyan, #0E1A33 navy, #D946EF magenta, #A78BFA lavender). All released under GPLv2 to match the project license.

### Upstream art preserved for the historical record
The original upstream uLaunch PNGs that Q OS replaced are archived at:
* `archive/upstream-art-p2/` — 8 SpecialEntry PNGs as XorTroll authored them
* `archive/upstream-art-p3/` — 9 defaults and chrome PNGs
* `archive/upstream-art-p4/` — 7 status overlay PNGs

Naming convention: `<original_name>.png.upstream-<sha8>` where sha8 is the first 8 characters of the SHA256 of the upstream PNG. This preserves the upstream pixels without shipping them in the active romfs.

### System icon assets retained
Battery state icons (10 through 100, plus Charging) and Connection strength icons (0 through 3, plus None) are functional Switch system iconography. We currently retain the upstream art for these because they are pure functional symbology. They may be rebuilt as Q OS originals in a future release; a "retain or rebuild" decision is on the inventory checklist.

## License chain

This project is GPLv2 because its dependencies are GPLv2. The propagation chain:

* **Plutonium** is GPLv2 → statically linked into uMenu → propagates
* **Atmosphere-libs** is GPLv2 → statically linked into uSystem → propagates
* **uLaunch upstream** is GPLv2 → forked → propagates

Result: Q OS uMenu fork is GPLv2 in its entirety. Source is published. Modifications are documented. Distribution requires offering source. We comply with all GPLv2 obligations.

Full audit: [LICENSE-AUDIT.md](./LICENSE-AUDIT.md).

## Final word

If you are reading this and you feel like you should be on this list and are not, that is a mistake. Open an issue with your name, your GitHub handle, and what you contributed (to the upstream we forked, or to a library we use, or directly to this fork). We will add you. Credit is the whole point.

This fork exists because the people listed above did the hard work first. Saying thank you in a CREDITS file is the minimum we can do. We mean it.
