# Hekate + Bootloader Landscape (v3.1 context)

> Hekate is NOT the v3.1 implementation target (we run on Atmosphère/HOS, not
> bare-metal).  This audit captures the PRE-HOS landscape for design-context
> reasons + extracts any Nyx-menu design insights worth borrowing.
>
> Primary sources: github.com/CTCaer/hekate (README, nyx_gui source, frontend/gui.c,
> gfx/gfx.h, frontend/gui_tools.c), switchbrew.org/wiki/BCT, local repo files
> under src/libs/ and src/projects/uMenu/source/ul/menu/qdesktop/.
> Audit date: 2026-05-18.

---

## 1. Hekate architecture and process boundary

### What Hekate is

Hekate (github.com/CTCaer/hekate) is a custom graphical bootloader for Nintendo
Switch. It is a fully PRE-HOS binary: it runs directly on the ARM Cortex-A57 cluster
after the boot ROM and Package1 hand off execution, before Horizon OS or any sysmodule
has started. Hekate owns the hardware exclusively while it runs — there is no kernel,
no IPC, no applet model. It interacts with display hardware, SD card, USB, and the
battery/power controller through bare-metal register writes and libc-equivalent code
it ships itself.

### Boot-chain position

```
Power-on
  → Boot ROM (read-only, on-chip) — reads BCT from eMMC boot0 into IRAM
  → Package1ldr (in IRAM) — decrypts Package1 / PK11, initialises BPMP
  → [On unpatched Erista: fusée-gelée RCM exploit] ← Hekate enters here
      via browser-based payload injector pushing hekate_ctcaer.bin over USB
  → Hekate / Nyx runs (bare-metal, no HOS)
  → User selects a boot entry
  → Hekate chainloads Package2 + Atmosphere exosphère + kernel
  → Horizon OS boots, sysmodules start, uLaunch/uMenu applet starts
```

On this hardware (OG Erista T210, no modchip, browser fusée RCM — per
project_qos_switch_context.md) the payload injection happens every boot because
AutoRCM is not in use. The user injects hekate_ctcaer.bin via browser exploit; Nyx
appears; the user picks the Atmosphère CFW entry; HOS boots.

### Process boundary that matters for v3.1

Everything in uMenu/qdesktop (our code) runs AFTER HOS is up. Hekate is gone by that
point — its memory has been reclaimed and its code has not executed since the
chainload. The two worlds share:

- `sdmc:/atmosphere/reboot_payload.bin` — a file on the SD card that Atmosphère reads
  at reboot time to decide where to jump. Hekate writes nothing to this file; it only
  reads it if it is configured to. Our code writes it indirectly via `bpc:ams`.
- `sdmc:/bootloader/hekate_ipl.ini` — read by Hekate at boot to enumerate entries.
  Our code does not touch this file at runtime; `qd_HekateIni.cpp` is a reader-only
  used for displaying the boot-entry list in the Settings layout.

**Conclusion for v3.1:** There is no shared runtime between Hekate and Q OS uMenu.
They execute at completely different times. Any Hekate "integration" is purely
file-based (SD card files) or reboot-triggered (write a payload → reboot → Hekate
picks it up on next boot).

---

## 2. Nyx menu — windowing model + design insights

### Rendering stack

Source: `nyx/nyx_gui/gfx/gfx.h`, `nyx/nyx_gui/frontend/gui.c`.

**Framebuffer:** Nyx writes directly to a 32-bit ARGB framebuffer (pixel format
X8R8G8B8) at 1280×720. The framebuffer pointer, width, height, and scanline stride
are tracked in `gfx_ctxt_t`. There is no GPU shader pipeline — all painting is CPU
scanline work via helpers like `gfx_set_pixel`, `gfx_set_rect_pitch`, and
`gfx_set_rect_land_pitch`.

```c
typedef struct _gfx_ctxt_t {
    u32 *fb;       // raw framebuffer pointer
    u32 width;     // 1280
    u32 height;    // 720
    u32 stride;    // scanline stride in pixels
} gfx_ctxt_t;
```

**LVGL on top of the raw framebuffer:** `gui.c` uses LVGL (Light and Versatile
Graphics Library) as the widget layer. LVGL draws into the framebuffer via the
`_nyx_disp_init()` flush callback. This means Nyx has a real retained-mode widget
tree — labels, buttons, containers, tabviews — even though the ultimate output is a
bare CPU-painted framebuffer.

### Windowing model

Nyx is **not windowed** in the desktop sense. Every "window" in Nyx is fullscreen:
`nyx_create_standard_window()` creates LVGL objects sized to `LV_HOR_RES` ×
`LV_VER_RES` (1280×720). Sub-tool screens (eMMC tools, emuMMC, options, info) are
full-screen replacements, not floating panels. There is no z-ordering of multiple
visible windows.

Modal dialogs are implemented as darkened overlays: `lv_obj_create()` with
`mbox_darken` styling placed at center alignment. These are the closest thing to
"floating panels" — they dim the background and put a message box on top.

**Tab navigation:** The main Nyx screen uses LVGL's `tabview` widget. The tabs
visible in the Nyx UI (Home, Tools, eMMC, emuMMC, Options, Info/About) correspond to
`tabview` pages. The tab bar is styled with `tabview_btn_pr` and `tabview_btn_tgl_pr`
styles (pressed / toggled states). Switching tabs is O(1) — LVGL shows the new page
and hides the old one; no re-creation.

**Status bar:** A persistent `gui_status_bar_ctx status_bar` struct is maintained
and refreshed by `manual_system_maintenance()` at a periodic task rate. It renders
battery, clock, and temperature readouts at the top of every screen.

### Input handling

Two parallel input paths feed into LVGL's input device abstraction:

1. **Touch:** `_fts_touch_read()` — reads the FTS touchscreen IC directly. Supports
   up to multi-touch; 3-finger gesture triggers a screenshot.
2. **Joy-Con:** `_jc_virt_mouse_read()` — maps analog stick to a virtual cursor,
   A/B/X/Y to mouse buttons, +/- to console toggle. The cursor is drawn as an
   LVGL object on top of the scene.

Input is polled by LVGL's task handler on each frame; there is no interrupt-driven
event queue.

### Design insights worth borrowing

**1. Status-strip / live-data in a dropdown — open-time snapshot pattern.**
Nyx's status bar polls hardware every maintenance tick and redraws. Our right
hot-corner dropdown already uses a stronger version of this: status rows (battery,
time, network, volume) are rendered as pre-baked SDL textures at `Open()` time; only
the dev rows (nxlink, remote shell) use a `RefreshDevRowLabel()` live-update path
to avoid per-frame IPC. This is more conservative than Nyx (which renders live every
tick) and costs less — good.

**2. Tab-based top-level navigation, tool sub-screens are full-size replacements.**
Nyx's insight: users expect tabs at the top level and full-screen drill-downs for
tools. This maps directly to what Q OS v3.1 should do for a "Settings" or "System
Tools" area: keep the desktop full-screen, push tool panels as full-screen overlays
navigated with B-to-back, not as floating sub-windows that compete with the desktop
chrome. The "windowed within the desktop" metaphor adds complexity Nyx avoids
entirely.

**3. Persistent status bar across every view.**
Nyx always shows battery + time regardless of which tool screen is open. Q OS uMenu
already mirrors this with `GlobalChrome` / topbar. For v3.1 any new full-screen views
(vault, task manager, settings) should keep the topbar alive at the same z-order.

**4. Modal darkened overlay for confirmations.**
Nyx uses `mbox_darken` overlays instead of new screens for yes/no confirmations. Q OS
can use the same idiom for destructive actions (power off, reset, format) without
routing through a full navigation push. This is already partially present in
`qd_ContextMenu.cpp` but could be standardised.

**Key design language Nyx establishes for Switch system tools:**
- Fullscreen always (no floating windows on a 720p screen — too small)
- Tab bar at top, content fills remaining space
- Status strip persistent across all views
- Modals dim background, float centered
- Input duality: touch-primary, Joy-Con virtual cursor as fallback

---

## 3. Payload chainload mechanism (back-to-Hekate path)

### Overview

The "reboot to Hekate" path uses Atmosphère's `bpc:ams` IPC extension, NOT a
Hekate-specific API. The complete chain is:

```
uMenu (userland applet)
  → open bpc:ams IPC port (Atmosphère exosphère extension)
  → bpcamsSetRebootPayload(buf, size)
      writes hekate_ctcaer.bin bytes into IRAM scratch region
  → bpcRebootSystem()
      Atmosphère intercepts the reboot, detects staged payload in IRAM
      jumps to payload instead of normal HOS reboot target
  → CPU now running Hekate again
```

### Local implementation — qd_Power.cpp

`RebootToHekate()` in
`src/projects/uMenu/source/ul/menu/qdesktop/qd_Power.cpp` does:

1. `fopen("sdmc:/atmosphere/reboot_payload.bin", "rb")` — reads the Hekate binary
   that was placed on the SD during initial CFW setup.
2. `bpcamsInitialize()` — connects to Atmosphère's extended bpc service.
3. `bpcamsSetRebootPayload(payload_buf, payload_size)` — stages the binary in IRAM.
4. `bpcInitialize()` + `bpcRebootSystem()` — issues the reboot; Atmosphère
   intercepts and chains to the staged payload.

The IPC header lives at:
`src/libs/libnx-ext/libnx-ipcext/include/ipcext/bpcams.h`:
```c
Result bpcamsInitialize(void);
void   bpcamsExit(void);
Result bpcamsSetRebootPayload(void *payload_buf, size_t payload_size);
```

And the libstratosphere variant at:
`src/libs/Atmosphere-libs/libstratosphere/source/ams/ams_bpc.os.horizon.h`:
```c
Result amsBpcSetRebootPayload(const void *src, size_t src_size);
```

Both wrap the same underlying `bpc:ams` IPC command.

### Why action 6 in the hot-corner dropdown delegates to an NRO instead

`qd_Power.cpp::RebootToHekate()` — the direct `bpc:ams` path — failed silently on
this Switch's HOS 20.0.0 environment: `bpcamsSetRebootPayload` returned OK but the
subsequent reboot landed in stock firmware, not Hekate. Root cause was never fully
pinned (likely an IRAM staging timing issue on fw20). Tomvita's
`reboot_to_hekate.nro` executes the same `bpc:ams` chain but as a separate homebrew
process whose launch/teardown lifecycle is managed by the homebrew loader applet,
giving it a clean IPC surface without uMenu's applet-ownership interference.

The working implementation (commit `8a3bbe92`, branch
`fix-hekate-reboot-via-nro-delegate`, HW-confirmed 2026-05-16) in action 6:

```cpp
// qd_HotCornerRightDropdown.cpp FireAction(6):
smi::LaunchHomebrewLibraryApplet(
    std::string("sdmc:/switch/reboot_to_hekate.nro"), std::string(""));
if (g_MenuApplication) {
    g_MenuApplication->FadeOutToNonLibraryApplet();
    g_MenuApplication->Finalize();
}
```

Without `FadeOutToNonLibraryApplet` + `Finalize`, uMenu re-asserts foreground after
the SMI returns and kills the NRO before it can execute — a cycle-C1 pattern
documented in `qd_DesktopIcons.cpp:4869-4879`.

### UMS-specific path — RebootToHekateUms()

`RebootToHekateUms()` uses the same `bpc:ams` mechanism but first tries to stage
`sdmc:/bootloader/payloads/hekate_ums.bin` (a Hekate build that boots directly into
UMS mode via its `bootwait=0` + UMS autoboot entry), falling back to the generic
`sdmc:/atmosphere/reboot_payload.bin` if the UMS-specific binary is absent.

---

## 4. bpmp + BCT + boot.dat

### BPMP (Boot and Power Management Processor)

The T210 SoC contains an auxiliary Cortex-class microcontroller called the BPMP
(Boot and Power Management Processor). It handles:

- Clock/power rail sequencing before the main A57 cluster starts
- DRAM initialisation (training, ZQ calibration) using parameters from the BCT
- Thermal management and voltage scaling at runtime

The BPMP runs its own firmware blob embedded in Package1 / the BCT and is
initialised by Package1ldr during the very first boot stage. By the time Hekate (or
any custom bootloader) receives execution, the BPMP has already done its job and the
DRAM is up. Hekate can optionally set `bpmpclock` in `nyx.ini` to tune the BPMP
clock (auto, 589 MHz, or 408 MHz) for performance vs power, but this is a runtime
tuning knob, not a security-critical path.

**v3.1 relevance: none.** uMenu runs long after BPMP has been operational for the
entire HOS session. No v3.1 feature needs to interact with BPMP.

### BCT (Boot Configuration Table)

Source: switchbrew.org/wiki/BCT.

The BCT is a Tegra-standard data structure stored in eMMC boot partition 0 (four
copies: normal, backup, safe-mode variants). The boot ROM reads the BCT into IRAM
and uses it to find and load Package1. Key fields:

| Field | Role |
|---|---|
| `StartBlock` / `StartPage` | Where on eMMC the bootloader binary lives |
| `LoadAddress` | IRAM address to copy the bootloader to (typically `0x40010000`) |
| `EntryPoint` | Execution entry address within the loaded binary |
| `Length` | Bootloader binary size |
| `CustomerData` | Free-use region; Hekate reads a `boot_cfg` struct here to implement forced autoboot without modifying the rest of the BCT |

On Erista the BCT is signed from offset 0x510, which means the
`CustomerData` region before that offset can be modified freely without
re-signing. Hekate stores its boot-entry selection and autoboot flags in
`CustomerData` so it can force-autoboot into a specific entry even if the SD is
absent.

**v3.1 relevance: none.** BCT modification is not needed for any Q OS feature.
It is mentioned here for architecture diagram completeness.

### boot.dat

`boot.dat` is a Hekate-specific file on the SD card that contains the Hekate binary
itself in a format Hekate's payload launcher can read and boot directly. It is NOT a
standard Nintendo or Atmosphère file — it is a Hekate convention for its own payload
format. When `boot.dat` is present in `sdmc:/` (or a configured path), Hekate's UMS
payload or another Hekate instance can boot it without needing the RCM/fusée path.

On this Switch's SD card `boot.dat` was confirmed present during the 2026-05-16
session (UMS listing). It is the on-SD copy of `hekate_ctcaer.bin` in the
`boot.dat` wrapper. Its role: provides a recoverable boot path even if the SD's
partition table is intact but the fusée payload chain has changed.

**v3.1 relevance: none.** Q OS uMenu does not read or write `boot.dat`.

---

## 5. AutoRCM + fusee + hekate_ipl.ini configuration surface

### fusée-gelée / RCM injection

On unpatched Erista (T210) hardware, the USB Recovery Mode (RCM) can be exploited by
the Tegra boot ROM's failure to validate the size field of USB DFU control-transfer
data. Sending a crafted payload over USB while the device is in RCM copies arbitrary
code into IRAM and jumps to it. This is the `fusée-gelée` exploit
(github.com/Qyriad/fusee-launcher, browser port used on this device).

The workflow for this Switch: hold Vol+ at power-on (or jig short pin 10), connect
USB, run the browser-based injector, which pushes `hekate_ctcaer.bin`. Hekate boots.
This must be done every cold boot because AutoRCM is not in use (it is a hard-NO
per project rules — see `rules/erista-only-no-modchip-dev.md`; AutoRCM writes to
fuses/IRAM registers in a way that is not reversible and we avoid it).

### hekate_ipl.ini — launch surface

`sdmc:/bootloader/hekate_ipl.ini` is Hekate's boot-entry registry. Structure:

```ini
[config]
autoboot=0          ; 0=show Nyx, N=boot entry N automatically
autoboot_list=0     ; 0=read entries from this file, 1=read from ini/ folder
bootwait=3          ; seconds to show Nyx before autobooting (0-20)
backlight=100       ; screen brightness 0-255
bootprotect=0       ; prevent HOS from writing to bootloader/ folder
autonogc=1          ; auto-apply nogc patch (gamecard write protection)

[Atmosphere CFW]
fss0=atmosphere/package3
kip1=atmosphere/kips/*
logopath=bootloader/res/atmosphere.bmp
icon=bootloader/res/atmosphere_icon.bmp

[Stock OFW]
stock=1

[UMS SD card]
payload=bootloader/payloads/hekate_ums.bin
```

Key entry parameters relevant to Q OS:

| Key | Meaning |
|---|---|
| `fss0=` | Path to Atmosphère `package3` — tells Hekate to extract and chainload the full Atmosphère stack (exosphère + kernel patches + sysmodules). This is what boots Q OS. |
| `payload=` | Launch an external payload binary. Used for UMS-specific Hekate builds, Android, Linux, etc. Incompatible with `fss0` in the same entry. |
| `autoboot=N` | Bypass Nyx and jump directly to entry N. Setting this to the Atmosphère entry number causes Hekate to skip showing Nyx entirely — the user never sees the bootloader UI. |
| `bootwait=0` | Combined with `autoboot`, eliminates the countdown window. |
| `id=` | A 1-7 character identifier that Hekate stores in BCT `CustomerData`. External tools can request boot to a specific `id` without knowing the entry index. |

Our local reader `qd_HekateIni.cpp` parses this file to show the available boot
entries in the Q OS Settings layout, letting the user select a "default boot entry"
that would be used if autoboot were enabled.

---

## 6. Hekate UMS payload (mass-storage internals)

### What UMS is in Hekate

UMS (USB Mass Storage) is an in-process mode within Hekate — not a separate payload.
When the user navigates to **Tools → USB Tools → SD Card** in Nyx, the call chain is:

```
Nyx GUI button tap
  → action_ums_sd() in gui_tools.c
  → _create_mbox_ums(MMC_SD, ...)
  → usb_device_gadget_ums(config)   ← runs the USB gadget loop
```

`usb_device_gadget_ums` implements a full Bulk-Only Transport (BOT) USB mass storage
device using Hekate's own USB stack (bare-metal register writes to the Tegra XUSB
controller). It exposes the SD card's raw sectors as a SCSI block device. The
function runs a blocking event loop until one of:

- Safe Eject from the connected computer's OS
- USB cable removal
- User taps the "Stop UMS" button in the Nyx message box

After the UMS loop exits, Hekate restores the backlight and returns to the Nyx UI.
The SD card is never "unmounted" from Hekate's perspective in the Linux/macOS sense —
Hekate owns the hardware; it simply stops serving USB commands and resumes its own
FatFS access to the SD.

### Why Spotlight / macOS sometimes fights the unmount

This is entirely a macOS-side issue, confirmed during the 2026-05-16 session. When
macOS mounts the UMS volume, Spotlight indexes it in the background. On "Safe Eject",
macOS tries to flush its metadata write cache before the unmount — if Spotlight still
holds the volume open (common on first mount), macOS shows "disk not ejectable"
errors. The fix applied was a launchctl unload of the Spotlight indexer for the
volume before ejection. Hekate-side behavior is correct: it simply terminates the
USB session when it detects the host's Safe Remove signal at the BOT protocol level.
No Hekate change is needed or possible for this.

### Separate UMS payload (hekate_ums.bin)

For `RebootToHekateUms()` (our code, qd_Power.cpp), the SD contains an optional
`sdmc:/bootloader/payloads/hekate_ums.bin` — a Hekate build whose `hekate_ipl.ini`
inside the payload image has `autoboot=2` (or whatever entry index UMS is assigned)
and `bootwait=0`. This causes Hekate to jump straight into UMS mode on boot without
showing Nyx. The user's SD is exposed immediately; they do their file work; they
power cycle to return to normal operation.

This is a separate binary from the main `hekate_ctcaer.bin` / `reboot_payload.bin`.
It is not shipped with Hekate by default — the user must build or obtain a
pre-configured UMS-autoboot Hekate image. Our `RebootToHekateUms()` falls back to
the generic `reboot_payload.bin` if the UMS-specific binary is absent (which just
shows normal Nyx where the user can manually navigate to UMS).

---

## 7. Nyx ↔ Q OS integration opportunities

### Current flow

```
Q OS uMenu running
  → User taps hot-corner → "Reboot to Hekate"
  → smi::LaunchHomebrewLibraryApplet("sdmc:/switch/reboot_to_hekate.nro")
  → FadeOutToNonLibraryApplet() + Finalize()
  → NRO executes: bpcamsSetRebootPayload(reboot_payload.bin) + bpcRebootSystem()
  → Hekate boots, Nyx shows
  → User picks Atmosphère entry
  → HOS + Q OS uMenu boots again
```

Total round-trip: ~60-90 seconds (reboot + Atmosphère init + uMenu startup).

### Integration opportunity A: autoboot bypass (skip Nyx entirely)

Hekate supports `autoboot=N` in `[config]` with `bootwait=0`. If the user sets their
Atmosphère entry as the autoboot target, the Nyx UI never appears — Hekate boots
straight to Q OS in under 30 seconds after the payload is staged.

**How to configure:**
1. In `sdmc:/bootloader/hekate_ipl.ini`, find the index of the Atmosphère entry
   (count from 1, excluding the `[config]` section).
2. Set `autoboot=<N>` and `bootwait=0` in `[config]`.

**Our code surface:** `qd_HekateIni.cpp` already reads and parses `hekate_ipl.ini`.
A future Settings UI could offer a "Set as default boot entry" toggle that writes
back `autoboot=N` + `bootwait=0` to the ini file, making the next reboot-to-Hekate
seamless. This is a ~50-line feature: parse the existing file, update the `[config]`
block, write it back. The write requires `bootprotect=0` (the default).

**Hekate-side changes needed:** None — the `autoboot` mechanism is a standard Hekate
feature already documented in its README. We only write the ini file.

### Integration opportunity B: boot to specific entry by ID

Hekate's BCT `CustomerData` mechanism (the `id=` key in `hekate_ipl.ini`) allows
external software to write a target entry ID into the BCT before rebooting. Hekate
reads the ID on startup and boots the matching entry without showing Nyx.

**How it works:** The Hekate binary reads offset `BOOT_CFG_AUTOBOOT_EN` + `id`
fields from BCT `CustomerData`. A process with raw eMMC access (or a payload that
patches those bytes) can request a specific named entry. Atmosphère's `bpc:ams`
service does not expose this path — it only stages a raw payload. So using entry IDs
requires either a helper payload (a small Hekate shim that patches CustomerData then
chainloads the real Hekate) or a Hekate feature that reads a file from SD for the
boot target.

**v3.1 assessment:** Not worth pursuing. The `autoboot` ini approach (Opportunity A)
achieves the same goal with zero complexity overhead. ID-based booting is useful when
multiple CFW configurations exist on the same device; Q OS users typically have one
Atmosphère entry.

### Integration opportunity C: direct Atmosphère boot without Nyx shown

The cleanest possible integration: Hekate is configured with `autoboot=1 bootwait=0`
pointing at the Atmosphère entry. The user never sees Nyx. The "Reboot to Hekate"
action in the hot-corner dropdown effectively becomes "Reboot to Q OS" — the device
reboots and lands back in Q OS uMenu with no user interaction required.

**Hekate-side changes needed: None.**
**Our code changes needed: None** (beyond the optional ini-writer described in A).
**User configuration needed:** One-time edit of `hekate_ipl.ini` on the SD.

This is already achievable today with the current v2.3.7 build. It is a
configuration state, not a code change.

### Summary: does v3.1 require any Hekate-side changes?

**No.** Every integration point — reboot-to-Hekate, UMS mode, autoboot bypass,
status info display — is achievable through:

1. `bpc:ams` IPC (already implemented in qd_Power.cpp + working via NRO delegate)
2. SD card file reads/writes (`hekate_ipl.ini`, `reboot_payload.bin`)
3. Standard Hekate configuration keys (`autoboot`, `bootwait`, `id`)

Hekate is a well-bounded PRE-HOS tool. Q OS operates entirely POST-HOS. The
interface between them is intentionally thin: a payload file on the SD and one
`bpc:ams` IPC call. That is all v3.1 needs, and both are already working.

---

## Appendix: file map

| Path | Role |
|---|---|
| `src/libs/libnx-ext/libnx-ipcext/include/ipcext/bpcams.h` | bpc:ams IPC declarations used by our code |
| `src/libs/Atmosphere-libs/libstratosphere/source/ams/ams_bpc.os.horizon.h` | libstratosphere variant of the same IPC |
| `src/projects/uMenu/source/ul/menu/qdesktop/qd_Power.cpp` | Full RebootToHekate / RebootToHekateUms implementation |
| `src/projects/uMenu/source/ul/menu/qdesktop/qd_HotCornerRightDropdown.cpp` | Action 6 — NRO delegate path (v2.3.7 canonical) |
| `src/projects/uMenu/source/ul/menu/qdesktop/qd_HekateIni.cpp` | hekate_ipl.ini parser (read-only; used by Settings layout) |
| `docs/research/HEKATE-REBOOT-REGRESSION-20260506.md` | Post-mortem of why the direct bpc:ams path failed on fw20 |
| `docs/research/IPC-SESSION-POOL-EXHAUSTION-20260518.md` | The 11-day crash sequence root-cause (unrelated to Hekate directly) |
| `sdmc:/atmosphere/reboot_payload.bin` | Hekate binary staged by bpc:ams at reboot time |
| `sdmc:/bootloader/hekate_ipl.ini` | Hekate boot-entry configuration (user-managed) |
| `sdmc:/bootloader/payloads/hekate_ums.bin` | Optional: UMS-autoboot Hekate build |
| `sdmc:/switch/reboot_to_hekate.nro` | Tomvita's NRO; current canonical reboot-to-Hekate path |
