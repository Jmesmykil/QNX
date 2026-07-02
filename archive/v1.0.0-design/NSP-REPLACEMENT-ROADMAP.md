# NSP-REPLACEMENT-ROADMAP.md

**Target path:** `/Users/nsa/Astral/QOS/tools/qos-ulaunch-fork/archive/v1.0.0-design/NSP-REPLACEMENT-ROADMAP.md`
**Author:** architect (Opus)
**Date:** 2026-04-18 HST
**Status:** DESIGN — research-only, no code changes authorized
**Supersedes:** none (first issue)
**Related SSOT:**
- `/Users/nsa/Astral/QOS/tools/qos-ulaunch-fork/ROADMAP.md` (Phase 1.5 version chain v0.1.0 → v1.0.0)
- `/Users/nsa/Astral/QOS/tools/qos-ulaunch-fork/UPSTREAM-ANALYSIS.md` (upstream arch, licenses, SMI protocol)
- `/Users/nsa/Astral/QOS/tools/qos-ulaunch-fork/INTEGRATION-SPEC.md` (port plan v0.1.0 → v1.0.0)
- `/Users/nsa/Astral/QOS/tools/mock-nro-desktop-gui/` (UX source for ported primitives)

> **Note on roadmap drift:** The existing `ROADMAP.md` and `INTEGRATION-SPEC.md` end the version chain at **v1.0.0 = themed + ported upstream uLaunch** (uSystem/uMenu/uLoader all still XorTroll code underneath). This document extends that chain with a **Ship-of-Theseus** post-v1.0.0 substitution plan (v0.3.x → v0.7.x → v1.0.0-native) where every upstream module is incrementally retired until no XorTroll C++ remains and the final artifact is a single Q OS-native `qos-shell.nsp` at TID `0100000000001000`. The numbering below intentionally overlaps with the existing roadmap — read this doc as the **native-shell track** that runs in parallel with the themed-fork track and swallows it by v1.0.0.

---

## H1 — Executive Summary

Q OS will ship a native TID `0100000000001000` (qlaunch) sysmodule bundle named `qos-shell.nsp` that boots on every Switch OG (Tegra X1, fw 20.0.0, Atmosphère 1.11.1) with zero uLaunch-upstream residue by v1.0.0. We get there by **plank-by-plank substitution** (Ship of Theseus): each rung retires one upstream C++ module and replaces it with Q OS code, while the binary keeps booting and keeps the library-applet chainload contract intact. Non-Switch Q OS work is not impacted — every Switch-specific path is `#[cfg(feature = "switch-shell")]` gated, and the repo-wide default-build SHA `5e16d8b6…` does not move from this lane.

**Final target (v1.0.0):**
```
atmosphere/contents/0100000000001000/
  exefs.nsp          (Atmosphère LayeredFS override — no keys needed)
    main             Q OS binary (C++ Plutonium UI shell + Rust core via cdylib FFI)
    main.npdm        Q OS NPDM granting qlaunch-class service handles
    romfs.bin        Q OS-only assets (no uLaunch residue, no XorTroll fonts/sounds)
```

**Hard constraints the plan respects:**
- TID = `0100000000001000` (qlaunch). **Not** TID `010000000000100D` (Album) — Album is an exploit entry-point used by hbmenu; qlaunch is the live home-menu slot and what we actually replace.
- NSP is unsigned. Atmosphère LayeredFS picks up `exefs.nsp` at path `atmosphere/contents/<TID>/exefs.nsp` without requiring Nintendo prod keys.
- NS (Nintendo Services) crashes if the system-applet process exits. The shell MUST enter a non-exiting main loop, same as upstream uSystem.
- fw 20.0.0 + Atmosphère 1.11.1 is the pinned test surface. Any libnx API drift must be versioned at the Rust FFI boundary.
- Safe-return to Nyx must stay 100% — the bundle is removable by deleting one directory, which restores stock qlaunch.

---

## H1 — Ship-of-Theseus Substitution Plan

### H2 — v0.3.x (Bridge to the native-shell track)

**Starting state:** v0.2.2 = stock uLaunch binary + Q OS cosmic-purple romfs (theme swap). v0.3.0 = Q OS C++ injected into uLaunch source tree (DockElement, EntryMenu grid wrap, chainload via smi, FocusSurface D-pad state machine). The native-shell track forks off here.

| Rung | What gets retired | What replaces it | Bootable? | Chainload intact? |
|---|---|---|---|---|
| v0.3.1 | uLaunch `default-theme/` romfs (Japanese sfx, upstream fonts) | Q OS `romfs/` with Dark Liquid Glass theme, Inter font, generated sfx | yes — romfs-only swap | yes (no code path touched) |
| v0.3.2 | uLaunch `ui_ThemesMenuLayout.cpp` (theme picker for .ultheme bundles) | Removed entirely (Q OS ships one theme, hardcoded) | yes | yes |
| v0.3.3 | `ui_LockscreenMenuLayout.cpp` (not in Q OS UX spec) | Removed, main menu is first screen | yes | yes |
| v0.3.4 | uManager standalone NRO | Stubbed — settings pulled into shell (Vault → Settings panel) | yes | yes |

**Invariant after v0.3.x:** uSystem + uMenu binary still produced by upstream Makefile tree. UI rewritten, service use unchanged.

### H2 — v0.4.x (Retire the Plutonium theme layer)

| Rung | What gets retired | What replaces it |
|---|---|---|
| v0.4.0 | upstream `ui_BackgroundScreenCapture.cpp` + static Background.png rendering | Q OS procedural wallpaper — Cold Plasma Cascade (6 plasma blooms, 80 stars, 18 data-streams, seed `1364153183`) ported from `mock-nro-desktop-gui/src/wallpaper.rs` |
| v0.4.1 | upstream `ui_InputBar.cpp` + hint bar glyph set | Q OS input bar (Finder-style glyph strip, ZR=LEFT, ZL=RIGHT semantics) ported from `mock-nro-desktop-gui/src/canvas.rs` input-bar draw |
| v0.4.2 | upstream OSK invocation (libapplet SwkbdApplet) | Q OS inline OSK ported from `mock-nro-desktop-gui/src/osk.rs` (full-width 1280px, draggable, title-bar handle — see main.rs v1.1.8 notes) |
| v0.4.3 | upstream `ui_SettingsMenuLayout.cpp` | Q OS Settings panel in Rust (firmware version via `set:sys` IPC, display/time/network stubs wired to `services/nifm.rs`, `services/time.rs` patterns from mock-nro-desktop) |
| v0.4.4 | upstream `ui_QuickMenu.cpp` (HOME button quick-menu) | Q OS quick-menu (compact overlay: sleep/restart/power/brightness — Rust UI, libnx IPC for psc:m/settings) |

**Invariant after v0.4.x:** Theme-layer, OSK, wallpaper, settings are 100% Q OS. Main menu still uMenu. SMI protocol still upstream.

### H2 — v0.5.x (Retire uMenu — the visible UI)

This is the big cut. uMenu is the library-applet NSO that runs over the eShop applet slot and draws everything. Replacing it means porting `mock-nro-desktop-gui` into the Plutonium library-applet container.

| Rung | What gets retired | What replaces it |
|---|---|---|
| v0.5.0 | `ui_StartupMenuLayout.cpp` (boot intro animation) | Q OS boot curve (one-shot animation, telemetry `EVENT CURVE kind=boot`) |
| v0.5.1 | `ui_MainMenuLayout.cpp` icon grid | Q OS `desktop_icons.rs` (11-col grid, 6-row cap, Finder D-pad surface) |
| v0.5.2 | `ui_EntryMenu.cpp` entry list | Q OS `launchpad.rs` (analog dock magnify 1.4×/1.2×/1.05×, 5s/12-frame auto-hide, BUILTIN_ICONS source-of-truth) |
| v0.5.3 | upstream title/homebrew enumeration UI (part of MainMenu) | Q OS Dispatch palette (`apps/dispatch.rs`), fuzzy search, keyboard-first |
| v0.5.4 | upstream "SD browse" entry | Q OS Vault (`apps/vault/*.rs`), column view, sidebar, preview, state persistence |
| v0.5.5 | upstream cursor/touch handling | Q OS cursor curve (`curve.rs`), multitouch (`multitouch.rs`), focus surface state machine (`input.rs`) |

**Key architectural move in v0.5.x:** uMenu binary is now **a thin Plutonium wrapper whose `OnLoad()` hands control to a Rust `cdylib`**. The Plutonium `pu::ui::Application` still exists (for SDL2 renderer + libnx HID init), but every `Layout` subclass is replaced by a single Q OS `QosShellLayout` that calls into Rust for all draw/input/state. See [H2 — C++ vs Rust Split Strategy].

**Invariant after v0.5.x:** No upstream UI code remains in the NSP. Plutonium persists as a render-primitive pipe to SDL2/libnx. SMI protocol + uSystem still upstream.

### H2 — v0.6.x (Retire the SMI protocol and uSystem loop)

uSystem is the TID 100 sysmodule that owns the NS session, runs `MainLoop()`, and brokers library-applet chainload. It's the hardest piece to replace because NS is intolerant of any exit-code from this process. The substitution is **in-place protocol rewrite**, not a binary swap.

| Rung | What gets retired | What replaces it |
|---|---|---|
| v0.6.0 | `smi_SystemProtocol.cpp` (magic 0x21494D53 = "SMI!") | Q OS QSMI protocol (magic `0x49534F51` = "QOSI"), same AppletStorage push/pop shape but Rust-defined message types |
| v0.6.1 | `sys_SystemApplet.cpp` main-loop body | Q OS main-loop in Rust — polls message queue, drives NS events, handles HOME button, never exits. Entry point is still a C++ `Main()` that calls `qos_shell_run()` from Rust cdylib. |
| v0.6.2 | `ecs_ExternalContent.cpp` (external NRO chainload helper) | Q OS chainload helper (Rust) that still calls `ldrShell` via libnx — the service use is unchanged, just the orchestration logic moves to Rust |
| v0.6.3 | `la_LibraryApplet.cpp` (library-applet launcher wrapper) | Q OS library-applet launcher. **Library-applet chainload contract preserved exactly** — `applet_AE_UILibraryApplet` donor slot, AppletStorage argument passing, result-code handling all identical. Just Rust-driven. |
| v0.6.4 | `app_ControlCache.cpp` (NACP title-record cache) | Q OS title cache in Rust, same `nsextGetApplicationControlData` call, same persistence to SD |
| v0.6.5 | `sf_IpcManager.cpp` (custom IPC endpoint for external NROs to talk back to uSystem) | Q OS IPC endpoint in Rust (same service name to keep existing NROs working, or renamed if we declare API break) |

**Invariant after v0.6.x:** Upstream C++ under `ul::system::*` is gone. What remains of C++ is only (a) the libnx/libstratosphere glue that must stay C++ (see below), and (b) Plutonium render pipe. Everything else is Rust.

### H2 — v0.7.x (Retire Plutonium)

Plutonium is XorTroll's C++/SDL2/libnx UI framework. Q OS's native shell doesn't need layout objects — the mock-nro-desktop-gui already has its own canvas-draw model that writes directly to an SDL2 surface.

| Rung | What gets retired | What replaces it |
|---|---|---|
| v0.7.0 | `pu::ui::Application`, `pu::ui::Layout`, `pu::ui::elm::Element` tree | Q OS `wm.rs` window manager + `canvas.rs` immediate-mode renderer, SDL2 accessed directly via `rust-sdl2` crate with Switch linker patch |
| v0.7.1 | `pu::render::Renderer` (SDL2_gfx/SDL2_image/SDL2_ttf wrapper) | Q OS renderer (Rust) calling SDL2 FFI directly — SDL2_image replaced with pure-Rust `image` crate, SDL2_ttf replaced with `rusttype` + `ab_glyph` |
| v0.7.2 | `libs/imgui` (not actually used by uMenu but linked in upstream) | Removed from link line |
| v0.7.3 | `libs/stb`, `libs/zip` | Removed or replaced with `flate2` + Rust equivalents |
| v0.7.4 | `libs/json` (nlohmann) | `serde_json` in Rust |

**Invariant after v0.7.x:** C++ surface is minimum possible — only what libnx/libstratosphere/Atmosphère-libs demand (see [H2 — C++ vs Rust Split Strategy]).

### H2 — v1.0.0 (Native-shell release gate)

- All substitutions v0.3.1 → v0.7.4 complete and hw-green on Switch OG (Erista).
- `archive/v1.0.0-design/NSP-REPLACEMENT-ROADMAP.md` (this doc) promoted to `archive/v1.0.0/SHIPPED.md` with final result matrix.
- No file under `qos-shell/` references XorTroll headers. `git grep -E '(XorTroll|uLaunch|uMenu|uSystem)' qos-shell/src/` returns zero hits.
- Boot log `sdmc:/switch/qos-shell-v1.0.0.log` shows 0 errors across a 60s stress mode (ZL+ZR 3s hold).
- Safe-return path verified: `rm -rf atmosphere/contents/0100000000001000/` → reboot → stock qlaunch boots.
- Library-applet chainload verified: hbmenu launched from shell, Album launched from shell, arbitrary NRO launched via Dispatch all return cleanly to Q OS shell.
- `mock-nro-desktop-gui-v1.0.0.nro` launchable from Dispatch, exits return to Q OS shell, no NS crash.

---

## H1 — C++ vs Rust Split Strategy

### H2 — What MUST stay C++

These pieces cannot be cleanly written in Rust without unacceptable friction against libnx, libstratosphere, and Atmosphère-libs. All are ABI-pinned to fw 20.0.0 / AMS 1.11.1:

| Layer | Reason C++-mandatory |
|---|---|
| **`main.cpp` entry stub** | libnx's `__nx_applet_type`, `__libnx_alloc/free`, `__nx_fs_num_sessions`, `__nx_heap_base` are `extern "C"` globals whose addresses the dynamic linker expects at fixed symbols. C++ (or C) entry wrapper is the path of least resistance. <30 lines total. |
| **`main.npdm` generation** | NPDM is a binary blob produced by `build_pnpm` from a JSON descriptor (see `uSystem.json`). Not code — build tooling. |
| **libnx service init glue** | `smInitialize`, `fsInitialize`, `nsInitialize`, `appletInitializeServiceSession`, `nfpInitialize` — all C ABI with macro-heavy headers. Wrap once in C++ `extern "C"` shim, call from Rust via bindgen. |
| **Atmosphère-libs/libstratosphere hooks** | `ams::os::*`, `ams::sf::*`, IPC object-framework (`sf::hipc::ServerManager`) — heavy C++ template metaprogramming, not bindgen-friendly. Keep a thin C++ wrapper per used service. |
| **SDL2 video-subsystem init on Switch** | `switch-libdrm_nouveau` init sequence is SDL2-internal and documented in C. Rust's `rust-sdl2` compiles, but the Switch backend path is C-only. Init in C++, hand surface pointer to Rust. |
| **libstratosphere memory pool allocator** | Required for the 4MB libstratosphere heap + 1MB libnx heap pattern. Replicating in Rust would mean reimplementing `ams::os::HeapHelper`. Not worth it. |

**Total C++ surface target at v1.0.0:** ~300 lines in 4 files:
- `qos-shell/src/main.cpp` (~120 lines — entry, heap, service init, calls `qos_shell_run()`)
- `qos-shell/src/sdl_init.cpp` (~60 lines — SDL2 + nouveau init, surface handoff)
- `qos-shell/src/service_shims.cpp` (~80 lines — `extern "C"` wrappers for libstratosphere object creation that Rust calls)
- `qos-shell/src/npdm_patch.cpp` (~40 lines — runtime NPDM version-assert for Atmosphère version-lock detection)

### H2 — What SHOULD become Rust

| Module | Rust crate / file (maps from mock-nro-desktop-gui) |
|---|---|
| Scene graph / window manager | `wm.rs` (already exists in mock) |
| App list / desktop icons | `desktop_icons.rs`, `icon_cache.rs`, `icon_category.rs` |
| Launchpad (dock) | `launchpad.rs` with analog magnify |
| Vault file browser | `apps/vault/{model,view,sidebar,preview,state_persistence,actions}.rs` |
| Dispatch palette | new `apps/dispatch.rs` (pattern from `mock-nro-desktop/src/apps/dispatch.rs`) |
| OSK | `osk.rs` |
| Context menu | `ctxmenu.rs` |
| Wallpaper (Cold Plasma Cascade) | `wallpaper.rs` |
| Input state machine | `input.rs` + `multitouch.rs` + `curve.rs` |
| Theme tokens | `theme.rs` |
| Animation curves | `anim.rs` |
| Canvas primitives | `canvas.rs` |
| NRO asset loader | `nro_asset.rs` |
| FS utilities | `fs_util.rs` |
| Telemetry emitter | new `telemetry.rs` (pattern from `mock-nro-desktop/src/telemetry.rs`) |
| Service wrappers | new `services/{ns,am,set,nifm,time,psc,psm,nfp,bsd}.rs` (patterns from `mock-nro-desktop/src/services/*.rs`) |
| QSMI protocol | new `qsmi.rs` (replaces SMI; same AppletStorage transport) |

**Build artifact:** Rust compiles to `libqos_shell.a` (staticlib) with `crate-type = ["staticlib"]`, linked into `main` at final link step. Target triple: `aarch64-nintendo-switch-freestanding` (same triple `switch-nro-harness` and the `mock-nro-*` tracks use).

### H2 — FFI Boundary Spec

**Direction 1: C++ → Rust (main entry handoff)**
```c
// in qos-shell/src/ffi.h  — authored by hand, stable across all Rust revs
extern "C" {
    // Called from main.cpp after libnx/SDL2/libstratosphere are up.
    // surface_ptr = SDL_Surface* cast to void*. width/height in px.
    // Never returns. On fatal error, calls abort() which triggers Atmosphère crash report.
    void qos_shell_run(void* surface_ptr, int width, int height);

    // Called from libstratosphere signal handler on HOME button.
    void qos_shell_on_home_button(void);

    // Called from applet event thread on focus gain / loss.
    void qos_shell_on_focus(int gained);
}
```

**Direction 2: Rust → C++ (service shim calls)**
```c
// in qos-shell/src/service_shims.h
extern "C" {
    // NS title enumeration — wraps nsextGetApplicationControlData loop.
    // Writes up to `max` records to `out`. Returns count.
    int qos_svc_ns_list_titles(struct QosTitleRecord* out, int max);

    // Library-applet chainload — wraps appletStartLibraryApplet.
    // applet_id is one of the libnx AppletId constants.
    // Blocks until applet exits; returns exit result.
    int qos_svc_la_launch(int applet_id, const void* arg_storage, int arg_len);

    // Chainload arbitrary NRO via ldrShell (same as uLaunch's LaunchHomebrewApplication).
    int qos_svc_chain_nro(const char* nro_path, unsigned long long donor_app_id);

    // Set-sys firmware version query.
    int qos_svc_get_firmware_version(char* buf, int buf_len);

    // ... one extern "C" per service we wrap
}
```

**Rules:**
1. No C++ types cross the FFI boundary. Only `extern "C"` primitives, POD structs, and opaque `void*`.
2. Strings cross as `const char*` + explicit length (no null-terminator assumption on Rust side).
3. All FFI calls are blocking. Rust owns its own thread model above this line.
4. Every `qos_svc_*` wrapper in C++ is `<40 lines` and does only: init (if not already), call libnx function, translate result code to Rust-friendly `int`, return.
5. Rust headers are generated by `cbindgen` from `qos-shell/Cargo.toml` `[package.metadata.cbindgen]`. Regenerated on every Rust build. Checked into repo so C++ side does not require Rust toolchain.
6. Panic safety: Rust `panic = "abort"` in release profile. No unwinding across FFI.

---

## H1 — NPDM Service-Handle Table

The NPDM is what signs our NSP as a qlaunch replacement — Atmosphère's Process Manager (PM) reads the NPDM and grants only the listed services. Refusing a service at PM-grant time makes the sysmodule fail to spawn. The table below is the **minimum superset** of services uSystem currently uses plus what Q OS Rust shell will need. Source: upstream `uSystem.json` service_access `["*"]` (wildcard, which AMS interprets as "match the ACID whitelist"). We narrow it because wildcard is a security anti-pattern and because AMS 1.11.1's ACID whitelist is versioned.

| Service | Purpose | Required by |
|---|---|---|
| `ns:am2` | Application records, NACP metadata, `nsextGetApplicationControlData` for title enumeration | Dispatch palette title list, desktop icons |
| `ns:vm` | Version management (firmware version reads) | Settings panel firmware row |
| `ns:ec` | E-commerce session (needed if we keep Nintendo eShop entry working) | If shell keeps eShop launch, else drop |
| `apm:sys` | Power management — CPU/GPU performance modes | Shell boot mode assert, dock/handheld transitions |
| `applet:SE` | System-applet class — this is THE service that makes us a qlaunch replacement. PM grants only one process this per boot. | Shell process itself |
| `appletAE` | Application/Applet Entry — `applet_AE_UILibraryApplet` chainload donor slot | Library-applet chainload (hbmenu, Album, OSK) |
| `hid:sys` | System-applet HID access (button remapping, controller pairing) | Input state machine, quick-menu controller pairing |
| `hid` | Standard HID (button/analog/touch polling) | All input |
| `set:sys` | System settings — language, firmware, region, device nickname | Settings panel reads |
| `set` | User settings | Settings panel writes |
| `set:cal` | Calibration data (only if we show serial number/region; prefer drop) | Optional |
| `bsd:s` / `bsd:u` | BSD sockets — needed for brain-server health check from Terminal-style panel | Terminal panel, future Dispatch net actions |
| `nsd:a` | NSD application services | Network panel |
| `nifm:s` | Network interface manager (system scope) | Network panel, wifi scan/connect |
| `nfp:user` | Amiibo read — if shell ever surfaces amiibo-launch tiles | Optional, deferred |
| `pm:shell` | Process manager shell — launch/terminate processes | NRO chainload (ldrShell path) |
| `pm:info` | Process info queries | Debug panel, future |
| `ldr:shel` | Loader shell — actually loads the NRO/NSO | NRO chainload |
| `acc:u0` | User accounts (admin mode) | User panel, Mii loading |
| `fsp-srv` | Filesystem service (FS sessions) | Vault, icon cache, theme loads, log writes |
| `psc:m` | Power state change notifier | Sleep/wake, auto-hide timer pause on sleep |
| `psm` | Power state manager | Battery percent, charging state |
| `pcv` | Voltage/clock control (only if we expose perf sliders) | Deferred — do not grant unless feature lands |
| `ssl` | TLS for brain-server HTTPS health | Terminal panel over TLS |
| `time:s` / `time:u` | Time services — clock, RTC | Clock widget |
| `btm:sys` | Bluetooth manager system — controller pairing | Settings → Controllers |
| `btdrv` | Bluetooth driver — low-level BT ops | Settings → Controllers (deep) |
| `caps:a` | Capture/album reads — for "launch Album" shortcut and screenshot gallery | Album launch from shell |
| `caps:ss` | Screenshot service — take screenshot from quick-menu | Quick-menu screenshot |
| `ldn:s` | Local wireless networking — if we surface LAN mode | Deferred |
| `audren:u` | Audio renderer (for sound effects) | UI sfx — or drop sfx entirely and skip |
| `audout:u` | Legacy audio out (what upstream uLaunch uses — the SDL2 pin reason) | If we keep SDL2 audio path, else drop |
| `aoc:u` | Add-on content — DLC enumeration | Deferred |
| `lr` | Location resolver — maps TID to content path | Used indirectly via ns; may not need direct |

**Narrowing rule:** At v0.6.0 we audit every granted service against actual FFI call sites. Anything not called in 30 days of hw testing gets removed from NPDM. Attack surface minimization is a Q OS pillar.

**NPDM JSON delta from upstream `uSystem.json`:**
- `program_id` stays `0x0100000000001000` — identity as qlaunch replacement.
- `name` changes from `uSystem` to `qosShell`.
- `service_access` changes from `["*"]` to the narrowed list above.
- `service_host` — we keep `["*"]` only if our IPC endpoint needs to register arbitrary names; otherwise narrow to the specific Q OS IPC service name (e.g. `qos:shl`).
- `kernel_capabilities.syscalls` — same superset as upstream (qlaunch needs broad syscall access). Do not narrow without syscall-trace evidence.
- `kernel_capabilities.application_type` stays `2` (system applet).
- `kernel_capabilities.min_kernel_version` stays `0x0030` (fw 3.0+ baseline; AMS re-signs against installed fw).

---

## H1 — File-Tree Mapping — qos-shell.nsp

### H2 — SD card layout (final v1.0.0)

```
/Volumes/SWITCH SD/
├── atmosphere/
│   └── contents/
│       └── 0100000000001000/            ← TID qlaunch (NOT 010000000000100D Album)
│           ├── exefs.nsp                ← the bundle Atmosphère LayeredFS picks up
│           │   [contents of exefs.nsp, unpacked view:]
│           │   ├── main                 ← ELF: C++ entry + linked libqos_shell.a
│           │   ├── main.npdm            ← binary NPDM from qos-shell.json
│           │   └── rtld                 ← libnx dynamic linker stub (standard)
│           └── romfs.bin                ← Q OS assets only, built from qos-shell/romfs/
└── switch/
    ├── qos-shell-v1.0.0.log             ← EVENT telemetry (CURVE/ANIM/INPUT/FINDER/VAULT/QSMI)
    └── qos-mock-desktop-gui-v1.0.0.nro  ← legacy GUI NRO, launchable from Dispatch
```

### H2 — Build-tree layout (where the source lives)

```
/Users/nsa/Astral/QOS/tools/qos-shell/        ← new directory, sibling to qos-ulaunch-fork/
├── Cargo.toml                                 ← workspace root
├── Makefile                                    ← drives both cargo build + C++ compile + NSP pack
├── qos-shell.json                              ← NPDM descriptor (see above table)
├── src/
│   ├── main.cpp                                ← ~120 lines, entry + heap + service init
│   ├── sdl_init.cpp                            ← ~60 lines, SDL2/nouveau bringup
│   ├── service_shims.cpp                       ← ~80 lines, extern "C" service wrappers
│   ├── npdm_patch.cpp                          ← ~40 lines, AMS version check
│   ├── ffi.h                                   ← cbindgen-generated
│   └── rust/                                   ← Cargo subcrate, crate-type = ["staticlib"]
│       ├── Cargo.toml
│       └── src/
│           ├── lib.rs                          ← exports qos_shell_run
│           ├── wm.rs
│           ├── desktop_icons.rs
│           ├── launchpad.rs
│           ├── osk.rs
│           ├── ctxmenu.rs
│           ├── wallpaper.rs
│           ├── canvas.rs
│           ├── theme.rs
│           ├── anim.rs
│           ├── curve.rs
│           ├── input.rs
│           ├── multitouch.rs
│           ├── icon_cache.rs
│           ├── icon_category.rs
│           ├── nro_asset.rs
│           ├── fs_util.rs
│           ├── telemetry.rs
│           ├── qsmi.rs
│           ├── apps/
│           │   ├── mod.rs
│           │   ├── dispatch.rs
│           │   ├── vault/{mod,model,view,sidebar,preview,state_persistence,actions}.rs
│           │   ├── settings.rs
│           │   ├── terminal.rs
│           │   ├── library.rs
│           │   └── saves.rs
│           └── services/
│               ├── mod.rs
│               ├── ns.rs
│               ├── am.rs
│               ├── set.rs
│               ├── nifm.rs
│               ├── time.rs
│               ├── psc.rs
│               ├── psm.rs
│               ├── nfp.rs
│               └── bsd.rs
├── romfs/                                       ← becomes romfs.bin
│   ├── fonts/inter-regular.ttf                  ← Inter font, OFL-licensed
│   ├── icons/                                   ← PNG/JPEG app icons, Q OS-made
│   ├── sfx/*.ogg                                ← generated, not XorTroll-derived
│   ├── config/
│   │   ├── theme.json                           ← Dark Liquid Glass tokens
│   │   └── dock-layout.json                     ← default dock layout
│   └── wallpapers/
│       └── cold-plasma-cascade-seed-1364153183.png  ← fallback static, procedural is preferred
├── out/                                         ← build artifacts, gitignored
│   ├── main
│   ├── main.npdm
│   ├── romfs.bin
│   └── exefs.nsp
└── archive/                                     ← per-version build archives (ROADMAP §6 ritual)
```

**Ritual note:** the existing `qos-ulaunch-fork/` directory is preserved as the themed-fork track. The native-shell track lives in a sibling `qos-shell/` directory. When v1.0.0-native ships, `qos-ulaunch-fork/` is marked deprecated but archived; nothing moves or deletes from it.

---

## H1 — Risk Matrix

| # | Risk | Prob | Impact | Mitigation |
|---|---|---|---|---|
| R1 | **Plutonium version lock-in** — upstream Plutonium is a single-author GPLv2 lib. If XorTroll stops maintaining, or if we fork and then libnx/SDL2 drift makes the fork unbuildable, we're stuck maintaining a UI framework we didn't write. | HIGH (by v0.5) | MED | Plutonium is retired by v0.7.x anyway. Between v0.5.0 (Rust cdylib takes over Layout) and v0.7.0 (Plutonium removed), Plutonium is a render pipe only — maintenance surface is small. Plan: never touch Plutonium source; wrap around it. |
| R2 | **libnx ABI drift across firmware versions** — libnx bindings change between fw updates (new services, renamed IPC commands, added members to NACP). Our Rust FFI pins one libnx commit; a new fw needs a libnx bump, which might break our `extern "C"` signatures. | MED | HIGH | Pin libnx in devkitPro sysroot version. Every libnx bump is its own rung (v0.6.x.N). Add a `qos_svc_libnx_version()` export that logs libnx commit to telemetry on every boot. Do NOT auto-update devkitPro. |
| R3 | **NPDM grant refusal by PM** — if we request a service AMS's ACID whitelist forbids, PM refuses to spawn the process. Symptom: black screen at boot, Atmosphère crash report pointing at `svcCreateProcess` failure. | MED | CRITICAL | Audit NPDM service_access against AMS 1.11.1 ACID whitelist before v0.6.0. Start narrowed (known-good list above), grow only when FFI call-site evidence shows it's needed. Keep `uManager`-equivalent (disable Q OS shell from hbmenu) through v1.0.0 as escape hatch. |
| R4 | **Library-applet chainload contract break** — NS tracks which donor applet slot the library-applet is running on. Mis-handling `applet_AE_UILibraryApplet` release causes NS to refuse subsequent launches. Cascading: hbmenu fails to launch, Album fails to launch, NRO Dispatch fails. | LOW | CRITICAL | v0.6.3 is dedicated to this. Library-applet launcher in Rust must call the same libnx functions in the same order uLaunch calls them. `qos_svc_la_launch()` is a direct port of `la::LibraryApplet::Start` from upstream, not a rewrite. Hw test matrix: launch hbmenu, launch Album, launch arbitrary NRO, return from each 3× in a row. |
| R5 | **NS crash if shell process exits** — fundamental Nintendo invariant. Rust panic that reaches FFI boundary = `abort()` = process exits = NS crashes = black screen until reboot. | LOW | CRITICAL | `panic = "abort"` in Rust profile. Top-level `qos_shell_run()` wraps everything in `std::panic::catch_unwind`. Any panic logs to `sdmc:/switch/qos-shell-panic-<timestamp>.log` and the shell re-enters its main loop with fallback minimal UI. Never return from `qos_shell_run()`. |
| R6 | **FIA / GPLv2 regulatory — XorTroll's GPLv2 on Plutonium** — if by v0.7.0 we still statically link any Plutonium-derived code, our binary inherits GPLv2 obligation. | HIGH until v0.7.x | LOW | Write-fresh policy: after v0.7.0, zero lines of Plutonium or uMenu/uSystem source in qos-shell/. Clean-room: the analysts who read uLaunch source (that's this research) do NOT implement qos-shell/. Implementers get only this doc and the upstream's public API headers (libnx, Atmosphère-libs), not XorTroll's `.cpp` files. Q OS-specific UX code (wallpaper, dock, Vault, Dispatch) is Rust-original from `mock-nro-desktop-gui`, not ported from uLaunch. |
| R7 | **SDL2 version pin (switch-sdl2 2.28.5-3)** — newer SDL2 uses audren, which crashes when suspending games in the library-applet context. Pin documented in upstream INTEGRATION-SPEC.md §7. | MED | HIGH | Ship the pinned `.pkg.tar.xz` in `qos-shell/deps/`. Build-script assert fails fast if wrong version detected. When v0.7.0 removes SDL2_mixer/image/ttf, risk narrows to SDL2 core only. Long-term (post v1.0): replace SDL2 entirely with direct nouveau/libdrm calls. |
| R8 | **Atmosphère version-lock (AMS 1.11.1)** — AMS IPC behavior and ACID whitelist change between versions. AMS 1.12 could refuse our NPDM or change `applet_AE_UILibraryApplet` semantics. | MED | HIGH | `npdm_patch.cpp` runs an AMS version assert on every boot, logs to telemetry, refuses to boot Q OS shell on unsupported AMS versions (falls through, Atmosphère then runs stock qlaunch). uManager-equivalent disable path stays in. |
| R9 | **cbindgen / bindgen drift** — C++ side consuming a Rust-generated `ffi.h` that regenerated mid-review creates subtle ABI mismatches. | LOW | MED | Commit `ffi.h` to repo. CI check: regenerate, diff, fail build on unexpected delta. Only updates on intentional FFI change. |
| R10 | **Default-build SHA drift** (`5e16d8b6…`) — Switch-specific code leaking into non-Switch builds would mutate the repo-wide default SHA, violating kernel target-isolation mandate. | LOW | HIGH | `qos-shell/` is `#[cfg(feature = "switch-shell")]` at crate level. Default features = empty. `cargo check` on non-Switch hosts does not compile any Switch code. CI verifies default SHA unchanged by running `cargo build --release` on a non-Switch target pre/post any qos-shell/ change. |
| R11 | **NSP format drift** — Atmosphère's LayeredFS handling of unsigned NSP at `exefs.nsp` path could change in future AMS versions (currently: AMS unpacks `exefs.nsp` as if it were an exefs directory, grants NPDM by reading `main.npdm` from inside). | LOW | HIGH | Alternative form accepted by AMS: loose files under `atmosphere/contents/0100000000001000/exefs/` (directory instead of packed NSP). Keep both build targets (NSP + loose-exefs) in Makefile. If AMS breaks NSP unpacking, switch to loose-exefs with zero code change. |
| R12 | **Telemetry log writes blocking on FS contention** — when uMenu launches a game, FS sessions are reallocated. A write in progress could block or race. | LOW | MED | Buffer telemetry in-memory, flush only at (a) idle frames, (b) HOME button press, (c) shutdown. Never flush during applet transition. |

---

## H1 — Decision Points for Creator

These are the open questions where architectural directions diverge and the creator's call is required before v0.5.0 starts. Each has a default recommendation with rationale.

### H2 — D1: Port mock-nro-desktop-gui directly, or fork from uMenu source?

**Recommended: Option A (port mock-nro-gui directly).** GPL containment win larger than adaptation cost. Mock track's v1.1.8 maturity makes this viable.

### H2 — D2: Pure C++ shell, or hybrid Rust-core C++-UI?

**Recommended: Option C (hybrid Rust-core + thin C++-UI-shim).** 300 lines of C++ for lifetime-of-OS stability against libnx drift is the right price.

### H2 — D3: Single-binary TID 100, or split daemon+menu like current uLaunch (uDaemon/uSystem + uMenu)?

**Recommended: Option B (split daemon+menu).** Firmware forces this; there's no real choice. Match upstream's topology exactly; replace the content only.

### H2 — D4: Keep ZIP `.ultheme` theme format, or hardcode Dark Liquid Glass?

**Recommended: Option B (hardcode Dark Liquid Glass).** Q OS is opinionated. Theming is for v1.5+ if ever. Ship one look.

### H2 — D5: Preserve upstream IPC service name `ul:usr`, or rename to `qos:shl`?

**Recommended: Option B (rename to `qos:shl`).** The ecosystem is tiny; breakage is acceptable. v1.0.0 is a clean break.

### H2 — D6: Kill Bluetooth stack (btm:sys, btdrv) in shell, or keep?

**Recommended: Option B (drop BT grants for v1.0.0).** Quick-menu → "Controller pairing" launches Nintendo's own controller applet via library-applet chainload. Attack surface > convenience.

---

## H1 — Glossary

- **TID 0100000000001000** — Title ID `qlaunch`, the home menu applet Nintendo ships. The slot Q OS replaces. Not to be confused with TID `010000000000100D` = Album applet.
- **Atmosphère LayeredFS** — Overlay mechanism on built-in title content. Path `atmosphere/contents/<TID>/exefs.nsp` overrides stock `qlaunch` exefs. No Nintendo signing keys required.
- **NSP** — Nintendo Submission Package. PFS0 container holding `main`, `main.npdm`, optional `rtld`, plus NCAs. Unsigned NSP is fine for LayeredFS override.
- **NPDM** — Nintendo Program Description Metadata. Binary file declaring program ID, service access, kernel capabilities, FS permissions.
- **PM** — Horizon's process manager. Grants service handles per NPDM.
- **ACID** — AMS's NPDM signing-authority descriptor. Whitelist of services each NPDM class is allowed to request.
- **applet_AE_UILibraryApplet** — Library-applet donor slot used for custom library applets (what uMenu runs on).
- **SMI protocol** — uLaunch's inter-process protocol between uSystem and uMenu. AppletStorage push/pop with magic `0x21494D53 = "SMI!"`. Q OS's replacement is QSMI, magic `0x49534F51 = "QOSI"`.
- **Plutonium** — XorTroll's C++/SDL2/libnx UI framework. GPLv2. Retired by Q OS v0.7.0.
- **cdylib / staticlib** — Rust crate output types. We use `staticlib` so Rust code links into the C++-entry `main` ELF as a single artifact.
- **Ship of Theseus** — Replace every plank of a ship one at a time. Applied here: start with stock uLaunch + Q OS theme, replace each module one-by-one, end with zero XorTroll code.

---

## H1 — Change Log

| Date | Change | Author |
|---|---|---|
| 2026-04-18 HST | Initial issue. Six-rung substitution plan. C++/Rust split. NPDM table. File tree. 12-row risk matrix. 6 creator decision points. | architect (Opus) |

---

## H1 — Next Steps (post-doc-approval, non-binding until creator decides D1-D6)

1. Creator decides D1-D6 above. Default recommendations locked in if no objection within 72h.
2. Register `qos-shell` track in `STATE.toml` under new section `[qos_shell]` parallel to `[qos_ulaunch_fork]`.
3. v0.3.1 rung (Q OS romfs swap on the themed track) is already in flight on the `qos-ulaunch-fork` side — let it finish; it's compatible with this plan.
4. v0.4.0 rung (procedural wallpaper) starts the substitution in a new `qos-shell/` directory. Implementer agent must not have read uLaunch source (clean-room per R6).
5. Spawn Sonnet agent `Worker-shell-1` at v0.4.0 kickoff. Do not use Opus for implementation (rate-limit discipline).

---

*End of NSP-REPLACEMENT-ROADMAP.md — 2026-04-18 HST — architect (Opus) — one-shot issue*
