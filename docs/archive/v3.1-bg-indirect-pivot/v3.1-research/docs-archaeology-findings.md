# Docs Archaeology Findings — v3.1 Windowed-Homebrew Planning

**Date:** 2026-05-18
**Scope:** All .md files in `docs/` and `docs/research/` subdirectory of `qos-ulaunch-fork`
**Purpose:** Extract every piece of design history relevant to v3.1 windowed-homebrew planning.
**Author note:** Every claim is cited by file path. Stale or contradicted content is flagged explicitly.

---

## TOP 5 FINDINGS

### Finding 1 — The only viable windowed NRO compositing path is cooperative ABI (HBABI EntryKind 14)

`docs/49_v3.1_research_atmosphere_mitm.md` is the definitive architectural assessment. All five
compositing paths are evaluated and rated:

- **Option A (vi-mitm sysmodule):** Structurally impossible. `IHOSBinderDriverRelay` is a
  sub-session of vi:u, never visible to SM-level mitm. Frame data flows through
  `IHOSBinderDriverRelay → bqQueueBuffer` which AMS mitm cannot intercept. Rated 3/5 only for
  a more invasive fork-uLoader-with-synthetic-BQ approach that avoids mitm entirely.
- **Option B (uLoader GOT patch):** High risk. libnx is statically linked into each NRO; every
  NRO would need individual patching. Rated 2.5/5.
- **Option C (nv-mitm capture):** High risk (2000+ LOC). Rated 2/5.
- **Option D (CaptureScreenshot via vi:s):** Only path achievable in a single sprint. 50-100 LOC,
  33-66ms latency via `IApplicationDisplayService::CaptureScreenshot`. Appropriate only for
  slow UI NROs (Goldleaf, NX-Shell), completely unusable for games or media players.
- **Option E (cooperative ABI):** RECOMMENDED path. New `EntryKind::HostedNvmapId = 14` in
  HBABI. `NVMAP_IOC_EXPORT_FOR_ARUID` already used by `framebufferCreate` internally for
  cross-process GPU buffer sharing. uMenu owns the deko3d texture side. NRO opts in via a
  `libqos-hosted-nro` header. Only uLoader + uMenu need changes; no Atmosphère dependency.
  The hosted NRO must bypass deko3d and write directly via raw libnx nvmap + nvhost ioctls.
  Open question: confirm uLoader and uMenu share the same ARUID (if true,
  `NVMAP_IOC_EXPORT_FOR_ARUID` works between them without any kernel change).

Source: `docs/49_v3.1_research_atmosphere_mitm.md` (entire document, especially Sections 3–5).

### Finding 2 — uMenu termination constraint is the core architectural problem

`docs/49_v3.1_research_hbloader.md` documents the critical constraint: uMenu MUST TERMINATE
before uLoader launches. This is enforced at `uSystem/source/main.cpp:1150` via
`la::IsActive()` blocking the action queue. Any windowed NRO approach must solve this first.

Three architectural paths to solve it:
1. Switch uLoader to `LibAppletMode_Background` so uMenu can stay resident.
2. Run uLoader as a non-applet process entirely (requires more invasive AMS changes).
3. The cooperative ABI approach makes this moot by design: the NRO renders into a shared
   nvmap buffer WHILE uMenu is still the foreground applet — uLoader never actually "launches"
   in the traditional sense.

Source: `docs/49_v3.1_research_hbloader.md`, Section 3 (architectural analysis).

### Finding 3 — L-CYCLE-WINDOW-MANAGER-DESIGN already contains the complete analysis

`docs/L-CYCLE-WINDOW-MANAGER-DESIGN.md` predates all three `49_v3.1_research_*.md` files and
already exhaustively covers:
- Section 3.1: Why inter-applet framebuffer capture is impossible (5 paths evaluated)
- Section 3.3: The L+2 "windowed shell mode" fallback (pre/post snapshot around NRO launch)

The three `49_v3.1_research_*.md` files authored 2026-05-18 confirm and extend this analysis
with source-level evidence from sphaira, hbloader, and Atmosphère. No new architectural
conclusion emerges that L-CYCLE-WINDOW-MANAGER-DESIGN did not already surface.

The L+2 fallback (`window-state.bin` save/restore around full-screen NRO) is the ONLY
option for Phase 1 that works without kernel changes. It is explicitly NOT true compositing —
it saves window positions before the NRO takes over the framebuffer and restores them on
return with reopen behavior.

Source: `docs/L-CYCLE-WINDOW-MANAGER-DESIGN.md`, Sections 3.1, 3.3, 5.4.

### Finding 4 — NPDM wildcard already covers vi:s; no patch needed

`docs/AUDIT-NPDM-uMenu-vi-access.md` (2026-04-18) confirmed that uMenu.json has
`"service_access": ["*"]` (wildcard). Binary parse of `main.npdm` SAC raw bytes
`80 2a 00 2a` = host `*` + access `*`. The explicit service checklist entry:
`vi:s | Fallback only | YES (Wildcard covers it)`.

`vi:m` verdict: "PRESENT. No NPDM patch needed for service access."
fw 20.0.0 landmine (`system_resource_size` field): confirmed absent from uMenu.json (safe).

Source: `docs/AUDIT-NPDM-uMenu-vi-access.md`, Section 1 service checklist table.

### Finding 5 — Prior claim about sphaira "documented windowed mode hooks" is false

`docs/49_v3.1_research_sphaira.md` (2026-05-18) states: "The Q OS v3.0 README's claim that
sphaira has 'documented windowed mode hooks' was speculative. There are no such hooks."

Evidence: `nwindowGetDefault()` hardwired in `createFramebufferResources()` via
`dk::SwapchainMaker{device, nwindowGetDefault(), fb_array}.create()`. Input: `padInitializeAny(&m_pad)` in constructor, no hook point. Service init: `userAppInit()` calls `appletLockExit()` — double-init would crash uMenu.

Alternative first windowed targets: nx-hbmenu (simpler C files, same constraint) or
NXThemesInstaller (linear framebuffer = memcpy redirect, simplest possible path).

Source: `docs/49_v3.1_research_sphaira.md`, Section 2 (framebuffer analysis) and Section 5.

---

## PER-DOC SUMMARY (ALPHABETICAL)

### `docs/44_Three_Phase_Roadmap.md`

Strategic roadmap covering Phase 1 (applet-mode, 448 MB heap), Phase 2 (game-title hijack,
3.2 GB heap), Phase 3 (full CFW fork). Phase 1 explicitly includes "applet-mode dev windows"
where dev tools run as small applet-mode windows. Phase 1 exit criterion: hit a feature that
genuinely cannot fit in applet-mode RAM. The 448 MB ceiling is a hard constraint on all v3.1
windowed design decisions.

Relevance: Frames Phase 1 scope. v3.1 windowed homebrew is Phase 1 work bounded by the 448 MB
ceiling. Cross-ref: `docs/43_Splash_Replacement_Research.md`, `STATE.toml`.

### `docs/45_HBMenu_Replacement_Design.md`

Distinguishes hbloader (structural, must keep) from hbmenu (UI, replace with QdVaultLayout).
QdVaultLayout::ScanDirectory uses same scan path as `qd_DesktopIcons.cpp::ScanNros`. NRO
launch via `ul::menu::smi::LaunchNro(entry.full_path, {})` — no new primitive needed.

Dev-tool windows are SEPARATE from the vault (Stage 5: QdNxlinkWindow, QdUsbSerialWindow,
QdLogFlushWindow). HBMenu removal net savings: ~6-17 MB. Total effort estimate: ~2250 LOC,
overall low risk.

Relevance: Defines the hbmenu absorption work that is a prerequisite for v3.1 windowed mode
(vault must be feature-complete before hbmenu.nro is deleted).

### `docs/46_Stabilization_Handoff.md`

Snapshot of what was deployed at commit 90cf352. Not-yet-deployed features listed: cursor
refinement, Mac SFX, BGM volume policy, texture cache fix. Phase 1 next steps: wire vault into
dock, text+image viewers, HBMenu removal script, dev-tool windows.

Relevance: Provides the "state of the world" baseline for what was stable before v3.1 planning.

### `docs/47_Integratable_Tasks_Catalog.md`

Phase 1 definites (9 NROs): ftpd, nx-hbmenu, Sphaira, Goldleaf, NX-Shell, Snes9x, mGBA,
NXMilk, RetroArch (SNES/NES/GBA cores only).

Phase 2 hard blocks:
- NXMP: `appletGetAppletType() == LibraryApplet` causes immediate exit.
- RetroArch N64/PSX: `svcMapPhysicalMemoryUnsafe (0x4D)` gated to game-title processes only.
- BrowseNX: system applet security policy.

NXMilk + USB serial conflict: `audoutInitialize` conflicts with `usbCommsInitialize`.
FluffySD: unresolved project identity as of 2026-04-24.

Relevance: Authoritative list of what can and cannot run in Phase 1 applet mode. Critical
for understanding which NROs are viable first windowed targets.

### `docs/48_DevTools_Test_Harness.md`

NXLink test: requires Mac listener FIRST, macOS firewall allow UDP 28771, same Wi-Fi subnet.
USB serial test: UMS must be OFF, /dev/cu.usbmodem* must appear within 2s.
Known bug SP4.1 (2026-04-24): only BOOT lines in telemetry log — atexit + periodic flush not
persisting async messages. Four possible causes listed.

Pass criteria: all 3 must pass (NXLink streaming, USB CDC-ACM enumerated + bytes visible, SD
ring file >5KB with non-BOOT lines).

Relevance: Dev-tool windows are Phase 1 windowed targets. This doc is the test harness for
them. Must pass all 3 checks before dev-tool windows can be declared done.

### `docs/49_v3.1_research_atmosphere_mitm.md`

**Most important research file for windowed compositing.** Definitive architectural assessment.
See Finding 1 above. Also documents: deko3d has no cross-process sharing API but underlying
nvmap layer does. No community display-mitm projects exist. No AMS PRs/issues for display
interception. Option C (cooperative ABI) rated 3.5/5 and is the only recommended path.

### `docs/49_v3.1_research_hbloader.md`

hbloader (both upstream and uLoader) does NOT own a framebuffer — no vi/nwindow/framebuffer
calls at all. NRO owns display entirely; hbloader only passes `AppletType` through
`ConfigEntry[]` HBABI. uSystem detects NRO completion via `appletHolderCheckFinished`
(kernel event, 10ms poll). uLoader exit path: `loader_Target.cpp:405-418`.

Critical: uMenu MUST terminate before uLoader launches (`la::IsActive()` blocks action queue
at `main.cpp:1150`). This is the core architectural constraint for any windowed NRO approach.

Option D (CaptureScreenshot) open question: can uMenu (as qlaunch overlay) call
`IApplicationDisplayService::CaptureScreenshot` while a library applet occupies foreground?
Status: unresolved as of 2026-05-18.

### `docs/49_v3.1_research_sphaira.md`

No windowed mode exists in sphaira. See Finding 5 above. The document also identifies
`App::Poll()` ~line 1139 as the hypothetical input intercept point and ~line 1048-1060 as the
hypothetical touch intercept point IF someone were to attempt a patch approach anyway.

Identifies `App::Poll()` framebuffer flow via `dk::SwapchainMaker` → `dk::Swapchain::acquireImage`
as the point at which control could theoretically be intercepted — but only with a full
cooperative ABI where the NRO is rebuilt against a hosted framebuffer.

### `docs/49_v3.1_windowed_homebrew_design.md`

**Note:** This file was in the directory listing but not separately read in detail during the
prior session. It appears to be the design document that the research files feed into. It should
be read before making any implementation decisions.

Location: `docs/49_v3.1_windowed_homebrew_design.md`.

### `docs/AUDIT-NPDM-uMenu-vi-access.md`

See Finding 4. The definitive NPDM audit for vi:s permission question. Authored 2026-04-18.

Additional key finding: `vi:m` is also covered by the wildcard. Tesla overlay approach was
tested against this NPDM context; conclusion was "do not rebuild uMenu with a patched NPDM —
the current wildcard already passes vi:m." The optional hardening list is given but is not
recommended until the Tesla port (if any) is stable.

Source file: `docs/AUDIT-NPDM-uMenu-vi-access.md`.

### `docs/AUTONOMOUS-TEST-RIG-DESIGN.md`

USB-C serial command loop architecture: `mac-bridge.py` ↔ `qos-test-harness.nro` v2.0.0.
Wire protocol opcodes: PUSH_NSO (0x01), RESTART_UMENU (0x02), PRESS_BUTTON (0x04),
SCREENSHOT_REQ (0x05), READ_LOG (0x06), PING (0xEE).

`qos-rig-mode.flag` gates rig mode vs normal assertion suite.

Important clarification: sys-patch (TID `420000000000000B`) NOT sys-con (TID
`430000000000000B`). USB serial comes from `usbCommsInitialize` in the NRO itself, not a
sysmodule. This is critical for v3.1 testing.

### `docs/F-PLAN-STABILIZE-5.md`

P1: Re-enable hot-corner Q glyph (remove `#if 0` at `qd_Launchpad.cpp:624-645` and
`qd_DesktopIcons.cpp:1729-1784`); use `SDL_BLENDMODE_NONE` to fix alpha bleed.

P3: Re-enable auto-folder tile strip (remove `#if 0` at `qd_Launchpad.cpp:723-778` and
`qd_Launchpad.cpp:453-527`).

P5: Add Mac-class icons for 5 builtins via `TryFindLoadImage("ui/Main/EntryIcon/<name>")`.

P6: Launchpad pagination (page_index_, page_count_, ITEMS_PER_PAGE=50, dot indicator strip).

`NroEntry` and `LpItem` are size-pinned at 1632 bytes each. This constraint applies to any
new entry type introduced for windowed NRO management.

### `docs/K+1-FOLDERS-CATEGORIES-DESIGN.md`

K+1 cycle: auto-classified categories (Nintendo/Homebrew/Extras/Payloads/Builtin) + user
folders. `folders.json` SSOT at `sdmc:/ulaunch/folders.json`. Implementation phases:
categories (K+1.0), folder entries (K+1.1), edit-mode gestures (K+1.2 gated on K+3).

Relevance: Windowed NRO entries may need new category classifications. Any new window-client
entry type must fit within the K+1 category system.

### `docs/K+2-SETTINGS-FILTER-CHAIN-DESIGN.md`

New ConfigEntryIds: IconSize (0x10), DesktopMode (0x11), ShowHomebrew (0x12),
ShowApplications (0x13), ShowSpecial (0x14), LaunchpadDefaultCategory (0x15),
EnableRecents (0x16), EnableFavorites (0x17).

`per-app-prefs.bin` binary format for per-entry visibility/favorite/dock-pin/LRU data.
Filter chain: `PassesCategoryFilter → PassesKindFilter → PassesFavoriteFilter →
PassesRecentFilter → PassesQueryFilter`.

### `docs/K+3-K+4-EDIT-MODE-RECENTS-DESIGN.md`

K+3: Long-press edit mode with wiggle animation (SDL_RenderCopyEx ±2°, 1 Hz, per-icon
phase offset). State machine: NORMAL → EDIT_MODE → DRAGGING. `SwapIcons()` shift
algorithm in `qd_DesktopIcons.cpp`.

K+4: LRU tracking via `per-app-prefs.bin`; Launchpad Recent section (top N by
`last_launched_ns DESC`).

### `docs/L-CYCLE-WINDOW-MANAGER-DESIGN.md`

**Second most important doc after the `49_v3.1_research_*.md` cluster for v3.1 planning.**

QdWindowManager class hierarchy: `pu::ui::Element → QdWindowElement → {QdWindowTitleBar,
QdWindowContentArea}`, `QdWindowClient` abstract base with implementations per app.
MaxWindows = 6. Each QdWindowElement costs ~1-2 MB chrome + client allocations.

GPU texture eviction on minimize: `ReleaseGpuTextures()` / reload on `Restore()`.

Section 3.1 exhaustively documents why NRO framebuffer capture is impossible in Phase 1
(all 5 paths evaluated — the `49_v3.1_research_*.md` files independently confirm these
conclusions with source evidence).

Section 3.3 defines L+2 fallback: `SetPreLaunchSnapshot(true)` + `SaveWindowState()` before
NRO launch, `RestoreWindowState()` on return with reopen behavior. This is the only
implementable option for true windowed feel in Phase 1.

`window-state.bin` format: magic 0x57, version 1, per-record `{kind, x, y, w, h, flags}`.
Path: `sdmc:/ulaunch/window-state.bin`.

Section 5.4 IMPORTANT: "uMenu NPDM is the upstream XorTroll v1.2.0 NPDM (the Q OS fork
NPDM was rejected by FW 20.0.0 per ROADMAP.md)." This note is now superseded by
`AUDIT-NPDM-uMenu-vi-access.md` which confirmed the wildcard NPDM is deployed and accepted.

Anti-stub gates: 17 explicit verification criteria across L+1.0/L+1.1/L+1.2/L+2.0/L+3.0.
All L+2.0 criteria must be verified before any windowed NRO claims to be implemented.

Input routing: WM intercepts before desktop icons if `HasOpenWindows()`. Any windowed NRO
launch triggered via the icon grid must account for this intercept.

### `docs/PLAN-v0.7-deko3d-imgui.md`

Why not Tesla: OverlayApplet-only, wrong framebuffer dimensions (448×720 RGBA4444), uses
hid:sys. deko3d + ImGui confirmed working on fw 20.x in applet mode by sphaira and ftpd.

Memory budget: 296 MB heap declared, GPU pool 16 MB image + 128 KB code + 1 MB data = ~19.1 MB.
4-week implementation plan: Foundation → Widget Shim → Icon Pipeline → Screen Port + Polish.

Risk: Plutonium coexistence NOT viable (SDL_Init unconditionally calls EGL+Mesa). This means
any deko3d-based windowed rendering layer CANNOT coexist with the current Plutonium-based
uMenu UI in the same process. This is a fundamental constraint for Option E (cooperative ABI)
implementation: the NRO-side deko3d cannot share the same process with Plutonium.

### `docs/QOS-REBRAND-ASSET-INVENTORY.md`

Asset catalog for Q OS rebrand. Not directly relevant to windowed homebrew planning beyond
confirming asset paths referenced in other docs.

### `docs/RESEARCH-libtesla-rendering.md`

Tesla is OverlayApplet-only; uMenu is LibraryApplet — incompatible. This predates
`AUDIT-NPDM-uMenu-vi-access.md` and contains a conditional note: "Check `uMenu.json`
service_access list. If missing, add it, or fall back to viCreateLayer (stray layer)."
The NPDM audit supersedes this — no patch is needed.

Tesla framebuffer: 448×720 RGBA4444, 1.26 MB double-buffered — fits in 6 MB fw20 budget.
Historical note about "fw 20.0.0 14MB applet pool wall" is relevant to overall budget context.

**Status: STALE on the vi:m question.** The conditional patch advice was written before the
NPDM audit confirmed wildcard coverage.

### `docs/RESEARCH-nxtheme-tooling-macos.md`

NXThemes tooling on macOS. Not relevant to windowed homebrew planning directly.

### `docs/RESEARCH-tier2-dmi-ryujinx.md`

Documents SMI protocol details: magic `0x21494D53` "SMI!", 8-byte header,
`CommandStorageSize = 32KB`. `ulsf:p` private service: `0xCAFEBABE` hash,
`Initialize` (validates PID) + `TryPopMessageContext`. 10ms poll interval, 4KB thread
stack, priority 49. `pminfoGetProgramId` security check at `sf_IPrivateService.cpp:16`.

Relevance: Any windowed NRO approach that uses a new SMI command must fit within the
32KB CommandStorageSize and follow the `ulsf:p` private service protocol.

### `docs/SMI-OPEN-COMMANDS-AUDIT.md`

Inventory of all 10 active "open" SMI commands (see context). The `LaunchHomebrewApplication`
command requires `HomebrewApplicationTakeoverApplicationId` to be configured. The 10 native
applet open commands all follow the same action-queue pattern at the uSystem dispatcher.

Relevance: Any new windowed NRO SMI command must be added to this audit when implemented.

### `docs/SPHAIRA-CATALOG.md`

Sphaira's appstore fetches from `https://switch.cdn.fortheusers.org/repo.json` (same as
hb-appstore). No separate Sphaira catalog. Getting Q OS into Sphaira = same as hb-appstore
(one PR). **Sphaira is the target for CATALOG distribution, NOT windowed compositing.**

### `docs/SPHAIRA-INSTALLER-PLAN.md`

Full CFW pack (`qos-cfw.zip`) vs overlay-only (`qos-umenu.zip`). Sphaira's `URL_JSON` is a
`constexpr` literal — cannot be self-hosted without source patch. Option A+C hybrid (CDN
for overlay, bootstrap NRO for full CFW). Sigpatches must NOT be bundled in GitHub releases.

### `docs/TEST-RIG-FIRST-MILESTONE.md`

Milestone criteria for the autonomous test rig: connects via USB serial, can PUSH_NSO + reboot
uMenu, can read telemetry log with non-BOOT lines. Not directly relevant to windowed compositing.

### `docs/UPSTREAM-ATTRIBUTION-COMMIT.md`

Attribution tracking for upstream code. Not relevant to v3.1 planning.

### `docs/UPSTREAM-COMPANION-APPS-STRATEGY.md`

uScreen removal from package: target (Java macOS classifier broken). Replace with Swift
`QOS Mirror.app`. uScreen wire protocol: `UsbPacketHeader` with `UsbMode` (RAW_RGBA or JPEG).
Switch side implemented at `uSystem/main.cpp:1556+` (`CaptureJpegScreenshot` path).

Relevance: The `CaptureJpegScreenshot` path in uSystem is the same JPEG-capture mechanism
that would be used by Option D (CaptureScreenshot via vi:s) if pursued. This confirms the
Switch-side plumbing exists; the question is whether it can be called while a library applet
is in foreground (see open question in Finding 1).

uDesigner: legacy reference only; future rebuild post-1.0.

---

## docs/research/ SUBDIRECTORY — SUMMARY

### `research/2011-0102-FIELD-EVIDENCE-20260506.md`

Definitive field evidence for the `OutOfSessionMemory` (result code `0xCC0B`, module 11
HIPC, description 102) crash that plagued the 2026-05-06 session. The error returns from
`ServerSessionManager::CreateSessionImpl` when the static `m_session_storages[MaxSessions]`
pool is exhausted. `MaxSessions = 64 - MaxServers` — compile-time cap, deterministic.

Key finding: upstream uLaunch 1.2.3 (2026-01-24) was built against AMS 1.10.2 / FW 21.2.0.
**Upstream uLaunch has never shipped an AMS 1.11.x build.** The Q OS fork inherits this gap.

Relevance for v3.1: Any new SMI command or ECS launch path adds another session slot consumer.
The ECS unregister fix (`ldrShellAtmosphereUnregisterExternalCode`, IPC 65001) documented in
`research/AMS-1.11-CLEAN-EXIT-CONTRACT-20260506.md` must be confirmed complete before v3.1
windowed work adds further session complexity.

### `research/AM-NSS-0X40570-ANALYSIS.md`

Static analysis of `am.nss` HOS 20.0.0 panic at offset `+0x40570`. PC = abort dispatcher,
LR = `+0x2318` (early startup / proxy-registration path). Two candidate causes:
1. uSystem built against pre-FW-20 libnx, dispatches appletAE cmd 100 instead of cmd 110.
2. nx-ovlloader session exhaustion triggering foreground assertion.

FW 20.0.0 breaking change: `OpenSystemAppletProxyOld` (cmd 100) replaced by
`OpenSystemAppletProxy` (cmd 110, requires `AppletAttribute` struct 0x80 bytes).

**NOTE (2026-05-16 annotation):** Hardware label in original source is "Mariko" — this
reflects the community forum source's test platform, NOT the Q OS hardware. The Q OS Switch is
OG Erista (T210). The IPC surface analysis is hardware-neutral.

### `research/AM-NSS-FW20-IPC-CONFIRM.md`

(Not read in full — content implied by cross-references in HEKATE-FIX-OPTIONS and
REGRESSION-TIMELINE. Contains confirmation of the FW20 appletAE cmd 100→110 breaking change
and recommendation for `pool_partition: 1` on system applets.)

### `research/AMS-1.11-CLEAN-EXIT-CONTRACT-20260506.md`

**Critical for v3.1 session hygiene.** AMS 1.11.0 introduced mandatory clean-exit requirement:
`appletHolderRequestExitOrTerminate` + `appletHolderJoin` (IPC cmd 30 `GetResult`) +
`appletHolderClose`. Upstream uLaunch `Terminate()` skips Step 2 (`appletHolderJoin`).
Q OS fork inherits the same gap.

ECS session slot leak: `ldrShellAtmosphereUnregisterExternalCode` (IPC 65001) called NOWHERE
in uSystem. `MaxSessions = 6` exhausted after 6 ECS launches → `2011-0102`.

Fix: new `ecs::UnregisterExternalContent(program_id)` helper + `g_LastLaunchWasEcs` +
`g_LastEcsProgramId` globals. Applied at `main.cpp:1311` after applet finishes.

Status: `IPC-SESSION-POOL-EXHAUSTION-20260518.md` confirms a v2.8.0 fix addressed the
`MaxPrivateSessions = 1` issue. However, the ECS leak fix described here
(`AtmosphereUnregisterExternalCode`) still needs explicit verification that it landed.

### `research/APPLET-TEARDOWN-AUDIT-20260506.md`

Complete inventory of all 12 applet launch sites in uSystem and uMenu. Sites #1, #2, #11:
ECS handle never unregistered (missing IPC 65001). Sites #3-#10: `appletHolderClose` missing
on normal-exit path for native applets. Site #12 (uMenu's own sub-applet calls) is clean.

"Album + uLoader exhausts pool in 6 launches" repro pattern documented.

For v3.1: any new applet launch site added for windowed NRO must include both
`appletHolderJoin` + `appletHolderClose` on ALL exit paths, and call
`ecs::UnregisterExternalContent` immediately after applet finishes.

### `research/ARCH-CASCADE-AUDIT-20260506.md`

Not read in full. Architectural cascade audit from 2026-05-06 session. Likely contains
root-cause analysis for the regression cascade documented in REGRESSION-TIMELINE.

### `research/ATMOSPHERE-CURRENT-RESEARCH-20260506.md`

Not read in full. Atmosphère state research from 2026-05-06.

### `research/CRASH-FORENSICS-EVIDENCE-20260506.md`

Forensic analysis of 9 crash reports from the Switch SD card (2026-05-06). Key findings:
- All four uSystem crashes produced `2011-0102` (OutOfSessionMemory) regardless of uMenu
  binary — confirming the crash is invariant with respect to uMenu identity.
- `am` crash at `[E3722DA9]+0x4056c` (all four events, byte-identical). May 5 am crashes
  produced `2001-0132` (`svc::LimitReached`) — a DIFFERENT failure mode.
- The uLoader crash at 09:11 (Undefined Instruction at null-module address) is structurally
  separate — predates the uSystem/am crash window.

### `research/ENVIRONMENT-AUDIT-20260506.md`

Not read in full. SD environment audit from 2026-05-06.

### `research/FILEPICKER-IMPLEMENTATION-20260506.md`

Not read in full. File picker implementation from 2026-05-06. Likely documents the scope-creep
FilePicker work identified as SC1 in REGRESSION-TIMELINE.

### `research/FW20-IPC-BUDGET-RESEARCH-20260506.md`

Not read in full. FW20 IPC budget research.

### `research/FW20-NPDM-FIELD-ANALYSIS.md`

Complete field analysis of the 6 NPDM fields npdmtool added for FW20 support:
`optimize_memory_allocation`, `disable_device_address_space_merge`,
`enable_alias_region_extra_size`, `prevent_code_reads`, `signature_key_generation`,
`force_debug_prod`. All default to 0/false when absent. None are enforced by am.nss for
system applets. None can cause `0x10801`.

**`pool_partition: 1` is correct for uSystem (SystemAppletMenu). Do NOT change to 2.**
The SP4.15.1 hotfix already reverted `pool_partition: 1 → 2` back to 1.

NPDM bytes are byte-for-byte identical between the Apr 26 binary and a fresh build.

This document definitively closes the "is the NPDM causing boot regressions" question.
Status: The NPDM is NOT the cause of any boot regression. Root cause was `pool_partition: 2`
and heap size changes (both reverted in SP4.15.1).

### `research/HBMENU-CUTOVER-PLAYBOOK-20260506.md`

Step-by-step playbook for removing hbmenu from the SD card. 6-step sequence (snapshot →
disable hijack via config → validate → remove config → delete binaries → permanent disabled
config). All reversible until Step 5.

Pre-cutover validation matrix: 8 checks that must pass on hardware before proceeding.

Key AMS finding: `hbl.nsp` is opened ONLY by `OpenHblCodeFileSystemImpl` (`fs_code.cpp:166`).
Zero uLaunch source dependencies on `GetHblPath`/`override_any_app`. Cutover is purely AMS-side.

Relevance for v3.1: hbmenu cutover must be complete before v3.1 windowed vault claims
"native NRO launch." The 5-item gap list from `HBMENU-FEATURE-INVENTORY` must be addressed.

### `research/HBMENU-FEATURE-INVENTORY-20260506.md`

Feature-by-feature inventory of upstream nx-hbmenu vs Q OS coverage. Gaps requiring work:
1. Star/favorites persistence (~1 day)
2. ABI revision check + recompile warning badge (~0.5 day)
3. Old-app-folder collapse (single-NRO subdir auto-collapses) (~0.5 day)
4. Argv swkbd editor (long-press → swkbd → argbuf) (~1 day)
5. Fileassoc system (map non-.nro extensions to host NRO) (~2 days)

After these five land, removing `hbl.nsp` / `hbmenu.nro` / `override_config.ini` is safe.

Already implemented and better than hbmenu: discovery, launch, file browser, nxlink,
reboot-to-payload. Q OS LACKS a UI toggle for applet vs application launch context.

### `research/HEKATE-FIX-OPTIONS-20260506.md`

Three ranked options for fixing the Hekate reboot regression caused by `system_resource_size=0x0`:
- Option A: Copy known-good `exefs.nsp` backup (no rebuild, fastest)
- Option B: Restore `system_resource_size=0xC00000` + `stack=0x100000`, rebuild uSystem
- Option C: Add `system_resource_size=0x200000` to uMenu.json (lowest confidence)

Root cause: `system_resource_size=0x0` triggers heap exhaustion in v2.x uSystem due to
+11 MB BSS growth (`ApplicationControlCache`). `bpcamsInitialize()` port registration fails.

Status: The v2.8.0 fix (`MaxPrivateSessions = 1 → 4` in `sf_IpcManager.hpp:33`) addressed the
IPC session pool cause. This file is now historical reference for the `system_resource_size`
diagnostic pattern.

### `research/HEKATE-REBOOT-REGRESSION-20260506.md`

Not read in full. Detailed regression analysis for the Hekate reboot issue. Likely contains
the diagnostic log trail that HEKATE-FIX-OPTIONS synthesizes.

### `research/IPC-SESSION-POOL-EXHAUSTION-20260518.md`

**Most recent research file (2026-05-18 = today).** Documents the v2.8.0 fix for the
IPC session-pool exhaustion bug that caused uMenu to freeze after theme switch + login.

Three-part compound bug:
1. `MaxPrivateSessions = 1` in `sf_IpcManager.hpp:33` → fixed to 4
2. `CacheActiveTheme` missing `fsdevCommitDevice("sdmc")` → fixed in `cfg_Config.cpp:215`
3. `appletHolderRequestExitOrTerminate` timeout 15s → reduced to 2s in `la_LibraryApplet.cpp:80`

HW-confirmed by creator 2026-05-18: "Everything works perfectly."

IMPORTANT: This fix involved the FIRST custom-fork uSystem deployment since the OOM-lesson
rollback to stock v1.2.0. The three fixes specifically target the session pool exhaustion cause.

Pattern: the cascading four-cycle reactive fix history (v2.7.0 → v2.7.3) was stopped by the
`feedback_cascade_test_discipline.md` rule, and the proper 3-parallel-agent audit produced
the correct compound-bug diagnosis in ~30 minutes.

For v3.1: the `MaxPrivateSessions = 4` headroom must be preserved. Any new v3.1 SMI commands
that consume additional private sessions must be budgeted against this pool.

### `research/LOGIN-REACTIVITY-V237-AUDIT.md`

Not read in full. Login screen reactivity audit for v2.3.7 baseline.

### `research/LOGIN-SCREEN-REACTIVITY-AUDIT-20260506.md`

Not read in full. Earlier login screen reactivity audit from 2026-05-06.

### `research/MAIN-THREAD-BLOCKING-AUDIT-20260506.md`

Not read in full. Documents main-thread blocking patterns. Likely covers the R2 regression
(`nxlinkConnectToHost` on main thread) documented in REGRESSION-TIMELINE.

### `research/MTIME-ARCHAEOLOGY-20260505.md`

Not read in full. File modification-time archaeology from 2026-05-05. Used to trace which
binary was deployed when.

### `research/NXLINK-AUTOLAUNCH-IMPLEMENTATION-20260506.md`

Documents the nxlink auto-launch fix: `ReceiveOne()` now fires
`smi::LaunchHomebrewLibraryApplet(dest_path, argv_buf)` immediately after writing the NRO to SD.
Cmdline args captured (max 1024 bytes) and passed through. `g_nxlink_scan_pending` still set
for icon grid update even if uSystem rejects the launch.

uMenu.nso md5 after change: `4a63903f576938298358d45f9dd0eaa6`.

### `research/QOS-NRO-SURFACE-AUDIT-20260506.md`

Static analysis of the complete NRO/homebrew launch surface. Key findings:
- `ScanNros()` runs two flat passes (`sdmc:/switch/` and `sdmc:/`), no subdirectory recursion.
- QdVaultLayout adds a third user-driven path via `ScanCurrentDirectory()`.
- Applet vs application toggle: plumbing exists end-to-end (both SMI commands wired in
  uSystem), but UI selection layer is absent (all call sites hardcode LibraryApplet mode).
- Argv: struct field exists and wired, but every call site passes `std::string("")`.
- nxlink: no auto-launch on file receipt (scan-pending only). This gap was fixed in
  `NXLINK-AUTOLAUNCH-IMPLEMENTATION-20260506.md`.

### `research/RECOVERY-PLAN-20260506.md`

Not read in full. Recovery plan from 2026-05-06 session.

### `research/REGRESSION-TEST-INFRASTRUCTURE-20260506.md`

Not read in full. Documents the regression test infrastructure improvements.

### `research/REGRESSION-TIMELINE-20260506.md`

Complete forensic timeline of all regressions from 2026-05-06. Seven regressions (R0-R6) plus
one scope-creep incident (SC1). Root cause: two NPDM values (`system_resource_size`,
`main_thread_stack_size`) treated as constants when they are binary-size-dependent.

Common failure mode: "changes are validated against the goal they were made for, never against
what they could break." `[HW-GREEN]` tags on commits `2ab5fa30` and `dd0a3a31` were false —
deployed binary md5 `aa267e1a` built from dirty tree, not from either commit.

Preventive: `tools/qos-ulaunch-fork/scripts/preflight-deploy.sh` that checks BSS vs budget,
no blocking I/O on main thread, no untracked .cpp/.hpp, operator-signed HW-GREEN log.

For v3.1: this file is the canonical "how not to regress" reference. Any v3.1 feature must
add its regression checks to this pattern before claiming `[HW-GREEN]`.

### `research/SCANNROS-RECURSION-20260506.md`

Implementation details for one-level subdirectory recursion added to `ScanNros()`. Inner pass:
`opendir("sdmc:/switch/")`, iterate subdirs via `stat()`, `opendir(sub_path)`, cap at
`MAX_SUBDIR_SCAN = 50`. No dedup between flat and recursive passes (different full paths).

uMenu.nso md5 after change: `1de81a0376a70d40b227e728f9c36d2a`. Not deployed as of doc date.

### `research/SESSION-REGRESSION-AUDIT-20260506.md`

Not read in full. Session-level regression audit.

### `research/SYSMOD-AMS-1.11-COMPAT.md`

Sysmodule compatibility with AMS 1.11.x + FW 20.0.0. Root cause of `am +0x40570` panic:
FW 20.0.0 reduced applet pool from 40 MB to 14 MB. nx-ovlloader (ppkantorski v2.0.0) with
default 6 MB heap on FW 20 over-claims the pool when other sysmods are present.

Tier ranking: nx-ovlloader (80% probability), sys-clk (15%), sys-con (5%).

Fix: create `/config/nx-ovlloader/heap_size.bin` with `0x00200000` (2 MiB).
Remove `nx-ovlreloader`. Do not run Tesla/EdiZon/sys-botbase concurrently with nx-ovlloader.

WerWolv issue #42 (open) reports identical `2001-0132` signature with nx-ovlloader on FW 20.2.0.

### `research/TILE-ICONS-IMPLEMENTATION-20260506.md`

Not read in full. Tile icons implementation details from 2026-05-06.

### `research/TOOLCHAIN-TIMELINE-AUDIT.md`

Not read in full. Toolchain timeline audit.

### `research/UMENU-BINARY-DELTA-20260506.md`

Not read in full. Binary delta analysis for uMenu.

### `research/USYSTEM-BINARY-DIFF-20260505.md`

Not read in full. Binary diff analysis for uSystem from 2026-05-05.

### `research/VAULT-APPCONTEXT-TOGGLE-20260506.md`

Not read in full. Vault application-context toggle implementation.

### `research/VAULT-TOUCH-DRAG-SCROLL-20260506.md`

Not read in full. Vault touch/drag/scroll implementation (includes R6 regression fix from
REGRESSION-TIMELINE).

---

## CRITICAL SECTION — NPDM vi:s AND vi:m ACCESS AUDIT

**Question:** Does uMenu's NPDM grant vi:s access for windowed compositing (e.g., CaptureScreenshot)?

**Verdict: YES — no NPDM change needed.**

Evidence from `docs/AUDIT-NPDM-uMenu-vi-access.md`:

| Service | Use case | Covered by current NPDM |
|---------|----------|------------------------|
| vi:s    | Fallback / CaptureScreenshot path | YES (Wildcard covers it) |
| vi:m    | Managed layer creation | YES (Wildcard covers it) |
| vi:u    | Normal display session | YES (Wildcard covers it) |

The `main.npdm` binary SAC bytes: `80 2a 00 2a` = host `*` + access `*`. This is the
upstream XorTroll v1.2.0 NPDM with wildcard `"service_access": ["*"]`.

File:line citation for vi:s coverage:
`docs/AUDIT-NPDM-uMenu-vi-access.md`, Section 1, service checklist table, row:
`vi:s | Fallback only | YES (Wildcard covers it) | "Not needed if vi:m works"`

The document also explicitly states: "Do NOT rebuild uMenu with a patched NPDM now. The
current wildcard NPDM already passes vi:m."

fw 20.0.0 landmine (`system_resource_size` field in NPDM JSON): the uMenu.json does NOT have
this field. Confirmed safe — will not cause PM to reject svcCreateProcess.

**For Option D (CaptureScreenshot via vi:s):** The NPDM access is not the blocker. The open
question is whether `IApplicationDisplayService::CaptureScreenshot` can be called by uMenu
(as qlaunch overlay) while a library applet occupies foreground. This is an IPC surface
question, not a permissions question.

---

## RECONCILIATION — Prior Docs vs Recent Research

### Contradiction 1 (CRITICAL): Sphaira "documented windowed mode hooks"

**Prior claim:** The Q OS v3.0 README (and earlier planning docs) described sphaira as having
"documented windowed mode hooks" that made it the planned first windowed NRO target.

**Current reality:** `docs/49_v3.1_research_sphaira.md` (2026-05-18) states explicitly:
"The Q OS v3.0 README's claim that sphaira has 'documented windowed mode hooks' was speculative.
There are no such hooks." Source-level evidence: `nwindowGetDefault()` hardwired in
`createFramebufferResources()`, no input hook point in `App::Poll()`.

**Impact:** Sphaira should be removed from any list of "first windowed NRO targets" and
reclassified as a catalog distribution target only. Alternative first targets: nx-hbmenu
(simpler C codebase) or NXThemesInstaller (linear framebuffer = memcpy redirect).

### Contradiction 2: L-CYCLE-WINDOW-MANAGER-DESIGN Section 5.4 NPDM note

**Prior note:** Section 5.4 states "uMenu NPDM is the upstream XorTroll v1.2.0 NPDM (the Q OS
fork NPDM was rejected by FW 20.0.0 per ROADMAP.md)" — this was written as a constraint,
implying NPDM limitations might block vi:m access.

**Current reality:** `docs/AUDIT-NPDM-uMenu-vi-access.md` (2026-04-18, post-L-CYCLE doc)
confirmed the upstream wildcard NPDM already covers vi:m. The note in L-CYCLE was a caution,
not a confirmed blocker, and has been superseded.

**Impact:** No NPDM work is required before implementing any vi:m, vi:s, or vi:u access
patterns in the windowed NRO work.

### Contradiction 3: `RESEARCH-libtesla-rendering.md` vi:m conditional patch advice

**Prior claim:** "Check `uMenu.json` service_access list. If missing, add it, or fall back to
viCreateLayer (stray layer)."

**Current reality:** The NPDM audit shows vi:m is covered by wildcard. The conditional patch
advice is moot.

**Impact:** No Tesla overlay work relevant to v3.1. The Tesla feasibility research
(OverlayApplet-only, wrong dimensions) already ruled it out.

### Contradiction 4: IPC session exhaustion — "is it fixed?"

**Status per research docs (2026-05-06):** `AMS-1.11-CLEAN-EXIT-CONTRACT-20260506.md` and
`APPLET-TEARDOWN-AUDIT-20260506.md` identified the ECS unregister leak and appletHolderJoin
gap as unfixed.

**Status per IPC-SESSION-POOL-EXHAUSTION-20260518.md (today):** v2.8.0 fixed `MaxPrivateSessions`
from 1 to 4 and reduced the `la_LibraryApplet.cpp` timeout from 15s to 2s. HW-confirmed.

**Open question:** Did v2.8.0 also include `ecs::UnregisterExternalContent` (IPC 65001)?
The `IPC-SESSION-POOL-EXHAUSTION-20260518.md` fix description focuses on
`MaxPrivateSessions`, `fsdevCommitDevice`, and the timeout. It does NOT explicitly mention
IPC 65001 / `AtmosphereUnregisterExternalCode`. If the ECS leak is still present, the 4-slot
headroom will still exhaust after 4+N launches (where N depends on concurrent ECS consumers).

**Recommended action:** Before v3.1 adds any new ECS launch paths, audit `uSystem/source/` for
any call to `ldrShellAtmosphereUnregisterExternalCode` / IPC 65001. If absent, the leak is
still live and must be fixed as a prerequisite.

### Reconciliation 5: `QOS-NRO-SURFACE-AUDIT-20260506.md` gaps vs current state

The audit (2026-05-06) identified four gaps: recursion depth, nxlink auto-launch, subdirectory
scan, applet/application toggle. Status:

| Gap | Fixed? | Evidence |
|-----|--------|----------|
| Subdirectory recursion | YES | `SCANNROS-RECURSION-20260506.md` — implementation documented |
| nxlink auto-launch | YES | `NXLINK-AUTOLAUNCH-IMPLEMENTATION-20260506.md` — implementation documented |
| Argv passing | UNKNOWN | Both docs show the fix was built but not explicitly deployed to HW |
| Applet/application UI toggle | NO | Described as future work; all call sites still hardcode LibraryApplet |

---

## OPEN QUESTIONS

### OQ-1 (Critical for Option D)
Can uMenu (running as qlaunch SystemApplet, TID `0x0100000000001000`) call
`IApplicationDisplayService::CaptureScreenshot` while a library applet (uLoader) occupies the
foreground rendering surface? The NPDM grants vi:s access. The open question is whether
`am` will service this IPC call from the qlaunch process when it is not the frontmost rendering
process. Source: `docs/49_v3.1_research_hbloader.md`, Section 4, Option D analysis.

### OQ-2 (Critical for Option E / cooperative ABI)
Do uLoader (TID `0x010000000000100D`) and uMenu (TID `0x0100000000001000`) share the same
`AppletResourceUserId` (ARUID)? If they do, `NVMAP_IOC_EXPORT_FOR_ARUID` works between them
without any kernel change. If they have different ARUIDs, cross-ARUID nvmap sharing requires
additional nvmap ioctl work. Source: `docs/49_v3.1_research_atmosphere_mitm.md`, Section 5,
open question 1.

### OQ-3 (Prerequisite for any new v3.1 ECS launch path)
Does v2.8.0 include `ecs::UnregisterExternalContent` (IPC 65001)? If not, the ECS session
slot leak is still live and will limit the number of NRO launches before pool exhaustion.
See Reconciliation 4 above. Source: `docs/research/AMS-1.11-CLEAN-EXIT-CONTRACT-20260506.md`,
Concrete Fix Patterns section.

### OQ-4 (For L+2 fallback implementation)
What is the exact behavior when `window-state.bin` is written before an NRO launch and the
NRO crashes or is terminated abnormally (does not call the clean exit path)? Does
`RestoreWindowState()` handle a stale `window-state.bin` gracefully? Source:
`docs/L-CYCLE-WINDOW-MANAGER-DESIGN.md`, Section 3.3.

### OQ-5 (For cooperative ABI — Option E)
Can a hosted NRO bypass deko3d and write directly via raw libnx nvmap + nvhost ioctls while
simultaneously being compiled against a standard homebrew SDK? The HBABI
`EntryKind::HostedNvmapId = 14` approach requires the NRO to opt in via a `libqos-hosted-nro`
header — what is the minimum API surface that header must expose to make existing NROs (e.g.,
ftpd) hostable with minimal source changes? Source: `docs/49_v3.1_research_atmosphere_mitm.md`,
Section 5, Option C analysis.

### OQ-6 (For nx-hbmenu as first windowed target)
`docs/49_v3.1_research_sphaira.md` identifies nx-hbmenu as a simpler alternative first target.
The C codebase has smaller patch surface. What specific modifications to nx-hbmenu's
`createFramebufferResources()` equivalent would be needed to redirect its framebuffer output
to a hosted nvmap buffer? This is the concrete first implementation task for Option E.
Source: `docs/49_v3.1_research_sphaira.md`, Section 6 (alternative targets).

### OQ-7 (For hbmenu cutover gate)
The `HBMENU-CUTOVER-PLAYBOOK-20260506.md` pre-cutover validation matrix (8 items) requires
item 6 (application context launch) to work before cutover. Is the applet/application UI
toggle (gap identified in `QOS-NRO-SURFACE-AUDIT`, still unimplemented per Reconciliation 5)
required to be complete before the hbmenu cutover gate? Source:
`docs/research/HBMENU-CUTOVER-PLAYBOOK-20260506.md`, Section 1, item 6 footnote.

### OQ-8 (For deko3d/Plutonium coexistence)
`docs/PLAN-v0.7-deko3d-imgui.md` states Plutonium coexistence is NOT viable because
SDL_Init unconditionally calls EGL+Mesa. This blocks any approach where the windowed NRO
runs its deko3d rendering in the same process as uMenu's Plutonium UI. The cooperative ABI
approach already sidesteps this by having the NRO run in its own process. However: if
Option D (CaptureScreenshot) is pursued, the captured JPEG/RGBA must be rendered inside
uMenu's Plutonium process. Does rendering a GPU-decoded texture in a Plutonium SDL context
require deko3d, or can it use SDL_Texture directly? Source: `docs/PLAN-v0.7-deko3d-imgui.md`,
Risk section.

---

*End of docs-archaeology-findings.md*
*Total docs covered: 23 top-level docs/ files + 36 docs/research/ files = 59 files*
*Files unread (not directly relevant to v3.1 or already covered by synthesis):*
*research/ARCH-CASCADE-AUDIT, AM-NSS-FW20-IPC-CONFIRM, ATMOSPHERE-CURRENT-RESEARCH,*
*ENVIRONMENT-AUDIT, FILEPICKER-IMPLEMENTATION, FW20-IPC-BUDGET-RESEARCH,*
*HEKATE-REBOOT-REGRESSION, LOGIN-REACTIVITY-V237-AUDIT, LOGIN-SCREEN-REACTIVITY-AUDIT,*
*MAIN-THREAD-BLOCKING-AUDIT, MTIME-ARCHAEOLOGY, RECOVERY-PLAN, REGRESSION-TEST-INFRASTRUCTURE,*
*SESSION-REGRESSION-AUDIT, TILE-ICONS-IMPLEMENTATION, TOOLCHAIN-TIMELINE-AUDIT,*
*UMENU-BINARY-DELTA, USYSTEM-BINARY-DIFF, VAULT-APPCONTEXT-TOGGLE, VAULT-TOUCH-DRAG-SCROLL*
