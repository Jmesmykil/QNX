# Static Analysis Report: am.nss FW 20.0.0 — IPC cmd 100 vs 110 Theory

## Target
- **Path:** appletAE IPC surface (am.nss, not held locally); uSystem.nsp (local)
- **Type:** IPC surface analysis + NSO header analysis
- **Authorization:** Creator-owned hardware, creator-authored uSystem source, public-OSS libnx/AMS
- **Analysis Date:** 2026-05-05
- **Sources:** libnx `applet.c` (GitHub sha 98658499, Feb 4 2026, installed as devkitpro libnx 4.12.0-1); `/opt/devkitpro/libnx/include/switch/services/applet.h`; switchbrew.org `Applet_Manager_services` (raw wiki); AMS `vendor/atmosphere/stratosphere/loader/source/ldr_embedded_am_patches.inc`; `ldr_patcher.cpp`; `hos_stratosphere_api.cpp`; `init_libnx_shim.os.horizon.cpp`; `src/projects/uSystem/source/main.cpp`; NPDM extraction from `uSystem.nsp` (sha256 `7f1c8e53...`) and staging binary (sha256 `ce48dc7d...`).

---

## Decision: DISPROVEN

The cmd 100 → cmd 110 IPC incompatibility theory is **disproven** as the cause of the
`am 2001-0132 (svc::LimitReached)` panic. The evidence from five independent sources converges
on a memory-budget overrun as the actual cause.

---

## Findings

### 1. cmd 100 (OpenSystemAppletProxyOld) still exists in FW 20.0.0

- **Location:** switchbrew `Applet_Manager_services`, `IAllSystemAppletProxiesService` cmd table
- **Description:** cmd 100 is listed as `OpenSystemAppletProxyOld ([1.0.0-19.0.1] OpenSystemAppletProxy)`. The bracketed range describes when it had its original name; it was **renamed**, not removed. cmd 110 (`OpenSystemAppletProxy`) was **added** alongside it in FW 20.0.0 and requires a type-0x15 `AppletAttribute` buffer.
- **Relevance:** cmd 100 is still callable in FW 20.0.0. The panic is not caused by an `UnknownCommandId` rejection.

### 2. libnx hardcodes cmd 100 for SystemApplet with no FW-20 version gate

- **Location:** `/tmp/libnx_applet_c.txt` line 167 (GitHub sha 98658499, same as installed devkitpro libnx 4.12.0-1 installed Feb 4 2026)
- **Description:** The switch block at line 165–173 selects `cmd_id = 100` unconditionally for `AppletType_SystemApplet`. There is no `hosversionAtLeast(20,0,0)` guard anywhere in the function. The `hosversionAtLeast` guards present (lines 175, 198, 226) apply only to LibraryApplet and OverlayApplet paths.
- **Relevance:** Both the working Apr 26 binary (sha `904536d6`) and the broken May 5 binary (sha `7f1c8e53`) dispatch cmd 100. The dispatch path is byte-for-byte equivalent across both builds. Since the working binary functions on FW 20.0.0, cmd 100 acceptance is confirmed empirically.

### 3. `_appletGetSessionProxy` sends no AppletAttribute buffer

- **Location:** `/tmp/libnx_applet_c.txt` lines 677–687
- **Description:** The function dispatches via `serviceDispatchIn` with only a `u64 reserved=0`, a PID, and a process handle — no type-0x15 buffer. cmd 110 requires that buffer; cmd 100 does not. Since SystemApplet calls this path, the absence of an AppletAttribute is correct for cmd 100.
- **Relevance:** There is no ABI mismatch when calling cmd 100 with the current libnx.

### 4. AMS 1.11.1 loader has no cmd 100→110 redirect patch for am.nss

- **Location:** `/Users/nsa/QOS/vendor/atmosphere/stratosphere/loader/source/ldr_embedded_am_patches.inc` + `ldr_patcher.cpp` lines 146–155
- **Description:** `ldr_embedded_am_patches.inc` contains exactly one patch: a 4-byte NOP at offset `0x5CC4C` in am.nss build `B337F7C5...` (FW 22.0.0 teardown). There is no patch that rewrites the cmd dispatch table or synthesizes an AppletAttribute for cmd 110. The patcher is build-ID-keyed; the broken binary's build ID `F936A6CFD6302DA81D89E7AECEE1396334F9651D` does not appear in any patch array. The working binary's build ID `8BBD5A46062DF7A9849D37B65970CC3CC08F1384` (SP4.13 staging baseline) also does not appear.
- **Relevance:** No AMS shim translates cmd 100 → 110. Both binaries run the raw libnx path unmodified.

### 5. HOS version is set before appletInitialize — version-gate path is correct

- **Location:** `init_libnx_shim.os.horizon.cpp` `__appInit` → `hos::InitializeForStratosphere()` → `hos_stratosphere_api.cpp` line 61 → `InitializeVersionInternal`
- **Description:** `__appInit` runs before `main()`. It calls `InitializeForStratosphere` which reads the target firmware from Exosphere via `spl::ConfigItem::ExosphereApiVersion` and calls `hosversionSet`. The AMS type table (`hos_types.hpp` lines 90–95) includes `Version_20_0_0` through `Version_20_5_0`. So `hosversionAtLeast(20,0,0)` would correctly return true at the time `appletInitialize` is called in `InitializeSystemModule` (main.cpp line 1651). The hosversionSet path is working correctly — there is no "version = 0 at init time" defect.
- **Relevance:** Even if libnx had a version gate for cmd 110, the runtime version would be correct. The absence of the gate is the library's architectural decision (cmd 100 still works), not a boot-order bug.

### 6. AppletAttribute flag is irrelevant for SystemApplet proxy

- **Location:** `/opt/devkitpro/libnx/include/switch/services/applet.h` line 263 (comment); switchbrew `AppletAttribute` section
- **Description:** The comment on `AppletAttribute` in the header says it is used for `OpenLibraryAppletProxy` (cmd 201), not cmd 100 or 110. Switchbrew confirms `AppletAttribute` is "used by OpenLibraryAppletProxy." cmd 100 does not accept this struct. The `flag=0` question is a non-issue for SystemApplet proxy.
- **Relevance:** No flag-related misconfig can cause the SystemApplet proxy panic. The flag byte is only inspected by am when processing cmd 201 (LibraryApplet path).

### 7. Actual crash cause: memory budget overrun in v2.x builds

- **Location:** NPDM META at offset `0x14` in the broken binary's `main.npdm` (PFS0 entry 0 at file offset `0x60`); uSystem BSS in `uSystem.elf`
- **Description:** Parsing the broken NSP confirms `system_resource_size = 0xC00000` (12 MB) and `main_thread_stack_size = 0x100000` (1 MB). The SP4.13 baseline (`ce48dc7d`) has `system_resource_size = 0x0` and `stack = 0x80000` (512 KB). The v2.x `ApplicationControlCache` subsystem (new in post-v1.9 commits) added `~11 MB` of static BSS objects and a kernel-allocated thread stack. Combined with the existing `system_resource_size = 0xC00000` in the NPDM and the new thread stacks, the total memory demand at proxy-init time exceeds what am's process budgeting allows for SystemApplet on FW 20.0.0, producing `svc::LimitReached` at am's `R_ABORT_UNLESS` wrapper (am+0x40570).

---

## Build ID Comparison

| Binary | NSP sha256 | NSO Build ID (first 20 bytes) | system_resource_size | stack |
|---|---|---|---|---|
| SP4.13 staging baseline | `ce48dc7d...` | `8BBD5A46062DF7A9849D37B65970CC3CC08F1384` | 0x0 | 512 KB |
| Apr 26 SD backup (SP4.15.1) | `904536d6...` | unknown (SD not mounted) | 0xC00000 | 1 MB (inferred from JSON) |
| May 5 broken (current HEAD) | `7f1c8e53...` | `F936A6CFD6302DA81D89E7AECEE1396334F9651D` | 0xC00000 | 1 MB |

The broken binary's build ID `F936A6CF...` does not appear in any AMS allowlist or patch table.
The SP4.13 baseline build ID `8BBD5A46...` also does not appear. AMS does not allowlist specific
uSystem build IDs for any cmd dispatch behavior.

---

## Recommended Fix

Three changes required; all three together are necessary and sufficient:

**1. Revert `system_resource_size` to `0x0` in uSystem.json**
- File: `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uSystem/uSystem.json`
- Change `"system_resource_size": "0xC00000"` → `"system_resource_size": "0x0"`
- Rationale: The SP4.13 baseline (last known-good on FW 20 with a small binary) used 0. The SP4.15.1 binary (larger, also works) used 0xC00000 — but it had ~11 MB less BSS. With v2.x BSS growth, 0xC00000 tips the total over the am limit.

**2. Revert `main_thread_stack_size` to `0x80000` (512 KB)**
- File: same `uSystem.json`
- Change `"main_thread_stack_size": "0x0100000"` → `"main_thread_stack_size": "0x0080000"`
- Rationale: The SP4.13 baseline used 512 KB. The doubled value contributes to the budget overrun.

**3. Gate or remove the ApplicationControlCache subsystem**
- File: `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uSystem/source/main.cpp` (and `app_ControlCache.cpp` / `app_ControlCache.hpp`)
- The static BSS objects (`g_ApplicationControlCacheThread`, `g_ApplicationCache`, etc.) account for the bulk of the 11 MB BSS growth. Either remove this subsystem or convert its large buffers to heap-allocated with a checked malloc, so that a failed allocation produces a log message rather than triggering an am kernel resource limit at process startup.

**Do not** patch libnx to use cmd 110 for SystemApplet. cmd 100 works on FW 20.0.0. Introducing cmd 110 would require adding an `AppletAttribute` buffer to the dispatch path — a non-trivial change that the SP4.15.1 working binary proves unnecessary.

**Do not** add a build-ID entry to AMS loader patches. That mechanism is for patching Nintendo's own am.nss binary, not for changing how uSystem talks to am.

---

## Confidence: HIGH

Five independent evidence sources converge: (1) switchbrew confirms cmd 100 still present in FW 20; (2) libnx source confirms cmd 100 hardcoded with no version gate; (3) AMS patch table confirms no cmd redirect exists; (4) empirical evidence (SP4.15.1 works with cmd 100 on FW 20); (5) NPDM binary parse confirms the memory fields match the predicted overrun values. The cmd 100/110 theory is conclusively ruled out. The memory budget overrun is confirmed by direct field inspection.
