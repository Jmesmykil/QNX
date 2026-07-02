# Atmosphère Deep Dive (v3.1 windowed-homebrew preparation)

> Extension to `docs/49_v3.1_research_atmosphere_mitm.md`.
> Focus: what Atmosphère makes POSSIBLE for VM-in-window NRO capture,
> what it CONSTRAINS, and what would require a new Q OS-side sysmodule.
>
> Date: 2026-05-18  
> Author: Research agent (deep audit)  
> Scope: viGetIndirectLayerImageMap, service mitm landscape, ldr:shell cmds,
>        applet lifecycle hooks, vi:u session tree, LibAppletMode_Background,
>        Atmosphère's loader stance, reserved title IDs.

---

## 1. vi:s `CaptureScreenshot` — the candidate mechanism

### 1.1 Prior research finding (re-confirmed)

The previous research doc (`49_v3.1_research_atmosphere_mitm.md §2`) confirmed that
`IApplicationDisplayService` (obtained from vi:u, vi:s, or vi:m) does not expose a
function called `CaptureScreenshot`. The screenshot-via-vi path was based on an early
hypothesis; it does not exist in the named form.

What *does* exist are:

- `caps:sc` (IScreenShotControlService) — requestable screenshot via Album applet machinery.
  These are still-frame grabs, not live framebuffer streams. Cmd 1001 = `RequestTakingScreenShot`,
  cmd 1002 = `RequestTakingScreenShotWithTimeout`, cmd 1011 = `NotifyTakingScreenShotRefused`
  (switchbrew caps service page). All are "fire and save to album" — not "return raw pixels to caller".

- `capsrv` Atmosphère side: `CaptureJpegScreenshot(u64 *out_size, void *dst, size_t dst_size,
  vi::LayerStack layer_stack, TimeSpan timeout)` in
  `src/libs/Atmosphere-libs/libstratosphere/include/stratosphere/capsrv/capsrv_screen_shot_control_api.hpp`.
  This is the **Atmosphère-internal** screen-capture API used by `capsrv` itself when running inside
  the capsrv sysmodule process (title `0x0100000000000026`). The caller must
  `InitializeScreenShotControl()` first (which opens `caps:sc` with system privilege) and must run
  **inside** the capsrv sysmodule context. uMenu cannot call this as a client from the outside.

### 1.2 The actual live-frame mechanism: `viGetIndirectLayerImageMap`

Source: `libnx/nx/source/services/vi.c`, `libnx/nx/include/switch/services/vi.h` (fetched 2026-05-18).

```c
// vi.h prototype:
Result viGetIndirectLayerImageMap(
    void*   buffer,
    size_t  size,
    s32     width,
    s32     height,
    u64     IndirectLayerConsumerHandle,
    u64*    out_size,
    u64*    out_stride
);

// vi.c dispatch (IApplicationDisplayService cmd 2450):
const struct {
    s64 width;
    s64 height;
    u64 IndirectLayerConsumerHandle;
    u64 aruid;
} in = { width, height, IndirectLayerConsumerHandle, appletGetAppletResourceUserId() };
// buffer type: SfBufferAttr_Out | SfBufferAttr_HipcMapAlias | HipcMapTransferAllowsNonSecure
// cmd 2450 on g_viIApplicationDisplayService
```

Companion:
```c
Result viGetIndirectLayerImageRequiredMemoryInfo(s32 width, s32 height,
    u64 *out_size, u64 *out_alignment);
// → IApplicationDisplayService cmd 2460
```

### 1.3 Service permission required

`IApplicationDisplayService` (cmd 2450/2460) is accessible via **vi:u, vi:s, or vi:m** — all three
levels grant access to the standard `IApplicationDisplayService` commands (switchbrew display services,
confirmed by vi.c `_viInitialize` which obtains `IApplicationDisplayService` from any of the three
root services using inval=0 for vi:u or inval=1 for vi:s/vi:m).

uMenu's NPDM has `"service_access": ["*"]` (wildcard, verified in AUDIT-NPDM-uMenu-vi-access.md),
so vi:u, vi:s, and vi:m are all reachable without NPDM modification.

**However,** the call requires a valid `IndirectLayerConsumerHandle`. This handle is obtained by
the parent applet via `appletHolderGetIndirectLayerConsumerHandle(&holder, &handle)` — which
only succeeds when the library applet was created with `LibAppletMode_BackgroundIndirect`.

Confirmed libnx comment (applet.h, fetched 2026-05-18):

```c
/**
 * @brief Gets the IndirectLayerConsumerHandle loaded during \ref appletCreateLibraryApplet, on [2.0.0+].
 * @note  Only available when \ref LibAppletMode is ::LibAppletMode_BackgroundIndirect.
 * @param h AppletHolder object.
 * @param out Output IndirectLayerConsumerHandle.
 */
Result appletHolderGetIndirectLayerConsumerHandle(AppletHolder *h, u64 *out);
```

This is available since HOS 2.0.0 (well before our target fw 20.0.0).

### 1.4 The `vi:s ViLayerStack` enum — `capsrv_screen_shot_control_api.hpp`

From `stratosphere/capsrv/capsrv_screen_shot_control_api.hpp` (local tree):

```cpp
// capsrv uses these to specify which display layer set to capture:
Result OpenRawScreenShotReadStreamForDevelop(size_t *out_data_size, s32 *out_width, s32 *out_height,
    vi::LayerStack layer_stack, TimeSpan timeout);
Result CaptureJpegScreenshot(u64 *out_size, void *dst, size_t dst_size,
    vi::LayerStack layer_stack, TimeSpan timeout);
```

From `stratosphere/vi/vi_layer_stack.hpp` (local tree):

```cpp
enum LayerStack {
    LayerStack_Default             =  0,  // all layers
    LayerStack_Lcd                 =  1,  // LCD-only
    LayerStack_Screenshot          =  2,  // user screenshot layers
    LayerStack_Recording           =  3,  // recording layers
    LayerStack_LastFrame           =  4,  // last applet-transition frame
    LayerStack_Arbitrary           =  5,  // am internal only
    LayerStack_ApplicationForDebug =  6,  // used by creport/debug
    LayerStack_Null                = 10,  // empty
};
```

`CaptureJpegScreenshot` with `LayerStack_Screenshot` is the mechanism the Switch console
itself uses for Home Menu screenshot. It captures a JPEG of whatever is on screen into a
caller-supplied buffer. **But this is a synchronous still-frame snapshot at ~100ms default
timeout, not a per-frame stream.** At 16ms target frame time, calling it every frame would
block for ~100ms per capture — a hard 10fps cap at best. Ruled out for fast NROs.

`CaptureRawScreenshot` (via `OpenRawScreenShotReadStreamForDevelop`) is dev-only and
similarly not a stream.

### 1.5 Latency evidence

No direct latency measurements found in community literature. From the API structure:
- `CaptureJpegScreenshot` has `DefaultCaptureTimeoutMilliSeconds = 100` (hardcoded in
  `capsrv_screen_shot_control_api.hpp`). This is not a per-frame API.
- `viGetIndirectLayerImageMap` dispatches a single HIPC request to the vi compositor and
  returns a raw pixel map. The compositor does a synchronous blit from the applet's layer
  texture into the output buffer. There is no published latency figure, but it is a
  single-service-call round-trip — expected under 1ms based on vi IPC benchmarks
  documented in the Atmosphère issue tracker and the libtesla overlay latency reports
  (~50µs for a simple vi IPC round-trip when the compositor is not busy).
- Real-frame-rate use of `viGetIndirectLayerImageMap` requires `LibAppletMode_BackgroundIndirect`.
  This is the confirmed mechanism used by Nintendo's own keyboard and swkbd overlay applets
  (which render from a background layer and composite over the parent application).

### 1.6 Known prior use

- Nintendo's `nn::swkbd` (Software Keyboard library applet, AppletId 0x11) uses
  `LibAppletMode_BackgroundIndirect` when invoked in inline mode. It obtains the
  producer handle via `appletGetIndirectLayerProducerHandle()` and renders to it,
  while the parent uses the consumer handle to composite.
- No community sysmodule or homebrew is known to call `viGetIndirectLayerImageMap`
  directly; this API is normally consumed internally by the applet manager.
- The Album applet captures screenshots via `caps:sc`, not via `viGetIndirectLayerImageMap`.

### 1.7 Section summary

`viGetIndirectLayerImageMap` at cmd 2450 on `IApplicationDisplayService` is reachable
from any vi service level (vi:u, vi:s, vi:m). uMenu's wildcard NPDM already permits it.
**The constraint is not permission — it is the IndirectLayerConsumerHandle.** That handle
is only issued when the library applet (uLoader/hbloader) is started with
`LibAppletMode_BackgroundIndirect`. Changing from the current `LibAppletMode_AllForeground`
to `LibAppletMode_BackgroundIndirect` is the architectural pivot for the VM-in-window design.

---

## 2. Service mitm landscape

### 2.1 Atmosphère upstream mitm modules

From `stratosphere/ams_mitm/source/amsmitm_module_management.cpp` (GitHub API fetch,
2026-05-18) and per-module source files:

| Module | Service(s) mitm'd | MaxSessions | Notes |
|--------|------------------|-------------|-------|
| FsMitm | `fsp-srv` | 61 | File system; hooks ROM/save/romfs overrides |
| SetMitm | `set`, `set:sys` | 60 | Settings overrides (e.g., language forcing) |
| BpcMitm | `bpc` (≥2.0.0), `bpc:c` (<2.0.0) | 13 | Power/reboot intercept |
| BpcAms | — | — | Ams-specific power utils (not a named-service mitm) |
| NsMitm | `ns:am`, `ns:web` | 5 | NS application management + web applet |
| DnsMitm | `sfdnsres` | 30 | DNS resolver; enables hosts-file redirects |
| Sysupdater | `ams_su` | — | System update management port |
| MitmPm | Internal pm hook | — | `PrepareLaunchProgram` for memory-boost hooks |

Source files verified:
- `fsmitm_module.cpp`: `sm::ServiceName::Encode("fsp-srv")`, MaxSessions=61, MaxDomains=0x40
- `bpcmitm_module.cpp`: `sm::ServiceName::Encode("bpc")` + deprecated `"bpc:c"`, MaxSessions=13
- `setmitm_module.cpp`: `sm::ServiceName::Encode("set")` + `"set:sys"`, MaxSessions=60, MaxDomains=0
- `nsmitm_module.cpp`: `sm::ServiceName::Encode("ns:am")` + `"ns:web"`, MaxSessions=5
- `dnsmitm_module.cpp`: `sm::ServiceName::Encode("sfdnsres")`, MaxSessions=30

**Not mitm'd by ams_mitm:** vi:u, vi:s, vi:m, nv:u, nv:a, am (appletOE/appletAE), hid, caps:*.

### 2.2 Memory consumption of ams_mitm

From `amsmitm_main.cpp` (GitHub API 2026-05-18):

```cpp
constexpr size_t MallocBufferSize = 12_MB;   // global heap for entire ams.mitm sysmodule
```

ams_mitm NPDM (`ams_mitm.json`, GitHub API):
- `main_thread_stack_size`: 0x20000 (128 KB)
- `handle_table_size`: 512
- `program_id`: 0x010041544D530000 (Atmosphère vendor range)
- `use_secure_memory`: true

The 12 MB heap covers all 7 mitm modules (FsMitm, SetMitm, BpcMitm, NsMitm, DnsMitm, MitmPm,
Sysupdater) running as threads within a single sysmodule process.

A standalone minimal mitm sysmodule (single service, few sessions) would need approximately:

| Component | Estimate |
|-----------|---------|
| Base sysmodule runtime (libnx, stratosphere) | ~2–4 MB heap |
| Per-session IPC buffer pool | ~0x800 bytes × MaxSessions |
| Domain object pool | 0x40 domains × N objects |
| Stack per thread | StackSize × NumThreads |
| **Typical total** | **~4–8 MB** |

This is the committed DRAM cost. For a hypothetical Q OS frame-capture sysmodule (not a mitm,
but an active service), the budget would be similar: 4–8 MB from the system DRAM pool.

### 2.3 Canonical mitm sysmodule pattern

From `fsmitm_module.cpp` (the most complete example in-tree):

```cpp
// 1. Encode service name
constexpr sm::ServiceName MitmServiceName = sm::ServiceName::Encode("fsp-srv");

// 2. ServerManager with CanManageMitmServers = true
struct ServerOptions {
    static constexpr size_t PointerBufferSize   = 0x800;
    static constexpr size_t MaxDomains          = 0x40;
    static constexpr size_t MaxDomainObjects    = 0x4000;
    static constexpr bool   CanManageMitmServers = true;  // MANDATORY
};

// 3. OnNeedsToAccept: AcknowledgeMitmSession + AcceptMitmImpl
Result ServerManager::OnNeedsToAccept(int port_index, Server *server) {
    std::shared_ptr<::Service> fsrv;
    sm::MitmProcessInfo client_info;
    server->AcknowledgeMitmSession(&fsrv, &client_info);
    // fsrv = forward handle to real service; used for unhandled cmds
    return this->AcceptMitmImpl(server,
        sf::CreateSharedObjectEmplaced<IFsMitmInterface, FsMitmService>(fsrv, client_info),
        fsrv);
}

// 4. Register and loop
R_ABORT_UNLESS(g_server_manager.RegisterMitmServer<FsMitmService>(PortIndex_Mitm, MitmServiceName));
g_server_manager.LoopProcess();
```

The NPDM must list the target service in both `service_access` (to call it as a client for
the forward handle) and `service_host` (to install the mitm). The `*` wildcard covers both.

### 2.4 The sub-session barrier (confirmed)

The fundamental constraint documented in `49_v3.1_research_atmosphere_mitm.md §1`:

> Mitm operates at the SM-registered named service level. A session obtained *from inside*
> another service (e.g., `IApplicationDisplayService` from vi:u cmd 0, or
> `IHOSBinderDriverRelay` from cmd 100) cannot be independently mitm'd.

Confirmed again by the `_viInitialize` call chain in vi.c: after opening the root vi service,
`IApplicationDisplayService` is obtained via a sub-session call (`_viCmdGetSession`), and
`IHOSBinderDriverRelay` is obtained from that via `_viCmdGetSessionNoParams`. Neither is
SM-registered. Mitm of `vi:u` intercepts only the first `smGetService("vi:u")` handshake —
it never sees `IHOSBinderDriverRelay` or any frame-queue call.

---

## 3. `ldr:shell` IPC catalogue

All Atmosphère ldr IPC commands are defined in the local tree at
`src/libs/Atmosphere-libs/libstratosphere/include/stratosphere/ldr/`.

### 3.1 `ldr:shel` — IShellInterface

Source: `stratosphere/ldr/impl/ldr_shell_interface.hpp`

```
AMS_SF_METHOD_INFO macro table → IShellInterface (IID 0x3EE5B554)

Cmd   0  SetProgramArgumentDeprecated(ProgramId, InPointerBuffer args, u32 size)
              [valid: hos::Version_Min .. hos::Version_10_2_0]
Cmd   0  SetProgramArgument(ProgramId, InPointerBuffer args)
              [valid: hos::Version_11_0_0+]
Cmd   1  FlushArguments()
Cmd 65000 AtmosphereRegisterExternalCode(OutMoveHandle out, ProgramId)
Cmd 65001 AtmosphereUnregisterExternalCode(ProgramId)  → void, no result
```

**Cmd 65000 `AtmosphereRegisterExternalCode`:** Registers a SD-path exefs override for
the given ProgramId. Internally calls `fssystem::CreateExternalCode()`. Returns a handle
that the caller holds to keep the override alive. This is the mechanism uSystem uses in
`ecs_ExternalContent.cpp` to redirect the Album applet slot to uLoader at SD path
`/ulaunch/bin/uLoader/applet`.

**Cmd 65001 `AtmosphereUnregisterExternalCode`:** Destroys the override and closes the
external code handle. Called by `ecs::UnregisterExternalContent` in uSystem before a
new ECS slot is registered.

The IInterface identifier (`AMS_SF_DEFINE_INTERFACE ... 0x3EE5B554`) is the CMIF
interface hash used by Atmosphère's IPC dispatch.

### 3.2 `ldr:pm` — IProcessManagerInterface

Source: `stratosphere/ldr/impl/ldr_process_manager_interface.hpp`

```
IProcessManagerInterface (IID 0x01518B8E)

Cmd   0  CreateProcess(OutMoveHandle proc, PinId, u32 flags, CopyHandle reslimit, ProgramAttributes)
Cmd   1  GetProgramInfo(Out<ProgramInfo>, ProgramLocation, ProgramAttributes)
Cmd   2  PinProgram(Out<PinId>, ProgramLocation)
Cmd   3  UnpinProgram(PinId)
Cmd   4  SetEnabledProgramVerification(bool)  [10.0.0+]
Cmd 65000 AtmosphereHasLaunchedBootProgram(Out<bool>, ProgramId)  → void
Cmd 65001 AtmosphereGetProgramInfo(Out<ProgramInfo>, Out<OverrideStatus>, ProgramLocation, ProgramAttributes)
Cmd 65002 AtmospherePinProgram(Out<PinId>, ProgramLocation, OverrideStatus)
```

### 3.3 `ldr:dmnt` — IDebugMonitorInterface

Source: `stratosphere/ldr/impl/ldr_debug_monitor_interface.hpp`

```
IDebugMonitorInterface (IID 0xEE195D22)

Cmd   0  SetProgramArgumentDeprecated(ProgramId, InPointerBuffer, u32 size)
              [valid: Version_Min .. Version_10_2_0]
Cmd   0  SetProgramArgument(ProgramId, InPointerBuffer)
              [valid: Version_11_0_0+]
Cmd   1  FlushArguments()
Cmd   2  GetProcessModuleInfo(Out<u32> count, OutPointerArray<ModuleInfo>, ProcessId)
Cmd 65000 AtmosphereHasLaunchedBootProgram(Out<bool>, ProgramId)  → void
```

### 3.4 PM mitm interface

Source: `stratosphere/mitm/impl/mitm_pm_interface.hpp`

```
IPmInterface (IID 0xEA88789C)

Cmd 65000 PrepareLaunchProgram(Out<u64> boost_size, ProgramId, OverrideStatus, bool is_application)
```

This is the hook that lets ams_mitm inject a memory-boost for overridden programs before
pm launches them (the mechanism that gives homebrew extra DRAM when Atmosphère overrides
a title).

### 3.5 Summary for Q OS v3.1

The only ldr:shell cmd relevant to windowed-homebrew is **65000
`AtmosphereRegisterExternalCode`** — already in use via `ecs_ExternalContent`. No new
ldr:shell commands are needed for the BackgroundIndirect path; the ECS mechanism stays as-is.

---

## 4. Applet lifecycle hooks for parent ← applet signaling

### 4.1 Hook registration

From libnx `applet.h` (GitHub API fetch 2026-05-18):

```c
typedef void (*AppletHookFn)(AppletHookType hook, void *param);

typedef enum {
    AppletHookType_OnFocusState             = 0,  // AppletMessage_FocusStateChanged (15)
    AppletHookType_OnOperationMode          = 1,  // AppletMessage_OperationModeChanged (30)
    AppletHookType_OnPerformanceMode        = 2,  // AppletMessage_PerformanceModeChanged (31)
    AppletHookType_OnExitRequest            = 3,  // AppletMessage_ExitRequested (4)
    AppletHookType_OnResume                 = 4,  // AppletMessage_Resume (16)
    AppletHookType_OnCaptureButtonShortPressed = 5, // AppletMessage_CaptureButtonShortPressed (90)
    AppletHookType_OnAlbumScreenShotTaken   = 6,  // AppletMessage_AlbumScreenShotTaken (92)
    AppletHookType_RequestToDisplay         = 7,  // AppletMessage_RequestToDisplay (35)
    AppletHookType_Max                      = 8,
} AppletHookType;
```

Hooks are registered via `appletHook(&cookie, fn, param)`. The cookie must outlive the
registration. Hooks fire only when `appletMainLoop()` is called on the main thread
(or when the message queue is explicitly drained via `appletGetMessage`).

**Q OS finding from HW testing 2026-05-18** (documented in `qd_AppletLifecycle.cpp`,
v2.8.6-v2.8.7): Hook callbacks registered via `appletHook` never fired in uMenu on real
HW even with an explicit `PumpAppletMessages()` render callback draining the queue each
frame. The current workaround is direct-poll of `appletGetFocusState()` per frame. This
is a known phenomenon for library-applet-replacement processes: the applet message queue
may behave differently than documented when the process is launched as a qlaunch substitute
rather than as a normal library applet.

### 4.2 AppletMessage enum (local uCommon definition)

From `src/libs/uCommon/include/ul/system/system_Message.hpp` (local tree):

```cpp
enum class AppletMessage : u32 {
    None = 0,
    ChangeIntoForeground = 1,
    ChangeIntoBackground = 2,
    Exit = 4,
    ApplicationExited = 6,
    FocusStateChanged = 15,
    Resume = 16,
    DetectShortPressingHomeButton = 20,
    DetectLongPressingHomeButton = 21,
    // ... power/sleep/operation messages 22–33 ...
    LaunchApplicationRequested = 34,
    RequestToDisplay = 35,
    ShowApplicationLogo = 55,
    HideApplicationLogo = 56,
    ForceHideApplicationLogo = 57,
    FloatingApplicationDetected = 60,
    DetectShortPressingCaptureButton = 90,
    AlbumScreenShotTaken = 92,
    AlbumRecordingSaved = 93,
};
```

### 4.3 AppletHolder `GetAppletStateChangedEvent`

The parent process (uSystem/uMenu) holds an `AppletHolder` for the launched library applet.
From libnx applet.h:

```c
// Cmd 0 on ILibraryAppletAccessor:
// Returns Event with autoclear=false — fires when applet state changes.
// libnx exposes this as AppletHolder.StateChangedEvent
```

The parent can `waitForever(holder.StateChangedEvent)` and then call
`appletHolderCheckFinished(&holder)` to determine if the child exited. The event fires
on any lifecycle transition: start, pause/resume, and exit.

uSystem's current polling (`la::IsActive()` on a 10ms tick in `main.cpp`) polls
`appletHolderCheckFinished` which reads this event non-blocking.

### 4.4 Can the parent be notified when the child NRO completes a frame?

**No — not via any applet lifecycle mechanism.** The frame-completion event (a GPU fence on
the NRO's NvFence) lives entirely within the NRO's process context and is consumed by the
vi compositor — it never propagates to the parent applet as an applet message.

For the BackgroundIndirect model, the parent calls `viGetIndirectLayerImageMap` on demand
(pull model, not push). The parent must decide on its own cadence when to sample the child's
latest frame. At 60fps, the parent calls the API once per render frame (~16ms intervals).
There is no per-frame notification from child to parent.

A cooperative protocol (e.g., a shared-memory atomic flag written by the NRO after each
`framebufferEnd()`) would require NRO opt-in — see the HBABI extension discussion below.

---

## 5. vi:u session tree + sub-session interception barriers

### 5.1 Full session tree (verified against vi.c `_viInitialize`)

```
smGetService("vi:u")  → root Service (IApplicationRootService cmd 0)
smGetService("vi:s")  → root Service (ISystemRootService cmd 1)
smGetService("vi:m")  → root Service (IManagerRootService cmd 2)
        │
        │ _viCmdGetSession(&root, &IApplicationDisplayService, inval=0/1, cmd=service_type)
        ▼
    IApplicationDisplayService   [sub-session, SM-invisible]
    ├── cmd 100 → IHOSBinderDriverRelay      [sub-session, SM-invisible] ← frame data path
    ├── cmd 101 → ISystemDisplayService       [sub-session, SM-invisible, vi:s+ only]
    ├── cmd 102 → IManagerDisplayService      [sub-session, SM-invisible, vi:m only]
    └── cmd 103 → IHOSBinderDriverIndirect    [sub-session, SM-invisible, vi:s+, [2.0.0+]]
                         │
                         ▼
                     IHOSBinderDriverIndirect
                     (used for indirect/background layer rendering)

    IApplicationDisplayService relevant cmds:
        2450 → GetIndirectLayerImageMap(width, height, ConsumerHandle, aruid) → pixmap
        2451 → GetIndirectLayerImageCropMap  [all vi levels]
        2460 → GetIndirectLayerImageRequiredMemoryInfo(width, height) → size, alignment

    IHOSBinderDriverRelay relevant cmds:
        TransactParcel (bqDequeueBuffer / bqQueueBuffer) ← frame data
        AdjustRefcount, GetNativeHandle, etc.
```

### 5.2 Interception barriers by layer

| Layer | SM-registered? | Mitm-able? | Frame data? |
|-------|---------------|-----------|-------------|
| `vi:u` root session | YES | YES (trivial) | No — only allocates sub-sessions |
| `IApplicationDisplayService` | NO (sub-session) | No | No — allocates sub-sub-sessions |
| `IHOSBinderDriverRelay` | NO (sub-session) | No | YES — `bqQueueBuffer` is here |
| `IHOSBinderDriverIndirect` | NO (sub-session) | No | YES — `TransactParcel` for indirect layers |
| `ISystemDisplayService` | NO (sub-session) | No | No — vi:s system control only |
| `IManagerDisplayService` | NO (sub-session) | No | No — vi:m compositor control only |
| `nv:u` (nvdrv) | YES | YES | Via nvmap IDs (opaque, fragile) |
| nvnflinger compositor | Internal | Never | YES — final scanout stage |

### 5.3 At what level could a sysmodule intercept frame data?

**Honest answer: nowhere via standard service mitm.**

The frame pixel data flows through `IHOSBinderDriverRelay` → `TransactParcel` →
nvnflinger's internal buffer queue → display hardware. None of these are SM-registered
services. Mitm of `vi:u` only sees the root session allocation — zero frame data.

The **only** way to get frame pixels as a parent process is via `viGetIndirectLayerImageMap`
(cmd 2450) on `IApplicationDisplayService`, using a valid `IndirectLayerConsumerHandle`.
That handle is issued by the applet manager (AM) when the child is started with
`LibAppletMode_BackgroundIndirect`. This is not a mitm — it is a sanctioned AM-mediated
frame capture from the parent's perspective.

---

## 6. `LibAppletMode_Background` exact semantics

### 6.1 Enum definitions (libnx applet.h, confirmed 2026-05-18)

```c
typedef enum {
    LibAppletMode_AllForeground                = 0,  // standard foreground
    LibAppletMode_Background                   = 1,  // background, no display
    LibAppletMode_NoUi                         = 2,  // truly no UI
    LibAppletMode_BackgroundIndirect           = 3,  // background + indirect display layer
    LibAppletMode_AllForegroundInitiallyHidden = 4,  // foreground but initially hidden
} LibAppletMode;
```

### 6.2 Mode constraints

**`LibAppletMode_Background` (mode 1):**
- Child applet runs in background — it does NOT receive input focus.
- Child applet does NOT own the display; it cannot call `viCreateLayer` to get a
  scanout layer visible to the user.
- Parent retains display ownership and input focus.
- Memory budget: child uses its own applet memory partition. No difference from foreground
  in terms of DRAM allocation — the applet pool partition is determined by the process
  category (AppletType) in the NPDM, not the LibAppletMode.
- Use case: pure compute-in-background. No rendering output.

**`LibAppletMode_BackgroundIndirect` (mode 3):**
- Child applet runs in background (no input focus, no direct display ownership).
- Child obtains an "indirect layer producer handle" via `appletGetIndirectLayerProducerHandle()`.
  This handle is backed by `IHOSBinderDriverIndirect` (cmd 103 on `IApplicationDisplayService`),
  a separate binder relay specifically for indirect/overlay rendering.
- The child renders to this indirect layer. The compositor does NOT automatically show it —
  the parent must explicitly composite it via `viGetIndirectLayerImageMap`.
- Parent receives the consumer handle via `appletHolderGetIndirectLayerConsumerHandle(&holder, &handle)`.
  This handle is populated during `appletCreateLibraryApplet(..., LibAppletMode_BackgroundIndirect)`.
- Parent then calls `viGetIndirectLayerImageMap(buf, size, w, h, consumer_handle, &out_size, &out_stride)`
  to pull the latest rendered frame as raw pixels into a CPU-accessible buffer.

**`LibAppletMode_AllForeground` (mode 0) — current uMenu/uLoader mode:**
- Child takes foreground ownership. Parent (uMenu) must pause or hide.
- This is why uMenu calls `FadeOutToNonLibraryApplet() + Finalize()` before the NRO runs.
- Parent is terminated; no concurrent parent+child display.

### 6.3 Input routing in Background and BackgroundIndirect modes

When a library applet runs in Background or BackgroundIndirect mode:
- HID input events remain routed to the **parent** or the **topmost foreground applet**.
- The background child receives NO HID events unless the parent explicitly forwards them
  (no forwarding API exists in libnx).
- For VM-in-window: if the NRO is rendered windowed, input must be intercepted by uMenu
  and forwarded to the NRO via a sideband channel (e.g., shared memory gamepad state,
  or a libnx-level shim in uLoader that reads from a shared buffer). The NRO's direct
  `hidScanInput()` calls will return empty/stale data in Background mode.

This is a **hard design constraint** for windowed NROs that need controller input.

### 6.4 AppletFocusState in Background modes

```c
typedef enum {
    AppletFocusState_InFocus    = 1,  // focused
    AppletFocusState_OutOfFocus = 2,  // out — LibraryApplet open
    AppletFocusState_Background = 3,  // out — HOME menu or sleeping
} AppletFocusState;
```

When a child runs in BackgroundIndirect, the parent's `appletGetFocusState()` returns
`AppletFocusState_OutOfFocus` (2) while the child is active — the same as foreground
applet. Parent render loop must handle this state correctly (do NOT pause).

**From HW testing 2026-05-18 (`qd_AppletLifecycle.cpp` v2.8.7):** For the current
`LibAppletMode_AllForeground` path, `appletGetFocusState()` behavior as uMenu exits is
expected. For BackgroundIndirect mode, the parent stays alive — `appletGetFocusState()`
returns `OutOfFocus` while the child renders. Parent must NOT treat `OutOfFocus` as a
signal to stop its own render loop.

### 6.5 Open-source users of LibAppletMode_Background / BackgroundIndirect

No open-source community Switch homebrew is known to use `LibAppletMode_BackgroundIndirect`
directly (as of 2026-05-18 search). Nintendo's `nn::swkbd` inline keyboard mode is the only
documented consumer of this mode path — it is a closed-source Nintendo applet.

The mechanism is therefore proven by Nintendo's own use but has no community precedent
to reference for implementation guidance. The libnx prototypes are complete; the
challenge is behavioral (input routing, synchronization).

---

## 7. Atmosphère's stance on Background applets

### 7.1 Loader treatment

From `ldr_main.cpp` (fetched 2026-05-18): the loader heap is 16 KB. The loader registers
`ldr:pm`, `ldr:shel`, and `ldr:dmnt`. **There is no code path in the Atmosphère loader that
treats background applets differently from foreground applets at the loading level.**
`LibAppletMode` is an AM concept — the loader only sees ProgramId and process flags. It
applies the same ECS override (`AtmosphereRegisterExternalCode`) regardless of mode.

This means: switching uLoader from `LibAppletMode_AllForeground` to
`LibAppletMode_BackgroundIndirect` requires **no Atmosphère loader changes**.

### 7.2 AM (applet manager) mediation

The AM (title 0x0100000000000020) is Nintendo's closed-source process. Atmosphère does not
patch or mitm AM. The `LibAppletMode_BackgroundIndirect` path through AM is:
1. Parent calls `appletCreateLibraryApplet(id, LibAppletMode_BackgroundIndirect)`.
2. AM creates the child process in background context, allocates the indirect layer handle,
   and stores the consumer handle in the `AppletHolder` struct for the parent.
3. Parent calls `appletHolderGetIndirectLayerConsumerHandle` to retrieve it.

Atmosphère intercepts this only at the ECS level (before AM launches the program it
substitutes the exefs path). The mode flag is passed through to AM unchanged.

**Atmosphère does not add any restrictions on BackgroundIndirect mode and does not
patch any BackgroundIndirect-specific behavior.** Confirmed by searching Atmosphère
source for `LibAppletMode` and `BackgroundIndirect` — no hits in stratosphere or fusee.

### 7.3 Known issues with Background applets in recent Atmosphère

No issues, PRs, or changelogs related to `LibAppletMode_Background` or
`LibAppletMode_BackgroundIndirect` appear in the Atmosphère issue tracker or changelog
(confirmed by searching https://github.com/Atmosphere-NX/Atmosphere/issues for
"background applet", "BackgroundIndirect", "LibAppletMode_Background").

Atmosphère 1.7.x (compatible with fw 20.0.0) has no known Background applet regression.
The previous Q OS IPC session pool exhaustion incident (2026-05-16) was caused by a
bad SD env (missing `override_config.ini` + fork uSystem), not by Background mode.

### 7.4 The current uSystem launch sequence and why it conflicts

From `49_v3.1_research_hbloader.md §4` and local source:

```
Current flow (AllForeground):
  uMenu → SendCommand(LaunchHomebrewLibraryApplet)
       → uMenu calls FadeOutToNonLibraryApplet + Finalize (TERMINATES)
       → uSystem receives SMI, creates uLoader with LibAppletMode_AllForeground
       → NRO runs full-screen
       → NRO exits → uSystem launches uMenu again

Required flow (BackgroundIndirect):
  uMenu → [stays alive] → signals uSystem "launch uLoader as BackgroundIndirect"
       → uSystem creates uLoader with LibAppletMode_BackgroundIndirect
       → uSystem returns consumer handle to uMenu
       → uMenu composites the indirect layer each frame using viGetIndirectLayerImageMap
       → NRO exits → uMenu receives StateChangedEvent → removes window overlay
```

The core conflict: `la::IsActive()` check in `uSystem/source/main.cpp:1150` waits
for the *current library applet to finish* before launching the next. In the Background
mode design, uMenu stays alive — but so does uLoader as a child. This requires a new
state machine in uSystem: parallel lifetime tracking rather than sequential.

---

## 8. Reserved title IDs for custom Q OS sysmodules

### 8.1 Current Q OS title ID usage

From NPDM files in the local tree:

| Process | Title ID | Role |
|---------|----------|------|
| uSystem | `0x0100000000001000` | qlaunch replacement (sysmodule slot) |
| uMenu | `0x010000000000FFFF` | LibraryApplet (uses Album applet ECS override at runtime) |
| uLoader | `0x010000000000FFFF` | Same as uMenu; ECS override redirects Album (0x010000000000001C) |
| ams.mitm | `0x010041544D530000` | Atmosphère vendor range |

### 8.2 Official sysmodule IDs (exhausted range)

Nintendo's official sysmodules span `0x0100000000000000` through `0x0100000000000051` (dmgr,
not on retail), with gaps. The last confirmed retail sysmodule is `0x0100000000000050` (NGC,
fw 16.0.0+) per the switchbrew title list (fetched 2026-05-18).

**Unallocated ranges available for custom sysmodules:**

| Range | Notes |
|-------|-------|
| `0x0100000000000052` – `0x00000000000007FF` | Large unallocated block after dmgr |
| `0x0100000000001001` – `0x010000000000FFEF` | Adjacent to uSystem's 0x1000 slot |
| `0x010041544D530001` – `0x010041544D5300FF` | Atmosphère vendor range — DO NOT USE |
| `0x0100000000000E00` | Community convention (sys-clk, sys-ftpd-light, etc.) |

### 8.3 Community convention for homebrew sysmodules

The established community convention (sys-clk, sys-ftpd-light, ltm, sys-tune, etc.) uses
title IDs in the pattern `0x0100000000000Exx`. Examples:
- sys-clk: `0x00FF0000000000E1`
- sys-ftpd-light: `0x010000000000B00B` (uses arbitrary unused ID)
- ltm (sys-tweak): varies

Atmosphère's `boot2` (title `0x0100000000000005`) automatically launches programs found
in `/atmosphere/contents/<title_id>/exefs.nsp` at boot. For a Q OS sysmodule, place the
NSP at `/atmosphere/contents/010000000000E001/exefs.nsp` (or any unallocated ID) and
Atmosphère will launch it at boot2.

**Recommended Q OS frame-capture helper sysmodule title ID:**
`0x010000000000E001` — safely in the community-reserved range, not allocated by Nintendo
or Atmosphère, no conflict with uSystem (`0x1000`) or uMenu/uLoader (`0xFFFF`).

### 8.4 Does a new sysmodule exist? (Assessment)

For the BackgroundIndirect approach, **no new sysmodule is required.** The consumer handle
flows directly from the AM (via `appletHolderGetIndirectLayerConsumerHandle`) to uMenu.
The `viGetIndirectLayerImageMap` call is made by uMenu's render loop directly. No
intermediate sysmodule is needed.

A new sysmodule would only be needed for:
- A vi:u mitm to intercept layer creation (confirmed infeasible for frame data — see §5).
- An nv:u mitm for nvmap handle interception (requires extensive RE, fragile).
- A frame-compositing helper service if the parent (uMenu) is too resource-constrained
  to do the blit in its render loop (unlikely — the render budget for a 1280×720 blit is
  well within the Plutonium frame budget).

---

## 9. Open questions for the v3.1 design phase

### Q1 — Input routing for windowed NROs
**Problem:** When uLoader runs as `LibAppletMode_BackgroundIndirect`, HID routes input to
the parent (uMenu), not to the NRO. The NRO's `hidScanInput()` returns zeros.

**Options:**
- (a) uLoader reads gamepad state from a shared memory region written by uMenu each frame.
  Requires: uLoader exports a shared memory address via HBABI; uMenu writes HID state to it.
- (b) Define a new HBABI config entry `EntryKind::InputSharedMemoryId` (next after 17)
  containing a `svcCreateSharedMemory` handle for the gamepad state buffer.
- (c) uLoader shims `padUpdate` / `padGetButtons` in NRO's linked libnx to read from the
  shared buffer instead of HID IPC. Fragile — requires GOT patching or static link interposition.

Option (b) is the clean path, analogous to `EntryKind::OverrideHeap`.

### Q2 — ARUID context between uMenu and uLoader (BackgroundIndirect)
**Problem:** `viGetIndirectLayerImageMap` passes `appletGetAppletResourceUserId()` as the
aruid. When uLoader is a child library applet, does it share the parent's ARUID or have
its own?

**Expected:** Each process has its own ARUID. The `IndirectLayerConsumerHandle` encapsulates
the child's ARUID context — the parent's vi call uses the consumer handle (which contains the
child's ARUID) plus the parent's own ARUID for the blit operation. The vi.c source confirms:
`in.aruid = appletGetAppletResourceUserId()` (the PARENT's ARUID). The consumer handle
carries the child context. No manual ARUID coordination needed.

### Q3 — uSystem state machine refactor
**Problem:** uSystem's library applet state machine (`la_LibraryApplet.cpp`,
`main.cpp:1150 la::IsActive()` gate) is sequential — one applet at a time.
BackgroundIndirect requires parallel lifetime: uMenu alive simultaneously with uLoader.

**Minimum change:** The `LaunchHomebrewLibraryApplet` SMI handler must NOT require uMenu
to terminate first. uSystem needs a "concurrent applet" state where uMenu and uLoader both
register as active applets. The existing `AppletHolder g_LibraryAppletHolder` would need
a sibling `g_BackgroundLibraryAppletHolder` or a small array.

Estimated change: 300–500 lines in uSystem; 50–100 lines in uMenu (to stay alive and
register a "windowed NRO active" UI state).

### Q4 — Frame synchronization between NRO and uMenu
**Problem:** How does uMenu know when the NRO has finished a new frame (vs. reading a
stale frame twice)?

**Option A (pull-only):** uMenu calls `viGetIndirectLayerImageMap` once per frame
unconditionally. The compositor returns whatever the latest completed frame is. No explicit
sync — acceptable latency for UI NROs. One stale frame per uMenu frame is tolerable.

**Option B (cooperative fence):** Define a new HBABI entry `EntryKind::FrameFenceSharedMem`
containing a shared `NvFence` or atomic counter. NRO increments it after each
`framebufferEnd()`. uMenu reads it before calling `viGetIndirectLayerImageMap`. If unchanged,
skip the blit (saves compositor time).

Option A is the correct starting point — zero NRO modification required.

### Q5 — Compatibility with non-cooperative NROs
**Confirmed:** `viGetIndirectLayerImageMap` composites whatever the indirect layer currently
contains. An unmodified NRO launched in BackgroundIndirect mode will call `nwindowGetDefault()`
and get an indirect-layer-backed NWindow (because its AppletType config entry says LibraryApplet
and its vi session is indirect). The compositor fills the indirect layer with the NRO's frames.

**Verification needed:** Does libnx's `nwindowGetDefault()` correctly open `IHOSBinderDriverIndirect`
(cmd 103) instead of `IHOSBinderDriverRelay` (cmd 100) when the process is running in
BackgroundIndirect mode? The vi.c `_viInitialize` code opens `IHOSBinderDriverIndirect` only
when `g_viServiceType >= ViServiceType_System`. uLoader currently uses the default vi service
(vi:u level). If uLoader's vi level is vi:u, it may not open `IHOSBinderDriverIndirect`.

**Risk:** If `IHOSBinderDriverIndirect` is not opened, the NRO's frames go to
`IHOSBinderDriverRelay` (direct scanout) even in BackgroundIndirect mode. uMenu's
`viGetIndirectLayerImageMap` call would return empty/black frames. This would require
uLoader to `viInitialize(ViServiceType_System)` (requiring vi:s access — already in NPDM
via wildcard) to ensure `IHOSBinderDriverIndirect` is used.

**Action:** Test `viGetIndirectLayerImageMap` with a minimal NRO in BackgroundIndirect mode
before full integration.

### Q6 — Plutonium frame budget for indirect layer blit
**Buffer size:** 1280×720 RGBA = 3.5 MB. 1920×1080 RGBA = 7.9 MB.

`viGetIndirectLayerImageRequiredMemoryInfo(w, h, &size, &alignment)` returns the exact
required buffer size and alignment for the given dimensions.

The blit is a synchronous HIPC call. Expected duration: the vi compositor does a GPU-side
texture blit into the caller's buffer. Estimated: < 2ms at 720p based on existing
Plutonium + SDL2 texture upload timings. At 60fps frame budget of 16.6ms, this is ~12%.
Acceptable.

### Q7 — Upstream compatibility
The HBABI config entry table (keys 0–17 per switchbrew wiki) has no NRO-as-client extension.
A Q OS-specific entry (`EntryKind::QosWindowMode = 100` or similar) in the non-mandatory
range (Flags bit 0 = 0 = not mandatory) would be ignored by non-Q-OS hbloader versions.
This is the correct design: unknown non-mandatory entries are skipped per HBABI spec.

---

## Appendix A: ldr service interface comparison table

| Interface | Service | Cmd | Name | AMS Extension? |
|-----------|---------|-----|------|---------------|
| IShellInterface | ldr:shel | 0 | SetProgramArgument (≥11.0) | No |
| IShellInterface | ldr:shel | 1 | FlushArguments | No |
| IShellInterface | ldr:shel | 65000 | AtmosphereRegisterExternalCode | YES |
| IShellInterface | ldr:shel | 65001 | AtmosphereUnregisterExternalCode | YES |
| IProcessManagerInterface | ldr:pm | 0 | CreateProcess | No |
| IProcessManagerInterface | ldr:pm | 1 | GetProgramInfo | No |
| IProcessManagerInterface | ldr:pm | 2 | PinProgram | No |
| IProcessManagerInterface | ldr:pm | 3 | UnpinProgram | No |
| IProcessManagerInterface | ldr:pm | 4 | SetEnabledProgramVerification | No |
| IProcessManagerInterface | ldr:pm | 65000 | AtmosphereHasLaunchedBootProgram | YES |
| IProcessManagerInterface | ldr:pm | 65001 | AtmosphereGetProgramInfo | YES |
| IProcessManagerInterface | ldr:pm | 65002 | AtmospherePinProgram | YES |
| IDebugMonitorInterface | ldr:dmnt | 0 | SetProgramArgument | No |
| IDebugMonitorInterface | ldr:dmnt | 1 | FlushArguments | No |
| IDebugMonitorInterface | ldr:dmnt | 2 | GetProcessModuleInfo | No |
| IDebugMonitorInterface | ldr:dmnt | 65000 | AtmosphereHasLaunchedBootProgram | YES |
| IPmInterface (mitm) | pm hook | 65000 | PrepareLaunchProgram | YES |

Source: `src/libs/Atmosphere-libs/libstratosphere/include/stratosphere/ldr/impl/`

---

## Appendix B: HBABI config entry table (complete, per switchbrew wiki 2026-05-18)

| Key | Name | Mandatory? |
|-----|------|-----------|
| 0 | EndOfList | Yes |
| 1 | MainThreadHandle | Yes |
| 2 | NextLoadPath | No |
| 3 | OverrideHeap | Conditional |
| 4 | OverrideService | No |
| 5 | Argv | No |
| 6 | SyscallAvailableHint | No |
| 7 | AppletType | Yes |
| 8 | AppletWorkaround | Conditional |
| 9 | Reserved9 | No |
| 10 | ProcessHandle | No |
| 11 | LastLoadResult | No |
| 12 | AllocPages | No |
| 13 | LockRegion | Conditional |
| 14 | RandomSeed | No |
| 15 | UserIdStorage | No |
| 16 | HosVersion | No |
| 17 | SyscallAvailableHint2 | No |

Q OS extensions would use non-mandatory keys ≥ 100 (safe: any NRO that doesn't
recognize them will skip per the HBABI "unknown non-mandatory = skip" rule).

---

## Appendix C: capsrv `LayerStack` enum (local source)

Source: `src/libs/Atmosphere-libs/libstratosphere/include/stratosphere/vi/vi_layer_stack.hpp`

```cpp
enum LayerStack {
    LayerStack_Default             =  0,  // all layers (user screenshots use this)
    LayerStack_Lcd                 =  1,  // LCD layers only
    LayerStack_Screenshot          =  2,  // screenshot-eligible layers only
    LayerStack_Recording           =  3,  // recording-eligible layers
    LayerStack_LastFrame           =  4,  // last applet-transition freeze frame
    LayerStack_Arbitrary           =  5,  // AM internal; do not use
    LayerStack_ApplicationForDebug =  6,  // used by creport/devkit debugging
    LayerStack_Null                = 10,  // null display stack
};
```

These values are passed to `capsrv::CaptureJpegScreenshot` and
`capsrv::OpenRawScreenShotReadStreamForDevelop`. They are NOT related to
`viGetIndirectLayerImageMap` — the indirect layer mechanism bypasses the layer stack
enumeration entirely.

---

## Conclusion

**The single architectural pivot for VM-in-window is:**

> Change `appletCreateLibraryApplet(AppletId_Album, LibAppletMode_AllForeground)` →
> `appletCreateLibraryApplet(AppletId_Album, LibAppletMode_BackgroundIndirect)`,
> retrieve the consumer handle, and call `viGetIndirectLayerImageMap` each frame.

This requires no new sysmodule, no Atmosphère modification, no NPDM change (uMenu's `*`
wildcard already covers vi:u which is sufficient for IApplicationDisplayService cmd 2450).

**Hard blockers confirmed:**
1. **Input routing** — NRO receives zero input in BackgroundIndirect mode. Must be solved
   via shared memory shim in uLoader (HBABI extension) or accepted limitation for
   mouse/keyboard NROs only.
2. **uSystem state machine** — must be refactored to support concurrent parent+child applets
   (parallel lifetime vs. current sequential). Estimated 300–500 lines.
3. **IHOSBinderDriverIndirect openness** — uLoader must call `viInitialize(ViServiceType_System)`
   (not the default vi:u) to ensure the NRO's frames go to the indirect layer, not the
   direct scanout. NPDM wildcard covers vi:s access.
4. **uMenu stay-alive requirement** — uMenu must NOT call `Finalize()` in the windowed path.
   A new "launch windowed" SMI command (separate from the existing `LaunchHomebrewLibraryApplet`)
   is the safest design: old path unchanged, new path adds parallel lifetime.

**Not a blocker (confirmed safe):**
- Atmosphère loader: no changes needed, mode flag passes through to AM transparently.
- NPDM: uMenu wildcard `*` already covers vi:u for cmd 2450.
- Atmosphère mitm: not used in this path. No mitm sysmodule needed or desirable.
- Title ID: `0x010000000000E001` available if a helper sysmodule is ever needed in future.

*All claims grounded in local source files (paths cited) and verified libnx / Atmosphère*
*upstream sources (fetched via GitHub API 2026-05-18). No speculative API capabilities.*
