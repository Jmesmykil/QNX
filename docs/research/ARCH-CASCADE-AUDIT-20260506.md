# Static Analysis Report: Q OS uMenu/uSystem Cascade-Regression Architecture Audit

## Target
- **Path:** `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uMenu/`, `uSystem/`, `libs/uCommon/`
- **Type:** Embedded C++ source (Nintendo Switch system applet + library applet)
- **Authorization:** Creator-owned fork of public-OSS uLaunch
- **Analysis Date:** 2026-05-06

---

## Six Structural Risk Categories

### 1. Process-wide mutable globals with unconstrained consumer set

**Pattern:** File-scope `extern` globals shared implicitly across every translation unit. Two objects dominate: `g_GlobalSettings` (declared in `uMenu/source/main.cpp:17`) and `g_MenuApplication` (declared at `uMenu/source/main.cpp:153`). Every layout and element declares `extern` re-references to both.

**Consumer count (measured):** `g_GlobalSettings` is consumed by 17 translation units. `g_MenuApplication` is consumed by 29 translation units, spanning all ui/, qdesktop/, am/, and smi/ subdirectories. Additionally, three telemetry tick globals (`g_qos_boot_t_appinit_ticks`, `g_qos_boot_t_main_ticks`, `g_qos_boot_t_premain_ticks`) are declared file-scope in `main.cpp:22-24` and re-externed in `qd_DesktopIcons.cpp:72-73`.

**Past regressions traced here:** Login screen reactivity regressions. `g_GlobalSettings.system_status` carries the selected user, current lockscreen state, and menu path. Any caller that reads it stale or writes it out of sequence produces wrong boot-path selection in `ui_LockscreenMenuLayout.cpp:11-16` and `ui_IMenuLayout.cpp:OnFinishedSleep`. The first-main-frame welcomer guard lives on `g_MenuApplication`'s loaded_menu flag (`ui_MenuApplication.hpp:103`); a race between `LoadMenu()` and the SMI `OnMessage` dispatcher touching the same flag has caused repeated welcome-screen double-trigger.

**Mitigation:** Encapsulate `g_GlobalSettings` behind a `GetGlobalSettings()` accessor with a `const &` return. Treat the `system_status` sub-struct as write-once at boot; do not let `LaunchMenu()` / `UpdateStatus()` in uSystem and `InitializeSettings()` / `MainLoop()` in uMenu write to overlapping fields simultaneously without explicit lifecycle ownership.

---

### 2. Unversioned, positional SMI binary protocol with implicit enum-as-ABI

**Pattern:** `smi_Protocol.hpp` defines two C++ `enum class : u32` types — `SystemMessage` (26 values, line 64–96) and `MenuMessage` (11 values, line 17–36). The wire encoding is a bare `u32` cast of the enum value embedded in a `CommandCommonHeader.val` field. No version field, no magic beyond `0x21494D53`. Adding an enum entry at the tail is safe; inserting, reordering, or removing entries shifts every downstream ordinal. Each new command requires five coordinated edits: enum entry, `smi_Commands.hpp` inline, `smi_Protocol.cpp` `ReceiveCommand` dispatch branch, `uSystem/source/main.cpp` `HandleMenuMessage` switch (~line 767–1090), and the matching push-function in the same switch's writer lambda (~line 1044–1089).

**Consumer coupling:** uSystem and uMenu are built separately and deployed to SD card independently. There is no shared version handshake: if a developer deploys only a new uMenu NSO without rebuilding uSystem (or vice versa), enum ordinals in the deployed binary no longer match the running peer. The protocol has no capability negotiation. The `HomeLongRequest` comment in `smi_Protocol.hpp:29-34` documents this risk explicitly ("wire-protocol enum value MUST stay at the tail") but relies on developer discipline, not enforcement.

**Past regressions traced here:** Every SMI command addition (album hijack, Hekate reboot, OpenControllerKeyRemapping, HomeLongRequest) was a five-touch change with no automated check that uSystem and uMenu stay in sync.

**Mitigation:** Add a static `constexpr u32 kProtocolVersion` field to `CommandCommonHeader` and assert equality on receive. Even a 1-byte version field caught at first message would catch mismatched partial deploys before they cause behavior corruption.

---

### 3. NPDM system_resource_size as a shared pool knob with cross-component side-effects

**Pattern:** uSystem's `system_resource_size` is declared in `uSystem.json:7` as `0x400000` (4 MB). uMenu's is `0x0` (`uMenu.json:11`). The `system_resource_size` field allocates from the system's `ResourceLimit` pool for use by the process's memory mapper. When uSystem holds a system resource reservation, the kernel enforces that pool against all other library applets it launches — including uMenu. Reducing or zeroing the uSystem reservation releases pool back to the global kernel limit but can starve AMS proxy-init paths that expect certain ResourceLimit headroom. Increasing it claims from the same pool that bpc:ams and other sysmodules draw from.

**Past regressions traced here:** The `0xC00000 → 0x0 → 0x400000` history (referenced in the audit brief) maps directly to this: the zero-drop made AMS proxy-init fail; the 0x400000 restore satisfied proxy-init but at the cost of reduced headroom for bpc:ams on FW20. The uSystem comment at `main.cpp:197-201` acknowledges the tuning was hardware-validated only at 20 MB libstratosphere heap; the analogous tuning exists implicitly for system_resource_size.

**Mitigation:** Document the derivation of `0x400000` in the JSON file itself as a comment block. Add a build-time assertion or CI check that `system_resource_size` is not changed without an accompanying entry in a tracked changelog. Never change this value without a hardware-boot test.

---

### 4. Render hot-path blocking via SDL texture allocation and SetText on every frame

**Pattern:** `IMenuLayout::OnMenuUpdate` is registered via `AddRenderCallback` (`ui_IMenuLayout.cpp:359`) and fires every frame on the main thread. Inside it, `UpdateBatteryTextAndTopIcons`, `UpdateTimeText`, and `UpdateDateText` are called per layout, each guarded by a change-detection branch. The battery path calls `MakeBatteryIcon()` (`ui_IMenuLayout.cpp:81-154`) which allocates a `std::vector<u8>` on the heap, fills it, then calls `SDL_CreateTexture` + `SDL_UpdateTexture`. The connection icon path does the same via `MakeConnectionIcon()`. Both calls are inside `if(state_changed)` guards, so the common case (no change) is cheap. However `SetText()` inside Plutonium triggers a `TTF_RenderUTF8_Blended`-equivalent path on the Plutonium side when the text changes; this is not guarded by the same cached-state discipline across all callers.

**Past regressions traced here:** The login-screen reactivity regression described in the audit brief was exactly a `SetText`/TTF path called per-frame. The current `UpdateTimeText` and `UpdateBatteryTextAndTopIcons` have change guards, but any new qdesktop layout that calls `SetText` inside `OnMenuInput` or `OnMenuUpdate` without a staleness check will reproduce the regression immediately.

**Mitigation:** Codify a rule: no `SetText`, no texture allocation, and no `SDL_CreateTexture` inside any function called from `AddRenderCallback` unless wrapped in a `last_value != cur_value` guard. A static analyzer grep for `SetText` calls inside `*Update*` or `*Render*` methods can catch violations at review time.

---

### 5. AMS/Horizon API surface assumption drift under firmware revision

**Pattern:** uSystem makes direct `serviceDispatchOut` calls against `appletGetServiceSession_CommonStateGetter()` with hardcoded command ID 5 (`main.cpp:666`) — a raw IPC call because libnx does not expose this particular call. This is a known workaround (`// Thank you so much libnx...`). The `hosversionAtLeast` guards (`main.cpp:1362`, `1418`, `1428`) protect against old firmware, but there is no upper-bound guard for FW20+ tightening. The AMS submodule is not pinned to a git hash in the Makefile — it is a subtree on disk, and the build log (`BUILD-v0.2.0.log:step5`) shows that even at the time of the v0.2.0 baseline attempt, the Atmosphere-libs ABI was already broken (`DebugEventInfo*` conversion error). The `svc_stratosphere_shims.hpp:391` failure proves that the AMS library boundary is fragile against AMS version drift even with no source changes on the Q OS side.

**Past regressions traced here:** Hekate reboot regressions trace here indirectly: `appletRequestToReboot()` behavior changed across FW releases, and `spsmShutdown` was added then removed because it linked an unwanted ~22 KB of spsm machinery that caused a black-screen boot regression (documented in `main.cpp:1003-1006`). Each time an AMS or libnx API was touched to fix a regression, a new assumption boundary was hit.

**Mitigation:** Pin the Atmosphere-libs subtree to a specific commit hash in the Makefile and enforce it with a `git submodule status` check in CI. For each raw `serviceDispatchOut` call, add a comment with the FW version range it was validated against. Add `hosversionAtMost` guards around any raw IPC that is known to break above a specific firmware.

---

### 6. Non-deterministic build environment producing different binaries from identical source

**Pattern:** The Makefile (`uMenu/Makefile:10`) sources `$(DEVKITPRO)/libnx/switch_rules` at build time with no version pin. `BUILD.md:15` says "pinned to version uLaunch requires — see UPSTREAM-ANALYSIS.md" but the install instruction is `sudo dkp-pacman -S libnx` with no version specifier. devkitPro's rolling pacman repository can update libnx, the gcc cross-compiler, or the ABI headers between builds without any explicit action. The build log confirms the v0.2.0 baseline used `devkitA64 15.2.0` and `libnx 4.12.0-1`. If those packages have since been superseded, a `make` today produces a binary that differs in link order, LTO inlining decisions, or ABI struct padding from the binary tested on hardware on Apr 26.

**Consequence of non-determinism:** The previously-confirmed case where `git commit 9246dc1b` produced a different binary on two separate dates with no source change is this pattern. Any regression observed only on one developer's machine is a build-environment regression, not a code regression, and cannot be diagnosed from source alone.

**Mitigation:** Pin `libnx` and `switch-dev` to exact pacman package versions (e.g., `sudo dkp-pacman -U libnx-4.12.0-1-any.pkg.tar.zst`). Check the installed version in the Makefile's `all:` prerequisite and fail with a readable error if mismatched. Long-term: a Dockerfile or Nix flake that locks the entire cross-compilation toolchain to a single reproducible state.

---

## Ranked Top 3 Highest-Risk Patterns

### #1: Unversioned SMI binary protocol (Risk 2)

This is the highest-risk single pattern because it is the only one that can corrupt behavior silently at runtime with no crash, no assert, and no log. Every other risk class produces a visible failure (crash, IPC error, black screen). A mismatched SMI enum causes one subsystem to interpret another's message payload as a different command entirely. The album hijack taking 5+ deploy cycles is almost certainly this: a uSystem and uMenu built at different enum ordinals, where LaunchApplication and OpenAlbum swapped positions between deploys.

**Architectural change that eliminates the risk class:** Replace the bare `u32 val` enum cast with a versioned header: `struct CommandCommonHeader { u32 magic; u32 protocol_version; u32 cmd_id; }`. Assert `protocol_version` equality on every receive, and make the version a compile-time constant generated from the enum hash or member count. A mismatch aborts immediately with a clear error log entry instead of executing the wrong action.

### #2: Process-wide mutable globals with 29-consumer fan-out (Risk 1)

`g_MenuApplication` is written in one place (`main.cpp:316`) and read in 29 translation units. Any change to `MenuApplication`'s internal layout, the `loaded_menu` state machine, or the `LaunchMenu` call sequence has implicit behavioral dependencies in all 29 consumers simultaneously, none of which are enforced at compile time.

**Architectural change that eliminates the risk class:** Gate `g_MenuApplication` behind a process-singleton accessor (`MenuApplication::Get()`) that returns a `const` reference by default, requiring an explicit `MenuApplication::GetMutable()` for write paths. The compiler then identifies every write site, making the dependency graph explicit. Longer term, move per-layout state (lockscreen enabled, launch-failed, chosen-homebrew) into the individual `IMenuLayout` subclass rather than on the `MenuApplication` god object.

### #3: Non-deterministic build environment (Risk 6)

Silent binary divergence makes every regression potentially unfalsifiable. A fix that "works on my machine" may have worked because a library was at a different version, not because the code change was correct.

**Architectural change that eliminates the risk class:** A Dockerfile with `FROM devkitpro/devkita64:20250101` (or a specific pinned image) + `RUN dkp-pacman -U libnx-4.12.0-1...` pinned by URL and SHA256. Every build that produces a deployable binary must run inside this container. The container image hash becomes part of the release artifact record.

---

## What I Would Do FIRST to Prevent the Next Regression

**Add a SMI protocol version assert.**

In `smi_Protocol.hpp`, change `CommandCommonHeader` to:

```cpp
struct CommandCommonHeader {
    u32 magic;
    u32 protocol_version;  // was: u32 val (cmd_id)
    u32 cmd_id;
};
constexpr u32 CommandProtocolVersion = 1;  // bump on every enum change
```

In `SendCommandImpl`, write `CommandProtocolVersion` into the out-header. In `ReceiveCommandImpl`, assert `in_out_header.protocol_version == CommandProtocolVersion` and return `ResultInvalidOutHeaderMagic` (reuse the existing error path) if mismatched.

This is a 15-line change across `smi_Protocol.hpp` and `smi_Protocol.cpp`. It requires a synchronized rebuild and redeploy of both uSystem and uMenu to take effect, but after that single coordinated deploy, every future partial deploy that produces a version mismatch will fail loudly at first message — before any wrong action executes — instead of silently corrupting behavior for 5+ deploy cycles.

No other single change has the same ratio of implementation cost to regression surface eliminated.
