# Tesla Overlay + Mod-Loading Ecosystem Deep Dive

> Reconnaissance for (1) v3.1 immediate use — copy cross-applet composition
> technique from libtesla, and (2) v3.2+ absorption planning — what Q OS
> uMenu might pull in over time.
>
> Date: 2026-05-18
> Cross-ref: `docs/RESEARCH-libtesla-rendering.md` (full rendering-stack deep
> dive, 2026-04-18), `docs/v3.1-research/atmosphere-deep-dive.md` (vi/mitm/
> BackgroundIndirect audit, 2026-05-18). This doc does NOT duplicate what
> those cover — it focuses on Tesla composition technique transfer + absorption
> matrix + mod-ecosystem landscape that those docs did not cover.

---

## Part A — Tesla overlay framework

### A.1 libtesla architecture

**Canonical sources (2026-active):**

| Repo | Status | License |
|---|---|---|
| `WerWolv/libtesla` (v1.3.2, 2023-05-27) | Stale | GPL-2.0 |
| `ppkantorski/libultrahand` | ACTIVE (2026) | GPL-2.0 + CC-BY-4.0 |
| `WerWolv/nx-ovlloader` | Upstream | GPL-2.0 |
| `ppkantorski/nx-ovlloader` v2.0.0+ | ACTIVE | GPL-2.0 |

**Architecture tiers** (from `RESEARCH-libtesla-rendering.md`):

1. `nx-ovlloader` sysmodule: allocates process heap via `svcSetHeapSize`, loads
   `/switch/.overlays/ovlmenu.ovl` as an NRO. Derived from `nx-hbloader` (confirmed
   by `nx-ovlloader/source/main.c` — same NRO-map/unmap trampoline structure, same
   `AppletType_LibraryApplet` config entry). Title ID: `0x0100000000000000` range,
   community convention.
2. Loaded `.ovl` calls `tsl::loop<YourOverlay>(argc, argv)` as its `main()`.
3. libtesla owns the vi layer + rendering + font + input inside that loop.

**Framebuffer spec** (from `include/tesla.hpp` line 85-88, confirmed via gh api fetch):

| Parameter | Value |
|---|---|
| Width × Height | 448 × 720 px |
| Format | `PIXEL_FORMAT_RGBA_4444` (2 bytes/px) |
| Buffers | 2 (double-buffered) |
| FB footprint | 448 × 720 × 2 × 2 = **1.26 MB** |

**Why it fits in 6 MB on fw 20.0.0:** The Nintendo shared fonts are mapped from
a system-owned region via `plGetSharedFontByType` — they do NOT count against the
process `svcSetHeapSize` quota.

---

### A.2 sys-tesla / overlay-loader mechanism

**nx-ovlloader main.c** (full source fetched via `gh api`):

- Runs as a Library Applet (config entry `AppletType_LibraryApplet` at line ~entries[2]).
- `loadNro()` calls `fsOpenSdCardFileSystem`, opens `g_nextNroPath` (default:
  `sdmc:/switch/.overlays/ovlmenu.ovl`).
- Maps NRO via `svcMapProcessCodeMemory` into a randomly-chosen address.
- Sets permissions: `.text` R+X, `.rodata` R, `.data+.bss` RW.
- Trampolines into NRO entrypoint (`nroEntrypointTrampoline`).
- On NRO exit, `g_nextNroPath` is checked for a new overlay to chain-load. This is
  how Tesla-Menu hands off to a user-picked `.ovl` and then back.

**Atmosphère integration:** nx-ovlloader is installed at
`/atmosphere/contents/<TID>/exefs.nsp` + `flags/boot2.flag`, launched at boot by
Atmosphère's boot2 hook. It does NOT use service mitm — it is a standard custom
sysmodule that gets HOS application-level privileges through its NPDM. The key
privilege it needs is `vi:m` (manager-level vi service) to call `viCreateManagedLayer`.

**Active fork:** `ppkantorski/nx-ovlloader` v2.0.0 adjusts `g_appletHeapSize` per
firmware version: 8 MB (HOS ≤ 19), 6 MB (HOS 20.x), 4 MB (HOS 21+). Override:
`/config/nx-ovlloader/heap_size.bin`. This is the heap-tuning milestone for fw 20.

---

### A.3 Cross-applet composition technique (V3.1-RELEVANT)

**This is the highest-priority section.**

#### How Tesla draws on top of a running game without owning the screen

Tesla does NOT use `LibAppletMode_BackgroundIndirect` or `viGetIndirectLayerImageMap`.
Its technique is orthogonal and simpler: it creates its own `vi:m` managed layer at the
**maximum Z-order**, which composites above everything else the `nvnflinger` compositor
renders — games, Home Menu, other applets.

**Exact vi* call sequence** (from `tesla.hpp` lines 1042-1064, confirmed via
`gh api repos/WerWolv/libtesla/contents/include/tesla.hpp`):

```cpp
// Step 1: Open the display service at manager level
viInitialize(ViServiceType_Manager);
viOpenDefaultDisplay(&this->m_display);
viGetDisplayVsyncEvent(&this->m_display, &this->m_vsyncEvent);

// Step 2: Create a managed layer (requires vi:m)
viCreateManagedLayer(&this->m_display,
    static_cast<ViLayerFlags>(0), 0, &__nx_vi_layer_id);
viCreateLayer(&this->m_display, &this->m_layer);
viSetLayerScalingMode(&this->m_layer, ViScalingMode_FitToLayer);

// Step 3: Set Z-order to maximum — draws on top of EVERYTHING
if (s32 layerZ = 0;
    R_SUCCEEDED(viGetZOrderCountMax(&this->m_display, &layerZ)) && layerZ > 0)
    viSetLayerZ(&this->m_layer, layerZ);   // ← THE KEY: topmost Z-slot

// Step 4: Add to ALL layer stacks (so it appears in every display context)
tsl::hlp::viAddToLayerStack(&this->m_layer, ViLayerStack_Default);
tsl::hlp::viAddToLayerStack(&this->m_layer, ViLayerStack_Screenshot);
tsl::hlp::viAddToLayerStack(&this->m_layer, ViLayerStack_Recording);
tsl::hlp::viAddToLayerStack(&this->m_layer, ViLayerStack_Arbitrary);
tsl::hlp::viAddToLayerStack(&this->m_layer, ViLayerStack_LastFrame);
tsl::hlp::viAddToLayerStack(&this->m_layer, ViLayerStack_Null);
tsl::hlp::viAddToLayerStack(&this->m_layer, ViLayerStack_ApplicationForDebug);
tsl::hlp::viAddToLayerStack(&this->m_layer, ViLayerStack_Lcd);

// Step 5: Set size/position
viSetLayerSize(&this->m_layer, cfg::LayerWidth, cfg::LayerHeight);  // 448x720
viSetLayerPosition(&this->m_layer, cfg::LayerPosX, cfg::LayerPosY); // 0,0

// Step 6: Create window + framebuffer via libnx wrappers
nwindowCreateFromLayer(&this->m_window, &this->m_layer);
// framebufferCreate follows, then double-buffer render loop
```

**`viAddToLayerStack` internals** (from `tesla.hpp` lines 267-273):
```cpp
static Result viAddToLayerStack(ViLayer *layer, ViLayerStack stack) {
    const struct {
        u64 stack;
        u64 layerId;
    } in = { stack, layer->layer_id };
    return serviceDispatchIn(
        viGetSession_IManagerDisplayService(), 6000, in);
}
```
This is a direct HIPC dispatch to `IManagerDisplayService` command 6000 — a vi:m
sub-session command not exposed at vi:s or vi:u level.

**What `viCreateManagedLayer` does (vs. `viCreateLayer`):**

- `vi:u` / `vi:s`: only `viCreateLayer` (stray layer) — visible only within the
  caller's own display context.
- `vi:m`: `viCreateManagedLayer` — creates a compositor-managed layer that persists
  across display context switches and participates in the global Z-order.

**The Z-ordering guarantee:** `viGetZOrderCountMax` returns the maximum allowed Z
value for the display. Tesla sets its layer to `layerZ` (the max). The nvnflinger
compositor composites layers in ascending Z-order — Tesla's layer is literally drawn
last (on top).

**Why this works across ALL applets/games without those applets knowing:**
The layer is in the compositor (nvnflinger), not inside any game process. Games render
their pixels to their own layers; the compositor then blends Tesla's transparent-
background layer on top when the overlay is visible. Zero game-side cooperation needed.

#### Transfer assessment for Q OS v3.1

**Q OS v3.1 objective** is different from Tesla: v3.1 wants to composite a *running
NRO's output* inside a uMenu window — capturing the NRO's framebuffer, not creating
a new overlay layer on top of it.

The Tesla technique is **NOT** the solution for v3.1's windowed-NRO goal, but it IS
transferable for a different, simpler use case:

| Use case | Tesla technique applies? | Notes |
|---|---|---|
| v3.1: show NRO output inside uMenu window | NO — Tesla adds overlay ON TOP; v3.1 needs to CAPTURE the NRO's own layer | Use `viGetIndirectLayerImageMap` instead — see `atmosphere-deep-dive.md` |
| v3.1: uMenu HUD drawn on top of a running NRO | YES — directly copyable | Open vi:m managed layer at max Z from uMenu's process |
| v3.2: "floating notification" banner above any title | YES — directly copyable | Ultrahand-Overlay already does this with its toast system |
| v3.2: Q OS system overlay (performance meter, quick toggles) | YES — directly copyable | Highest-value direct Tesla technique steal |

**Conclusion for v3.1:** The `viGetIndirectLayerImageMap` path (BackgroundIndirect mode)
documented in `atmosphere-deep-dive.md` remains the correct v3.1 mechanism. Tesla's
z-order technique is a valuable steal for the **HUD/system-overlay** surface, not the
windowed-capture surface.

**The one transferable primitive for v3.1:** If uMenu needs to draw a chrome/frame
AROUND the windowed NRO (title bar, close button, resize handles), it should open its
own `vi:m` managed layer at max-Z and draw the chrome there. The NRO's frames would be
composited below via `viGetIndirectLayerImageMap`, and uMenu's chrome layer sits on top.
This is identical to Tesla's layering model.

---

### A.4 Input model + game focus during overlay

**Input interception sequence** (from `tesla.hpp` lines 252-294):

```cpp
// Custom hid:sys shim (cmd 503 = hidsysEnableAppletToGetInput)
static Result hidsysEnableAppletToGetInput(bool enable, u64 aruid) {
    const struct { bool enable; u64 aruid; } in = { enable, aruid };
    return serviceDispatchIn(hidsysGetServiceSession(), 503, in);
}

// When overlay opens:
// 1. Disable input for all background applets (ARUIDs 0x0100000000001000-0x1020)
hidsysEnableAppletToGetInput(!enabled, appletAruid);
// 2. Disable input for the application (game)
hidsysEnableAppletToGetInput(!enabled, applicationAruid);
// 3. Enable input for Tesla itself (ARUID 0 = the overlay process)
hidsysEnableAppletToGetInput(true, 0);
```

**Game pause behavior:** The game is NOT paused. It continues executing normally.
Only HID input routing is changed — the game's `hidGetNpadStates()` calls return
stale/empty data while the overlay has input focus. The game's render loop keeps
running; its frames continue to be visible below the overlay layer.

**Input restoration on overlay close:** The enable/disable calls are reversed. Game
resumes receiving input immediately.

**V3.1 relevance — the BG-2/BG-4 input-dead blocker:**

The blocker documented in `atmosphere-deep-dive.md §6.3` is that when an NRO runs
in `LibAppletMode_BackgroundIndirect`, HID input routes to the parent (uMenu), not
the NRO. The NRO's `hidScanInput()` returns zeros.

Tesla's `hidsysEnableAppletToGetInput` shim is the INVERSE of what v3.1 needs:
- Tesla: takes input AWAY from the game (aruid) and gives it to the overlay (aruid 0).
- v3.1: uMenu has input but needs to forward it to the background NRO.

**Direct transfer is NOT possible** — `hidsysEnableAppletToGetInput` enables input
FOR a process (aruid), it does not FORWARD input FROM one process TO another. The NRO
cannot receive a forwarded HID stream; it reads from HID IPC directly.

**What DOES transfer:** The aruid-routing mechanism proves the Switch HID service
supports per-ARUID input enable/disable at cmd 503. This is how a v3.1 forwarding
shim would work if it ran inside uLoader (not in uMenu): uLoader would receive the
input enabled ARUIDs from uMenu and call `hidsysEnableAppletToGetInput(true, nroAruid)`
on behalf of the NRO. But this still hits the structural blocker: the NRO in
BackgroundIndirect mode does not have an ARUID that hid routes to — that's the actual
constraint. The aruid list in hid:sys is tied to the foreground applet chain, not
background processes.

**Honest assessment:** Tesla's input intercept pattern does not resolve the BG-2/BG-4
blocker. The shared-memory HBABI extension approach documented in `atmosphere-deep-dive.md
§Q1` remains the correct path for input forwarding to windowed NROs.

---

## Part B — sys-* sysmodule landscape

### B.1 sys-clk (overclock manager)

**Source:** `https://github.com/retronx-team/sys-clk`
**HamletDuFromage maintained fork** is the active modern version.

| Attribute | Detail |
|---|---|
| Function | Per-title CPU/GPU/memory clock profiles, dock-aware |
| Sysmodule TID | `0x00FF0000636C6BFF` (community range) |
| Install path | `/atmosphere/contents/00FF0000636C6BFF/exefs.nsp` + `flags/boot2.flag` |
| Config | `/config/sys-clk/config.ini` |
| Log | `/config/sys-clk/log.txt`, `/config/sys-clk/context.csv` |
| Tesla overlay | YES — `/switch/.overlays/sys-clk-overlay.ovl` |
| Manager NRO | YES — `/switch/sys-clk-manager.nro` (also surfaced via Vault) |
| Services used | `clkrst`, `pcv`, `apm`, `ts` (temp sensor) — all standard sysmodule services |
| Q OS conflict? | None. sys-clk is a passive observer/setter; no vi or applet conflict |

**Absorption verdict:** MAYBE v3.2 — Settings → Performance panel. Surface live CPU/GPU/mem
clocks and allow preset selection without replacing sys-clk itself. sys-clk continues to
enforce; uMenu's UI is a frontend for its `/config/sys-clk/config.ini`.

---

### B.2 sys-con (USB controller driver)

**Source:** `https://github.com/cathery/sys-con`

| Attribute | Detail |
|---|---|
| Function | Enables third-party USB controllers (Xbox 360, Xbox One, DualShock) |
| Type | Atmosphère custom sysmodule |
| Minimum FW | 5.0.0+ |
| Config path | `/config/sys-con/` |
| Services | Not documented publicly; likely `hid:sys` write-side + USB device enumeration |
| Q OS conflict? | None known — HID write path doesn't conflict with vi or applet services |

**Absorption verdict:** NO. sys-con solves a hardware-driver problem. Absorbing USB
controller firmware parsing into uMenu is out of scope and would fragment a well-maintained
specialist project.

---

### B.3 sys-patch / SysPatch (general HOS tweak modules)

`exelix11/SysPatch` (note: canonical repo could not be fetched — 404, likely renamed
or moved). The general category of "HOS patcher sysmodules" applies several firmware-level
patches at boot: typically signature checks, region locks, and online service restrictions.

These run entirely at boot, modify system memory regions, and then idle. They do not
register services or communicate with uMenu. No absorption case.

**Absorption verdict:** NO. Boot-time patcher; not a user-facing feature surface.

---

### B.4 MissionControl (Bluetooth controller bridge)

**Source:** `https://github.com/ndeadly/MissionControl`

| Attribute | Detail |
|---|---|
| Function | Enables non-Switch BT controllers (PS4, PS5, Xbox, Switch Pro clones) natively |
| Architecture | Mitm of `btm` + `hid` IPC; intercepts `WriteHidData` to translate controller data |
| Load | Atmosphère boot2 launch |
| Services | `btm`, `hid` — man-in-the-middle of the Bluetooth module |
| Q OS conflict? | Low risk: MissionControl intercepts Bluetooth-specific IPC, not vi/applet |
| UI surface | None — pairs via system BT menu |

**Absorption verdict:** NO. Different problem domain (Bluetooth protocol translation).
The mitm architecture is complex and hardware-specific. uMenu has no natural surface for
"configure Bluetooth controller firmware" in the Vault.

---

### B.5 ldn_mitm (local-wireless emulation)

**Source:** `https://github.com/spacemeowx2/ldn_mitm`

| Attribute | Detail |
|---|---|
| Function | Replaces the native `ldn` (Local Device Network) service with UDP-based LAN emulation |
| What it enables | Playing local-wireless-only games online, via `switch-lan-play` companion |
| Architecture | Direct service replacement via Atmosphère mitm of `ldn` service |
| Services | `ldn` — full service replacement, not just interception |
| Q OS conflict? | Potential: if uMenu also tries to use `ldn`, the mitm version may behave differently. Unlikely in practice (uMenu does not use ldn) |

**Absorption verdict:** NO. Network layer tool; requires companion infrastructure
(`switch-lan-play` relay server). Not a uMenu UI surface.

---

### B.6 nx-ovlloader (Tesla overlay host sysmodule)

**Source:** `https://github.com/WerWolv/nx-ovlloader`
**Active fork:** `https://github.com/ppkantorski/nx-ovlloader`

Already documented in A.1-A.2. Key absorption question: should Q OS replace or extend it?

**Absorption verdict:** NO for replacement. The ppkantorski fork is active and does
the one job well. Q OS can CATALOG the installed overlays from `/switch/.overlays/*.ovl`
in the Vault (same as the HBMenu absorption pattern for `/switch/*.nro`) without touching
nx-ovlloader itself.

---

## Part C — LayeredFS + content override

### C.1 LayeredFS mechanism

LayeredFS is implemented in `Atmosphère/stratosphere/ams_mitm/source/fs/fsmitm_*.cpp`
(local tree: `src/libs/Atmosphere-libs/`). The `fsp-srv` mitm intercepts filesystem
IPC calls and redirects per-title file access.

**Per-title overlay convention:**

```
/atmosphere/contents/<TitleId>/romfs/     ← romfs overlay (game assets)
/atmosphere/contents/<TitleId>/exefs/     ← exefs overlay (code stubs, .ips patches)
/atmosphere/contents/<TitleId>/exefs.nsp  ← full exefs replacement (used by uSystem)
/atmosphere/contents/<TitleId>/flags/     ← boot/disable flags
```

FsMitm checks on every `fsp-srv` open whether the title has a LayeredFS dir on SD.
If yes, reads from SD first (overlay wins); if not found, falls through to the real
game image. The mitm is transparent to the game.

**Flags:**
- `flags/boot2.flag` — launch the program at Atmosphère boot2 (used by sys-clk,
  nx-ovlloader, etc.)
- `flags/disable.flag` — Atmosphère skips the override, loads stock firmware version

**uSystem's specific use:** `/atmosphere/contents/0100000000001000/exefs.nsp` replaces
qlaunch entirely. This is not LayeredFS (no per-file override); it's a full exefs
replacement (NSP). The distinction: LayeredFS = partial file overlay; exefs.nsp =
complete code replacement.

### C.2 Community mod-loading conventions

| Path pattern | Purpose |
|---|---|
| `/atmosphere/contents/<TID>/romfs/` | Game asset mods (textures, models, sound) |
| `/atmosphere/contents/<TID>/exefs/*.ips` | Assembly-level code patches |
| `/mods/<title>/` | Some mod managers use this secondary convention |
| `/switch/.overlays/*.ovl` | Tesla-ecosystem overlays |
| `/switch/.packages/*/` | Ultrahand packages (INI-scripted automation) |
| `/config/<app>/` | Per-sysmodule config (sys-clk, nx-ovlloader, ultrahand) |

**Q OS Vault v3.x opportunity:** For each title in the Vault library view, a "Mods"
sub-pane could enumerate `/atmosphere/contents/<TID>/romfs/`, `/atmosphere/contents/<TID>/exefs/`,
and any `.ips` files — showing the user which game mods are active. This is purely
filesystem enumeration; no LayeredFS code changes required.

---

## Part D — Q OS absorption viability matrix

| Component | What it does | Absorb? | Reasoning |
|---|---|---|---|
| libtesla / libultrahand | Overlay rendering library | NO absorb — COPY technique | Use `vi:m` + max-Z layer for any uMenu HUD/chrome. Do not claim the overlay namespace. |
| nx-ovlloader sysmodule | Host process for .ovl files | NO | Working; absorbing would break all existing Tesla overlays. Catalog overlays in Vault instead. |
| Tesla-Menu / Ultrahand-Overlay | Overlay picker UI | NO | Active community projects with large ecosystems. Q OS can launch Ultrahand from Vault. |
| sys-clk sysmodule | CPU/GPU/mem overclock | NO (sysmodule stays) | Surface sys-clk config INI in uMenu Settings → Performance. Read/write config file. |
| sys-clk overlay | Overlay for clock display | NO | The ovl itself is fine. Vault catalogs it. |
| sys-clk manager NRO | Standalone manager | SURFACE in Vault | NRO appears in Vault as any other homebrew. No special integration. |
| sys-con | USB controllers | NO | Hardware driver, wrong scope. |
| MissionControl | BT controller bridge | NO | Different domain, complex mitm. |
| ldn_mitm | LAN play service | NO | Network infrastructure; requires companion server. |
| SysPatch / sys-tweak | Boot-time HOS patches | NO | Boot-time only, no UI surface. |
| LayeredFS mods | Per-title romfs/exefs overlays | SURFACE v3.x | Vault "Mods" pane per title. Enumeration only — no LayeredFS code. |
| `/switch/.overlays/` directory | Installed Tesla overlays | SURFACE v3.2 | Vault Overlays section, same as HBMenu → Vault absorption pattern for NROs. |
| `/switch/.packages/` directory | Ultrahand packages | SURFACE v3.2 | Vault Packages section. Read INI metadata for display name/icon. |

---

## Part E — Techniques to STEAL for v3.1 (not absorb, just learn from)

These are concrete primitives and patterns from the Tesla ecosystem that Q OS should
adopt — not as absorption (not claiming the namespace) but as engineering lessons.

### E.1 vi:m managed layer at max Z-order (STEAL for v3.1 chrome)

**Source:** `WerWolv/libtesla`, `include/tesla.hpp` lines 1042-1064 (via gh api fetch).

When uMenu needs to draw chrome (window frame, title bar, close button) ON TOP of the
NRO's window content, open a `vi:m` managed layer at `viGetZOrderCountMax()` and draw
the chrome there. The NRO's indirect-layer content composites below.

```cpp
// uMenu chrome layer init (steal directly from tesla.hpp):
viInitialize(ViServiceType_Manager);
viOpenDefaultDisplay(&display);
viCreateManagedLayer(&display, 0, 0, &layer_id);
viCreateLayer(&display, &layer);
viSetLayerScalingMode(&layer, ViScalingMode_FitToLayer);
s32 maxZ; viGetZOrderCountMax(&display, &maxZ);
viSetLayerZ(&layer, maxZ);
// Add to all relevant stacks:
// viAddToLayerStack via IManagerDisplayService cmd 6000
viSetLayerSize(&layer, CHROME_W, CHROME_H);
viSetLayerPosition(&layer, 0, 0);
nwindowCreateFromLayer(&window, &layer);
// framebufferCreate + RGBA4444 double buffer
```

**Memory cost:** ~1.26 MB double-buffer at 448×720. This is fine within uMenu's budget.
uMenu already has `vi:m` access via NPDM wildcard (confirmed `AUDIT-NPDM-uMenu-vi-access.md`).

### E.2 Heap sizing for fw 20.x (STEAL for any new sysmodule)

**Source:** `ppkantorski/nx-ovlloader` v2.0.0.

The `svcSetHeapSize` ceiling is firmware-dependent. Use the ppkantorski tiered sizes:

```c
if      (hosversionBefore(20, 0, 0)) g_appletHeapSize = 0x800000; // 8 MB, HOS ≤19
else if (hosversionBefore(21, 0, 0)) g_appletHeapSize = 0x600000; // 6 MB, HOS 20.x
else                                 g_appletHeapSize = 0x400000; // 4 MB, HOS 21+
```

Any future Q OS custom sysmodule targeting fw 20.0.0 should use this pattern.

### E.3 Shared-font access via `plGetSharedFontByType` (STEAL for uMenu font budget)

**Source:** `RESEARCH-libtesla-rendering.md §6` + tesla.hpp.

Nintendo shared fonts do not count against `svcSetHeapSize` quota. If uMenu v0.7
(the Tesla-renderer port) uses `plGetSharedFontByType` for all text rendering, the
2–3 MB font data is free from the process heap budget.

```cpp
PlFontData font;
plGetSharedFontByType(&font, PlSharedFontType_Standard);
// font.address is system-mapped — zero process heap cost
```

### E.4 RGBA4444 framebuffer (STEAL for v0.7 renderer)

**Source:** `RESEARCH-libtesla-rendering.md §2`.

The reason uMenu crashes on fw 20.0.0 is 8.29 MB for 1920×1080 RGBA8888. Tesla's
448×720 RGBA4444 at 1.26 MB fits in 6 MB comfortably. The visual degradation at
RGBA4444 (4-bit per channel) is acceptable for UI chrome (not game output). See
`RESEARCH-libtesla-rendering.md §10 Recommendation 2` for the 960×540 RGBA4444
option if 1080p-equivalent sharpness is needed.

### E.5 Ultrahand package INI format (STEAL for Vault package metadata)

**Source:** `ppkantorski/Ultrahand-Overlay` README.

Ultrahand packages use INI files with `[name]`, `[icon]`, and script sections in
`/switch/.packages/<PackageName>/package.ini`. If Q OS Vault v3.2 surfaces an
"Overlays & Packages" view, parsing these INI files gives a display name and icon
path without any Ultrahand-specific runtime dependency.

### E.6 Toast notification via JSON file (STEAL for sysmodule → uMenu IPC)

**Source:** Ultrahand-Overlay architecture (README excerpt, 2026-05-18 fetch).

Ultrahand allows external sysmodules to push toast notifications by writing JSON to
a watched path in `/config/ultrahand/`. This is a zero-IPC-session sideband channel.
Q OS could use the same pattern for sysmodule → uMenu notifications (e.g., sys-clk
thermal throttle alerts) without designing a new IPC service.

```
// Convention (steal from Ultrahand):
// Sysmodule writes: /config/ultrahand/notifications/<ts>.json
// uMenu reads on next frame sweep: { "title": "...", "body": "...", "level": "warn" }
// uMenu displays toast, deletes file.
```

---

## Open questions

**OQ-1 — vi:m privilege for uMenu chrome layer (v3.1)**
`RESEARCH-libtesla-rendering.md §7` notes uMenu should "attempt `viCreateManagedLayer`
if NPDM grants `vi:m`". The wildcard NPDM covers this, but the actual library applet
runtime privilege may be restricted by AM regardless of NPDM. Needs HW test: call
`viInitialize(ViServiceType_Manager)` from uMenu and check result. If it returns
`ResultPermissionDenied`, fall back to stray layer (`viCreateLayer`) at lower Z —
still functional, just without guaranteed top-Z priority in all display contexts.

**OQ-2 — nx-ovlloader ARUID conflict**
nx-ovlloader runs as `AppletType_LibraryApplet`. When uMenu is also a LibraryApplet
replacement for qlaunch, do the two Library Applet processes conflict in the AM
applet chain? Current Q OS state uses ECS to redirect the Album applet slot to uLoader.
If a user has nx-ovlloader installed (as another LibraryApplet), AM may reject the
second one. Needs audit: what happens when nx-ovlloader's TID coexists with uMenu's
ECS override.

**OQ-3 — sys-clk config.ini write safety**
For the v3.2 Settings → Performance integration, uMenu would write
`/config/sys-clk/config.ini` while sys-clk is running. Is the INI file read by
sys-clk on a polling interval or on title-switch? If polled, a partial write could
cause a bad profile. Solution: write to a `.tmp` file first, then rename (atomic on
FAT32 at single-file granularity on Switch's SD implementation).

**OQ-4 — Ultrahand-Overlay coexistence or replacement**
The user already has `Ultrahand-Reload.nro` installed. If Q OS v3.2 Vault surfaces an
"Overlays" section that launches overlays directly (bypassing Ultrahand-Menu), do users
need Ultrahand at all? The answer is yes for package scripting (Ultrahand's INI
automation engine has no Q OS equivalent). Q OS should complement, not compete, with
Ultrahand — at least until v3.3+.

**OQ-5 — libultrahand license (CC-BY-4.0 sublayer)**
The `ppkantorski/libultrahand` extension layer is CC-BY-4.0 on top of the GPL-2.0
tesla.hpp base. If Q OS v0.7 uses libultrahand directly (not just steals the technique),
attribution in the uMenu credits screen is required. The GPL-2.0 base also means
uMenu source must remain GPL-2.0 compatible — which it is (uLaunch fork is GPLv2).

---

## Source index

| Source | URL / Path | Date accessed |
|---|---|---|
| libtesla.hpp (full source) | `gh api repos/WerWolv/libtesla/contents/include/tesla.hpp` | 2026-05-18 |
| nx-ovlloader main.c | `gh api repos/WerWolv/nx-ovlloader/contents/source/main.c` | 2026-05-18 |
| Ultrahand-Overlay README | `https://github.com/ppkantorski/Ultrahand-Overlay` | 2026-05-18 |
| sys-clk README | `https://github.com/retronx-team/sys-clk` | 2026-05-18 |
| sys-con README | `https://github.com/cathery/sys-con` | 2026-05-18 |
| MissionControl README | `https://github.com/ndeadly/MissionControl` | 2026-05-18 |
| ldn_mitm README | `https://github.com/spacemeowx2/ldn_mitm` | 2026-05-18 |
| vi display services | `https://switchbrew.org/wiki/Display_services` | 2026-05-18 |
| RESEARCH-libtesla-rendering.md | `docs/RESEARCH-libtesla-rendering.md` | Prior session (2026-04-18) |
| atmosphere-deep-dive.md | `docs/v3.1-research/atmosphere-deep-dive.md` | 2026-05-18 |
