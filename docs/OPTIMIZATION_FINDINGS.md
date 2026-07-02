# QOS uLoader/uSystem — Optimization & Root-Level Architecture Audit

Source: 3-agent deep audit (workflow wxu62ads6), 2026-06-21. Scope: ALL of uSystem + uLoader (every file beyond uMenu).
Goal: cut compute, beat Nintendo perf, max compatibility, build NROs-as-windows at the root level (no band-aids).

## AREA: uSystem CORE daemon — /Users/astral/QOS/tools/qos-ulaunch-fork/src/projects/uSystem/source/main.cpp (2693 lines) + la_LibraryApplet.cpp + app_Application.cpp + app_ControlCache.cpp + ecs_ExternalContent.cpp + sf_IpcManager.cpp + sys_SystemApplet.cpp + smi_SystemProtocol.cpp

### Root-level architecture notes

NROS AS WINDOWS — NATIVE ARCHITECTURE (no band-aids):

The fundamental constraint is AM's single-library-applet-per-SystemApplet rule: at any moment, exactly one AppletHolder backed by an ILibraryAppletAccessor may be live inside uSystem. This is not a uLaunch limitation — it is enforced in the am sysmodule (am::ILibraryAppletCreator::CreateLibraryApplet returns ResultNotPermitted when a holder is already active against the same system-applet session). The evidence from STATE.toml (the attempted dual-holder resident-uMenu in v3.7.47-v3.8.2, which broke game launching, rc=0xD37C) confirms this limit is real.

THE ROOT-LEVEL SOLUTION IS A SINGLE AM IPS PATCH:

Target: am sysmodule binary, the check inside ILibraryAppletCreator::CreateLibraryApplet that enforces the one-holder-per-system-applet limit. The patch removes or widens this limit to allow N concurrent AppletHolder objects under a single SystemApplet. STATE.toml already identifies this as 'F_absorption: am IPS patch (HOME-from-game am-2128-0035 limit)' — the exact same root cause.

With the IPS patch applied:

1. MULTI-HOLDER REGISTRY IN la_LibraryApplet.cpp:
   Replace the single g_LibraryAppletHolder with a std::map<WindowId, AppletHolder> g_WindowHolderMap. Each NRO window maps to its own AppletHolder. uMenu's holder is one permanent entry (WindowId=0 or kMenuWindowId). Open/close/focus operations act on a specific WindowId. The ECS program_id per holder is tracked in a parallel map<WindowId, u64> g_WindowEcsProgramId replacing the single g_PendingEcsProgramId.

2. CONCURRENT ECS SLOTS:
   Each window's NRO launch calls ecs::RegisterExternalContent(takeover_program_id[window_id], nro_path). With N windows, N ECS registrations are live simultaneously. Raise __nx_fs_num_sessions accordingly (3 + N). The AMS ServerManager ECS slot budget (currently MaxEcsExtraSessions=5 in sf_IpcManager.hpp) limits the number of concurrent windows — this is the second constraint to lift (increase MaxEcsExtraSessions and the AMS ServerManager pool).

3. FOREGROUND WINDOW CONCEPT:
   Horizon's compositor still has one foreground applet slot (the VI layer z-order owner). The windowed desktop does NOT need multiple foreground claims — it needs multiple running applets whose framebuffers are composited by the overlay layer. uMenu owns the foreground VI layer (the Tesla/persistent chrome layer in overlay_TestLayer.cpp). Each NRO window runs in LibAppletMode_AllForeground but gets a new OffscreenLayer (or SubLayer) assigned to it by a custom vi layer manager. The overlay's persistent max-Z vi layer (already implemented, currently disabled in Initialize() with UL_ENABLE_TESLA_OVERLAY=0) acts as the compositor chrome — window frames, title bars, close buttons drawn by uSystem on top of all NRO framebuffers.

4. WINDOW LIFECYCLE (corrected, no band-aids):
   - Open window: ecs::RegisterExternalContent(takeover_pid[slot], nro_path); la::Create(applet_id, la_version) on g_WindowHolderMap[id]; la::Launch(id) → NRO starts, gets its own offscreen/managed VI layer; uSystem assigns a window chrome tile in the overlay.
   - Focus window: vi layer z-order reorder (bring that window's layer to front). uMenu stays as the 'desktop shell' rendering the taskbar/dock behind.
   - Close window: la::ForceTerminateNow(holder[id]); ecs::UnregisterExternalContent(pid[id]); remove from registry; release VI layer slot.
   - uMenu NEVER exits. It is the permanent background compositor surface. Game launches require la::Terminate of ALL NRO windows (am's game-launch rule: no library-applet holder while a title is running) — this is WHERE a 'suspend all windows, launch game, restore' mechanic lives.

5. THE HOME-FROM-GAME ROOT CAUSE (direct fix):
   The am 2128-0035 (0x4680, exitreason=2 = Abnormal) is triggered because uMenu#2 launches as a second in-session library applet while the am slot from uMenu#1's exit is not yet clean (the ILibraryAppletAccessor is technically closed but am's internal state machine has not reset). The IPS patch that removes the single-holder limit also removes this state machine check. With the patch, LaunchMenu after a game exit is identical to LaunchMenu at boot — am simply opens a new holder, no state machine conflict. The 2-second settle sleep (line 2035) and the FS-readiness probe (lines 1914-1964) become optional quality-of-life improvements rather than mandatory workarounds.

6. ULOADER'S ROLE WITH MULTI-WINDOW:
   uLoader (the homebrew loader NRO) currently serves as the ECS takeover shim that intercepts a system applet slot and loads an arbitrary NRO into it. With multi-window, uLoader still performs this function — one uLoader instance per window, each in its own AppletHolder. The TargetInput (nro_path + args) is pushed as AppletStorage before appletHolderStart, exactly as today. No changes needed to uLoader itself for the basic multi-window scenario.

COMPUTE EFFICIENCY NORTH STAR:

uSystem should spend near-zero CPU in the idle desktop state. The path to that:
1. MainLoop: waitMulti-driven (event-per-holder + applet-message event + SMI waiter), not polled. Wakes only when a real event fires.
2. ApplicationControlCacheThread: semaphore-gated on work, not time-polled. Wakes only when an app is installed/uninstalled.
3. EventManagerMain: already correct (waitMulti(UINT64_MAX)). Remove the spurious post-wakeup sleep.
4. UsbViewer threads: 16 ms cadence (60 fps), not 1 ms (1000/s).
5. Verify thread: 100 ms poll, not 100 µs.
6. No blocking calls on MainLoop thread: Terminate() paths (NRO/app/menu) all go through the non-blocking state machines. The 2-second settle sleep becomes an event-driven FS probe. The 15-second app::Terminate() blocking eventWait is eliminated.

With these changes uSystem consumes demonstrably less CPU than stock qlaunch during idle desktop use, because stock qlaunch's SystemApplet loop runs at a similar cadence but cannot be verified to be event-driven — Q OS's version will be provably event-driven with measured near-zero idle CPU from the armGetSystemTick profiling hooks already in place.

### Findings (23)

| file:line | cat | impact | eff | opportunity |
|---|---|---|---|---|
| la_LibraryApplet.cpp:56-105 | architecture | high | L | For the NROs-as-windows (multi-window desktop) goal: replace the single-holder model with a holder-per-window abstraction. Each 'window' corresponds to one AppletHolder running in LibAppletMode_AllForeground (or a future LibAppletMode_Background once AM restrictions are relaxed via an AMS IPS patch) |
| main.cpp:2300-2397 | compatibility | medium | S | Fetch the account list outside the lock scope, before acquiring any record locks. The UIDs are needed only for EnsureApplicationEntry and DeleteApplicationEntryRecursively — both of which can be deferred to after the lock is released. Pattern: (1) fetch UIDs without any record lock, (2) acquire all  |
| ecs_ExternalContent.cpp:34-50 | compatibility | medium | S | Raise __nx_fs_num_sessions to 4 to give the ECS a dedicated slot with room for the readiness probe. Also: the ECS SubDirectoryFileSystem wrapping happens at RegisterExternalContent time but only needs the SD session while the applet is alive. Consider caching the RemoteFileSystem wrapper in a proces |
| main.cpp:2267 | compute-cut | high | M | Replace the fixed 10 ms sleep with event-driven blocking. uSystem already has a library-applet StateChangedEvent (appletHolder.StateChangedEvent), an applet message event (appletGetMessageWaitHandle), and the SMI storage waiters. Combine them via waitMulti with a generous timeout (50–100 ms) and onl |
| main.cpp:2035 | compute-cut | high | M | Replace with the same event-driven FS-readiness probe used in the NRO path (Phase 3 above). The problem being solved (am needs time to release the application slot before accepting a foreground library-applet launch) has the same root cause as the NRO fsp-srv probe: a server-side session teardown la |
| main.cpp:590-616 | compute-cut | high | M | This function runs synchronously on the MainLoop thread blocking all IPC, applet messages, and action dispatch. Move it to a background thread (or the EventManagerMain thread, which already handles record changes). The EventManagerMain thread already calls equivalent logic when nsGetApplicationRecor |
| app_Application.cpp:69-84 | compute-cut | high | M | Migrate app::Terminate() to the same non-blocking pattern as app::RequestExitNonBlocking() + app::CheckTerminated() + app::ForceTerminateNow(). The blocking Terminate() is now only called from the SystemMessage::TerminateApplication SMI handler (line 1083) — switch it to push a TerminateApplication  |
| main.cpp:1870-1912 | compute-cut | medium | M | Run Phase 2 and Phase 3 in PARALLEL in a dedicated short-lived thread, or coalesce them into a single probe loop that tests both conditions (sm + fs) in the same iteration. Better: since the trace evidence shows sm clears in ~10 ms and fs is the real bottleneck, skip the sm probe entirely — it provi |
| app_ControlCache.cpp:187-188 | compute-cut | medium | S | Replace the spin-poll with a semaphore or condition variable. In InitializeControlCache, create an ams::os::SemaphoreType (or ams::os::EventType with AutoClear) and signal it in AllowCacheDrain(). The cache thread does ams::os::AcquireSemaphore() and blocks at the kernel level. Zero CPU cost during  |
| app_ControlCache.cpp:192-196 | compute-cut | medium | S | Add a counting semaphore or condition variable that is signaled in RequestCacheApplication() and RequestRemoveApplicationCache() before pushing to their queues. The steady-state thread waits on the semaphore and only wakes when there is actually work. The 10 ms sleep becomes the fallback 'drain rema |
| app_ControlCache.cpp:287-308 | compute-cut | medium | S | Replace IsQueryLocked()/spin with a proper mutex acquire: take g_ApplicationCacheLock directly inside QueryApplicationNacpMisc (which already does this) and block at the kernel level. The outer retry loop then only fires on a genuine cache miss (app not yet cached), not on a lock contention hit. The |
| app_ControlCache.cpp:60-65, 251-257 | compute-cut | medium | S | Replace std::find_if(g_ApplicationCache->begin(), g_ApplicationCache->end(), [app_id]...) with g_ApplicationCache->find(app_id) in both functions. This is a one-line fix in each site and restores the hash map to O(1) lookup. With 100+ installed games, the current code does 100 comparisons per lookup |
| main.cpp:2411-2437 | compute-cut | medium | S | Raise the sleep cadence to match the target frame rate: 33 ms for 30 fps, or 16 ms for 60 fps. The write thread should sleep 16 ms; the capture thread (read) should capture at the same rate. This reduces USB viewer CPU load by ~16x with no perceptible quality loss to any human viewer. The RwLock con |
| main.cpp:173-175 | compute-cut | medium | S | Raise VerifyStepWaitTimeNs from 100'000 (100 µs) to at least 100'000'000 (100 ms). For a verify operation that takes minutes, 10 fps of progress updates is more than sufficient for UI feedback. A 100 ms interval reduces ns IPC volume by 1000x during verification and frees the ns session for other ca |
| la_LibraryApplet.cpp:120-157 | compute-cut | medium | M | la::Terminate() is still called from one place: the TerminateMenu action handler (line 1640). This is distinct from the non-blocking NRO/app terminate paths. To remove the 2-second wait, install an appletGetMessage handler in uMenu that responds to RequestExit by setting a termination flag and break |
| main.cpp:2232-2265 | correctness | medium | S | Introduce a boolean 'g_LaunchedMenuThisIteration' set to true by LaunchMenu() and reset at the top of MainLoop. The safety-net should check this flag before calling LaunchMenu() again. Alternatively, refactor the final check to look at la::IsActive() AFTER the NRO/app terminate handlers run (current |
| main.cpp:224-246 | compute-cut | medium | S | When UL_ENABLE_TESLA_OVERLAY is 0, reduce LibnxHeapSize back toward a smaller value. The 32 MB was sized for Tesla's ~5.4 MB framebuffer + headroom. Without Tesla, the libnx heap only needs to cover stdlib allocs, IPC storages, and general malloc — the pre-Tesla value of 1 MB was too small (per the  |
| main.cpp:2395 | compute-cut | low | S | Remove the svcSleepThread(100'000) at line 2395 entirely. EventManagerMain already blocks forever in waitMulti(UINT64_MAX) at lines 2320/2323 and only wakes on a real event. The post-wakeup sleep serves no debounce or rate-limiting purpose: nsGetApplicationRecordUpdateSystemEvent is an auto-clear ke |
| main.cpp:1685-1775 | compute-cut | low | S | For the production path (g_DevMode == false) this is already dead code — no change needed. For dev mode, consider using inotify-equivalent (sdmcWaitForChange, if available) or increasing the poll interval from 30 to 150+ iterations (~1.5 s) since cmd files are manual FTP drops, not sub-second events |
| main.cpp:608-613 | correctness | low | S | Remove the second unconditional call at line 613 — it is dead code given the stub's always-true return. If/when CacheSingleApplication is re-implemented with real NS calls, the duplicate would issue two full NS IPC round-trips per added game. Removing it now prevents a latent double-IPC bug. Also no |
| main.cpp:2098-2106 | compute-cut | low | S | Replace std::vector with std::deque for g_ActionQueue: front-erase is O(1) on std::deque vs O(N) on std::vector (since elements after i must be shifted). Alternatively, since only one action is consumed per MainLoop iteration, use a single pop_front() from a std::deque after HandleAction returns tru |
| main.cpp:2130-2183 | perf | low | S | Cache the watchdog deadline as an absolute tick (g_LastMenuHeartbeatTick + kWatchdogStaleTicks) and only re-evaluate when the current tick exceeds it. This is a single comparison per iteration with no branch into the watchdog body for 30 seconds at a time. Alternatively, since kWatchdogStaleTicks is |
| la_LibraryApplet.cpp:263-282 | compute-cut | low | S | With only 17 entries, the linear scan is not a real bottleneck. However, for correctness: GetAppletIdForProgramId and GetProgramIdForAppletId both call UL_ASSERT_FAIL on a miss — if an unknown program_id or applet_id is ever passed, they crash uSystem. Add an out-parameter boolean or return an Optio |

## AREA: uSystem subsystems + shared libs (la/, ecs/, sf/, smi/, sys/, app/, overlay/, uCommon headers)

### Root-level architecture notes

## Root-Level Architecture for NROs as Concurrent Windows

### Why the Current Architecture Cannot Do It Without AM Modification

The fundamental constraint is HOS AM's library-applet slot model. AM enforces that:
1. A background system-applet caller (uSystem via `appletCreateLibraryApplet`, cmd 4) can only create ONE in-session library applet. The second creation returns 2128-0035 (0x4680). This is the HOME-from-game blocker.
2. A foreground library-applet caller (uMenu via `appletCreateLibraryAppletSelf`, cmd 10) CAN create additional applets — this is the `LaunchHomebrewWindowedLibraryApplet` architecture already partially implemented.
3. A library applet cannot coexist with an Application (game) — AM returns 0xD37C on `appletCreateApplication` while any library-applet holder is open.

The **NRO-as-window** architecture must therefore be built entirely on `appletCreateLibraryAppletSelf` (cmd 10), called from uMenu (the foreground library-applet), NOT from uSystem. uSystem's role is limited to ECS registration (`ecs::RegisterExternalContent` via `ldrShellAtmosphereRegisterExternalCode`, cmd 65000) because ldr:shell is not granted to uMenu's NPDM. This is already the `LaunchHomebrewWindowedLibraryApplet` architecture in the SMI protocol.

### Native Root-Level Design for Concurrent Windows

**Component: uSystem (system applet, always resident)**
- Owns `g_PendingEcsProgramId` and performs `ldrShellAtmosphereRegisterExternalCode` + `ldrShellAtmosphereUnregisterExternalCode` (ECS) on behalf of uMenu.
- Manages a pool of `MaxEcsExtraSessions=5` ECS registrations per the existing `sf_IpcManager.hpp:58` count. This limits concurrent windowed NROs to 5 (the pool ceiling). Raising this constant raises the concurrent window ceiling — the only uSystem-side constraint.
- Provides `LaunchHomebrewWindowedLibraryApplet` SMI: receives TargetInput, performs ECS register, returns resolved AppletId to uMenu.
- DOES NOT own any `AppletHolder` for windowed NROs — those are entirely uMenu-side.
- On HOME press while windowed NROs are foreground: sends `HomeRequest` to uMenu's SMI message queue (already implemented). uMenu manages its own `appletCreateLibraryAppletSelf` lifecycle for the windowed NRO.

**Component: uMenu (library applet, foreground)**
- Owns ALL windowed NRO `AppletHolder` objects via `appletCreateLibraryAppletSelf` (libnx `applet.h:1183`). This is the only IPC path AM allows from a background-non-existent caller.
- Each NRO window = one `AppletHolder` created with `LibAppletMode_AllForeground` or `LibAppletMode_BackgroundIndirect`. `AllForeground` takes full display; `BackgroundIndirect` lets uMenu capture the framebuffer via `viGetIndirectLayerImageMap` — but this is gated by am 2114-0011 on consumer hardware (DEAD per ABSORPTION_PROGRAM.md). Therefore: each windowed NRO must use `AllForeground` mode and only ONE can be truly foreground at a time. The window compositor (uMenu's QdWindowManager) can hold N holders but only the topmost gets display time.
- Window Z-ordering: uMenu's internal `QdWindowManager` tracks which NRO `AppletHolder` is the current focus target. Focus switch = uMenu calls `appletHolderRequestToGetForeground` on the target holder and `appletHolderSendToBackground` on the previous. HOS AM supports this between library-applet holders created by the same foreground caller.
- Window lifecycle: creation = `appletCreateLibraryAppletSelf` (from uMenu, foreground required) → ECS push of TargetInput → `appletHolderStart`. Destruction = `appletHolderRequestExitOrTerminate` → Join → Close → `ldrShellAtmosphereUnregisterExternalCode` (via uSystem SMI, since uMenu lacks ldr:shell).

**Component: am IPS patch (the Atmosphère absorption target)**
- Patches `ILibraryAppletCreator::CreateLibraryApplet` cmd 4 to permit the 2nd in-session create from a background/system-applet caller (uSystem). This unblocks the HOME-from-game uMenu relaunch path.
- The IPS patch is orthogonal to the NRO-as-window architecture but is required for the HOME-from-game relaunch path (uSystem re-creating uMenu after game exit). Without it uSystem must use the `ForceTerminateNow` + `LaunchMenu` relaunch path, which already works for NRO-exit (via cmd_resetmenu), but fails for game-exit (the 2-second svcSleepThread workaround at line 2035 is the current stopgap).

**Session Pool Sizing**
- Each windowed NRO requires: 1 ECS slot in AMS's 6-slot ServerManager pool (via `RegisterExternalContent`) + 1 `ILibraryAppletAccessor` session slot in AM. The current `MaxEcsExtraSessions=5` allows 5 concurrent windowed NROs. Raising to 10 (and adjusting the ServerOptions MaxDomains/MaxSessions in `sf_IpcManager.hpp` accordingly) allows 10 concurrent windows, which covers the realistic desktop use case.
- The `MaxPrivateSessions=4` for uMenu's SMI connection is correct as-is (handles theme-switch relaunch transitions). No change needed.

**Compute Reduction Strategy for Many Windows**
- The `AppletStorage` pool pre-allocation (finding above on smi_Protocol.hpp:252-282) is critical for windowed NROs: each window launch/close pair currently costs 4+ am IPC calls for the storage lifecycle. With 10 concurrent windows and frequent focus switches, pooling eliminates ~40 IPC calls per focus-switch cycle.
- The cache thread's `unordered_map::find_if` O(n) scan (finding above) becomes significant as the NRO library grows — must be fixed before any large NRO catalog is loaded.
- The 10 ms fixed MainLoop sleep (finding above) must be replaced with event-driven waiting before multi-window is usable: with 10 windows each potentially sending SMI messages, the 10 ms poll adds up to 100 ms of aggregate message latency across the window set.

**Compatibility Notes**
- NROs launched via `appletCreateLibraryAppletSelf` inherit the uMenu process's permissions, not a fresh sandboxed environment. NROs that call `appletInitialize` will conflict (double-init of the applet IPC session). uLoader's existing shim handles this by intercepting the NRO's applet init path; extending that shim to route `hidInitialize`/`viInitialize` correctly is required for full NRO compatibility.
- The `bcat_delivery_cache_storage_journal_size` field in `ApplicationNacpMisc` (app_ControlCache.hpp:22) is defined but never written in `FetchApplicationNacpMiscSync` (app_ControlCache.cpp:282 — the field is missing from the sync fetch). This is a silent data-miss that would cause `EnsureSaveData` to skip creating the BCAT journal if it were ever checked. Add `out_nacp_misc.bcat_delivery_cache_storage_journal_size = control_data->nacp.bcat_delivery_cache_storage_journal_size;` to `FetchApplicationNacpMiscSync` to close the gap.

### Findings (25)

| file:line | cat | impact | eff | opportunity |
|---|---|---|---|---|
| sf_IpcManager.cpp:14-15 | architecture | low | S | Profile actual peak stack usage with the poison-pattern technique (fill stack with 0xCC, run a workload, measure how far the pattern was overwritten). If stack usage is well below 32 KB, halve it to 16 KB saving 16 KB of static BSS. If ECS dispatch is near the limit, split the thread into two: one f |
| sf_IpcManager.cpp:21-22 | architecture | low | S | Add a `MaxSessions` ceiling to `MaxPublicSessions` (currently 32 public sessions for a single-client menu is very generous). Reduce `MaxPublicSessions` from 32 to 4 (only uMenu ever connects; there is exactly one public client). This frees session slot bookkeeping space in the ServerManager. Also si |
| overlay_FontCache.cpp:67-106 | architecture | low | S | Pre-warm the entire chrome character set at FontCache::Initialize time (after `stbtt_InitFont` succeeds) by iterating the exact set of codepoints and sizes that the overlay draws. This front-loads all rasterization to init, making DrawString a pure cache-hit path thereafter. Additionally, replace `u |
| main.cpp:2619 | architecture | low | S | Bump `__nx_fs_num_sessions` to 4 to accommodate the probe path without risk. Alternatively, cache the SD filesystem session as described in the ecs::RegisterExternalContent finding and use the cached session for the probe too (no extra session needed). The 4-session bump is the minimal safe change. |
| main.cpp:2267 | compute-cut | high | M | Replace the unconditional 10 ms sleep with a timerWaiter + event-based svcWaitSynchronizationN on the set {AppletMessage event, GeneralChannel event, SMI storage-available event, g_TerminatingNro/g_TerminatingApp deadline tick}. When nothing is pending the thread sleeps in the kernel until a real ev |
| main.cpp:2029-2036 | compute-cut | high | S | Replace with the same event-based FS-probe loop already used in the NRO path (Phase 3 above): poll `fsOpenSdCardFileSystem` with 10 ms sleep intervals, break on first success. Based on real NRO evidence the FS probe typically succeeds in 1-2 iterations (~10-20 ms). The 2 s sleep was added experiment |
| app_ControlCache.cpp:287-308 | compute-cut | high | S | Replace the spin with a proper wait: `ScopedLock lock(g_ApplicationCacheLock); return QueryApplicationNacpMisc(...)` -- just take the mutex. The mutex is libnx's non-recursive `Mutex`, which uses the kernel `svcArbitrateUnlock` / `svcArbitrateLock` path and will park the waiter thread at the kernel  |
| main.cpp:224-247 | compute-cut | high | S | Make the libnx heap size conditional on `UL_ENABLE_TESLA_OVERLAY`: 4 MB when overlay is disabled (well above the 1 MB original baseline plus margin), 32 MB when enabled. This returns ~28 MB to Horizon's available memory pool for the running game or homebrew NRO when no overlay is active. When the ov |
| main.cpp:1867-1964 | compute-cut | medium | S | Check `R_SUCCEEDED` and break BEFORE `svcSleepThread`; only sleep on failure. Also, both probes can be combined into a single loop: attempt sm connect + FS open in the same iteration body. First success on either leg is a sufficient signal; FS-open alone is strictly stronger so the sm probe can be d |
| app_ControlCache.cpp:187-188 | compute-cut | medium | S | Replace the pre-launch spin with a semaphore or CondVar: `AllowCacheDrain()` signals the semaphore and the worker thread waits on it (svcWaitSynchronization1 with infinite timeout). Replace the inter-item 10 ms poll with a condition variable or eventSignal when new items are pushed to the queue (`Re |
| app_ControlCache.cpp:110-111 | compute-cut | medium | S | Allocate a single `NsApplicationControlData` once as a member of the cache thread's stack-local scope outside the retry loop (or as a static inside `ApplicationControlCacheMain`), and reuse it across all retries and across sequential `AddNextApplicationCache` calls. Since the thread is single-thread |
| ecs_ExternalContent.cpp:37-48 | compute-cut | medium | M | Cache the SD IFileSystem session for the uLoader applet path once at initialization (it never changes at runtime). Only the `exefs_path` argument varies between calls; the SD root can be kept open permanently and reused as a subdir of the same root. This cuts fsp-srv IPC from O(launch_count) to O(1) |
| sf_IPrivateService.cpp:12-28 | compute-cut | medium | S | Cache the result: once the menu program_id is known (first successful Initialize call), store it and skip pm:info on subsequent calls. The program_id does not change between launches of the same uMenu binary. Alternative: have `la::SetMenuProgramId` store the program_id at launch time and let `Initi |
| smi_Protocol.hpp:252-282 | compute-cut | medium | M | The CommandStorageSize is fixed at 0x8000 (32 KB). Pre-allocate a pool of N AppletStorage objects at initialization and reuse them with a ring/free-list, instead of create/close per command. With the existing single-threaded serial command protocol (one request/response pair at a time per holder), t |
| overlay_TestLayer.cpp:41-45 | compute-cut | medium | S | Switch to Tesla's proven 448×720 framebuffer with `ViScalingMode_FitToLayer`. The chrome geometry fits on a narrower buffer because the border, titlebar, and button positions are expressed as fractions of the buffer dimensions — they scale correctly. This cuts the framebuffer from 3.69 MB to 0.97 MB |
| overlay_Renderer.cpp:58-63 | compute-cut | medium | S | When `color.packed` has both bytes equal (which `kColorTransparent = 0x0000` and `kColorSurfaceDark` may not, but can be verified at compile time), the existing `memset` fast path already handles it. For the general case where hi != lo, use NEON to broadcast the `u16` color into a 128-bit register a |
| main.cpp:2410-2416 | compute-cut | medium | S | Gate the write on whether a USB host is connected (usbCommsGetNumConnected or equivalent libnx API). When not connected, sleep at 100 ms cadence instead of 1 ms. For the read threads, use `capsscCaptureJpegScreenShot` with a finite timeout instead of a sleep-then-capture loop. This cuts idle USB ove |
| main.cpp:1685-1774 | compute-cut | low | S | Switch to a dedicated event or named semaphore signalled by a file-watch sysmodule, or coalesce into a single directory-listing call. Alternatively, use a single flag file (sdmc:/ulaunch/cmd) with a one-byte payload identifying which command, cutting three stat() calls to one per poll window. Better |
| app_ControlCache.cpp:60-66 | compute-cut | low | S | Replace `std::find_if(g_ApplicationCache->begin(), g_ApplicationCache->end(), [app_id](const auto &entry) { return entry.first == app_id; })` with `g_ApplicationCache->find(app_id)`. Same fix applies in `QueryApplicationNacpMisc` (line 251). Both are O(n) linear scans where O(1) hash lookup is alrea |
| la_LibraryApplet.cpp:83-88 | compute-cut | low | S | Use `appletHolderJoin` directly and read `g_LibraryAppletHolder.exitreason` plus the exit result from the holder struct after join, OR call `serviceDispatch` once for the DIAG log and skip `appletHolderJoin`'s internal GetResult by closing the holder directly. The cleanest fix: `appletHolderJoin` al |
| la_LibraryApplet.cpp:263-282 | compute-cut | low | S | Replace with two small `constexpr` or `static` lookup tables (e.g. two `std::array` or `std::unordered_map` initialized once) so both lookups are O(1). At 17 entries the linear scan is trivially cheap today, but with the NRO-as-window architecture the lookup will be on the hot path for every windowe |
| overlay_TestLayer.cpp:196-200 | compute-cut | low | S | After the overlay re-enable, suppress the vsync-fail log until after the first successful frame (i.e. gate on `frame_counter > 0`). This avoids log spam during the initialization window before the first vsync fires. Additionally, the 100 ms timeout can be raised to 200 ms with no perceptible differe |
| overlay_FontCache.cpp:183-190 | compute-cut | low | S | Cache the ascent per `pixel_height` inside `Ascent()` using a small array (e.g. 4 slots for the sizes used). `stbtt_GetFontVMetrics` is O(1) but requires pointer indirection into the font blob; caching eliminates it for repeated calls at the same size. More broadly: the overlay's static chrome text  |
| main.cpp:2095-2106 | compute-cut | low | S | Replace `g_ActionQueue` with `std::deque<Action>` (O(1) pop_front) or a `std::queue<Action>` wrapper. Since the code always processes `begin()` (index 0 first), and `erase` at index 0 shifts everything down, a `deque` is the exact right container here. Alternatively keep the vector but use a head-in |
| main.cpp:2395-2396 | compute-cut | low | S | Move the `svcSleepThread(100_000)` into a `continue` path only reachable when `waitMulti` fails, or remove it entirely — `waitMulti` with `UINT64_MAX` timeout already parks the thread at zero CPU cost. The sleep was likely a defensive measure against a tight spin on error, but `waitMulti` failure he |

## AREA: uLoader (homebrew NRO loader — hbloader-style code-memory load + trampoline)

### Root-level architecture notes

NROs-AS-WINDOWS — HOW TO BUILD IT NATIVELY AT THE ROOT LEVEL (no band-aids)

The fundamental constraint: Nintendo's `am` sysmodule enforces a single-library-applet-slot model for background callers (confirmed root cause of am 2128-0035). You cannot create a second library-applet process slot from a background daemon. Therefore true concurrent NRO windows CANNOT be achieved by spawning multiple uLoader instances — am blocks that at the IPC level. The only viable path is: ONE uLoader process instance hosting MULTIPLE NRO windows as concurrent threads within that single process.

ARCHITECTURE: Single-Process Multi-Thread NRO Host

Phase 1 — Per-window state structs (all currently-global state in loader_Target.cpp becomes per-window):
- Define `struct NroWindow { u8 *heap_base; size_t heap_size; u64 map_addr; NroHeader header; char path[NroPathSize]; char argv[NroArgvSize]; char current_argv[NroArgvSize]; u32 run_count; Handle thread_handle; u8 *thread_stack; size_t thread_stack_size; u64 tls_backup[0x100/8]; ConfigEntry cfg_entries[13]; AccountUid user_id; }`.
- Replace all `g_Target*` globals with a `NroWindow g_Windows[MAX_WINDOWS]` array. `MAX_WINDOWS` = 4 for the Erista's 4 GB total minus system overhead; in applet mode ~440 MB heap / 4 = ~110 MB per window (enough for RetroArch, mgba, etc.).

Phase 2 — Partitioned heap:
- `svcSetHeapSize` once for the full pool (existing logic in `SetupTargetHeap` unchanged).
- Divide the returned heap region into N equal (or per-request) slices: `window.heap_base = g_MasterHeap + i * slice_size; window.heap_size = slice_size`.
- Each window's NRO image loads into the low part of its own slice; `EntryKind::OverrideHeap` points to the remainder of that slice.

Phase 3 — Per-window threads and stacks:
- Allocate thread stacks from the master heap BEFORE slicing it for NROs: reserve `N * THREAD_STACK_SIZE` at the top of the heap (e.g. 4 × 256 KB = 1 MB), then divide the remainder among NRO windows.
- For each window, `svcCreateThread` with its own stack and a trampoline that takes the `NroWindow*` as a parameter (pass in x0 before branching to the NRO entrypoint). `svcStartThread` launches it.
- The main uLoader thread becomes a coordinator: it receives launch commands (from uSystem via SMI), finds a free NroWindow slot, initializes it, creates and starts the window thread.

Phase 4 — Per-window trampoline (replace the global loader_TargetImpl.s):
- The trampoline must be parameterized: it receives `NroWindow*` in a callee-saved register (x19), resets sp to the window's own stack top (not the global `__stack_top`), calls the NRO entrypoint, saves the return value into `window->last_result`, and calls back into a C function `window_LoadNextOrExit(NroWindow*)` instead of the global `ext_LoadTargetImpl`.
- Because the trampoline is per-window code and must be in RX memory, it is either: (a) a single shared trampoline that reads the NroWindow* from TLS, or (b) a per-window trampoline copy in RX memory (simpler but wastes one page per window). Option (a) is cleaner: store `NroWindow*` in the uLoader thread's TLS slot [0x1F8] (the 'reserved for user' word in the Switch TLS layout), read it in the trampoline via `mrs x8, tpidrro_el0; ldr x19, [x8, #0x1F8]`.

Phase 5 — Per-window exception entry:
- The `__libnx_exception_entry` global ADRP+LDR of `g_TargetMapAddress` must become a TLS-relative read: `mrs x7, tpidrro_el0; ldr x7, [x7, #<NroWindow offset for map_addr>]`. This correctly routes exceptions to the NRO window that owns the faulting thread.

Phase 6 — TLS:
- Each thread already gets its own TLS block in libnx. BackupTls/RestoreTls become per-window operations on the window thread's own TLS. The uLoader coordinator thread retains its own TLS. No global TLS backup needed.

Phase 7 — IPC / display integration (the hardest part):
- Each NRO window needs its own GPU surface / vi layer to render into. This is where `appletCreateLibraryAppletSelf` comes in: uMenu (the foreground library applet) can call `appletCreateLibraryAppletSelf` to open IPC sub-sessions for additional surfaces. The BackgroundIndirect path (`viGetIndirectLayerImageMap`) was ruled out (2114-0011 gate), but `vi:u`/`vi:s` direct layer creation by the foreground NRO itself (each NRO creates its own vi layer with a `ViLayerHandle`) IS viable if the NRO is running in foreground context. The multi-window compositor must live in uMenu (SDL2 window manager), not in uLoader. uLoader's job is just to load+map the NRO and provide it a heap; the NRO renders into an off-screen SDL_Texture that uMenu composites. This means each windowed NRO must use a QOS-specific rendering ABI (a new `EntryKind` in loader_HbAbi.hpp: `EntryKind::WindowedRenderTarget = 18`), passing an SDL_Texture* or shared-memory handle as its framebuffer. Unmodified NROs (RetroArch, mgba etc.) cannot use windowed mode — they continue in full-screen single-applet mode. Only NROs compiled against a QOS windowed SDK stub can run as windows.

Phase 8 — am IPS patch prerequisite:
- Until the am 2128-0035 patch lands, uLoader runs as the SINGLE library-applet slot. All NRO windows must fit inside that one slot (single-process multi-thread). The am patch (ABSORPTION_PROGRAM.md §AM-FIX DETAILED PLAYBOOK) would allow a future `uLoader-B` second slot, doubling available window capacity, but it is not required for Phase 1–7.

PERFORMANCE GAINS OVER STOCK:
- Removing UldrTrace (24–36 IPC calls per launch eliminated)
- Persistent fsdev across NRO chain hops (6 IPC calls per hop eliminated)
- Cached CodeMemoryCapability (2 syscalls per chain hop eliminated)
- Lazy heap-size query (2 syscalls eliminated in common applet case)
- Single-call NRO unmap (2 syscalls eliminated per chain hop)
- Single fread for full NRO (1 IPC dispatch eliminated)
- All together: approximately 35–50 IPC/syscall operations saved per NRO launch on the hot path, which translates to measurably faster launch latency from the uMenu desktop tap to NRO first-frame.

### Findings (25)

| file:line | cat | impact | eff | opportunity |
|---|---|---|---|---|
| loader_Target.cpp:405-422 | architecture | high | L | This is the central architectural point for NRO-as-window. The current design is one-at-a-time sequential: uLoader runs one NRO to completion, then optionally starts another. To run multiple NROs concurrently as windows, the architecture must change: each NRO needs its own thread + stack + heap regi |
| loader_Target.cpp:286-289 | architecture | high | L | For NRO-as-window, this flat-pool design must become partitioned. Each concurrent NRO window needs: (a) its own code-mapping region (svcMapProcessCodeMemory from its own heap slice); (b) its own OverrideHeap slice. The heap must be divided into N slices of size `heap_size / N` (or per-window configu |
| loader_TargetImpl.s:1-29 | architecture | high | L | For NRO-as-window (multiple concurrent NROs), each NRO window needs its own thread with its own stack — the current shared-stack-reset trick only works for one active NRO at a time. Each window thread needs a `svcCreateThread` with a per-window stack region carved from either (a) the uLoader's 1 MB  |
| loader_Target.cpp:112-162 | compatibility | high | S | The `applet_heap_size` and `applet_heap_reservation_size` settings default to 0 in a stock Atmosphère install (the hbloader section in system settings is absent). When both are 0 AND `SelfIsApplet()` is true, the code falls through to using the full `ComputeMaximumHeapSize` result — which is correct |
| loader_TargetTypes.hpp:15-25 | architecture | medium | S | The `menu_caption` field (1024 bytes) is dead weight in QOS: it was used by stock hbmenu to display a caption in its UI, but QOS's uMenu shows the NACP name instead (per ABSORPTION_PROGRAM.md B1.1). Eliminate `menu_caption` from `TargetInput` (or reduce to 64 bytes for a short debug tag). `NroArgvSi |
| loader_ExceptionEntry.s:1-17 | architecture | medium | M | For NRO-as-window: the exception entry must look up the per-window map address based on which thread is executing (using `armGetTls()` or a thread-ID lookup into a per-window table). The simplest approach: each NRO window thread installs its own exception handler via libnx's thread-local exception e |
| loader_Target.cpp:28-29 | architecture | medium | M | For NRO-as-window with multiple concurrent NROs: each NRO window thread must have its own TLS (which is automatic — each thread has a separate TLS block in libnx). The BackupTls/RestoreTls mechanism is only needed for the single-thread approach where uLoader and the NRO share one thread. In the mult |
| loader_ProgramIdUtils.hpp:7-30 | architecture | low | S | Since QOS always launches uLoader as an applet (the application NPDM path exists for compatibility with older hbloader-style launches, not for primary use), the applet-type branch is always taken. The `IsApplication` path in `GetAppletType` (line 23) and the Application branch in `ReadTargetInput` ( |
| main.cpp:31-54 | dead-code | high | S | This is diagnostic/debug-only code introduced during the HOME-from-game root-cause hunt. Now that the root cause is confirmed (am 2128-0035, documented in STATE.toml and ABSORPTION_PROGRAM.md) and uLoader was ruled out as the failure site, this entire UldrTrace subsystem should be removed or compile |
| loader_Target.cpp:164-176 | compute-cut | high | S | Keep fsdev open across the `LoadTarget` / `LoadTargetImpl` loop. Open fs+sm once in `Target()` before calling `SetupTargetHeap`+`LoadTargetImpl`, and close only when uLoader fully exits (or on error). The scope-exit pattern at line 214 is convenient but pays 3 open + 3 close IPC calls per NRO load.  |
| loader_Target.cpp:258 | correctness | high | S | Add the missing check after computing `target_size` at line 286: `if(target_size >= g_TargetHeapSize) { UL_RC_ASSERT(hbloader::ResultNroTooLarge); }` (define ResultNroTooLarge). This prevents silent memory corruption for oversized NROs (e.g. RetroArch which is large) and eliminates a class of hard-t |
| main.cpp:28-29 | compute-cut | medium | S | 64 KB is already very tight (the trace function at line 37 explicitly notes fopen silently fails on this heap, requiring raw-fs IPC). Verify nothing in the cold path (logging, IPC wrappers in uCommon) secretly heap-allocates through libnx's default allocator after the switch to fake-heap. If any lat |
| main.cpp:96-108 | compute-cut | medium | M | Batch all setsys reads into a single open session. Because Atmosphère is always present (comment on line 101 admits this), `hosversion` can be hard-set from the already-read fw_ver without the extra `setsysGetFirmwareVersion` call. Better: since QOS owns the CFW stack, expose these two heap settings |
| loader_Target.cpp:63-110 | compute-cut | medium | S | Cache the result in a `static CodeMemoryCapability g_CodeMemCap = CodeMemoryCapability::Unavailable` with a `static bool g_CodeMemCapDetermined = false` flag, or use a `std::once_flag`. The capability cannot change between NRO loads within the same process. This eliminates the IPC probe + 2 syscalls |
| loader_Target.cpp:112-133 | compute-cut | medium | S | For the applet mode (most NRO launches), `applet_heap_size` or `applet_heap_reservation_size` from settings is used to clamp or subtract from the computed max. When `applet_heap_size > 0` (the common configured case), the two svcGetInfo calls are still made even though they only matter if the reques |
| loader_Target.cpp:219-243 | perf | medium | S | Read the combined header in a single fread: `fread(target_base, sizeof(NroStart) + sizeof(NroHeader), 1, f)` then alias the pointers into target_base. Reduces from 3 frend calls to 2. Better: since the NRO is loaded directly into the target heap at `target_base` anyway, a single `fread(target_base,  |
| loader_Target.cpp:264-265 | perf | medium | S | Cache the last-used `map_addr` from the previous load. After unmap (lines 185–197), the same virtual address range is free again. Attempt to re-use `g_TargetMapAddress` (already stored) as the hint for the next map: call `svcMapProcessCodeMemory(g_SelfProcessHandle, old_map_addr, ...)` directly inst |
| loader_Target.cpp:183-197 | perf | medium | S | Unmap in one call: `svcUnmapProcessCodeMemory(g_SelfProcessHandle, g_TargetMapAddress, reinterpret_cast<u64>(g_TargetHeapAddress), AlignUp<PageAlignment>(g_TargetHeader.size + g_TargetHeader.bss_size))`. This is legal because `svcUnmapProcessCodeMemory` on the contiguous range reverts all sub-segmen |
| uLoader_applet.json:6 | perf | medium | S | For multi-window NROs: increase `main_thread_stack_size` to 2 MB or more, OR do not use the main-thread stack for NRO windows at all — instead allocate per-window thread stacks from a pre-reserved region of the heap (before handing heap slices to NROs). The NPDM stack is the only fixed stack; everyt |
| loader_Target.cpp:272-283 | perf | low | S | The three regions are contiguous (they are at `map_addr + segment[n].file_off`). On Mesosphère (always present), check whether a single `svcSetProcessMemoryPermission` call covering the total range with a split-permission approach is possible — it is not directly, but the ORDER matters: map as Rx fi |
| loader_Target.cpp:303-395 | compute-cut | low | S | Build `g_TargetConfigEntries` directly in place: write into `g_TargetConfigEntries[i]` fields directly instead of declaring a local array and memcpy-ing. Eliminates the memcpy and the 104-byte stack frame for the local array. Minor but avoids touching the same 104 bytes twice in the data cache. Also |
| loader_Target.cpp:401 | compute-cut | low | S | Wrap in a `#ifdef UL_DEV_BUILD` or check `envIsDevelopment()` at runtime. In a release/non-debug build (when `force_debug=false` in the NPDM) the kernel ignores this break reason anyway (BreakReason_NotificationOnlyFlag means non-fatal, so the process continues), but the syscall round-trip is still  |
| loader_Input.cpp:9-13 | compute-cut | low | S | In `ReadTargetInput`, keep the applet session open after reading the input data and pass the open session state into the rest of uLoader so `WriteTargetOutput` (called at exit) doesn't have to re-open it. Currently the session is opened, data is read, then immediately closed, then re-opened later fo |
| loader_Input.cpp:19-21 | compute-cut | low | M | If uSystem never writes useful data to the common args storage (it only writes the TargetInput), verify whether the common args pop can be replaced with a drain that avoids constructing the AppletStorage struct. At minimum, move the `appletStorageClose` call earlier to free the handle sooner. Longer |
| Makefile:30 | perf | low | S | Add `-flto` (Link-Time Optimization) to CFLAGS/CXXFLAGS and LDFLAGS. uLoader is a small, self-contained binary (~50 KB NSO). LTO at this size has near-zero link cost and can eliminate cross-TU inlining barriers (the `DetermineCodeMemoryCapability`, `ComputeMaximumHeapSize`, and `InitializeFsdev` fun |
