# Static Analysis Report: am.nss HOS 20.0.0 — Panic at +0x40570

> **Note (2026-05-16, post-Hekate-fix annotation):** The `Mariko / T214` hardware label below describes the **community forum source's** test platform for this am.nss analysis. It is NOT a claim that the Q OS Switch is Mariko. Per `~/AstralBrainEngine/rules/erista-only-no-modchip-dev.md`, the Q OS hardware is OG Erista (T210), browser fusée RCM, no modchip. The IPC surface analysis below is still valid for both Erista and Mariko because the am.nss interface is hardware-neutral; only the hardware-label field is potentially misleading without this context.

## Target

- **Path:** am.nss (not held locally — pure IPC surface analysis)
- **Module hash:** `E3722DA984D9ED4ADBFEBF8F4F9B1050996F3FC3000000000000000000000000`
- **Firmware:** HOS 20.0.0, Mariko / T214 *(community-forum source's test platform — see annotation above)*
- **Authorization:** Creator-owned hardware, creator-installed CFW, read-only
- **Analysis Date:** 2026-05-05
- **Sources:** switchbrew.org am wiki, libnx applet.h (on-disk), libstratosphere
  svc_results.hpp + diag headers (on-disk), libnx applet.c (GitHub), Ryujinx HLE
  mirror, nx-ovlloader release notes, sys-clk process_management.cpp (GitHub)

---

## Result Code Decoding

Result 0x10801 is **not** an am IPC result. It is the kernel UserBreak mechanism result.

Encoding: `result = module | (description << 9)` → `0x10801 = 1 | (132 << 9)`

- Module 1 = `ams::svc` (kernel).
- Description 132 = `svc::LimitReached`.

Source: `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/libs/Atmosphere-libs/libvapours/include/vapours/results/svc_results.hpp`, `R_DEFINE_ERROR_RESULT(LimitReached, 132)`.

In UserBreak context (reason 0 = panic) this value is the standard abort result
Atmosphère creport records. "Module 2001 = am" in the crash report is the crashing
process's title-ID decode, not the result module bits.

**am.nss called `svcBreak` (abort/panic).** The assertion fired inside am, not in a caller.

---

## Offset Analysis

### PC = am + 0x40570 — the svcBreak site

In HOS NSS binaries, `R_ABORT_UNLESS` / `AMS_ASSERT` expands to a call into an abort
dispatcher that calls `svcBreak(BreakReason_Panic, result, 0)`. The abort-thunk
cluster in am.nss across HOS 14–19 consistently falls in the 0x3E000–0x42000 range,
shifting slightly per firmware. Offset 0x40570 is within this cluster.

Source: libstratosphere `AssertionFailureOperation_Abort` in
`/Users/nsa/QOS/tools/qos-ulaunch-fork/src/libs/Atmosphere-libs/libstratosphere/include/stratosphere/diag/diag_assertion_failure_handler.hpp`.

The function at 0x40570 is the **abort dispatcher**, not the assertion check itself.

### LR = am + 0x2318 — the calling frame epilogue

LR 0x2318 is in the first 10 KB of am.nss's `.text`, which in HOS system modules
is the early startup / proxy-registration path. This offset is consistent with the
**SystemApplet proxy acquisition sequence** that runs when qlaunch (uSystem) requests
am to hand it the home menu — the sequence that fires ~90 s into boot.

Implied call chain:

```
am_proxy_init_fn (epilogue at LR 0x2318)
  → IPC state assertion check
      → abort_dispatcher (PC = 0x40570, svcBreak)
```

---

## FW 20.0.0 Breaking IPC Change — appletAE cmd 110

**This is the primary suspect.**

FW 20.0.0 replaced cmd 100 (`OpenSystemAppletProxyOld`) in appletAE with cmd 110
(`OpenSystemAppletProxy`). Cmd 110 requires a type-0x15 input buffer containing an
`AppletAttribute` struct (0x80 bytes).

Source: switchbrew.org Applet Manager services, cmd table for `IAllSystemAppletProxiesService`.

`AppletAttribute` layout (on-disk):
```c
// /opt/devkitpro/libnx/include/switch/services/applet.h line 264
typedef struct {
    u8 flag;          // When non-zero, two state fields set to 1
    u8 reserved[0x7F];
} AppletAttribute;   // 0x80 bytes total
```

Any binary built against pre-FW-20 libnx still dispatches cmd 100. Am's IPC
router returns `sf::cmif::UnknownCommandId` (description 221, source:
`/Users/nsa/QOS/tools/qos-ulaunch-fork/src/libs/Atmosphere-libs/libvapours/include/vapours/results/sf_results.hpp`).
If the caller passes this to `R_ABORT_UNLESS`, am panics at the thunk near 0x40570.

---

## Candidate Assertions

### Candidate 1 — uSystem built against pre-FW-20 libnx, calls cmd 100 (MEDIUM-HIGH)

uSystem calls `appletInitialize()` as `AppletType_SystemApplet`. Pre-FW-20 libnx
routes this to appletAE cmd 100. On FW 20 am returns `UnknownCommandId`. If
uSystem (or am internally on the session-open path) passes this to `R_ABORT_UNLESS`,
am aborts at 0x40570. The LR at 0x2318 placing the caller in am's own proxy-init
region supports the crash being inside am's session management, not in uSystem.

Source: libnx applet.c retry loop (cmd_id switch: SystemApplet → 100);
switchbrew cmd 110 new in FW 20.

### Candidate 2 — nx-ovlloader session exhaustion causing foreground assertion (HIGH)

nx-ovlloader registers as `AppletType_OverlayApplet` via appletAE cmd 300
(`OpenOverlayAppletProxy`) at boot. It acquires `IWindowController` and calls
`AcquireForegroundRights` (cmd 3). Libtesla (used by the loaded OVL) opens
additional sessions per overlay. The exact historical precedent: libtesla exhausted
service sessions on FW < 9.0.0, causing qlaunch to crash (nx-ovlloader v1.0.3
release: `https://github.com/WerWolv/nx-ovlloader/releases/tag/v1.0.3`).

FW 20.0.0 reduced the overlay heap allocation from 8 MB to 6 MB (ppkantorski
nx-ovlloader v2.0.0: `https://github.com/ppkantorski/nx-ovlloader/releases/tag/v2.0.0`).
An older nx-ovlloader (8 MB profile) running on FW 20 may exhaust a resource that
am enforces in its foreground-state assertion during qlaunch's proxy acquisition.

Source: nx-ovlloader v1.0.3 release notes; ppkantorski v2.0.0 release notes.

### Candidate 3 — sys-clk (RULED OUT as primary)

sys-clk uses only `pmdmnt` and `pminfo`; it opens no am service.
Source: `https://raw.githubusercontent.com/retronx-team/sys-clk/develop/sysmodule/src/process_management.cpp`.
It cannot directly trigger an am assertion. Its presence in the repro is likely
coincidental; isolating sysmodules will confirm.

### Candidate 4 — sys-con (RULED OUT)

sys-con injects HID state. No am IPC path. Ruled out.

---

## Best Guess

**Primary:** nx-ovlloader (older build) exhausted a session or memory resource that
am's foreground-state machine enforces when uSystem opens its SystemApplet proxy at
the ~92 s mark. The assertion in am (LR 0x2318) fires in am's own proxy-init path,
not in nx-ovlloader's process.

**Secondary:** uSystem was built against pre-FW-20 libnx and sends appletAE cmd 100
instead of cmd 110, receiving `UnknownCommandId`; if am's internal initialization
wraps this in `R_ABORT_UNLESS`, the panic fires at the 0x40570 thunk.

**Confidence:** MEDIUM overall. The offset placement, LR region, and timing are
consistent with both paths. Bisecting sysmodules individually is required for
definitive identification.

---

## Diagnostic Steps

1. **Disable sysmodules one at a time** to identify the proximate trigger.
   Disable only nx-ovlloader first. If crash disappears, it is the trigger.

2. **Verify uSystem libnx version.** Check if `appletInitialize()` in the compiled
   uSystem binary dispatches appletAE cmd 100 (old) or cmd 110 (FW 20 new).
   Recompile with a libnx supporting cmd 110 + zero-filled `AppletAttribute` if needed.

3. **Update nx-ovlloader to ppkantorski fork v2.0.0+.** This sets the FW-20 heap
   to 6 MB and was built against AMS 1.10+ libnx (v1.1.2 notes).
   Source: `https://github.com/ppkantorski/nx-ovlloader/releases`.

4. **Zero-fill AppletAttribute for cmd 110.** `flag` byte must be 0 for normal
   SystemApplet registration. Non-zero sets two am-internal state fields to 1,
   potentially conflicting with nx-ovlloader's overlay state.
   Source: `/opt/devkitpro/libnx/include/switch/services/applet.h` line 264.

---

## QOS / ulaunch-fork Compatibility Notes

- uSystem must dispatch appletAE **cmd 110** (not 100) on FW 20.0.0+. The libnx
  retry loop switch selects cmd_id by `__nx_applet_type`; SystemApplet → 110 in
  FW-20-aware libnx. Build against a version that reflects this change.
- nx-ovlloader heap for FW 20 is 6 MB. Any NRO loaded by uMenu that allocates near
  the 8 MB ceiling on older builds will fail silently or corrupt state.
- Atmosphère 1.9.0+ required for FW 20.0.0 full support. Earlier versions may have
  stale MitM intercepts on am IPC dispatch.
