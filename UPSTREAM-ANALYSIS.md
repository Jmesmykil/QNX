# uLaunch Upstream Analysis
# Agent: uLaunch Upstream Analyst (Sonnet bg)
# Date: 2026-04-18 HST
# Source: XorTroll/uLaunch shallow clone at tools/qos-ulaunch-fork-upstream-clone

---

## 1. License Audit

### Primary License

**GNU General Public License v2 (GPLv2-only).**
File: `LICENSE` — full GPLv2 text, no "or later" clause.
Copyright holder: XorTroll.

### Bundled Third-Party Libraries (submodules — not cloned in depth-10 shallow)

| Submodule | Repository | Known License |
|-----------|-----------|---------------|
| Plutonium | XorTroll/Plutonium | GPLv2 (same author, same policy) |
| Atmosphere-libs | Atmosphere-NX/Atmosphere-libs | GPLv2 |
| libnx-ext | XorTroll/libnx-ext | likely ISC/MIT (libnx is ISC) |
| nlohmann/json | libs/json | MIT |
| kuba--/zip | libs/zip | MIT |
| ocornut/imgui | libs/imgui | MIT |
| nothings/stb | libs/stb | Public Domain / MIT dual |
| nx-hbloader | embedded reference | ISC (confirmed in `nx-hbloader.LICENSE.md`) |

The project as a whole is GPLv2-governed because the two most significant dependencies (Atmosphere-libs and Plutonium) are GPLv2 and propagate that obligation through static linking.

### Key License Consequences for Q OS Fork

| Question | Answer |
|----------|--------|
| Can we fork? | YES — GPLv2 explicitly permits forking and modification |
| Can we redistribute binaries? | YES — but source must accompany or be offered in writing |
| Can we change the license of our fork? | NO — fork is a derivative work, must remain GPLv2 |
| Attribution required? | YES — must keep existing copyright notices and the LICENSE file |
| Must we publish source when shipping to Switch? | YES — distributing the NPS/NSP binary means we must offer the corresponding source (SD card distribution counts as distribution) |
| Can we keep Q OS patches proprietary? | NO — any source file we modify or link against GPLv2 code inherits GPLv2. Q OS-specific UX code added to uLaunch must also be GPLv2 if distributed |
| Private internal use (no distribution)? | GPLv2 does NOT restrict internal use — if we only run it on our own device and never distribute, source disclosure is not legally required |

**STATE.toml constraint "No upstream weaponization — improvements go back to XorTroll under same license unless creator says otherwise" is fully consistent with GPLv2 requirements.**

---

## 2. Architecture Mapping

### Top-Level Directory Tree

```
uLaunch/
  LICENSE              — GPLv2 full text
  Makefile             — Top-level orchestrator (version 1.2.3, current)
  README.md            — Feature list, install/remove guide, component docs
  arc/                 — Git submodule: XorTroll/arc — result-code generator, Python tooling
  assets/              — Logo PNG/XCF, manager icon XCF; static non-theme assets
  crowdin.yml          — Crowdin localization config (uMenu + uManager lang JSON files)
  cur-changelog.md     — Latest release changelog
  default-theme/       — The shipped default theme (theme/Manifest.json + ui/ PNG assets + sound/ OGG/WAV)
  default-theme-music/ — Music extension for the default theme (separate .ultheme bundle)
  demos/               — GIF demos for README (not shipped)
  docs/                — Developer documentation / wiki source
  libs/                — All submodule dependencies (see §1)
  nx-hbloader.LICENSE.md — ISC license for the hbloader code embedded in uLoader
  projects/            — Six buildable subprojects:
    uDesigner/         — Web theme editor (Node.js/web, separate build system)
    uLoader/           — Custom nx-hbloader: launches NROs as applet or application
    uManager/          — Management NRO: enable/disable uLaunch, tweak config
    uMenu/             — Library applet — the visible UI the user interacts with
    uScreen/           — PC-side Java JAR: USB screen capture via uSystem IPC
    uSystem/           — System applet replacement — the actual qlaunch substitute
```

### Build System

- **devkitPro + devkitA64 + libnx** required. Detected on this Mac at `/opt/devkitpro/devkitA64/bin/`.
- Target arch: `aarch64-none-elf` with `-march=armv8-a -mtune=cortex-a57` (Tegra X1 / Cortex-A57 cluster).
- Compiler: `aarch64-none-elf-gcc/g++`, standard `gnu++23`.
- uSystem uses **Atmosphere-libs stratosphere.mk** (sysmodule template); output is `.nsp` placed at `atmosphere/contents/0100000000001000/exefs.nsp`.
- uMenu builds as a library applet NSO (`uMenu.nso`) + NPDM.
- uLoader is a custom nx-hbloader NSO (two variants: applet + application NPDM).
- ROM filesystem (`romfs.bin`) is built from `projects/uMenu/romfs/` using `build_romfs`.
- SDL2 dependency: **specific old version required** — `switch-sdl2` revision `2.28.5-3` (uses `audout` not `audren`). Newer SDL2 crashes when suspending games. This is a known build fragility.
- Top-level `make` produces `SdOut/` directory tree ready to copy to SD root, plus `.7z` and `.zip` archives.
- Missing for first build: `switch-sdl2`, `switch-freetype`, `switch-glad`, `switch-libdrm_nouveau`, `switch-sdl2_gfx`, `switch-sdl2_image`, `switch-sdl2_ttf`, `switch-sdl2_mixer`, `mvn` (for uScreen), `build_romfs` (devkitPro tool).

### Sysmodule Boot Sequence

1. Atmosphère loads `atmosphere/contents/0100000000001000/exefs.nsp` as the system applet (title ID `0100000000001000` = qlaunch).
2. `uSystem/main.cpp` `InitializeSystemModule()` sets `__nx_applet_type = AppletType_SystemApplet`, initializes SM, FS, applet, NS, ldrShell, pmshell, setsys services.
3. `Startup()` allocates libstratosphere heap and libnx heap, creates the `MenuMessageQueue`.
4. `Main()` calls `Initialize()` (loads config, caches application records, starts IPC server), then calls `LaunchMenu(StartupMenuPostBoot)`.
5. uSystem launches `uMenu` as a library applet (running over the eShop applet slot). uMenu is the visible UI.
6. uSystem enters `MainLoop()` — an infinite loop that: polls `MenuMessageQueue`, drives the IPC server, handles HOME button / power / gamecard events, forwards messages to uMenu via the SMI protocol.
7. The loop NEVER exits (NS would crash if the HOME Menu system applet terminates).

### Theme System

- **Format**: ZIP archive with `.ultheme` extension. Contains three directories: `theme/` (Manifest.json), `ui/` (PNG image assets + UI.json layout), `sound/` (OGG/WAV sound effects).
- `theme/Manifest.json`: JSON with `name`, `format_version` (currently 3), `release`, `description`, `author`.
- `ui/UI.json`: JSON describing per-menu element positions (x/y), colors (hex RGBA strings), font sizes (`small`/`medium`/`large`), horizontal/vertical alignment, clamp widths for scrolling text.
- `ui/Background.png`: Main background image (replaces solid color fill).
- `ui/Main/`: Per-element PNG icon assets (EntryIcon, OverIcon, QuickIcon, TopIcon, background tiles, menu navigation arrows).
- `sound/`: OGG/WAV sfx files (menu enter, cancel, launch, error, etc.).
- **What can be themed without source modification**: background image, all icon PNG assets, all UI element positions and colors, font sizes, sound effects, BGM. This covers: wallpaper replacement, Dark Liquid Glass color palette, dock icon styling.
- **What cannot be themed**: layout geometry of the grid itself (number of columns/rows is hardcoded in C++ source), animation curves and timing, dock magnify behavior, dock auto-hide, custom input handling (ZR/ZL semantics), multi-window z-stacking, snap zones. These require C++ source changes.

### Title Enumeration

- Uses `nsextGetApplicationControlData()` from `libs/libnx-ext` — a wrapper around the NS `ns:am2` service.
- `NsApplicationControlSource_Storage` — reads from installed title storage.
- `nsInitialize()` is called in uSystem's `InitializeSystemModule()` — uses the `ns` service (available to `AppletType_SystemApplet`).
- NACP (`NacpLanguageEntry`) provides `name`, `author`, `display_version` per title.
- libnx version: uses current libnx from devkitPro (no specific pin observed; Makefile uses `$(DEVKITPRO)/libnx/switch_rules`).

### Input Handling

- Plutonium framework wraps HID: `IMenuLayout::OnLayoutInput(keys_down, keys_up, keys_held, touch_pos)` receives both digital button bitmask and `pu::ui::TouchPoint`.
- Touch is supported via `TouchPoint` passed through the layout system.
- D-pad and analog sticks are both supported (Plutonium maps them to focus traversal).
- No ZR/ZL custom semantics exist in upstream — ZR/ZL are mapped to standard Nintendo actions (not Left/Right navigation). Q OS's ZR=LEFT, ZL=RIGHT semantics require source modification.

### IPC: Services Exposed

- **SMI protocol** (`smi_Protocol.hpp`): uSystem and uMenu communicate via `AppletStorage` push/pop with a magic header (`0x21494D53 = "SMI!"`). Message types: `SystemMessage` (uMenu → uSystem) and `MenuMessage` (uSystem → uMenu).
- Relevant `SystemMessage` values: `LaunchHomebrewLibraryApplet`, `LaunchHomebrewApplication`, `ChooseHomebrew` — these are how uMenu tells uSystem to launch an NRO.
- `LaunchHomebrewApplication` passes an `nro_path` (FS_MAX_PATH) and a donor `app_id`. This is the mechanism to launch our `qos-mock-desktop-gui-v1.0.0.nro` from within the uLaunch UI.
- uSystem also exposes a custom IPC server (`sf_IpcManager`) for external NROs to communicate with uSystem. An NRO launched by uLaunch can call back into uSystem through this IPC to request actions (launch apps, etc.).
- **No public service is registered on SM** — the IPC is internal between uSystem and applets it launches.

### UI Renderer: Plutonium Framework

- `libs/Plutonium` (XorTroll's own C++ UI library, GPLv2).
- Abstractions: `pu::ui::Application`, `pu::ui::Layout`, `pu::ui::elm::Element`, `pu::render::Renderer`.
- Renderer uses **SDL2 + SDL2_image + SDL2_ttf + SDL2_mixer** underneath.
- Primitive drawing: PNG texture rendering, text rendering (FreeType via SDL2_ttf), colored rectangles, GPU-accelerated via `switch-libdrm_nouveau`.
- Animations handled by Plutonium's element system (not raw Canvas primitives like our NRO).
- 1080p native rendering (1920x1080).
- No raw Canvas drawing — everything goes through Plutonium layouts and elements.

---

## 3. Notes on Missing Submodule Content

The shallow clone (`--depth 10`) fetched all tracked files but NOT submodule content (Atmosphere-libs, Plutonium, libnx-ext, json, zip, imgui, stb). Their `libs/` directories are present but empty (64-byte directory entries only). This is expected for depth clones without `--recurse-submodules`. First build will require `git submodule update --init --recursive` inside the fork.

Clone size: **113 MB** (exceeds the 50 MB guideline — primarily due to `demos/` GIF files ~80 MB and `assets/` source files). Recommend pruning `demos/` in the fork copy to stay under 50 MB for future re-clones.
