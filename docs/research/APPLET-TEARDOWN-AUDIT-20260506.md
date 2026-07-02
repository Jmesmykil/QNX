# Static Analysis Report: Applet-Launch Teardown Audit

## Target
- **Path:** `src/projects/uSystem/` + `src/projects/uMenu/`
- **Type:** Repository (C++ Nintendo Switch homebrew)
- **Authorization:** Creator-owned (Q OS uLaunch fork)
- **Analysis Date:** 2026-05-06

---

## Tabular Inventory: Every Applet-Launch Site

| # | Launch site | File:Line | Cleanup present | Cleanup MISSING |
|---|---|---|---|---|
| 1 | `LaunchMenu` → `ecs::RegisterLaunchAsApplet(uMenu)` | `main.cpp:463` | `appletHolderRequestExitOrTerminate` + `appletHolderClose` in `la::Terminate()` called from `HandleHomeButton` or `la::Create` guard | `ldrShellAtmosphereUnregisterExternalCode` (cmd 65001) **never called** on any exit path |
| 2 | `ActionType::LaunchHomebrewLibraryApplet` → `ecs::RegisterLaunchAsApplet(uLoader/applet)` | `main.cpp:1133` | Same `la::Terminate()` path available via `HandleHomeButton` | `ldrShellAtmosphereUnregisterExternalCode` **never called**; `appletHolderClose` only fires if Home button pressed |
| 3 | `ActionType::OpenWebPage` → `la::OpenWeb` | `main.cpp:1168` | `la::Terminate()` via `HandleHomeButton` only | `ldrShellAtmosphereUnregisterExternalCode` N/A (native applet, not ECS); `appletHolderClose` missing on normal user-exit path — only `g_LastLibraryAppletLaunchedNotMenu` reset then `LaunchMenu` called without first closing holder |
| 4 | `ActionType::OpenAlbum` → `la::OpenPhotoViewerAllAlbumFilesForHomeMenu` | `main.cpp:1178` | Same as #3 | Same as #3 |
| 5 | `ActionType::OpenUserPage` → `la::OpenMyPageMyProfile` | `main.cpp:1197` | Same as #3 | Same as #3 |
| 6 | `ActionType::OpenMiiEdit` → `la::OpenMiiEdit` | `main.cpp:1207` | Same as #3 | Same as #3 |
| 7 | `ActionType::OpenAddUser` → `la::OpenPlayerSelectUserCreator` | `main.cpp:1217` | Same as #3 | Same as #3 |
| 8 | `ActionType::OpenNetConnect` → `la::OpenNetConnect` | `main.cpp:1227` | Same as #3 | Same as #3 |
| 9 | `ActionType::OpenCabinet` → `la::OpenCabinet` | `main.cpp:1239` | Same as #3 | Same as #3 |
| 10 | `ActionType::OpenControllerKeyRemapping` → `la::OpenControllerKeyRemappingForSystem` | `main.cpp:1256` | Same as #3 | Same as #3 |
| 11 | `ActionType::LaunchHomebrewApplication` → `ecs::RegisterLaunchAsApplication(uLoader/application)` | `main.cpp:1156` | `app::Terminate()` called on `TerminateApplication` SMI | `ldrShellAtmosphereUnregisterExternalCode` **never called**; ECS handle leaked every launch |
| 12 | `__wrap_libappletLaunch` (uMenu-side, for its own sub-applet calls) | `am_LibnxLibappletWrap.cpp:70` | `OnScopeExit` closes holder via `appletHolderClose`; `appletHolderJoin` in `__wrap_libappletStart:55` | No ECS involved; this path is **clean** |

---

## Specifically Identified Leak Sites

### Leak A — ECS handle never unregistered (sites #1, #2, #11)

`ecs::RegisterExternalContent` at `ecs_ExternalContent.cpp:17` calls `ldrShellAtmosphereRegisterExternalCode` (IPC 65000), which returns a **move-handle** that is consumed by `sf::RegisterSession`. The AMS ldr:shell interface defines `AtmosphereUnregisterExternalCode` (IPC 65001) as the matching release. That call **does not exist anywhere in the uSystem project source or build artifacts** (`uSystem.map` and `uSystem.lst` contain no reference to cmd 65001 or `AtmosphereUnregisterExternalCode`).

Every `LaunchMenu`, `LaunchHomebrewLibraryApplet`, and `LaunchHomebrewApplication` call registers an ECS slot that is never freed.

**Repro pattern — Album + uLoader exhausts pool in 6 launches:**
1. Open Album (site #4) — `appletHolderStart` succeeds, native applet slot used.
2. User exits Album → `g_LastLibraryAppletLaunchedNotMenu` is `true`, `LaunchMenu` fires.
3. `LaunchMenu` calls `ecs::RegisterLaunchAsApplet(uMenu)` — ECS slot 1 used, never freed.
4. From uMenu, open Homebrew Chooser → site #2 fires → ECS slot 2 used, never freed.
5. Repeat Album / uMenu cycle four more times.
6. On the 7th `RegisterLaunchAsApplet` call, AMS ServerManager has exhausted its 6-slot pool → `2011-0102 ResultOutOfSessionMemory`.

### Leak B — `appletHolderClose` missing on normal-exit path for native applets (sites #3–#10)

`la::Terminate()` (`la_LibraryApplet.cpp:78`) calls `appletHolderRequestExitOrTerminate` then `appletHolderClose`. However, this is only triggered by `HandleHomeButton` (forced close). The **normal exit** path — user exits Web/Album/MiiEdit/etc. naturally — lands in `MainLoop:1311`:

```
if(!la::IsActive() && g_LastLibraryAppletLaunchedNotMenu) {
    ...
    LaunchMenu(menu_start_mode);     // ← appletHolderClose NOT called here
    g_LastLibraryAppletLaunchedNotMenu = true set → false by LaunchMenu
}
```

`la::Create` at `la_LibraryApplet.cpp:42` does call `Terminate()` when `IsActive()` is true before creating a new one, so the holder gets closed *on the next launch*. But this means **one session is always open** — the most recently launched applet's holder is closed only when `la::Create` is next called. If uMenu then crashes or the user power-cycles without another launch, that holder leaks into the session pool permanently.

---

## Highest-Leverage Fix Site

**`ecs_ExternalContent.cpp` — add `ldrShellAtmosphereUnregisterExternalCode` teardown.**

This single location covers leak sites #1, #2, and #11 (all ECS-registered applets and applications). Every session-pool exhaustion crash ultimately traces back to the missing IPC 65001 call. The native-applet holder leak (sites #3–#10) contributes one slot at most at any given time; the ECS leaks accumulate unboundedly.

The fix applies either inside `RegisterExternalContent` (unregister on the la::Start failure path) or, more critically, as a new `UnregisterExternalContent(program_id)` helper that is called from `MainLoop:1311` after the applet finishes and before `LaunchMenu` is called.

---

## Correct Teardown Pseudocode (for `LaunchHomebrewLibraryApplet` — site #2)

Applied at `main.cpp:1311` in the `!la::IsActive() && g_LastLibraryAppletLaunchedNotMenu` block, immediately before `LaunchMenu`:

```cpp
// In MainLoop, after the non-menu library applet finishes:
if(!la::IsActive() && g_LastLibraryAppletLaunchedNotMenu) {

    // --- NEW: close the AppletHolder if not already closed ---
    // la::IsActive() already returned false, meaning the applet process exited.
    // appletHolderClose releases the kernel handle in the IPC session slot.
    // Safe to call even if holder was already closed by la::Terminate().
    appletHolderClose(&g_LibraryAppletHolder);  // expose g_LibraryAppletHolder or add la::CloseHolder()

    // --- NEW: release the ECS loader slot for ECS-backed applets ---
    // Only needed when the finished applet was launched via RegisterLaunchAsApplet/AsApplication.
    // Track whether last launch was ECS-backed with a new bool g_LastLaunchWasEcs.
    if(g_LastLaunchWasEcs) {
        // IPC cmd 65001: ldr:shell AtmosphereUnregisterExternalCode
        // Add to ecs_ExternalContent.hpp:
        //   Result UnregisterExternalContent(u64 program_id);
        // Implemented in ecs_ExternalContent.cpp as:
        //   serviceDispatch(ldrShellGetServiceSession(), 65001,
        //       .in_send_pid = false,
        //       .in_num_objects = 1,
        //       .in_objects = { &program_id });
        const auto unrg_rc = ecs::UnregisterExternalContent(g_LastEcsProgramId);
        if(R_FAILED(unrg_rc)) {
            UL_LOG_WARN("UnregisterExternalContent 0x%016lX failed: 0x%x",
                        g_LastEcsProgramId, unrg_rc);
        }
        g_LastLaunchWasEcs = false;
    }

    // --- EXISTING: collect output if needed, then relaunch uMenu ---
    // ... (output collection unchanged) ...
    UL_RC_ASSERT(LaunchMenu(menu_start_mode));
    // LaunchMenu itself calls RegisterLaunchAsApplet for uMenu, which now
    // correctly follows the un-register of the previous ECS slot.
}
```

New globals required: `bool g_LastLaunchWasEcs = false` and `u64 g_LastEcsProgramId = 0`. Set both in `ActionType::LaunchHomebrewLibraryApplet` and `ActionType::LaunchHomebrewApplication` handlers at the same time `g_LastLibraryAppletLaunchedNotMenu = true` is set.

The `UnregisterExternalContent` IPC call must also be added to the `LaunchMenu` path at `main.cpp:463` for the uMenu ECS slot, called just before the next `RegisterLaunchAsApplet`, or integrated into `la::Create` so that whenever a previous ECS-backed applet is being replaced, its slot is freed first.
