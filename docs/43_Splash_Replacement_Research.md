# 43 — Splash Replacement Research

**Author:** Research agent (2026-04-24)
**Scope:** Q OS branding at boot — two splash intercept points in the Hekate → Atmosphère → Horizon chain
**Constraint:** No signed firmware (BIS, package1, package2) modification. No pirated/leaked Nintendo assets.

---

## 1. Atmosphère Splash — Replacement Procedure

### What it is

The Atmosphère splash is a full-screen image displayed by Atmosphère's `boot` system-module
reimplementation (`stratosphere/boot`) during early Horizon initialisation. It is **not** the
Hekate boot logo (covered separately below). It is controlled entirely by Atmosphère and lives
on the SD card or inside the `package3` payload file.

### How the splash is stored and displayed

Atmosphère's `boot` sysmodule calls `ShowSplashScreen()` defined in
[`stratosphere/boot/source/boot_splash_screen.cpp`](https://github.com/Atmosphere-NX/Atmosphere/blob/master/stratosphere/boot/source/boot_splash_screen.cpp).
The function behaviour is:

1. Query the boot reason via `GetBootReason()`.
2. If the reason is `spl::BootReason_AcOk` (AC adapter plugged in) **or**
   `spl::BootReason_RtcAlarm2` (RTC alarm wake), return immediately — no splash shown.
3. Otherwise: call `InitializeDisplay()`, render the image for exactly two seconds
   (`os::SleepThread(TimeSpan::FromSeconds(2))`), then `FinalizeDisplay()`.

The default image is compiled-in via
[`stratosphere/boot/source/boot_splash_screen_notext.inc`](https://github.com/Atmosphere-NX/Atmosphere/blob/master/stratosphere/boot/source/boot_splash_screen_notext.inc),
which declares:

```cpp
constexpr size_t SplashScreenX = 535;
constexpr size_t SplashScreenY = 274;
constexpr size_t SplashScreenW = 210;
constexpr size_t SplashScreenH = 172;
constexpr u32 SplashScreen[] = { … };   // ARGB32 pixel data
```

This means the default splash is a **210 × 172 pixel** Atmosphère logo centred at (535, 274) on
the 1280 × 720 display. It is fully replaced when a custom image is injected into `package3`.

### Replacement: the `insert_splash_screen.py` script

Atmosphère ships a Python utility at
[`utilities/insert_splash_screen.py`](https://github.com/Atmosphere-NX/Atmosphere/blob/master/utilities/insert_splash_screen.py)
that writes a custom image into the `package3` binary on the SD card.

**Image format requirements (from the script source):**

| Property | Requirement |
|----------|-------------|
| Dimensions | 1280 × 720 **or** 720 × 1280 (auto-rotated to 720 × 1280 if given as 1280 × 720) |
| Colour depth | Any format PIL can open; converted internally to RGBA |
| Pixel encoding written into `package3` | BGRA, little-endian (bytes: B, G, R, A) |
| Stride | 768 pixels (192 bytes of zero-padding appended to each 720-pixel row) |

**Validated binary layout inside `package3`:**

| Field | Value |
|-------|-------|
| File magic | `PK31` at byte 0 |
| Total file size | 0x800000 (8 MB) |
| Splash data offset | 0x400000 |
| Splash data length | 0x3C0000 (3,932,160 bytes = 768 stride × 1280 rows × 4 bytes/pixel) |

**SD card target path:**

```
SD:/atmosphere/package3
```

As confirmed by the official
[configurations.md](https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/features/configurations.md),
the custom splash is activated by running:

```
python utilities/insert_splash_screen.py <your-1280x720-image> <SD:/atmosphere/package3>
```

The image source can be any PIL-compatible file (PNG recommended for lossless colour).

### When the Atmosphère splash is active vs. suppressed

The Atmosphère splash inside `package3` is the splash shown when Hekate loads Atmosphère via
**FSS0** (the default Q OS boot path: Hekate reads and executes components out of `package3`
without chainloading a separate `fusee.bin`). Under FSS0, **Hekate's own boot logo displays
first**, then Atmosphère's `boot` sysmodule shows the `package3` splash for two seconds before
Horizon's `boot2` launches services.

When Atmosphère is loaded via the older **fusee-payload chainload** (`payload` mode in
`hekate_ipl.ini`), the `package3` splash is still respected; only the order and duration of the
Hekate logo phase differs.

The splash is **skipped entirely** if the device woke from AC insertion (`BootReason_AcOk`) or an
RTC alarm (`BootReason_RtcAlarm2`). No configuration option exists to suppress or override this
skip; it is a hard-coded path in `boot_splash_screen.cpp`.

### How to test

1. Prepare a 1280 × 720 PNG at full quality.
2. Mount the SD card on macOS or Linux.
3. Run `python utilities/insert_splash_screen.py splash.png /Volumes/SWITCH/atmosphere/package3`.
4. Re-insert the SD card and cold-boot the Switch (hold Power, not AC-plug). The Q OS image
   should appear for two seconds before the Horizon home screen loads.
5. To confirm injection succeeded, run the same script in reverse (read back the BGRA data block
   at offset 0x400000) and visually compare.

### Hekate boot logo (distinct from Atmosphère splash)

Hekate displays its own logo **before** Atmosphère starts. It is controlled by:

- **Per-boot-entry override:** `logopath=SD:/path/to/custom.bmp` in the relevant `[config]`
  block of `SD:/bootloader/hekate_ipl.ini`.
- **Global fallback:** `SD:/bootloader/bootlogo.bmp` (applied to all boot entries that lack
  `logopath=`).
- **Built-in default:** compiled-in Hekate logo shown if neither file exists.

File format per
[`README_BOOTLOGO.md`](https://github.com/CTCaer/hekate/blob/master/README_BOOTLOGO.md):

| Property | Requirement |
|----------|-------------|
| Format | 32-bit ARGB BMP only (24-bit RGB BMP is not supported) |
| Maximum dimensions | 720 × 1280 pixels |
| Authoring orientation | Create in landscape (1280 × 720), rotate 90° counter-clockwise before saving |
| Background fill | Derived automatically from the first pixel of the image |

There is no script required; the BMP is read at boot time by Hekate's `bootloader/gfx/` display
code. The default Hekate logo is BLZ-compressed and compiled into the `hekate_ctcaer_*.bin`
payload, but it is entirely replaced by a SD-card BMP when one is present.

**Q OS implication:** placing `SD:/bootloader/bootlogo.bmp` (32-bit ARGB, landscape-authored,
rotated 90° CCW before saving) gives Q OS branding at the very first visual frame — before
Atmosphère even runs.

---

## 2. Nintendo Switch Logo — Feasibility

### What "the Nintendo Switch logo" is

After Atmosphère's `boot` sysmodule exits and `boot2` starts system services, the Applet Manager
(`am`, title ID `0100000000000023`) takes over display initialisation and renders a red
**"Nintendo Switch"** wordmark on a white background. The SwitchBrew wiki
([AM\_services](https://switchbrew.org/wiki/AM_services)) confirms that the `am` NCA contains
multiple raw embedded images including `NN_OMM_CHARGING_BIN_{begin|end}` (charging icons), a
low-battery icon, **and the Nintendo Switch logo** displayed during system boot. These are raw
binary blobs embedded directly in the `am` NSO executable, not in a separate resource NCA.

### Can it be replaced without touching signed firmware?

**Yes, via Atmosphère's ExeFS NSO patch mechanism — with important caveats.**

Atmosphère's `ldr` (loader) sysmodule
([`docs/components/modules/loader.md`](https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/components/modules/loader.md))
applies IPS or IPS32 patch files to NSOs before execution. Patch files are placed at:

```
SD:/atmosphere/exefs_patches/<patchset-name>/<NSO-build-id>.ips
```

Because the NSO build ID is globally unique per firmware version, patches are version-specific.
The `am` NSO is loaded from the system's installed firmware — a signed NCA in the BIS NAND
`System` partition — but the NSO itself is **not signed at the binary level** in a way that
prevents the loader from patching the decompressed in-memory image. Atmosphère `ldr` decompresses
the NSO, applies any matching `.ips` patches, then executes the patched image. No BIS write is
involved; the NAND is never touched.

The tool **switch-logo-patcher** by friedkeenan
([`friedkeenan/switch-logo-patcher`](https://github.com/friedkeenan/switch-logo-patcher)) automates
this exactly. It generates per-firmware-version `.ips` files that patch the logo pixels embedded
in the `am` (Applet Manager) and `vi` (Display services) NSOs. The patches target both modules
because two separate code paths can render the logo depending on firmware version. The script
enforces that the replacement image is exactly **308 × 350 pixels** (same as the original
embedded logo), taking input as any PIL-compatible format (PNG, BMP, etc.).

The resulting `.ips` files are placed under:

```
SD:/atmosphere/exefs_patches/boot_logo/<NSO-build-id>.ips
```

and loaded by Atmosphère `ldr` on next boot without any firmware modification.

### Why this is NOT a signed-firmware modification

The BIS `System` partition holds the `am` and `vi` NCAs encrypted and signed. Atmosphère does
**not** decrypt, modify, or re-sign those NCA files. Instead, when `ldr` maps the `am` NSO into
memory for execution, it intercepts that load, applies the patch bytes in RAM, and runs the
patched image. The BIS partition is read-only from the OS perspective throughout this path.
This is the same mechanism used by every other Atmosphère IPS patch (e.g., sigpatches, ES
patches). It requires no hardware write access and is fully reversible by removing the `.ips`
files from the SD card.

### Caveats and risks

1. **Firmware-version coupling.** Each IPS file targets one specific NSO build ID. When Nintendo
   releases a new firmware update, the `am`/`vi` NSOs change, their build IDs change, and any
   existing logo patches stop applying (the new firmware renders the stock logo, not the custom
   one). The patch set must be regenerated for every firmware version.

2. **Script requires firmware-specific dump.** `switch-logo-patcher` can generate patches without
   the original logo (each patch is 400+ KiB in that case), but providing the exact original logo
   bytes from a firmware dump produces minimal-size diff patches. Most users do not need to dump
   firmware; the script ships with pre-extracted build IDs for all known firmware versions up to
   the time of the script's last update.

3. **Size constraint.** The replacement image must be exactly 308 × 350 pixels. No scaling is
   applied. Designing to that canvas is a hard requirement.

4. **No official Atmosphère support.** The `boot_logo` patch mechanism is a community convention,
   not an Atmosphère first-party feature. Atmosphère's loader honours the exefs\_patches
   convention generically; the folder name `boot_logo` is a community-chosen patchset name with
   no special status.

5. **Safe boot / maintenance mode.** In safe-mode boot paths the full `am` sysmodule stack may
   not launch normally. The Q OS logo would not appear in those edge cases regardless.

### Verdict

**Replacing the Nintendo Switch boot logo via IPS exefs patches: Yes, fully feasible without
touching signed firmware.** The Atmosphère loader intercepts the NSO load in RAM. The mechanism
is well-proven, used in production by the Switch CFW community. The only ongoing cost is
regenerating patches per firmware version.

---

## 3. Recommended Q OS Path

Q OS should brand **both** visible splash points without touching signed firmware. At the Hekate
stage, place a 32-bit ARGB BMP at `SD:/bootloader/bootlogo.bmp` (authored in landscape
1280 × 720, rotated 90° counter-clockwise before saving as BMP) showing the Q OS wordmark or
cosmic-purple icon against a dark background; this is the first thing a user sees on cold boot.
At the Atmosphère stage, inject a 1280 × 720 PNG asset into `SD:/atmosphere/package3` using
`utilities/insert_splash_screen.py`; this two-second display bridges the gap between Hekate
handing off and Horizon bringing up the Home Menu (uMenu). For the Nintendo Switch red logo, use
`switch-logo-patcher` to generate firmware-specific IPS patches (308 × 350 px Q OS mark) placed
at `SD:/atmosphere/exefs_patches/boot_logo/` — this replaces the Nintendo wordmark entirely
without writing to BIS. Asset specifications the design team must produce: (a) a 1280 × 720 PNG
for the Atmosphère `package3` splash, (b) a 1280 × 720 BMP in 32-bit ARGB with the first pixel
set to the desired background colour for the Hekate logo, and (c) a 308 × 350 PNG for the
Nintendo Switch logo replacement IPS patch. In the `qos-ulaunch-fork` repository, add a
`tools/splash/` directory containing the three reference assets and a `Makefile` target (or shell
script) that calls `insert_splash_screen.py` and `gen_patches.py` so the full splash set can be
rebuilt whenever assets change or a new firmware version requires fresh IPS patches.

---

## 4. Sources

### Atmosphère source files

- **Splash screen logic (ShowSplashScreen, skip conditions, 2-second timer):**
  `https://github.com/Atmosphere-NX/Atmosphere/blob/master/stratosphere/boot/source/boot_splash_screen.cpp`

- **No-text splash data (embedded dimensions SplashScreenX/Y/W/H, ARGB32 array):**
  `https://github.com/Atmosphere-NX/Atmosphere/blob/master/stratosphere/boot/source/boot_splash_screen_notext.inc`

- **Text-variant splash data (with version string overlay, same dimensional structure):**
  `https://github.com/Atmosphere-NX/Atmosphere/blob/master/stratosphere/boot/source/boot_splash_screen_text.inc`

- **insert\_splash\_screen.py (format spec, stride, package3 layout, BMP→BGRA conversion):**
  `https://github.com/Atmosphere-NX/Atmosphere/blob/master/utilities/insert_splash_screen.py`

- **configurations.md (official custom splash documentation, SD path, command):**
  `https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/features/configurations.md`

- **boot.md (boot sysmodule description confirming black-and-white default splash):**
  `https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/components/modules/boot.md`

- **loader.md (NSO patching mechanism, /atmosphere/exefs\_patches/ directory spec, build-id naming):**
  `https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/components/modules/loader.md`

- **changelog.md (version 1.0.0 entry: BMP parsing removed, insert\_splash\_screen.py added):**
  `https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/changelog.md`

- **img/splash.png (reference Atmosphère splash asset, 512 × 512 for documentation):**
  `https://github.com/Atmosphere-NX/Atmosphere/blob/master/img/splash.png`

### Hekate source and documentation

- **README\_BOOTLOGO.md (format: 32-bit ARGB BMP, max 720 × 1280, SD paths, rotation requirement):**
  `https://github.com/CTCaer/hekate/blob/master/README_BOOTLOGO.md`

- **logos.c (BLZ-compressed embedded fallback logo, proof that BMP on SD supersedes compiled-in logo):**
  `https://github.com/CTCaer/hekate/blob/master/bootloader/gfx/logos.c`

- **Hekate bootlogo BMP reference assets:**
  `https://github.com/CTCaer/hekate/tree/master/res/bootlogo`

### Nintendo Switch logo replacement

- **switch-logo-patcher (gen\_patches.py, 308 × 350 px constraint, am/vi NSO build IDs, IPS output):**
  `https://github.com/friedkeenan/switch-logo-patcher`

- **gen\_patches.py source (patch\_info dictionary with am and vi NSO build IDs per firmware version):**
  `https://github.com/friedkeenan/switch-logo-patcher/blob/master/gen_patches.py`

### SwitchBrew wiki

- **AM services (confirmation that am NCA contains embedded boot logo as raw image data):**
  `https://switchbrew.org/wiki/AM_services`

- **Boot sysmodule (boot module role: hardware init, BCT validation, calls boot2):**
  `https://switchbrew.org/wiki/Boot`

- **Title list (am = 0100000000000023, vi = 010000000000002E):**
  `https://switchbrew.org/wiki/Title_list`

### Community guides

- **Hekate + Atmosphère splash guide (FSS0 vs fusee timing, BGRA format detail, alpha channel requirement):**
  `https://blog.northwestw.in/p/2023/11/18/hekate-atmosphere-splash-screen-guide`
