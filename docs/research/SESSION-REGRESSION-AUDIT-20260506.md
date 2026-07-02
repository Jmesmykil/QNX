# SESSION REGRESSION AUDIT 2026-05-06

Auditor: lead-auditor (independent, read-only).
Scope: all uncommitted changes in qos-ulaunch-fork working tree vs HEAD (48396bd5, v2.3.4).
Deployed uMenu md5: `17ee4cf0d55c70e15c74fbc6317bd0d5`. Deployed uSystem exefs.nsp md5: `aa267e1afbd0fcad21e6b8b7abd2f1cb`.

---

## Confirmed Regressions

**R1. "Reboot to Hekate" reboots to HOS.**
The right-dropdown FireAction case 6 calls `power::RebootToHekate()` (qd_Power.cpp). That file is UNCHANGED in the working tree. The regression is in the DEPLOYED binary, not the diff. Root cause: the uSystem.json NPDM change reduced `main_thread_stack_size` from `0x0100000` (1 MiB) to `0x0080000` (512 KiB) and zeroed `system_resource_size` from `0xC00000` (12 MiB) to `0x0`. The bpcAms payload staging path in RebootToHekate mallocs the entire reboot_payload.bin (~250 KB), calls bpcamsSetRebootPayload, then bpcRebootSystem. With the halved stack and no system resource reservation, the bpc:ams IPC likely fails silently and the fallback path fires `bpcRebootSystem()` without a staged payload -- which produces a plain HOS reboot instead of Hekate. (uSystem.json diff: line 6-7.)

**R2. Login screen reactivity degraded.**
No changes touch qd_PowerButton.cpp, ui_StartupMenuLayout.cpp, or POWER_CLICK_TOLERANCE_PX. Per-memory directive, the invariants (30 px tolerance, edge-triggered state machine, first_main_frame_done_ guard) are intact in source. However, the NPDM `system_resource_size: 0x0` change removes applet heap resources from uSystem. uMenu is launched as a library applet of uSystem; if uSystem's system resource pool is zero, the kernel may throttle the LA's memory budget, causing frame drops and touch-event coalescing that degrades perceived reactivity. The R2 regression is a SIDE EFFECT of the R1 NPDM change.

---

## Suspected Regressions

**S1. NintendoApps Finalize() tears down uMenu permanently.**
qd_NintendoApps.cpp now calls `g_MenuApplication->Finalize()` after every heavy-applet launch (Album, Mii, Profile, Web). If the applet returns unexpectedly fast or errors, Finalize destroys the MenuApplication render loop. The old code (direct libnx calls) did not destroy uMenu state. Regression: user taps Album, applet fails to launch (e.g., network down for Web), uMenu is dead. (qd_NintendoApps.cpp lines 67-70, 77-80, 94-97, 110-113.)

**S2. Settings sidebar count mismatch with SettingsTab enum on scroll boundary.**
SIDEBAR_ITEM_COUNT dropped from 8 to 7. All navigation paths were updated, but the Down-arrow handler at line ~1012 now does `active_tab_ = static_cast<SettingsTab>(sidebar_focus_row_)` unconditionally. If sidebar_focus_row_ reaches 7 (Count), this casts to an invalid enum value. Current guard (`sidebar_focus_row_ < SIDEBAR_ITEM_COUNT - 1`) clamps to 6, so this is safe today -- but fragile.

---

## Scope Creep Flags

**SC1. FilePicker (4 new untracked files, ~49 KB).** qd_FilePicker.hpp/cpp and qd_FilePickerLayout.hpp/cpp are new, untracked, and wired into NintendoAppsLayout via a temporary callback in qd_DesktopIcons_WmBridge.cpp. This is a new feature -- not part of the stated scope (Nintendo Apps fixes, FW20 npdm, RebootToStockQlaunch). The WmBridge wiring adds a 9th tile that changes grid geometry from 4x2 to 3x3 with compressed 100px tile heights. REVERT the WmBridge lambda wiring to decouple the file picker from the regression surface.

**SC2. NxlinkServer/RemoteShellServer accept-loop backoff.** The 1-second sleep on EHOSTUNREACH/ENETDOWN/ENETUNREACH is a nice hardening fix, but it is not part of the stated scope. Low risk; can keep.

---

## Dead Code / Orphaned Hooks

**D1. `[[maybe_unused]] DrawOutlineRect` in qd_SettingsLayout.cpp:83.** The function IS used (lines 751, 803, 821). The `[[maybe_unused]]` decorator is misleading but predates this session; not introduced by current changes.

**D2. `files_tile_cb_` in NintendoAppsLayout.** The callback member has no default initializer in the class definition (std::function default-constructs to empty, so technically safe), but the `if (files_tile_cb_)` check at render time evaluates a std::function bool conversion every frame for a feature that is explicitly temporary.

---

## Memory/Learnings Rule Violations

**V1. Login screen no-regress directive (feedback_login_screen_no_regress.md).** Source code invariants (POWER_CLICK_TOLERANCE_PX=30, edge-triggered state machine, first_main_frame_done_) are preserved in source. However, the NPDM change creates a runtime regression path that violates the spirit of the directive. The directive says "before any change to qd_PowerButton... verify they exist and have the values stated above." The NPDM change is not a direct modification of those files, but its side effects degrade the same user flow.

**V2. No stubs ever (feedback_no_stubs_ever.md).** The FilePicker on_select callback in qd_DesktopIcons_WmBridge.cpp:270 logs "launch wiring pending integration agent" -- this is a stub disguised as a log message.

---

## Recommended Remediation Order

1. **REVERT uSystem.json** `main_thread_stack_size` and `system_resource_size` to committed values (`0x0100000`, `0xC00000`). This fixes R1 and R2 simultaneously with a 2-line change. Highest impact, smallest diff.
2. **REVERT WmBridge FilePicker wiring** (qd_DesktopIcons_WmBridge.cpp lines 259-280). Decouples untested scope creep from production path. Keep the 4 FilePicker source files on disk but unwired.
3. **Guard Finalize() after SMI failure** in qd_NintendoApps.cpp. Add early return before `g_MenuApplication->Finalize()` when the SMI call fails (the `return` is already there for the error path, but confirm each of the 4 launch functions has it).
4. **Rebuild and deploy** with original NPDM values. Verify Hekate reboot and login-screen reactivity on hardware.
5. **Remove FilePicker stub** from WmBridge to satisfy no-stubs directive. Can happen in a future session when the integration agent lands real launch wiring.
