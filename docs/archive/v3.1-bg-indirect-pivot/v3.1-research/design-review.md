# v3.1 Design Review

> Critique of `docs/49_v3.1_windowed_homebrew_design.md` (2026-05-18 draft) +
> the Phase-1 feasibility experiment in `qd_FrameCaptureExperiment.cpp`.
> Reviewer mandate: find what is wrong or missing, not bless what looks correct.

## Severity legend
- BLOCKER — must fix before v3.1 Phase 2 starts.
- HIGH — should fix; if shipped as-is, will surface as creator pain in HW test.
- MEDIUM — design improvement; ship-acceptable but worth queuing.
- NOTE — informational; informs future work without blocking.

---

## Section A — Internal contradictions

### A-1 BLOCKER: The design's production primitive conflicts with its own Phase 1 experiment

The design doc's §0 TL;DR and §2.1 both establish `viGetIndirectLayerImageMap` (IApplicationDisplayService
cmd 2450) as the production capture mechanism for Phase 2. This primitive requires the library applet
to be launched with `LibAppletMode_BackgroundIndirect` and then have an `IndirectLayerConsumerHandle`
retrieved via `appletHolderGetIndirectLayerConsumerHandle`. The design also says "uMenu's wildcard NPDM
already grants this."

The Phase 1 experiment (`qd_FrameCaptureExperiment.cpp`) does NOT probe this mechanism at all. It
probes `capsscCaptureJpegScreenShot` — a screenshot-service call that is the acknowledged fallback/
probe primitive, NOT the production mechanism. This creates a direct gap: even a fully successful Phase
1 result (all 5 layer stacks return JPEG headers) proves only that `caps:sc` is reachable from uMenu's
NPDM context. It says nothing about whether `appletCreateLibraryApplet(..., LibAppletMode_BackgroundIndirect)`,
followed by `appletHolderGetIndirectLayerConsumerHandle` and `viGetIndirectLayerImageMap`, actually works.

Source:
- Design doc §2.1: "The PRODUCTION mechanism is `viGetIndirectLayerImageMap`"
- Design doc §6 / §2.1: "capsscCaptureJpegScreenShot ... remains as a fallback"
- `atmosphere-deep-dive.md §1.5`: latency for `capsscCaptureJpegScreenShot` is ~100 ms timeout-default;
  "not a per-frame API"

**The critical unasked question Phase 1 should also answer:** Does `viGetIndirectLayerImageMap` succeed
when called from uMenu's process context against a BackgroundIndirect child? The experiment as written
cannot answer this. Phase 1 is a valid quick smoke-test of NPDM permission, but if it passes, the team
needs a second probe for the actual production path or Phase 2 opens on an unverified primitive.

**Recommended fix:** Add a second probe path after the capssc loop: attempt
`appletCreateLibraryApplet(AppletId_LibraryAppletPhotoViewer, LibAppletMode_BackgroundIndirect)` with
a dummy NRO or wait-loop, retrieve the consumer handle, call `viGetIndirectLayerImageRequiredMemoryInfo`
(the non-destructive read), and log the result. This is the only on-hardware evidence that the
architectural pivot is viable before Phase 2 coding starts.

---

### A-2 BLOCKER: vi:m coexistence with vi:u is not confirmed — two `viInitialize` calls in one process

The design §2.3 says uMenu will call `viInitialize(ViServiceType_Manager)` to create the managed chrome
layer (the Tesla composition technique). The production capture path via `viGetIndirectLayerImageMap`
uses `IApplicationDisplayService` which is obtained from `vi:u` (ViServiceType_Application) by the
standard `viInitialize(ViServiceType_Default)` / `ViServiceType_Application` path that uMenu already
holds for its own SDL2 rendering.

**The question the design does not answer:** Does libnx support calling `viInitialize` twice with
different service types in the same process? Or does the second call replace the first?

From `libnx-applet-api-history.md §1.5` and libnx `vi.c` source: `viInitialize` is a service guard
wrapper. libnx has a single global `g_viIApplicationDisplayService` and related handles. A second
`viInitialize(ViServiceType_Manager)` call on a process that has already called `viInitialize()` via
SDL2 / Plutonium startup will hit the libnx service-guard path — if the guard considers the service
already initialized, it either no-ops or errors. The practical consequence: calling
`viCreateManagedLayer` may fail silently because the vi:m session was never actually opened.

From `tesla-overlay-mod-ecosystem.md §A.3`: Tesla's `viInitialize(ViServiceType_Manager)` is called
in a process that has NOT previously called `viInitialize` — Tesla's process is fresh, owned by
nx-ovlloader. uMenu has a long-running SDL2/Plutonium renderer that has already called `viInitialize`
internally during init.

**Evidence gap:** No research brief tested or cited any precedent for opening two vi service handles
(vi:u AND vi:m) simultaneously in the same long-running applet process. The design assumes this works
because the Tesla technique works — but Tesla runs in a separate fresh process.

**Recommended fix:** Before Phase 2 starts, add a minimal probe to the experiment or a dev-mode test:
call `viInitialize(ViServiceType_Manager)` from uMenu's running process (after Plutonium init) and
log the return code. If it returns success and `viCreateManagedLayer` succeeds, the assumption holds.
If it returns a service-guard collision, a different approach to chrome rendering is needed (e.g., a
stray layer via the existing vi:u session at a lower but still-above-NRO Z-order).

---

### A-3 BLOCKER: HID dead in BackgroundIndirect is treated as "might be a problem" — it IS the problem

The design §3 opens with: "The complication: HID is owned by the foreground applet. In Background
mode, the applet may not have HID access by default."

`atmosphere-deep-dive.md §6.3` is unambiguous: "When a library applet runs in Background or
BackgroundIndirect mode: HID input events remain routed to the parent or the topmost foreground applet.
The background child receives NO HID events unless the parent explicitly forwards them (no forwarding
API exists in libnx)."

`tesla-overlay-mod-ecosystem.md §A.4` confirms that Tesla's `hidsysEnableAppletToGetInput` mechanism
enables input FOR a process's ARUID, but does not constitute a forwarding channel from parent to
background child — the NRO in BackgroundIndirect mode does not have an ARUID registered in the hid
foreground chain at all.

The design proposes two options (A: pause HID, B: gate HID by focus) and defers Option B to v3.1.x.
But **Option A means the windowed NRO has zero input** — not just "no input when unfocused" but NO
input at all while running in BackgroundIndirect mode. A NRO that can't receive controller input isn't
usable except by mouse/keyboard users. The design §5 says "ship at whatever fps the experiment
establishes" but doesn't apply the same honest framing to input.

**What v3.1 actually ships under Option A:** A window that shows a live (or slow-JPEG) picture of an
NRO you cannot interact with. That is not windowed homebrew; it is windowed homebrew screenshot-mode.

The design should state this plainly in §0 TL;DR: "v3.1 delivers display-only windowed NROs. Input
forwarding is v3.1.x." The Phase 3 exit criterion ("two NROs simultaneously, each receiving input when
focused") is NOT a v3.1.x stretch goal — it is the minimum usable product for most NROs.

---

### A-4 HIGH: The diagram in §2.3 still shows `vi:s CaptureScreenshot` as the content path

The ASCII diagram in §2.3 labels the cross-applet capture arrow as "vi:s CaptureScreenshot":

    │           │ vi:s CaptureScreenshot       │
    └───────────┼──────────────────────────────┘

But §2.1 explicitly deprecates this in favour of `viGetIndirectLayerImageMap`. The diagram was not
updated to reflect the BackgroundIndirect architecture. A reader who skips the prose and reads the
diagram will think the JPEG-screenshot path is the architecture, not the indirect-layer path.

Source: Design doc §2.1 "capsscCaptureJpegScreenShot... remains as a fallback"; §2.3 diagram.

**Recommended fix:** Update the diagram to label the arrow "viGetIndirectLayerImageMap (BackgroundIndirect
consumer handle)" and move the "vi:s CaptureScreenshot (fallback)" path to a separate smaller diagram
or footnote in §6.

---

### A-5 MEDIUM: Design says `LibAppletMode_Background` in §2.2, but §2.1 and §8 require `BackgroundIndirect`

The §2.2 "new path" pseudocode says:
    uSystem: appletCreateLibraryApplet(AppletId_Album, LibAppletMode_Background)

`atmosphere-deep-dive.md §6.2` defines `LibAppletMode_Background` (mode 1) as: "Child applet runs in
background — it does NOT receive input focus. Child applet does NOT own the display; it cannot call
`viCreateLayer` to get a scanout layer visible to the user." That's compute-only. `LibAppletMode_BackgroundIndirect`
(mode 3) is what issues the `IndirectLayerConsumerHandle` and what §2.1 and BG-2's conclusion both
require for `viGetIndirectLayerImageMap`.

The pseudocode uses the wrong enum value. Using `LibAppletMode_Background` instead of
`LibAppletMode_BackgroundIndirect` means `appletHolderGetIndirectLayerConsumerHandle` returns an
error (libnx comment: "Only available when LibAppletMode is LibAppletMode_BackgroundIndirect") and the
entire architecture fails silently — the NRO runs in compute-only background with no display output.

Source: `atmosphere-deep-dive.md §6.1` LibAppletMode enum definitions; libnx `applet.h` comment on
`appletHolderGetIndirectLayerConsumerHandle`.

**Recommended fix:** Every occurrence of `LibAppletMode_Background` in §2.2 and §8 that refers to the
display-capture path must be `LibAppletMode_BackgroundIndirect`. Background (mode 1) should only be
mentioned in the context of future audio-only or compute-only NRO use cases.

---

### A-6 MEDIUM: `IHOSBinderDriverIndirect` openness — a confirmed risk buried in open questions

`atmosphere-deep-dive.md §Q5` identifies a concrete risk: uLoader calls `viInitialize` at the default
service level (vi:u). In BackgroundIndirect mode, the NRO's frames must flow to `IHOSBinderDriverIndirect`
(cmd 103 on `IApplicationDisplayService`) rather than `IHOSBinderDriverRelay` (cmd 100). But vi.c
only opens `IHOSBinderDriverIndirect` when `g_viServiceType >= ViServiceType_System` (vi:s or vi:m).

If uLoader's vi session stays at vi:u, the NRO's `nwindowQueueBuffer` calls go to
`IHOSBinderDriverRelay` (direct scanout) even in BackgroundIndirect mode. uMenu's
`viGetIndirectLayerImageMap` then returns empty/black frames because nothing ever wrote to the
indirect layer.

This is cited in §8 Risk #5 as a memory concern but NOT listed at all as a risk about the indirect
layer routing. The real risk — that the NRO frames simply never reach the indirect layer — is
absent from §8.

**Recommended fix:** Add to §8: "Risk N: uLoader vi service level must be vi:s (ViServiceType_System)
for the NRO's nwindowGetDefault() to route frames to IHOSBinderDriverIndirect. uLoader must call
`viInitialize(ViServiceType_System)` explicitly; the default vi:u path silently routes to direct
scanout instead, yielding black indirect-layer reads. NPDM wildcard already covers vi:s (confirmed
AUDIT-NPDM-uMenu-vi-access.md). Required change: one-line uLoader init change before
nroEntrypointTrampoline."

---

## Section B — Evidence chain holes

### B-1 BLOCKER: §2.1 claims `viGetIndirectLayerImageMap` is "callable from vi:u" — this needs a direct cite

Design doc §2.1: "Callable from vi:u service level — no vi:s privilege escalation required. uMenu's
wildcard NPDM already grants this."

`atmosphere-deep-dive.md §1.3` does confirm: "IApplicationDisplayService (cmd 2450/2460) is accessible
via vi:u, vi:s, or vi:m — all three levels grant access to the standard IApplicationDisplayService
commands." The cite chain is: `vi.c _viInitialize` opens `IApplicationDisplayService` from any root
service using inval=0 (vi:u) or inval=1 (vi:s/vi:m), and cmd 2450 is on `IApplicationDisplayService`.

**The gap:** The design states it as fact without citing `atmosphere-deep-dive.md §1.3`. A reader
following only the design doc cannot verify it. This is a citable claim that should be cited.

More importantly: the cited `atmosphere-deep-dive.md §1.3` notes the key constraint is NOT the service
level but the `IndirectLayerConsumerHandle`, and that handle is only issued under BackgroundIndirect.
The design acknowledges this but treats the "callable from vi:u" statement as the headline. The
headline should be: "Callable only when child is in BackgroundIndirect mode; the vi service level
(u/s/m) is irrelevant for permission — you need the handle, and the handle requires BackgroundIndirect."

### B-2 BLOCKER: §4 Phase 2 "~300-500 lines" estimate needs provenance

Design doc §4 Phase 2 states the uSystem state machine work is "~300-500 lines." `atmosphere-deep-dive.md §Q3`
is the source for this estimate: "Estimated change: 300–500 lines in uSystem; 50–100 lines in uMenu."
The design does not cite this.

More critically: the estimate is flagged in BG-2 as addressing only the minimum parallel-lifetime
change (`g_LibraryAppletHolder` + `g_BackgroundLibraryAppletHolder`). It does NOT include the new
SMI command, the IPC protocol extension, the consumer-handle routing from uSystem back to uMenu, or
the new `WindiwedNRO` state machine in uMenu. The full Phase 2 scope is closer to 800-1500 lines across
three components. The 300-500 number is the uSystem-only minimum slice, not the total Phase 2 scope.

Source: `atmosphere-deep-dive.md §Q3`; `49_v3.1_research_hbloader.md §5`: "This is a large, multi-
component change — not 100 lines. Rough order of magnitude: 1,000–3,000 lines."

**Recommended fix:** Cite BG-2 §Q3 and add: "Note: this estimate covers the uSystem state machine
refactor only. The full Phase 2 change set including new SMI command, consumer-handle plumbing from
uSystem to uMenu, and the capture loop implementation adds another 500-800 lines. Total Phase 2
estimate: 800-1,500 lines."

### B-3 HIGH: §10 Tier-1 NROs name `sys-clk-manager.nro` and `Ultrahand-Reload.nro` — no SD verification

Design §10 states "BG-6's audit identified the cleanest absorption candidates" and lists sys-clk
config surface as Tier-1. `tesla-overlay-mod-ecosystem.md §B.1` confirms sys-clk writes to
`/config/sys-clk/config.ini` and ships a manager NRO at `/switch/sys-clk-manager.nro`.

The design doc's §10 references the user's SD card having `Ultrahand-Reload.nro` and `sys-clk-manager.nro`
(phrased as if verified). The `docs-archaeology-findings.md §SYSMOD-AMS-1.11-COMPAT.md` mention is:
"nx-ovlloader... WerWolv issue #42 open... Do not run Tesla/EdiZon/sys-botbase concurrently with
nx-ovlloader." This establishes nx-ovlloader is on the SD; it does not explicitly confirm
`sys-clk-manager.nro` exists at `/switch/sys-clk-manager.nro` on this specific SD.

This is a minor omission but a pattern: the design presents SD state as confirmed when it was inferred
from sysmodule research. The absorption roadmap should be gated on actual SD inventory, not assumed presence.

---

## Section C — Phase-1 experiment correctness

### C-1 BLOCKER: 256 KB buffer is too small for 1920x1080 JPEG — success can be silent truncation

`qd_FrameCaptureExperiment.cpp` line 36: `constexpr size_t kJpegBufSize = 256 * 1024;`

The comment says "256 KB is a comfortable headroom for a 1080p JPEG at the system default quality."
This is wrong. The Switch screenshot system uses JPEG quality ~95 (default capsrv quality). A
1920×1080 frame at JPEG quality 95 is typically 400 KB to 1.2 MB depending on scene complexity.
A 720p frame at quality 95 runs 200–600 KB.

`capsscCaptureJpegScreenShot` does not return a truncation error when the output buffer is smaller
than the JPEG. It writes up to `jpeg_buf_size` bytes and sets `*out_size` to the number of bytes
actually written (which will equal `kJpegBufSize` = 262144). The experiment would then log:
`size=262144 jpeg_magic=YES` — a false positive that looks like a complete successful capture.
The captured JPEG would be truncated mid-stream, undecodable, but the experiment's logging doesn't
validate that the full JPEG was received — only the header bytes and the returned size.

**Real-world consequence:** On a complex scene (uMenu desktop with icons), the experiment will
almost certainly truncate and the log will read "success" incorrectly.

Source: libnx `capssc.h` function signature: `capsscCaptureJpegScreenShot(u64 *out_jpeg_size, void *jpeg_buf, size_t jpeg_buf_size, ...)` — `out_jpeg_size` is the bytes written, bounded by `jpeg_buf_size`.

**Recommended fix:** Increase `kJpegBufSize` to `2 * 1024 * 1024` (2 MB) to safely accommodate a
1080p JPEG at any quality level. A 2 MB heap allocation from uMenu's applet pool is trivial. Add a
post-capture check: if `out_size == kJpegBufSize`, log "WARNING: output may be truncated — buffer
size equals written size" to flag potential buffer exhaustion. Alternatively, call
`viGetIndirectLayerImageRequiredMemoryInfo` first to learn the exact required buffer size.

---

### C-2 HIGH: The experiment uses `capsscCaptureJpegScreenShot` but the design's production path is `viGetIndirectLayerImageMap`

This is the same gap called out in A-1 but framed from the experiment's perspective. The experiment
comment in `qd_FrameCaptureExperiment.cpp` line 2 says: "Goal: prove (or disprove) that uMenu's NPDM
grants caps:sc service access." That is a valid limited goal. But the experiment's own comment in
line 152 says it will "determine which ViLayerStack values returned valid JPEG headers — those are
the candidates for the v3.1 Phase 2 windowed-launch capture loop."

`ViLayerStack` values are relevant to `capsscCaptureJpegScreenShot`, not to `viGetIndirectLayerImageMap`.
`atmosphere-deep-dive.md §Appendix C` is explicit: "These [LayerStack enum] values are passed to
capsrv::CaptureJpegScreenshot... They are NOT related to `viGetIndirectLayerImageMap` — the indirect
layer mechanism bypasses the layer stack enumeration entirely."

The experiment's closing log message ("those are the candidates for the Phase 2 capture loop") is
architecturally incorrect. If the design commits to the BackgroundIndirect / `viGetIndirectLayerImageMap`
path, the ViLayerStack results from this experiment have no bearing on Phase 2.

**Recommended fix:** Change the closing log message to accurately state the experiment's scope: "This
confirms caps:sc is reachable from uMenu's NPDM. Note: Phase 2 uses viGetIndirectLayerImageMap (not
capssc) — this result does not predict Phase 2 capture behavior. See atmosphere-deep-dive.md §1.3."

---

### C-3 HIGH: Experiment runs BEFORE `Show()` — it runs against uMenu's own layer, not a running NRO

The call site in `main.cpp` lines 531-538 places `RunFrameCaptureExperiment()` AFTER `g_MenuApplication->Load()`
and BEFORE the `ShowWithFadeIn()` / `Show()` call. This means:

1. uMenu's Plutonium render loop has NOT started rendering frames to the display yet (Load initializes
   the UI tree but does not start painting).
2. No NRO is running — uMenu is the only process in the applet slot.
3. The ViLayerStack_Default and ViLayerStack_Screenshot captures will show whatever the system
   compositor has on screen at that moment (typically the previous applet's last frame or a blank
   screen).

This is the right timing for a "what does caps:sc see at boot" probe. However, it does NOT tell you
what `capsscCaptureJpegScreenShot` returns while an NRO is actively rendering in the applet slot —
which is the scenario Phase 2 actually needs. The experiment measures the idle-boot state, not the
running-NRO state.

This is acceptable as a "does the permission work at all?" test. The design should acknowledge it in
§6.4: "Phase 1 captures uMenu's own boot-time compositor state, not a running NRO's output. A second
hardware test with a running NRO is needed to confirm cross-applet capture works (this is Phase 2's
first day task)."

---

### C-4 MEDIUM: `ViLayerStack_Arbitrary` (value 5) is AM-internal — probing it may cause AM abort

`atmosphere-deep-dive.md §Appendix C`: `LayerStack_Arbitrary = 5` is annotated as "AM internal; do
not use." The libnx-applet-api-history.md §1.6 labels it "Arbitrary (used by am)." Neither document
predicts the outcome of calling `capsscCaptureJpegScreenShot` with this stack from a non-AM process.

The most benign outcome is a permission error. The less benign outcome is that AM interprets an
unexpected `LayerStack_Arbitrary` capture request from a non-AM caller as a state violation and
asserts internally — which on HOS 20.0.0 causes an am abort and a reboot. This is unlikely but not
zero-risk on real hardware.

**Recommended fix:** Remove `ViLayerStack_Arbitrary` from `kProbes[]`. The experiment gains nothing
from it that `ViLayerStack_Default` doesn't already cover, and the risk of an AM abort on real
hardware is non-trivial.

---

### C-5 NOTE: 7 log lines per boot is correct but the comment in the HPP says "7 lines" when the .cpp produces more

The HPP comment line 14 says: "Output: 7 log lines per call." The actual implementation logs:
- 2 lines before `capsscInitialize` result
- 1 line for `capsscInitialize` result
- For each of 5 probes: 1 line each (success or warn)
- 1 closing line
= 9 lines minimum (if all probes succeed), more if any warn.

Minor. The comment should say "9-14 log lines per call (varies by success/warn per probe)."

---

## Section D — Scoping honesty

### D-1 v3.1 as designed ships a display-only window, not an interactive windowed app

The design's §0 TL;DR says "uMenu stays alive in the background, captures the applet's rendered output
via the existing vi:s CaptureScreenshot mechanism and composites the captured pixels into a Q OS window
of any user-chosen size." The word "existing" and the framing around CaptureScreenshot is a carryover
from an earlier design iteration. The actual architecture (BackgroundIndirect + viGetIndirectLayerImageMap)
is deeper infrastructure work.

Compounding this: §3 Option A (v3.1 MVP) gives the NRO zero input. Combined with the JPEG-capture
path being 100ms/frame (10fps max), even `ViLayerStack_Default` via `capsscCaptureJpegScreenShot`,
v3.1 Phase 2 as designed would ship: a window showing a 10fps slide-show of an NRO you can't interact
with. The design needs to say this plainly, because the creator should greenlight scope knowing exactly
what ships.

The `atmosphere-deep-dive.md §1.5` does establish that `viGetIndirectLayerImageMap` is a
single-service-call round-trip expected under 1ms, which would give real-time frame rates — but only
if the BackgroundIndirect pivot works end-to-end. That's the entire architectural bet of Phase 2.

**The honest v3.1 deliverable statement:** "v3.1 ships a window that displays a live image of a running
NRO via viGetIndirectLayerImageMap at up to 60fps (display only — no input). Input forwarding via a
shared-memory HBABI shim in uLoader is v3.1.x."

---

## Section E — Omissions

### E-1 HIGH: No NRO close behavior specified

What happens when the user taps the window's × button? The design defines that
`appletHolderGetStateChangedEvent` fires when the NRO exits naturally. But it says nothing about the
× button initiating an exit. The NRO process (hbloader + NRO) is running in a background applet slot.
There is no `appletRequestExit()` call path from uMenu to the background child. The only way to stop
it is `appletHolderRequestExitOrTerminate` from uSystem.

`docs/research/AMS-1.11-CLEAN-EXIT-CONTRACT-20260506.md` (referenced in docs-archaeology-findings.md)
established the mandatory clean-exit contract: `appletHolderRequestExitOrTerminate` + `appletHolderJoin`
+ `appletHolderClose`. The design has no section on NRO lifecycle termination from the user side. If
the user hits × and uMenu just destroys the window without going through the applet teardown sequence,
the applet holder leaks — contributing to the ECS session pool exhaustion that caused the 2026-05-06
incident.

The design must specify: "Pressing × sends `appletHolderRequestExitOrTerminate` to uSystem via a new
SMI command. uSystem calls the clean-exit sequence. On `StateChangedEvent`, uMenu removes the window."

### E-2 HIGH: No multi-window behavior specified — second windowed NRO kills the first

`49_v3.1_research_hbloader.md §4 Note` and `atmosphere-deep-dive.md §Q3` both state: the
`la::IsActive()` check means uSystem's state machine is sequential — one library applet at a time.
Even with a `g_BackgroundLibraryAppletHolder` addition, the applet slot model (photoViewer ECS override)
is one-slot: opening a second background NRO window would try to launch a second uLoader in the same
Album applet slot, which AM will reject or which will kill the first uLoader.

The design's Phase 3 exit criterion includes "two NROs simultaneously" but gives zero analysis of
whether the applet slot model can support it. From the research evidence, it cannot without either:
(a) a second applet slot ECS override on a different AppletId (requires changing the Atmosphère
`override_config.ini` — the very thing noted in `libnx-applet-api-history.md §6.4` as having previously
broken the reboot trampoline), or (b) a way to run multiple NROs in a single uLoader process (a major
uLoader refactor).

The design should acknowledge this in §3 or §8: "Multiple simultaneous windowed NROs may require a
second ECS slot. The photoViewer slot is currently the only active override; adding a second slot risks
the override_config.ini breakage documented in `qd_NintendoApps.cpp`. This is a Phase 3 gating
investigation, not Phase 2 or v3.1.x scope."

### E-3 HIGH: Audio model for background NROs is never addressed

Every research brief — including the six listed in the cross-refs — addresses display. None addresses
audio. When uLoader runs in `LibAppletMode_BackgroundIndirect`:
- Does the NRO's `audoutInitialize()` succeed in background mode?
- Does the NRO's audio output mix with uMenu's BGM, fight it, or get silenced by AM?
- `docs/research/docs-archaeology-findings.md §47_Integratable_Tasks_Catalog`: NXMilk + USB serial
  conflict with `audoutInitialize` already documented — audio init from a second context is a known
  conflict point.

For NROs like RetroArch (which emulates audio at exact timing), running in BackgroundIndirect mode
with undefined audio ownership could produce garbled output, silence, or a crash on `audoutInitialize`.

The design §5 defers "performance optimization" but audio ownership is not a performance question — it
is a functional question about whether BackgroundIndirect mode permits `audoutInitialize` at all. This
must be investigated before Phase 2 is declared complete for audio-producing NROs.

### E-4 MEDIUM: The design is silent on what happens if the background NRO crashes

NROs crash. RetroArch in particular has been documented as occasionally faulting on certain cores.
If the NRO crashes inside uLoader, `StateChangedEvent` fires — but the event signals an abnormal
exit, not a clean exit. uMenu gets the event, removes the window. uSystem must then: close the applet
holder (calling `appletHolderClose` on a crashed applet), unregister the ECS slot, and decide
whether to restart uMenu or wait.

The `docs/research/CRASH-FORENSICS-EVIDENCE-20260506.md` documented uLoader Undefined Instruction
crashes as structurally separate from uSystem crashes. That is still true — but a crashed NRO in
BackgroundIndirect mode leaves an applet holder that must be cleaned up differently than a
clean-exit holder. The `appletHolderRequestExitOrTerminate` 2s timeout (reduced from 15s in v2.8.0)
would fire on a crashed process, eventually force-close it, and trigger `StateChangedEvent`. The
design should document this crash-recovery flow explicitly rather than leaving it as an exercise for
Phase 2 implementors.

### E-5 MEDIUM: ECS session leak status is unresolved — a prerequisite for adding any new ECS path

`docs-archaeology-findings.md §Reconciliation 4` identifies an open question: did v2.8.0 include
`ecs::UnregisterExternalContent` (IPC cmd 65001)? If the ECS leak is still live, the existing 4-slot
pool (`MaxPrivateSessions = 4`) is the only buffer against exhaustion. Adding a new BackgroundIndirect
launch path (which requires its own `AtmosphereRegisterExternalCode` + `AtmosphereUnregisterExternalCode`
cycle per windowed NRO launch) will accelerate pool exhaustion.

The design §12 sign-off criteria do not include "verify ECS leak is fixed." It should.

---

## Section F — What I would NOT change

These parts are correct and do not need revision:

1. **The core architectural pivot: LibAppletMode_BackgroundIndirect + viGetIndirectLayerImageMap.**
   `atmosphere-deep-dive.md §Conclusion` arrives at this unambiguously and the design correctly
   adopts it. The mechanism is proven by Nintendo's own swkbd inline mode. It is the right production
   path.

2. **The NPDM wildcard coverage conclusion.** `docs-archaeology-findings.md §Critical Section` and
   `AUDIT-NPDM-uMenu-vi-access.md` both confirm the wildcard covers vi:u, vi:s, and vi:m. No NPDM
   patch is needed for any aspect of v3.1. The design correctly states this.

3. **The Atmosphère-no-changes conclusion.** `atmosphere-deep-dive.md §7` confirms Atmosphère's
   loader treats BackgroundIndirect identically to AllForeground; no Atmosphère modification is needed.
   The design correctly confirms this.

4. **Sphaira reclassification.** §1 correctly identifies the false v3.0 "sphaira windowed hooks"
   claim and removes it. Sphaira is correctly repositioned as a catalog-distribution target.

5. **The via-vi:m managed layer chrome technique (Tesla steal).** The design correctly adopts
   `viCreateManagedLayer + viSetLayerZ(max) + viAddToLayerStack` from Tesla for window chrome. This
   technique is proven in production by dozens of Tesla overlays. The NPDM wildcard covers vi:m.

6. **The §12 sign-off criteria.** The four-gate structure (BG research folded, Phase 1 HW-tested,
   review addressed, creator approval) is the correct process. The order of operations is sound.

7. **The NRO compatibility matrix (§4.5).** The 17-NRO audit is thorough and the hazard classifications
   are accurate. The hazard annotations for EdiZon SE, RetroArch, and breeze reflect real evidence
   from community-nro-audit.md.

8. **The deferred scope list (§5).** Deferring Nintendo first-party applets, retail NCA titles, and
   performance optimization is the right scope discipline.

---

## Section G — Recommended order of operations

Given the critique above, Phase 2 should not start until these gates are cleared in order:

**Gate 0 (before any Phase 2 code): Verify ECS leak status**
Audit `uSystem/source/` for any call to `ldrShellAtmosphereUnregisterExternalCode` (IPC 65001).
If absent, fix the leak first. A new BackgroundIndirect launch path that leaks ECS slots will
exhaust the session pool faster than AllForeground did.

**Gate 1 (Phase 1 — existing experiment): Deploy and read the caps:sc probe**
Current experiment is valid for confirming NPDM permission. Fix the buffer size (256 KB → 2 MB)
and remove `ViLayerStack_Arbitrary` before deploying to hardware. Read the log.

**Gate 2 (Phase 1 extension): Probe the actual production primitive**
After caps:sc succeeds, add a second probe: call `viInitialize(ViServiceType_Manager)` from uMenu's
running process and verify it returns success alongside the existing SDL2/Plutonium vi session. This
directly tests the A-2 contradiction.

**Gate 3 (Phase 1 extension): Probe BackgroundIndirect consumer handle**
Add a minimal BackgroundIndirect probe: launch a dummy NRO (or the existing hbmenu.nro) with
`LibAppletMode_BackgroundIndirect`, retrieve the consumer handle, call
`viGetIndirectLayerImageRequiredMemoryInfo` (side-effect-free query), log the result. This is the
only on-hardware evidence that the architectural pivot is viable.

**Gate 4 (before Phase 2 coding): Fix `LibAppletMode_Background` → `LibAppletMode_BackgroundIndirect` in §2.2 pseudocode**
Update the design doc enum values, the diagram, and all references. The wrong enum used in a real
code review will cause hours of debugging silent black-frame output.

**Gate 5 (Phase 2 scope entry): Confirm uLoader vi:s init requirement**
Document that uLoader must call `viInitialize(ViServiceType_System)` before `nroEntrypointTrampoline`
in BackgroundIndirect mode, to ensure the NRO's frames go to `IHOSBinderDriverIndirect` rather than
`IHOSBinderDriverRelay`. Add this to the Phase 2 sub-task list.

**Gate 6 (Phase 2 scope entry): Scope the NRO close / crash path**
Add the × button close flow and crash-recovery flow to Phase 2 sub-task list. These are not optional
polish — an unclosed applet holder is a session leak.

**Gate 7 (Phase 2 scope exit): Audio smoke test**
Before declaring Phase 2 complete, test one audio-producing NRO (JKSV is audio-silent; use AIO
Switch Updater which plays no audio but initializes audout) to confirm `audoutInitialize` succeeds
in BackgroundIndirect mode.

---

*Review date: 2026-05-18*
*Reviewer: design-review agent (critical-eyes pass)*
*Scope: docs/49_v3.1_windowed_homebrew_design.md + src/projects/uMenu/source/ul/menu/qdesktop/qd_FrameCaptureExperiment.cpp*
