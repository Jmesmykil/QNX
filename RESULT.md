# uLaunch Fork — Hardware Results (updated 2026-04-25T08:00Z)

## Deployed on hardware — confirmed

| Sub-port | Status | Build hash | Notes |
|---|---|---|---|
| **SP1** — Cold Plasma Cascade wallpaper | ✅ **SHIPPED on hardware** | `fd7682e4` | 1280×720 native, SDL bilinear-scales to 1920×1080. STREAMING texture + LockTexture (no host alloc). First-pixel `040409FF` = design spec. |
| **SP2** — GPU-pool fix | ✅ **BUILT, staged** | `0c2de111` | Texture 8 MB → 3.5 MB to fit Switch GPU pool. 45/45 host tests PASS. |
| **SP3** — Input pump (libnx 4.x PadState API) | ✅ **BUILT, staged** | `539af96a` | `pump_input` + `multitouch_classify` compiled in. Inert until SP5 wires dispatch. 88/88 host tests PASS. |
| **SP3.1** — QDESKTOP_MODE layout ownership | ✅ **VERIFIED on hardware** | build-green | Upstream icon ring suppressed. `-DQDESKTOP_MODE` in Makefile. 14 method guards. Wallpaper renders at hardware boot. |
| **SP3.2** — Cursor + top bar + touch remap | ✅ **BUILT** (hw pending) | build-green | `qd_Cursor.hpp/cpp` 114+172 LOC. Top bar: time/date/battery/connection refresh per frame. Touch scale 3/2 correct for HID→layout. |
| **Telemetry v0.21** | ✅ **BUILT** (hw pending) | build-green | RingFile 4×512KB, fdatasync on WARN, boot-seq counter. `ul::tel::Init/Shutdown` wired in main.cpp. 11 log strings embedded. |
| **SP3.3** — Icon population | 🔄 **IN FLIGHT** | build-green | `IconKind` discriminator + `SetApplicationEntries` + NCA icon load. Agent ace69bc8 completing. |

## Current build artifact on SD

uMenu `main` at `/ulaunch/bin/uMenu/main` — SP3 build hash `539af96a`, 6,825,979 bytes. uSystem: upstream XorTroll v1.2.0 `776cb9f7` (required — fork uSystem NPDM rejected by fw 20.0.0).

## NPDM finding (resolved)

Fork `uMenu.json` had `system_resource_size: 0x3200000` (50 MB) — invalid for applet class. Fixed to `0x0`. Patched at `projects/uMenu/uMenu.json:11`. NPDM hash `c3ed33dd`.

---

# Audit Pipeline and Architecture Readiness Report (original — preserved below)

## Readiness Status
The `uMenu` audit pipeline and the `uSystem` SMI protocol have been analyzed. Configuration updates have been applied to respect the strict memory constraints:
- **`uSystem` (Daemon)**: Updated `uSystem.json` to explicitly use `pool_partition: 2` (System) and set `"system_resource_size": "0xE00000"` (14MB). Furthermore, the internal `LibstratosphereHeapSize` allocation in `source/main.cpp` was reduced from 20MB to 10MB to comfortably fit within this system pool constraint without crashing on boot.
- **`uMenu` (GUI)**: Updated `uMenu.json` to use `pool_partition: 1` (Applet pool) and configured `"system_resource_size": "0x3200000"` (50MB) to match the standard Album applet memory limit.

## Required Architecture Changes

### 1. In-Memory Icon and NACP Caching
According to the `cur-changelog.md` and current codebase, `uSystem` attempts to store NACP metadata and app icon data entirely in memory. This causes the daemon's memory footprint to inflate to around 40MB (assuming 200 installed games). 
**Requirement**: To permanently respect the 14MB qlaunch pool constraint, the caching architecture must be redesigned. `uSystem` must transition to streaming icons and metadata directly from disk (SD card) on-demand or implement a strict, size-bounded Least-Recently-Used (LRU) cache that guarantees the total application memory usage never exceeds 14MB.

### 2. Unbounded SMI Message Queue (IPC)
The `uSystem` SMI protocol implementation currently utilizes an unbounded `std::queue<ul::smi::MenuMessageContext>` (`g_MenuMessageQueue`) for asynchronous IPC communication with `uMenu`. `MenuMessageContext` contains large data structures.
**Requirement**: If `uMenu` crashes, becomes unresponsive, or is suspended for an extended period, background threads in `uSystem` (such as `ApplicationVerifyMain` and `EventManagerMain`) will continue pushing messages onto this queue. This unbounded growth will rapidly exhaust the remaining memory in the 14MB `qlaunch` pool, leading to a system-wide crash. The queue must be replaced with a bounded circular buffer, implement strict backpressure, or utilize a pre-allocated shared memory IPC mechanism to guarantee O(1) memory complexity during message passing.