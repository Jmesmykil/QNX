# AMS 1.11.x Master + FW 20.0.0 Compatibility — Crash Investigation

**Authored:** 2026-05-05
**Crash signature under investigation:** `am` (`0100000000000023`) panics with **User Break, reason 0, Result 0x10801 (2001-0132)** at offset `am + 0x40570`, LR `am + 0x2318`, ~92 s into boot. Module hash reported by fatal: `E3722DA984D9ED4ADBFEBF8F4F9B1050996F3FC3`.
**Active stack:** Atmosphère `1.11.1-master-d04c20a04` + EmuMMC (RAW1, sector 0x7990000) + sys-patch + nx-ovlloader 2.0.0 + sys-con 0.6.5 + sys-clk 2.0.1 + uSystem (uLaunch fork) overriding qlaunch at `atmosphere/contents/0100000000001000/exefs.nsp`.
**Bisect to date:** crash gone with all three of {sys-clk, sys-con, nx-ovlloader} disabled; sys-clk alone disabled does not cure → suspect lies in nx-ovlloader, sys-con, or in the qlaunch override interacting with the upstream am lifecycle change (see §4 and §7).

---

## 1. Decoding `2001-0132` / `0x10801`

| Field | Value | Source |
|---|---|---|
| Encoded result | `0x10801` | switchbrew Error_codes table |
| Module | 1 (KERNEL) | `2001` prefix = (module + 2000) per Atmosphère convention |
| Description | 132 | suffix |
| Symbolic name | **`KernelError_LimitReached`** (a.k.a. `Result.LimitReached`) | switchbrew |
| Switchbrew row | `0x10801 \| 1 \| 132 \| LimitReached` | https://switchbrew.org/wiki/Error_codes |

**Important framing.** The user-facing fatal screen says “User Break, reason 0”. That is **not** the result code — it is the panic *mechanism*: the process called `svcBreak(BreakReason_Panic, ...)` with the result `0x10801` as the payload. So the chain is: am received `LimitReached` from a kernel SVC, decided this was unrecoverable, called `svcBreak`, which Atmosphère’s fatal handler captured and rendered.

`KernelError_LimitReached` is what the kernel returns when a per-process resource limit is hit — most commonly handles, sessions, threads, or transfer-memory pages exhausted in that process. In `am` specifically, the resources actually constrained at runtime are session handles to `IApplicationProxy` / `ILibraryAppletProxy` and event handles bound to applet exit signalling. (This is consistent with the 1.11.0 changelog text: *“Closing down service handles or domains … now triggers a cleanup operation which in turn signals an event that qlaunch is waiting on.”* — see §4.) No public Atmosphère/libstratosphere source defines a friendlier `am`-specific alias for `0x10801`; libnx’s `applet.h` has no constant for it either, confirming this is the raw kernel result bubbling up.

Sources:
- https://switchbrew.org/wiki/Error_codes (LimitReached row, kernel module 1)
- https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/applet.h (no `am`-side alias for 132)

---

## 2. AMS commit `d04c20a04` identification

| Field | Value |
|---|---|
| Full SHA | `d04c20a049ca5190ddba84e8117abcbb5720b3ff` |
| Author | hexkyz `<mike.hexkyz@gmail.com>` |
| Date (UTC) | 2026-04-07 16:24:31 |
| Parent | `252f8685b493d0dfd428e9439b0296109776b935` |
| Subject | “git subrepo push libraries” (subrepo merge `82f1553c4` from Atmosphere-libs) |
| Files changed | `libraries/.gitrepo` (+2 / −2) |
| Tag | **`1.11.1`** points to this exact SHA (lightweight tag; this commit IS 1.11.1) |
| Branch | `master` |
| Superseded? | Yes — newer master commits exist (`022000f` docs changelog 1.11.1 same day; `e655fd4` 2026-04-24; `2c7e2bf` 2026-04-25), but `1.11.1` itself has not been re-tagged |

So `1.11.1-master-d04c20a04` **is** the 1.11.1 stable release — it is *not* a between-releases dev build, it is the tip-of-tag commit. The “master-” prefix in Atmosphère build strings is the build-system default, not a “use master at your own risk” warning.

Sources:
- https://api.github.com/repos/Atmosphere-NX/Atmosphere/commits/d04c20a04
- https://api.github.com/repos/Atmosphere-NX/Atmosphere/git/refs/tags/1.11.1
- https://sourceforge.net/projects/atmosph-re.mirror/files/1.11.1/atmosphere-1.11.1-master-d04c20a04+hbl-2.4.5+hbmenu-3.6.1.zip/download (mirror filename confirms tag↔SHA binding)

---

## 3. AMS 1.11.x release notes — FW 20 vs FW 22 (CRITICAL CORRECTION)

The task brief says “Switch HOS 20.0.0”. The Atmosphère 1.11.x line **does not target FW 20.x — it targets FW 22.x.** FW 20.x support sits in the AMS 1.9.x line. Concretely:

| AMS release | Date | FW added |
|---|---|---|
| 1.9.1 | 2025-05-29 | 20.1.0 |
| 1.9.2 | 2025-07-16 | 20.2.0 |
| 1.9.3 | 2025-07-30 | 20.3.0 |
| 1.9.4 | 2025-09-03 | 20.4.0 |
| 1.9.5 | 2025-09-30 | 20.5.0 |
| 1.10.0-prerelease | 2025-11-15 | 21.0.0 |
| 1.10.1 | 2025-12-09 | 21.1.0 |
| 1.10.2 | 2026-01-14 | 21.2.0 |
| **1.11.0** | **2026-04-03** | **22.0.0** |
| **1.11.1** | **2026-04-07** | **22.1.0** |

So the running stack is `AMS 1.11.1` (which expects ≥22.0.0) on what the brief reports as `HOS 20.0.0`. That is **not a supported pairing** — AMS 1.11.x changes assume the FW 22.0.0 `am` ABI. Either the system was not actually updated to 22.x (FW mismatch is itself a top crash candidate), or the FW string in the brief is stale/wrong.

Sources:
- https://github.com/Atmosphere-NX/Atmosphere/releases
- https://api.github.com/repos/Atmosphere-NX/Atmosphere/releases (tags 1.11.0, 1.11.1, 1.10.x, 1.9.x)
- https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/changelog.md

---

## 4. The 1.11.0 `am` lifecycle break — root cause class

Verbatim from the official 1.11.0 changelog:

> “A change in how applications/applets’ lifespan is managed broke the homebrew ecosystem. All applications and applets are expected to perform a clean exit by calling the relevant IPC commands. … Closing down service handles or domains (specifically for ILibraryAppletProxy or IApplicationProxy) now triggers a cleanup operation which in turn signals an event that qlaunch is waiting on.”

> “As a temporary solution, patches to the `am` sysmodule are now included which allow restoring the previous behavior and regain homebrew compatibility without any further changes.”

This patch is exactly commit **`93a82c0441b0014cb96770eae05767b8fb92d6f5`** by hexkyz, 2026-04-02 — message: *“loader: patch am to recover homebrew compatibility. This patch is to be removed if/once hbmenu/libnx re-designs the exiting logic.”*

What the patch actually does:

| File | Change |
|---|---|
| `stratosphere/loader/source/ldr_embedded_am_patches.inc` (new) | One entry: build ID `B337F7C5AD53E55F3D920FCD394B4DE7C3B7D606`, offset `0x5CC4C`, bytes `\x1F\x20\x03\xD5` (= ARM64 `NOP`), comment “Patch teardown function call to NOP” |
| `stratosphere/loader/source/ldr_patcher.cpp` | Loop: for each entry whose `module_id` matches the loaded am NSO, `memcpy` the bytes at the offset. **No firmware-version guard, no fallback build ID — strict module-hash equality only.** |

**Decisive observation for this crash.** The fatal report’s `am` build hash is `E3722DA984D9ED4ADBFEBF8F4F9B1050996F3FC3`. The AMS 1.11.0/1.11.1 patch only matches `B337F7C5AD53E55F3D920FCD394B4DE7C3B7D606`. Therefore on this device **the homebrew-compat NOP is never applied**: the teardown-call-that-signals-qlaunch fires unchecked, and any qlaunch override (uLaunch / uSystem at `0100000000001000`) that doesn’t implement the new clean-exit contract races with `am`’s session teardown until a per-process resource limit is hit → `0x10801 LimitReached` → `svcBreak`.

Sources:
- https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/changelog.md
- https://api.github.com/repos/Atmosphere-NX/Atmosphere/commits/93a82c0
- https://raw.githubusercontent.com/Atmosphere-NX/Atmosphere/master/stratosphere/loader/source/ldr_embedded_am_patches.inc
- https://raw.githubusercontent.com/Atmosphere-NX/Atmosphere/master/stratosphere/loader/source/ldr_patcher.cpp
- Issue #2746 (22.0.0 Support checklist): hexkyz tracker line *“Investigate and fix homebrew↔am communication”* — confirms this is a known unsolved area: https://github.com/Atmosphere-NX/Atmosphere/issues/2746

---

## 5. Known crash signatures: `2001-0132 / 0x10801` against `am`

| Issue | FW | AMS | Crashed module | Status / note |
|---|---|---|---|---|
| Atmosphere-NX/Atmosphere #2601 | 20.3.0 | 1.9.3-master-8b8e4438e | `hid` (`0100000000000013`) | Closed. Reporter Valou5231, 2025-08-01. emuMMC + Hekate. Same result code, **different victim** (hid not am). |
| Atmosphere-NX/Atmosphere #2708 | unknown FW (system non-bootable) | unknown | unknown — boot-time crash | Closed. sleepycatsonlawn, 2026-01-06. Triggered by adding sys-botbase contents folder; resolved by removing third-party sysmodule. **Same class: third-party content under `atmosphere/contents/` → `0x10801`.** |
| WerWolv/nx-ovlloader #42 | 20.2.0 | 1.9.2 | **am (`0100000000000023`)** | Open. Exact match for victim module + result code, on a *prior* AMS line; nx-ovlloader was implicated. Maintainer comments not retrievable from public mirror. |
| Atmosphere-NX/Atmosphere #2751 | 22.0.0 | 1.11.0 (self-built) | hbmenu NRO launch crash | Open. syngmail, 2026-03-21. emuMMC + SysNAND both affected. Symptom class identical to the 1.11.0 lifecycle break described in §4. |

The `WerWolv/nx-ovlloader#42` row is the closest historical match (am crash, 0x10801, third-party sysmodule visible) — and it sat unresolved against AMS 1.9.x. There is no evidence either nx-ovlloader 2.0.0 (ppkantorski fork, released 2025-11-26) or sys-con 0.6.5 has been re-validated against AMS 1.11.x.

Sources:
- https://github.com/Atmosphere-NX/Atmosphere/issues/2601
- https://github.com/Atmosphere-NX/Atmosphere/issues/2708
- https://github.com/WerWolv/nx-ovlloader/issues/42
- https://github.com/Atmosphere-NX/Atmosphere/issues/2751
- https://github.com/cathery/sys-con/issues (newest open issues all stop at FW 20.x / AMS 1.9.x — no AMS 1.11.x validation report)
- https://github.com/ppkantorski/nx-ovlloader/releases (v2.0.0 dated 2025-11-26, predates AMS 1.11.0 by ~4 months — no FW 22 / AMS 1.11 release notes)
- https://github.com/retronx-team/sys-clk/releases (latest 2.0.1, predates AMS 1.11.x — only re-noted as “binaries reuploaded for HOS 21.0.0”)

---

## 6. AMS master-vs-stable for production

There is no SciresM warning that *“running master will brick.”* Atmosphère’s tag policy is: each release is itself a master commit annotated with a lightweight tag; the “1.11.1-master-d04c20a04” build string is therefore identical to the 1.11.1 stable. SciresM has historically warned only against `AutoRCM` on patched units (https://x.com/sciresm/status/1117956835456061440) — not against master builds in general. No public source identifies any `1.11.x` commit as “do not flash.”

What the changelogs *do* warn about, in 1.11.0 and 1.11.1 verbatim: *“Please be sure to update fusee when upgrading to 1.11.1. fusee-primary no longer exists, and will not work any more.”* Stale fusee on a Mariko Hekate chain can produce its own boot-time fatals — though this would not normally manifest as a process-level am `0x10801`.

Sources:
- https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/changelog.md
- https://x.com/sciresm/status/1117956835456061440

---

## 7. emuMMC + AMS 1.11.x quirks

No public issue tracker entry singles out emuMMC sector-mount or RAW1 partition issues against AMS 1.11.x. Issue #2751 (§5) explicitly states *“Both emuMMC and SysNAND affected equally”*, ruling emuMMC out as the unique trigger for the 1.11.0 lifecycle break. The `cathery/sys-con` and `retronx-team/sys-clk` issue trackers contain no FW 22 / AMS 1.11 emuMMC reports as of this research.

Source:
- https://github.com/Atmosphere-NX/Atmosphere/issues/2751

---

## 8. Conflicting / unresolved data

- **Build hash mismatch.** Brief says FW 20.0.0; AMS 1.11.x is FW 22.x-only; the am build ID in the fatal (`E3722DA9…`) does not match the 1.11.0 patch target (`B337F7C5…`). One of these three facts is wrong, and the answer changes the diagnosis. No public lookup table maps either hash → FW version; the question cannot be settled from web sources alone — it has to be settled on-device by reading `system/Contents/registered/.../am.nsp` and the Hekate `bootloader/ini` FW string.
- **Issue #2708, #2751 maintainer comments** were not retrievable due to the public GitHub HTML being JS-rendered for some pages; the comment threads may contain a fix that this research does not capture.
- **GBAtemp threads** (`alulas-atmosphere-1-11-0-fw-22-0-0`, `atmosphere-v1-11-1-released-…`, `new-atmosphere-version-22-1-0-error`) refused fetch with HTTP 403 (anti-scrape). They are reachable in a real browser and worth a manual pass for first-hand qlaunch-override + uLaunch reports.

---

## Most likely cause

The fatal is the AMS 1.11.0 `am` lifecycle change (commit `93a82c0`) firing on a build of `am` that AMS does **not** recognise (`E3722DA9…` ≠ the patched `B337F7C5…`), so the homebrew-compat NOP is never applied. The custom uSystem qlaunch override at `0100000000001000` does not perform the new clean-exit IPC contract, so the un-NOP'd teardown path in `am` keeps signalling qlaunch and re-allocating session/event handles until a per-process kernel limit is hit — kernel returns `0x10801 LimitReached`, am calls `svcBreak`, fatal screen reports `2001-0132` at the panic site `am+0x40570`. nx-ovlloader 2.0.0 and sys-con 0.6.5 likely amplify (extra applet sessions, IPC traffic), which is why disabling all three calms the crash even though the root provoker is the qlaunch override × unpatched am pairing. sys-patch is not implicated.

## Concrete next bisect step

Boot AMS 1.11.1 with **only** the uSystem qlaunch override at `atmosphere/contents/0100000000001000/exefs.nsp` enabled and **all three** of {sys-clk, sys-con, nx-ovlloader} removed; if the crash still reproduces, the qlaunch override is confirmed as the primary trigger and the fix is to either (a) port uSystem to issue the new `am` clean-exit IPC, or (b) fork `ldr_embedded_am_patches.inc` to add the device’s actual `am` build ID `E3722DA984D9ED4ADBFEBF8F4F9B1050996F3FC3` with the same `\x1F\x20\x03\xD5` NOP at the equivalent teardown-call offset, and rebuild AMS loader.
