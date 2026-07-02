# AMS 1.11 Clean-Exit IPC Contract
**Date:** 2026-05-06
**Analyst:** dept-reverse-engineering / Protocol Analyst
**Target:** Creator-owned (Q OS uLaunch fork) + public OSS (Atmosphere, libnx)
**Sources cited by file path and line where available**

---

## The Contract

AMS 1.11.0 (FW 22.0.0 support) introduced a mandatory clean-exit requirement for all
applets and applications. The Atmosphere changelog states verbatim:
> "All applications and applets are expected to perform a clean exit by calling the
> relevant IPC commands."

The relevant IPC commands, drawn from `nx/source/services/applet.c` (libnx master),
are a **three-step teardown sequence** that every SystemApplet launching a
LibraryApplet must now complete before re-using the session slot:

```
Step 1 — Signal exit intention
  appletHolderRequestExitOrTerminate(&holder, timeout_ns)
    ↳ IPC cmd 0  : GetAppletStateChangedEvent  → Event handle
    ↳ IPC cmd 20 : RequestExit                 → ask applet to exit gracefully
    ↳ eventWait(StateChangedEvent, timeout)
    ↳ IPC cmd 25 : Terminate                   → force if timeout elapsed

Step 2 — Consume the exit result (MANDATORY on AMS 1.11 — was optional before)
  appletHolderJoin(&holder)
    ↳ eventWait(StateChangedEvent, UINT64_MAX)  → block until fully done
    ↳ IPC cmd 30 : GetResult                   → retrieve exit code, set exitreason

Step 3 — Release the kernel handle and zero the slot
  appletHolderClose(&holder)
    ↳ eventClose(PopInteractiveOutDataEvent)
    ↳ eventClose(StateChangedEvent)
    ↳ serviceAssumeDomain + serviceClose        → releases IPC session slot
    ↳ memset(holder, 0, sizeof(AppletHolder))
```

Step 2 (`appletHolderJoin` / IPC cmd 30 `GetResult`) is the new mandatory step.
On AMS <= 1.10, skipping it was tolerated because `am` still closed the session
on its side when the applet process exited. AMS 1.11's `am` patch for FW 22.0.0
tightened the proxy lifecycle: the session slot is not reclaimed server-side until
the client calls `GetResult`. Skipping Step 2 leaves the `ILibraryAppletAccessor`
session allocated in the `ServerManager` pool indefinitely.

**State machine for a compliant launch-to-teardown cycle:**

```
IDLE
  → appletCreateLibraryApplet()      : [CREATED]     IPC session slot consumed
  → appletHolderStart()              : [RUNNING]     cmd 10 Start
  → (applet runs, user exits)
  → appletHolderCheckFinished()=true : [FINISHED]    cmd 0 / eventWait returns
  → appletHolderJoin()               : [RESULT_READ] cmd 30 GetResult called
  → appletHolderClose()              : [IDLE]        session slot freed
```

`appletHolderClose` without a preceding `appletHolderJoin` leaves the session in
`[FINISHED]` not `[RESULT_READ]`, and the AMS 1.11 `am` patch does not free it.

---

## Where uLaunch Upstream Is on This

Upstream uLaunch's latest tagged release is **1.2.3 (2026-01-24)**, built against
AMS 1.10.2 / FW 21.2.0. The `unew` branch has 309 commits but carries no AMS 1.11
tag. Upstream has never shipped an AMS 1.11.x build.

Source: `docs/research/2011-0102-FIELD-EVIDENCE-20260506.md` §"uLaunch upstream FW20+ status"

Upstream `la_LibraryApplet.cpp` `Terminate()` (confirmed verbatim via fetch of
`raw.githubusercontent.com/XorTroll/uLaunch/unew/projects/uSystem/source/ul/system/la/la_LibraryApplet.cpp`):

```cpp
Result Terminate() {
    UL_RC_TRY(appletHolderRequestExitOrTerminate(&g_LibraryAppletHolder, 15'000'000'000ul));
    appletHolderClose(&g_LibraryAppletHolder);
    UL_RC_SUCCEED;
}
```

`appletHolderJoin` is absent. Upstream **does not comply** with the AMS 1.11 contract.
The uMenu `__wrap_libappletStart` path (used for uMenu's own sub-applet calls, site
#12 in the teardown audit) does call `appletHolderJoin`, but that covers uMenu-side
only — not the uSystem system-applet launcher.

---

## Q OS Fork Compliance Audit Summary

The Q OS fork's `la_LibraryApplet.cpp:78` is **identical** to upstream's `Terminate()`:
no `appletHolderJoin` call anywhere in the file
(`grep appletHolderJoin la_LibraryApplet.cpp` → zero matches).

Call sites with missing teardown, from `docs/research/APPLET-TEARDOWN-AUDIT-20260506.md`:

| Site | File | Problem |
|------|------|---------|
| Normal-exit path | `main.cpp:1311` | `appletHolderClose` skipped entirely; `LaunchMenu` called without closing previous holder |
| Home-button forced exit | `main.cpp:543` | `la::Terminate()` called — Step 1+3 present, Step 2 missing |
| TerminateMenu action | `main.cpp:1248` | same as above |
| `la::Create` guard | `la_LibraryApplet.cpp:43` | calls `Terminate()` on re-entry — Step 2 still missing |

Additionally, a second distinct leak class identified in the audit:

**ECS session slots never freed.** Every `LaunchMenu` and `LaunchHomebrewLibraryApplet`
call invokes `ldrShellAtmosphereRegisterExternalCode` (IPC 65000), which consumes a
`RegisterSession` slot in the `ServerManager` pool (`MaxSessions = 6`). The matching
`ldrShellAtmosphereUnregisterExternalCode` (IPC 65001) is called **nowhere** in the
uSystem project. After 6 cycles the pool is exhausted, producing `2011-0102
ResultOutOfSessionMemory` at `sf_hipc_server_session_manager.hpp:109`:

```cpp
R_UNLESS(session_memory != nullptr, sf::hipc::ResultOutOfSessionMemory());
```

Pool definition: `sf_hipc_server_manager.hpp:323-324` — static array, compile-time
size, no dynamic reclaim. `MaxSessions = MaxPrivateSessions(1) + MaxEcsExtraSessions(5) = 6`.

---

## Concrete Fix Patterns

### Before (current — leaks on every launch cycle)

```cpp
// la_LibraryApplet.cpp:78
Result Terminate() {
    UL_RC_TRY(appletHolderRequestExitOrTerminate(&g_LibraryAppletHolder, 15'000'000'000ul));
    appletHolderClose(&g_LibraryAppletHolder);   // Step 2 (Join/GetResult) skipped
    UL_RC_SUCCEED;
}

// main.cpp:1311 — normal applet-exit path
if(!la::IsActive() && g_LastLibraryAppletLaunchedNotMenu) {
    // ... collect output ...
    UL_RC_ASSERT(LaunchMenu(menu_start_mode));   // holder not closed; ECS not freed
}
```

### After (AMS 1.11 compliant)

```cpp
// la_LibraryApplet.cpp
Result Terminate() {
    UL_RC_TRY(appletHolderRequestExitOrTerminate(&g_LibraryAppletHolder, 15'000'000'000ul));
    appletHolderJoin(&g_LibraryAppletHolder);    // NEW: IPC cmd 30 GetResult
    appletHolderClose(&g_LibraryAppletHolder);   // Step 3 — frees session slot
    UL_RC_SUCCEED;
}

// main.cpp:1311 — normal applet-exit path
if(!la::IsActive() && g_LastLibraryAppletLaunchedNotMenu) {
    // NEW: close the holder on natural exit (applet exited itself, no Terminate called)
    if(serviceIsActive(&g_LibraryAppletHolder.s)) {
        appletHolderJoin(&g_LibraryAppletHolder);
        appletHolderClose(&g_LibraryAppletHolder);
    }

    // NEW: release ECS loader slot for ECS-backed launches
    if(g_LastLaunchWasEcs) {
        const auto rc = ecs::UnregisterExternalContent(g_LastEcsProgramId);
        if(R_FAILED(rc)) UL_LOG_WARN("UnregEcs failed: 0x%x", rc);
        g_LastLaunchWasEcs = false;
    }

    // ... collect output, then relaunch uMenu (unchanged) ...
    UL_RC_ASSERT(LaunchMenu(menu_start_mode));
}
```

New globals required: `bool g_LastLaunchWasEcs = false` and `u64 g_LastEcsProgramId = 0`.
Set both in the `LaunchHomebrewLibraryApplet` and `LaunchHomebrewApplication` action handlers
alongside the existing `g_LastLibraryAppletLaunchedNotMenu = true`.

`ecs::UnregisterExternalContent` is a new helper wrapping IPC 65001
(`ldrShellAtmosphereUnregisterExternalCode`) — the symmetric call to the already-present
IPC 65000 (`ldrShellAtmosphereRegisterExternalCode`) in `ecs_ExternalContent.cpp:17`.

---

## MaxSessions Sizing

Current: `MaxSessions = 6` (`sf_IpcManager.hpp:40-41`; upstream is identical per fetch).

| Scenario | Correct value |
|----------|---------------|
| Leak unfixed, sessions accumulate | Every applet launch permanently consumes one slot; 6 slots exhausted after 6 ECS launches. Raising to e.g. 12 delays but does not prevent the crash — wrong direction. |
| After fixing ECS unregister + appletHolderJoin | Each launch cycle is net-zero. `MaxSessions = 6` is sufficient for the steady state: 1 private session + up to 5 concurrent ECS sessions (uMenu + uLoader + 3 spare). |
| If uMenu + uLoader + future Q OS system NROs run concurrently | Raise `MaxEcsExtraSessions` to 7 or 8 (`MaxSessions` = 8 or 9) to give headroom. Static cost is `sizeof(ServerSession) * MaxSessions` in BSS — negligible. |

**Conclusion:** fix the leak, keep `MaxSessions = 6`. Do not use a larger pool to mask
the missing `appletHolderJoin` and IPC 65001 calls — that is a symptom treatment that
will exhaust any finite pool eventually.
