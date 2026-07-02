# Static Analysis Report: Login Screen Reactivity Audit

## Target
- **Path:** `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uMenu/`
- **Type:** Repository — C++ source (Horizon/Switch uMenu)
- **Authorization:** Owned
- **Analysis Date:** 2026-05-06

---

## Three Invariants — Current State

**1. `POWER_CLICK_TOLERANCE_PX = 30`**
INTACT. `qd_PowerButton.cpp:224` — `static constexpr s32 POWER_CLICK_TOLERANCE_PX = 30;`. Used correctly at line 291 via squared-distance check.

**2. Edge-triggered PowerButton state machine**
INTACT. `qd_PowerButton.cpp:269–301` — TouchDown sets `press_inside_` once; TouchMove branch at line 279 explicitly tracks position only, does not flip `press_inside_`; TouchUp at line 283 fires on cumulative distance check. The broken per-frame pattern is absent.

**3. `first_main_frame_done_` welcomer guard**
INTACT. `ui_MainMenuLayout.cpp:1461–1462` — guard fires in `OnMenuUpdate`, not `Initialize`. `Initialize` contains a comment at line 1692–1694 explicitly documenting why the call was moved. No duplicate `IsOpen()/Render()` block re-added.

---

## Regression Source

**File:line:** `qd_DevTools.cpp:128` — `nxlinkConnectToHost(true, true)`

**How it starves input:** `TryEnableNxlink()` calls `nxlinkConnectToHost()` on the **main UI thread**. The SDK documents an internal ~2 second broadcast timeout when no host is listening (the common case on a switch without a PC running nxlink). This call is wired as the `SetOnClick` handler of `qd_btn_nxlink` in `ui_StartupMenuLayout.cpp:221` — the login screen's dev-tools button row. When any user taps that button, the main loop blocks for up to 2 seconds. During that window, `OnMenuInput` and `OnMenuUpdate` do not execute, so **all power buttons on the login screen appear unresponsive**.

The two server files (`qd_NxlinkServer.cpp`, `qd_RemoteShellServer.cpp`) are **not the culprit**: both use `threadCreate` + `threadStart` (lines 625/639 and 303/317 respectively) and their `svcSleepThread(1'000'000'000)` calls execute inside `ThreadEntry`, which is the worker thread entry point. Those sleeps never touch the main thread.

`qd_SettingsLayout.cpp` has no sleeps, no mutexes, and no reference to the login/startup screen path.

`qd_HotCornerDropdown.cpp` has no blocking calls; input dispatch is synchronous and non-blocking.

---

## Fix

In `qd_DevTools.cpp`, move `nxlinkConnectToHost` off the main thread. The minimal correct fix:

**File:** `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uMenu/source/ul/menu/qdesktop/qd_DevTools.cpp`

Replace the body of `TryEnableNxlink()` so the blocking `nxlinkConnectToHost` call is dispatched to a one-shot detached thread (same `threadCreate`/`threadStart` pattern already used by `QdNxlinkServer::Start`). The function should return immediately (optimistically `true` or a pending state), and set `g_nxlink_fd` atomically from the worker when the host responds.

Alternatively, if the button is only intended to start the nxlink **server** (not the ad-hoc broadcast client), replace the `ui_StartupMenuLayout.cpp:221` call site to invoke `dev::TryEnableNxlinkServer()` instead of `dev::TryEnableNxlink()` — `TryEnableNxlinkServer()` calls `g_NxlinkServer.Start()` which is already worker-threaded and returns in microseconds.

---

## Confidence

High — the call chain is unambiguous: button OnClick -> `TryEnableNxlink()` -> `nxlinkConnectToHost()` (blocking, ~2 s timeout) -> main loop stalls.

---

## Adjacent Risks

- `qd_btn_usbserial->SetOnClick` (same file, line ~226) calls `TryEnableUsbSerial()`. If that function also performs a synchronous I/O init on the main thread, it carries the same starvation risk. Audit `qd_DevTools.cpp:TryEnableUsbSerial()` before closing this bug.
- The `HotCornerRightDropdown.cpp:589/601` call sites invoke `TryEnableNxlinkServer` and `TryEnableRemoteShell` (already worker-threaded, safe). They are unaffected.
- Any other `SetOnClick` handlers that call into `qd_DevTools` from main-thread layouts should be audited for blocking I/O.
