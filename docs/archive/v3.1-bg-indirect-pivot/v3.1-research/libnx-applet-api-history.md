# libnx + nx-hbloader + nx-hbmenu API History (v3.1 context)

> Historical and API context for Q OS v3.1 universal VM-in-window design.
> The v3.1 implementation will NOT extend HBABI; this document is for understanding
> what the current ABI looks like and what historical alternatives existed.
>
> Sources: libnx Changelog.md (v4.10.0 down to v1.x), upstream nx-hbloader main.c
> (fetchable via github.com/switchbrew/nx-hbloader), upstream nx-hbmenu main.c and
> nx_graphics.c (github.com/switchbrew/nx-hbmenu), libnx native_window.h,
> libnx applet.h / capssc.h / vi.h, SwitchBrew wiki Homebrew_ABI page,
> Atmosphere cfg_override.board.nintendo_nx.inc, local ulaunch source tree.
> All fetches performed 2026-05-18.

---

## 1. NWindow API Evolution

### 1.1 What NWindow is

The `NWindow` (native window) is libnx's abstraction of the Android IGraphicBufferProducer
binder interface that every Switch process uses to push frames to the vi compositor.
The struct definition (libnx `nx/include/switch/display/native_window.h`):

```c
typedef struct NWindow {
    u32 magic;
    Binder bq;              // IGraphicBufferProducer binder session
    Event event;
    Mutex mutex;
    u64 slots_configured;
    u64 slots_requested;
    s32 cur_slot;
    u32 width;
    u32 height;
    u32 format;
    u32 usage;
    BqRect crop;
    u32 scaling_mode;
    u32 transform;
    u32 sticky_transform;
    u32 default_width;
    u32 default_height;
    u32 swap_interval;
    bool is_connected;
    bool producer_controlled_by_app;
    bool consumer_running_behind;
} NWindow;
```

### 1.2 Core NWindow API (current, libnx 4.10.0)

```c
// Init / lifecycle
bool    nwindowIsValid(NWindow* nw);
NWindow* nwindowGetDefault(void);         // auto-initializes vi; fatal if fails
Result  nwindowCreate(NWindow* nw, Service* binder_session,
                      s32 binder_id, bool producer_controlled_by_app);
Result  nwindowCreateFromLayer(NWindow* nw, const ViLayer* layer);
void    nwindowClose(NWindow* nw);

// Configuration
Result  nwindowGetDimensions(NWindow* nw, u32* out_width, u32* out_height);
Result  nwindowSetDimensions(NWindow* nw, u32 width, u32 height);
Result  nwindowSetCrop(NWindow* nw, s32 left, s32 top, s32 right, s32 bottom);
Result  nwindowSetTransform(NWindow* nw, u32 transform);
Result  nwindowSetSwapInterval(NWindow* nw, u32 swap_interval);
bool    nwindowIsConsumerRunningBehind(NWindow* nw);  // inline accessor

// Buffer management
Result  nwindowConfigureBuffer(NWindow* nw, s32 slot, NvGraphicBuffer* buf);
Result  nwindowDequeueBuffer(NWindow* nw, s32* out_slot, NvMultiFence* out_fence);
Result  nwindowCancelBuffer(NWindow* nw, s32 slot, const NvMultiFence* fence);
Result  nwindowQueueBuffer(NWindow* nw, s32 slot, const NvMultiFence* fence);
Result  nwindowReleaseBuffers(NWindow* nw);
```

### 1.3 Key NWindow milestones from the Changelog

| libnx version | Change |
|---|---|
| 3.1.0 | `Removed bqDetachBuffer calls from nwindowReleaseBuffers` — cleanup fix, no API surface change |
| 3.2.0 | Linear framebuffer crash fix (arbitrary sizes) |
| 4.10.0 | `vi: swap close layer commands` (PR #688 by SamoZ256) — corrected `viCloseLayer` / `viDestroyManagedLayer` command ordering; **no NWindow struct changes** |

There has been **no NWindow API break** since the display system was introduced in early libnx.
The struct layout, function signatures, and vi-service binding have been stable across
all major versions. The 4.10.0 vi change is the only display-subsystem correction in
recent releases.

### 1.4 IndirectLayer functions (the most important v3.1-relevant primitives)

These exist in `vi.h` alongside the standard display API:

```c
// Read a child applet's layer into a CPU buffer (called by the PARENT process).
Result viGetIndirectLayerImageMap(
    void* buffer, size_t size,
    s32 width, s32 height,
    u64 IndirectLayerConsumerHandle,
    u64 *out_size, u64 *out_stride);

// Query required buffer size and alignment for the above.
Result viGetIndirectLayerImageRequiredMemoryInfo(
    s32 width, s32 height,
    u64 *out_size, u64 *out_alignment);
```

These are the primitives behind `LibAppletMode_BackgroundIndirect`. See Section 8 for
deep analysis of their relevance to v3.1.

### 1.5 vi service types

```c
typedef enum {
    ViServiceType_Default     = -1,
    ViServiceType_Application = 0,  // vi:u — standard homebrew/app access
    ViServiceType_System      = 1,  // vi:s — system-level; available to system applets
    ViServiceType_Manager     = 2,  // vi:m — manager; viCreateManagedLayer, viDestroyManagedLayer
} ViServiceType;
```

A NRO running under hbloader acquires `vi:u` via `nwindowGetDefault()`. uMenu (as a system
applet replacement via qlaunch slot) has `vi:s` access, which is the key to v3.1 capture.

### 1.6 ViLayerStack enum (used by capture services)

```c
typedef enum {
    ViLayerStack_Default             = 0,  // All layers
    ViLayerStack_Lcd                 = 1,  // LCD layers only
    ViLayerStack_Screenshot          = 2,  // User screenshot layers
    ViLayerStack_Recording           = 3,  // Recording layers
    ViLayerStack_LastFrame           = 4,  // Last applet-transition frame
    ViLayerStack_Arbitrary           = 5,  // Arbitrary (used by am)
    ViLayerStack_ApplicationForDebug = 6,  // Current application layers (debug/creport)
    ViLayerStack_Null                = 10, // Empty display
} ViLayerStack;
```

`ViLayerStack_ApplicationForDebug` is notable: it targets the current application's layers
specifically. This is what creport uses, not a general-purpose screenshot. `ViLayerStack_Default`
(0) captures everything, including the NRO's scanout layer.

---

## 2. libnx Framebuffer (linear) API

### 2.1 Struct

```c
typedef struct Framebuffer {
    NWindow *win;
    NvMap map;
    void* buf;            // GPU-mapped buffer base
    void* buf_linear;     // CPU-visible linear shadow (when framebufferMakeLinear called)
    u32 stride;           // Row stride in bytes
    u32 width_aligned;    // GPU-aligned width
    u32 height_aligned;   // GPU-aligned height
    u32 num_fbs;          // Buffer count (1/2/3)
    u32 fb_size;          // Per-buffer byte size
    bool has_init;
} Framebuffer;
```

### 2.2 Function signatures

```c
Result  framebufferCreate(Framebuffer* fb, NWindow *win,
                          u32 width, u32 height, u32 format, u32 num_fbs);
Result  framebufferMakeLinear(Framebuffer* fb);  // enables CPU-visible shadow buffer
void    framebufferClose(Framebuffer* fb);
void*   framebufferBegin(Framebuffer* fb, u32* out_stride);  // dequeue + return CPU ptr
void    framebufferEnd(Framebuffer* fb);  // flush + queue
```

Standard pixel formats: `PIXEL_FORMAT_RGBA_8888`, `PIXEL_FORMAT_RGBX_8888`,
`PIXEL_FORMAT_RGB_565`, `PIXEL_FORMAT_BGRA_8888`, `PIXEL_FORMAT_RGBA_4444`.

### 2.3 Typical call sequence for a non-deko3d NRO

```c
// Init path (before main loop):
nwindowSetDimensions(nwindowGetDefault(), 1280, 720);
framebufferCreate(&fb, nwindowGetDefault(), 1280, 720, PIXEL_FORMAT_RGBA_8888, 2);
framebufferMakeLinear(&fb);  // optional — makes buf_linear accessible

// Frame loop:
u32 stride;
u32 *pixels = framebufferBegin(&fb, &stride);
// ... CPU-render into pixels ...
framebufferEnd(&fb);

// Teardown:
framebufferClose(&fb);
```

The `framebufferMakeLinear` path is what simple switch homebrew (JKSV, Checkpoint, hbappstore)
uses. The alternative — used by games, RetroArch, deko3d apps — is to skip the linear
path and write directly to a GPU-tiled buffer via deko3d or nvgfx IOCTLs.

### 2.4 Who uses the linear path today

The linear framebuffer path is the **dominant pattern** across community homebrew:

- **nx-hbmenu (upstream)** — moved to deko3d in the `master` branch (`nx_graphics.c`
  uses `DkSwapchain` with `nwindowGetDefault()` as the surface). The `sdl2-renderer`
  branch uses SDL2 instead.
- **hbappstore** — uses linear framebuffer path (SDL2 wraps it).
- **JKSV, Checkpoint, Goldleaf** — use `framebufferCreate` + `framebufferMakeLinear`.
- **RetroArch** — uses its own GPU path via deko3d or nvgfx IOCTLs.
- **sphaira** — uses its own rendering path (see doc 49_v3.1_research_sphaira.md).

The critical v3.1 insight: regardless of which rendering path a NRO uses, it ultimately
calls `nwindowQueueBuffer`, which submits a GPU buffer handle to the vi compositor via
the IGraphicBufferProducer binder. The vi compositor is where capture must intercept.

---

## 3. HBABI ConfigEntry Catalogue

### 3.1 Struct layout

```c
struct LoaderConfigEntry {
    u32 Key;    // EntryType (see table below)
    u32 Flags;  // EntryFlag_IsMandatory = BIT(0)
    u64 Value[2];
};
```

The array is terminated by `EntryType_EndOfList` (Key=0). The NRO reads the array from
the pointer passed as the first argument to its entrypoint.

### 3.2 Complete EntryType catalogue

| Key | Name | Value[0] | Value[1] | Notes |
|-----|------|----------|----------|-------|
| 0 | EndOfList | ptr to notice text | sizeof notice text | Must be last entry |
| 1 | MainThreadHandle | main thread handle | 0 | Given to NRO for thread ops |
| 2 | NextLoadPath | ptr to 512-byte NRO path buf | ptr to 2048-byte argv buf | NRO writes next NRO path here to chain |
| 3 | OverrideHeap | heap base address | heap size | IsMandatory flag set |
| 4 | OverrideService | service handle | u64 name packed | Up to N overrides |
| 5 | Argv | 0 | ptr to argv string | argc not passed; argv is "nro_path [args...]" |
| 6 | SyscallAvailableHint | bitmask SVCs 0x00–0x3F | bitmask SVCs 0x40–0x7F | All-ones = all SVCs nominally available |
| 7 | AppletType | AppletType enum | flags (ApplicationOverride=BIT(0)) | Governs vi layer type in the NRO |
| 8 | AppletWorkaround | 0 | 0 | Set when AM services unavailable; NRO skips appletInitialize |
| 9 | Reserved9 | — | — | Deleted/reserved |
| 10 | ProcessHandle | own process handle | 0 | For svcMapProcessCodeMemory etc. |
| 11 | LastLoadResult | Result from previous load | 0 | Non-zero = previous NRO failed |
| 12 | AllocPages | ptr to alloc fn | ptr to free fn | Custom allocator |
| 13 | LockRegion | region base | region size | Hbloader's own memory; NRO must not overwrite |
| 14 | RandomSeed | 64-bit seed A | 64-bit seed B | Additional entropy |
| 15 | UserIdStorage | ptr to u128 uid buffer | 0 | Persistent across chain-loads |
| 16 | HosVersion | hosversionGet() packed | 0x41544d4f53504852 if AMS | 'ATMOSPHR' magic |
| 17 | SyscallAvailableHint2 | bitmask SVCs 0x80–0xBF | 0 | Extended syscall hint |

**AppletType entry detail.** The value at `entries[2]` (Key=7) determines what libnx's
`appletGetAppletType()` returns inside the NRO. This in turn controls which vi layer
the NRO's `nwindowGetDefault()` opens:
- `AppletType_LibraryApplet` → vi:u layer with library-applet Z-order
- `AppletType_SystemApplication` → vi:u layer with application Z-order and more memory

**NextLoadPath entry detail.** Value[0] and Value[1] point into hbloader's heap. The NRO
writes the path of the next NRO to load before returning to hbloader. hbloader loops on
this: unmap current NRO → load new NRO → trampoline. This is the chain-load mechanism
that hbmenu uses to launch arbitrary NROs via "select NRO → write path → return."

### 3.3 v3.1 does NOT extend HBABI

The v3.1 design uses `vi:s CaptureScreenshot` — an IPC call from uMenu against the running
hbloader process's compositor layer — not any HBABI extension. A new EntryType for
"render into this shared buffer" would require NRO cooperation and would break the
universality goal. The current 18 entry types remain frozen from v3.1's perspective.

No new EntryType will be added. No fork of nx-hbloader's ConfigEntry array is needed.

---

## 4. nx-hbmenu Actual Behavior

### 4.1 Main loop skeleton

Fetched from `github.com/switchbrew/nx-hbmenu` (master branch), file `nx_main/main.c`:

```c
#define FB_WIDTH  1280
#define FB_HEIGHT 720

int main(int argc, char **argv)
{
    // Input init
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&g_pad);
    padRepeaterInitialize(&g_pad_repeater, 20, 10);
    hidSetNpadHandheldActivationMode(HidNpadHandheldActivationMode_Single);
    touchInit();

    // System hooks
    appletLockExit();
    appletSetScreenShotPermission(AppletScreenShotPermission_Enable);

    // Services init (setsys theme, textInit, PHYSFS, assets, power, netloader, worker, status, menu, launch, font)

    if (R_SUCCEEDED(rc)) {
        graphicsInit(FB_WIDTH, FB_HEIGHT);  // creates DkSwapchain on nwindowGetDefault()
    }

    while (appletMainLoop())
    {
        padUpdate(&g_pad);
        padRepeaterUpdate(...);

        if (!uiUpdate()) break;
        g_framebuf = graphicsFrameBegin(&g_framebuf_width);  // dequeue, return CPU ptr
        memset(g_framebuf, 237, g_framebuf_width * FB_HEIGHT);
        menuLoop();       // software-render into g_framebuf
        graphicsFrameEnd();  // flush + present via deko3d
    }

    graphicsExit();
    // ... cleanup ...
    appletUnlockExit();
    return 0;
}
```

### 4.2 Graphics backend: deko3d, not raw framebuffer

Contrary to earlier assumptions, modern nx-hbmenu does NOT use `framebufferCreate`.
It uses **deko3d** (`nx_graphics.c`):

```c
void graphicsInit(u32 width, u32 height) {
    // Create DkDevice
    dkDeviceMakerDefaults(&deviceMaker);
    s_device = dkDeviceCreate(&deviceMaker);

    // Allocate GPU-cached image memory for 2 framebuffers
    DkImageFlags_UsagePresent
    DkImageFormat_RGBA8_Unorm

    // Create swapchain using nwindowGetDefault() as surface
    dkSwapchainMakerDefaults(&swapchainMaker, s_device,
                             nwindowGetDefault(), swapchainImages, FB_NUM);
    s_swapchain = dkSwapchainCreate(&swapchainMaker);

    // Create a CPU-cached linear work buffer (width*height*4 bytes)
    DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuUncached
    s_workMemBlock = dkMemBlockCreate(&memBlockMaker);

    // Record command lists: copy linear buffer → tiled framebuffer
    dkCmdBufCopyBufferToImage(s_cmdBuf, &linearSrc, &tiledDst, &copyRect, 0);
    dkCmdBufSignalFence(s_cmdBuf, &s_fence, false);
}

void graphicsFrameBegin(u32 *out_stride) {
    dkFenceWait(&s_fence, -1);       // wait for GPU done reading
    return dkMemBlockGetCpuAddr(s_workMemBlock);  // return linear CPU ptr
}

void graphicsFrameEnd(void) {
    dkMemBlockFlushCpuCache(s_workMemBlock, ...);
    int slot = dkQueueAcquireImage(s_queue, s_swapchain);
    dkQueueSubmitCommands(s_queue, s_cmdLists[slot]);  // copy linear→tiled
    dkQueuePresentImage(s_queue, s_swapchain, slot);   // vi present
}
```

The CPU writes to a linear `s_workMemBlock`. deko3d blits it to a tiled GPU framebuffer
and presents via the swapchain. From vi's perspective, this is identical to any other
GPU frame submit — the present goes through `nwindowQueueBuffer`.

### 4.3 Input handling

`menuUpdate()` in `main.c`:
- `padGetButtonsDown` polled each frame
- Supported: AnyLeft/Right/Up/Down, ZL/ZR, Y (netloader), X (edit mode), A (launch), B (back), Minus (themes), Plus (exit)
- Touch via `touchInit()` / `handleTouch(menu)`

### 4.4 Applet hooks

- `appletLockExit()` — prevents immediate exit on Home press (standard for homebrew)
- `appletSetScreenShotPermission(AppletScreenShotPermission_Enable)` — explicitly allows screenshots of its own layer. This is notable: it does NOT disable screenshots.
- NO explicit `appletGetMessage()` loop is installed in the main loop. Focus transitions are handled by the default libnx applet event system.
- `extern u32 __nx_applet_exit_mode` — used on error path. `__nx_applet_exit_mode = 1` triggers NextLoad exit instead of `exit(0)`.

### 4.5 HOS version assumptions

hbmenu checks `hosversionBefore(x,y,z)` for several conditional paths (netloader, touch).
It does not mandate any minimum HOS version. It reads firmware version via `setsysGetFirmwareVersion`.

### 4.6 Exit behavior

Normal exit: `appletMainLoop()` returns false when user presses Plus or Home press is handled.
The NRO returns 0 from `main()`. hbloader's trampoline returns control to hbloader's loop.
hbloader checks `g_nextNroPath`: if empty, it defaults back to `sdmc:/hbmenu.nro`.
There is no IPC signal from hbmenu to any parent — exit is pure process-return.

The NRO writes its next-load path to `g_nextNroPath` (the buffer hbloader pointed to in
`EntryType_NextLoadPath`) before returning. hbloader reads it, maps the new NRO, trampolines.
If the NRO leaves `g_nextNroPath` empty (or never writes it), hbloader re-loads the default
NRO (`DEFAULT_NRO = "sdmc:/hbmenu.nro"`).

---

## 5. nx-hbloader Launch Handshake

### 5.1 Upstream hbloader main()

From `github.com/switchbrew/nx-hbloader/source/main.c` (full source retrieved 2026-05-18):

```c
int main(int argc, char **argv)
{
    memcpy(g_savedTls, (u8*)armGetTls() + 0x100, 0x100);

    getIsApplication();              // checks InfoType_IsApplication; fallback: pmshell PID match
    getIsAutomaticGameplayRecording();  // [4.0.0+] checks NACP if application
    smExit();                        // Close SM — not needed after this point
    setupHbHeap();                   // svcSetHeapSize — alloc maximum available
    getOwnProcessHandle();           // thread trick to get self process handle
    getCodeMemoryCapability();       // detect svcCreateCodeMemory / svcControlCodeMemory
    loadNro();                       // load + map + trampoline; never returns

    diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 8));
}
```

### 5.2 Heap sizing

```c
static u64 calculateMaxHeapSize(void) {
    svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);

    if (mem_available > mem_used + 0x200000)
        size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;  // 2MB-aligned
    if (size == 0) size = 0x2000000 * 16;  // 512MB fallback

    if (size > 0x6000000 && g_isAutomaticGameplayRecording)
        size -= 0x6000000;  // reserve 96MB for recording

    return size;
}
```

The applet slot's memory ceiling determines how much heap hbloader can claim.
The Album/photoViewer slot budget is determined by Nintendo's NPDM for that slot.
hbloader's `g_appletHeapSize` and `g_appletHeapReservationSize` can be configured in
`/atmosphere/config/system_settings.ini` under `[hbloader]`.

### 5.3 ConfigEntry setup (complete)

```c
static ConfigEntry entries[] = {
    { EntryType_MainThreadHandle,      0, {0, 0} },
    { EntryType_ProcessHandle,         0, {0, 0} },
    { EntryType_AppletType,            0, {AppletType_LibraryApplet, 0} },
    { EntryType_OverrideHeap,          M, {0, 0} },          // IsMandatory
    { EntryType_Argv,                  0, {0, 0} },
    { EntryType_NextLoadPath,          0, {0, 0} },
    { EntryType_LastLoadResult,        0, {0, 0} },
    { EntryType_SyscallAvailableHint,  0, {UINT64_MAX, UINT64_MAX} },
    { EntryType_SyscallAvailableHint2, 0, {UINT64_MAX, 0} },
    { EntryType_RandomSeed,            0, {0, 0} },
    { EntryType_UserIdStorage,         0, {(u64)&g_userIdStorage, 0} },
    { EntryType_HosVersion,            0, {0, 0} },
    { EntryType_EndOfList,             0, {(u64)g_noticeText, sizeof(g_noticeText)} }
};
```

If running as `g_isApplication`:
```c
entries[2].Value[0] = AppletType_SystemApplication;
entries[2].Value[1] = EnvAppletFlags_ApplicationOverride;
```

### 5.4 TargetInput / TargetOutput (uLoader extension)

uLaunch's uLoader extends the upstream pattern with explicit `TargetInput`/`TargetOutput`
structs passed via applet storage (not HBABI):

- `TargetInput` (~3.5KB struct): contains `nro_path`, `nro_argv`, `target_once` flag,
  `nro_argv_argv_parent` for choose-mode, and heap-size hints. Pushed by uSystem to
  uLoader via `appletHolderPushInData` before `appletHolderStart`.
- `TargetOutput` (~512-byte struct): contains the chosen NRO path (in choose-mode).
  Pushed by uLoader to uSystem via `appletPushOutData` on exit.

This is a Q OS addition on top of vanilla hbloader. Upstream nx-hbloader has no TargetInput;
it uses `envSetNextLoad` inside the NRO's `launchFile` (see Section 4.6 / builtin.c).

### 5.5 uLoader NRO mapping sequence

From `loader_Target.cpp:178-403`:

```
fopen(g_NextTargetPath)   → fread NRO header and segments
virtmemFindCodeMemory(total_size) → svcMapProcessCodeMemory(g_procHandle, map_addr, heap_addr, total_size)
svcSetProcessMemoryPermission × 3:
    .text   → Perm_R | Perm_X
    .rodata → Perm_R
    .data+bss → Perm_Rw
memcpy(g_TargetConfigEntries, target_cfg_entries)  // build HB ABI array
svcBreak(PostLoadDll, ...)
nroEntrypointTrampoline(g_TargetConfigEntries, -1, map_addr)  // never returns
```

### 5.6 Exit signaling

uLoader detects NRO return when `nroEntrypointTrampoline` returns (the NRO's `main()` returned,
and libnx's CRT called `exit()`, which loops back to hbloader's loadNro loop). uLoader then:

1. Checks `g_TargetInput.target_once && g_TargetTimes >= 1`.
2. If true: calls `WriteTargetOutput` (pushes `TargetOutput` via `appletPushOutData`), then `exit(0)`.
3. uSystem detects exit via `appletHolderCheckFinished` — the kernel fires `StateChangedEvent`.
4. uSystem's main loop (10ms tick on `la::IsActive()`) catches it and re-launches uMenu.

There is **no custom IPC signal** — exit is purely via the kernel's applet state machine.

---

## 6. Photo Capture Applet Slot — Why This One

### 6.1 The mapping

- AppletId = `AppletId_LibraryAppletPhotoViewer` = 0x15
- Program ID = `0x010000000000100D` (`ncm::SystemAppletId::PhotoViewer`)
- Canonical name: `photoViewer` / "Album"
- Confirmed in libnx `applet.h`, Atmosphere `cfg_override.board.nintendo_nx.inc`,
  and local code at `qd_NsIconCache.hpp:Album = 0x010000000000100DULL`

### 6.2 Why photoViewer was chosen — the Atmosphere default

From Atmosphere's compiled-in default at `libstratosphere/source/cfg/cfg_override.board.nintendo_nx.inc`:

```cpp
constexpr ProgramOverrideKey DefaultAppletPhotoViewerOverrideKey = {
    .override_key = {
        .key_combination     = HidNpadButton_R,
        .override_by_default = true,   // <— override is ON by default, no key held needed
    },
    .program_id = ncm::SystemAppletId::PhotoViewer,
};
```

Atmosphere intercepts every launch of `0x010000000000100D` and redirects it to
`/atmosphere/hbl.nsp` (or the ulaunch override) **by default without any key combo**.
This is a compile-time default baked into libstratosphere.

**Why photoViewer and not another applet?** The rationale is not documented in any Atmosphere
source comment or SwitchBrew wiki. The engineering reasons, inferred from the code, are:

1. **Album is user-accessible but not safety-critical.** The Album applet opens when the
   user presses the Capture button (screenshot shortcut). It is a media viewer, not a
   system service. Hijacking it does not break firmware update, NFC, friends, or controller
   pairing flows.

2. **It is a library applet.** Library applets have a well-defined launch protocol via
   `appletCreateLibraryApplet`. System modules cannot be hijacked this way.

3. **It has a reasonable memory budget.** Nintendo's NPDM for photoViewer allocates
   enough heap for photo/video display. hbloader uses `calculateMaxHeapSize()` to grab
   everything available above a 2MB reserve, so the actual ceiling is whatever the slot's
   NPDM memory limit is. The numeric limit is **not publicly documented** in any SwitchBrew
   source, but the community consensus is approximately 448MB in library applet mode
   (the same ceiling as other library applets on current firmware).

4. **It is addressable without key combo.** `override_by_default = true` for photoViewer
   means the override fires every time that applet is launched — no R button required.
   For the `override_any_app` path, `override_by_default = false`, so R must be held.
   The photoViewer slot lets CFW always intercept to hbloader without user input.

5. **Historical accident / Rajkosto origin.** The comment in `OverrideConfigIniHandler`
   ("Taken and modified, with love, from Rajkosto's implementation") suggests this pattern
   was inherited from the ReiNX / SX OS era. photoViewer was chosen by Rajkosto; Atmosphere
   adopted it.

### 6.3 All library applet TIDs

From libnx `applet.h` AppletId enum, with program IDs from the comments:

| AppletId | Hex | Program ID | Name |
|----------|-----|------------|------|
| AppletId_None | 0x00 | — | None |
| AppletId_application | 0x01 | (varies) | Application |
| AppletId_OverlayApplet | 0x02 | `0100000000000100C` | overlayDisp |
| AppletId_SystemAppletMenu | 0x03 | `0100000000001000` | qlaunch |
| AppletId_SystemApplication | 0x04 | `0100000000001012` | starter |
| AppletId_LibraryAppletAuth | 0x0A | `0100000000001001` | auth |
| AppletId_LibraryAppletCabinet | 0x0B | `0100000000001002` | cabinet |
| AppletId_LibraryAppletController | 0x0C | `0100000000001003` | controller |
| AppletId_LibraryAppletDataErase | 0x0D | `0100000000001004` | dataErase |
| AppletId_LibraryAppletError | 0x0E | `0100000000001005` | error |
| AppletId_LibraryAppletNetConnect | 0x0F | `0100000000001006` | netConnect |
| AppletId_LibraryAppletPlayerSelect | 0x10 | `0100000000001007` | playerSelect |
| AppletId_LibraryAppletSwkbd | 0x11 | `0100000000001008` | swkbd |
| AppletId_LibraryAppletMiiEdit | 0x12 | `0100000000001009` | miiEdit |
| AppletId_LibraryAppletWeb | 0x13 | `010000000000100A` | LibAppletWeb |
| AppletId_LibraryAppletShop | 0x14 | `010000000000100B` | LibAppletShop |
| AppletId_LibraryAppletPhotoViewer | 0x15 | `010000000000100D` | **photoViewer / hbloader slot** |
| AppletId_LibraryAppletSet | 0x16 | `010000000000100E` | set (retail: absent) |
| AppletId_LibraryAppletOfflineWeb | 0x17 | `010000000000100F` | LibAppletOff |
| AppletId_LibraryAppletLoginShare | 0x18 | `0100000000001010` | LibAppletLns |
| AppletId_LibraryAppletWifiWebAuth | 0x19 | `0100000000001011` | LibAppletAuth |
| AppletId_LibraryAppletMyPage | 0x1A | `0100000000001013` | myPage |

### 6.4 Could we use a different slot for v3.1?

The slot choice is locked by Atmosphere's compiled-in default and by the Q OS override_config.ini
state. From the local comment in `qd_NintendoApps.cpp`:

> Two attempts to disarm that hijack via /atmosphere/config/override_config.ini
> both broke the AMS reboot trampoline that qd_Power.cpp::RebootToHekate depends on.

This means the Q OS SD environment treats `0x010000000000100D` as the mandatory hbloader
slot. Using a different applet slot (e.g., LibraryAppletController or LibraryAppletSwkbd)
would require:
1. Changing Atmosphere's compile-time default OR adding a `[hbl_config]` override for
   a different program_id, AND
2. Ensuring the new slot's NPDM has equal or better memory/service access.

For v3.1, the slot stays as photoViewer. The ECS/AMS redirection from photoViewer to
uLoader's binary is already wired and hardware-confirmed.

---

## 7. AppletId Enum — Windowability Classification

Using the full enum from libnx `applet.h` + `LibAppletMode`:

```c
typedef enum {
    LibAppletMode_AllForeground                = 0,  // Foreground — takes full screen
    LibAppletMode_Background                   = 1,  // Background — no display
    LibAppletMode_NoUi                         = 2,  // No UI at all
    LibAppletMode_BackgroundIndirect           = 3,  // Background with indirect layer (parent reads frames)
    LibAppletMode_AllForegroundInitiallyHidden = 4,  // Foreground, starts hidden
} LibAppletMode;
```

### 7.1 Classification by windowability

**Fullscreen-only by design (mode 0 only in practice):**

These applets present complex, fullscreen UI and are never launched in background mode
by any known first-party software:

- `LibraryAppletAuth` (pin entry)
- `LibraryAppletCabinet` (amiibo)
- `LibraryAppletDataErase` (factory reset UI)
- `LibraryAppletError` (error overlay)
- `LibraryAppletMiiEdit` (Mii editor)
- `LibraryAppletWeb` / `LibraryAppletOfflineWeb` / `LibraryAppletLoginShare` / `LibraryAppletWifiWebAuth` (web applets)
- `LibraryAppletShop` (eShop)
- `LibraryAppletMyPage` (profile/friends)
- `LibraryAppletPlayerSelect` (user selection)
- `LibraryAppletController` (controller pairing) — but confirmed usable from library applet context

**Potentially windowable in concept (BackgroundIndirect capable):**

`LibAppletMode_BackgroundIndirect` requires Atmosphere or Nintendo's IPC infrastructure to
provide an IndirectLayerConsumerHandle. In theory, any library applet launched in this mode
can be composed into a parent's display. In practice, only one known Nintendo use of
BackgroundIndirect exists (the swkbd inline keyboard panel), but the mechanism is generic.

- `LibraryAppletSwkbd` — known to support inline/background mode (`swkbdInline*` API)
- `LibraryAppletNetConnect` — theoretically could run background, but no known usage

**Overlay applets (always on top, separate display layer):**

- `AppletId_OverlayApplet` (overlayDisp, program `0x010000000000100C`) — THIS is the
  overlay compositor. It renders on top of everything. The v3.1 window manager that draws
  Q OS chrome OVER a running NRO should use overlayDisp's layer model, not a library applet.

**Not usable as applet targets:**

- `AppletId_None`, `AppletId_application`, `AppletId_SystemAppletMenu`,
  `AppletId_SystemApplication` — these are not invokable via `appletCreateLibraryApplet`.

### 7.2 The overlay vs. library applet distinction for v3.1

v3.1 uses the `vi:s CaptureScreenshot` capture approach, which means the NRO runs in
its own process (the photoViewer applet slot as usual), and uMenu reads frames via
`capsscCaptureRawImageWithTimeout` or `appletGetLastForegroundCaptureImageEx`. uMenu
does not run the NRO inside itself; it captures the NRO's scanout externally.

The display overlay chrome (the Q OS windowing border, title bar, minimize button) would
be rendered by uMenu's own vi layer on top of the captured NRO frame. This is the same
pattern as the Switch's album screenshot overlay.

---

## 8. Library Applet Pop/Popout — Viable as Frame Return Channel?

### 8.1 The mechanisms available

From libnx `applet.h`, the applet storage pipeline has three data channels:

```c
// Parent → Child:
Result appletHolderPushInData(AppletHolder *h, AppletStorage *s);
Result appletHolderPushExtraStorage(AppletHolder *h, AppletStorage *s);
// Interactive (bidirectional):
Result appletHolderPushInteractiveInData(AppletHolder *h, AppletStorage *s);
Result appletHolderPopInteractiveOutData(AppletHolder *h, AppletStorage *s);
// Child → Parent (output on exit):
Result appletHolderPopOutData(AppletHolder *h, AppletStorage *s);
```

Storage creation options:
```c
Result appletCreateStorage(AppletStorage *s, s64 size);                     // IPC-backed
Result appletCreateTransferMemoryStorage(AppletStorage *s, void* buffer,    // tmem-backed
                                         s64 size, bool writable);
Result appletCreateHandleStorage(AppletStorage *s, s64 inval, Handle handle); // handle-backed
Result appletCreateHandleStorageTmem(AppletStorage *s, void* buffer, s64 size); // tmem via handle
```

### 8.2 Could PopInteractiveOutData carry per-frame image data?

A 1280×720 RGBA8 frame = `0x384000` bytes (3.54 MB). The `appletCreateTransferMemoryStorage`
path uses `TransferMemory` (a region of shared physical memory mapped into both processes),
so there is no IPC copy overhead — the parent reads directly from the child's mapped pages.

**Theoretical feasibility:**
- Child (NRO in hbloader) calls `appletPushInteractiveOutData` with a
  `TransferMemoryStorage` pointing to its rendered framebuffer.
- Parent (uMenu) polls `appletHolderPopInteractiveOutData` each frame.
- Parent samples the shared buffer as a texture.

**Why this does NOT work for the universal VM-in-window:**

1. **The NRO has no knowledge of this channel.** The NRO calls `nwindowQueueBuffer`
   to present frames to vi. It does not know it is inside a Q OS windowed session.
   For the pop channel to work, the NRO would have to call `appletPushInteractiveOutData`
   explicitly — that is opt-in, not universal.

2. **uMenu is not alive while the NRO runs (current architecture).** Per the 49_v3.1
   hbloader research, uSystem waits for `!la::IsActive()` before launching uLoader.
   uMenu terminates before uLoader starts. There is no parent holding an `AppletHolder`
   open while the NRO renders. The interactive channel has no parent.

3. **Storage size is not documented as having a limit**, but the interactive data
   channel is designed for small control messages (swkbd sends inline keyboard events;
   album sends selection events). The `TransferMemory` variant could in principle hold
   3.54MB, but there is no system-level guarantee the IPC infrastructure handles
   multi-MB interactive payloads at 60Hz.

4. **PopOutData is one-shot (on exit only).** uLoader's `WriteTargetOutput` already
   uses this channel to return the chosen NRO path. Adding frame data here would require
   the NRO to stop rendering, encode a frame, call push, and then exit — not live video.

**Conclusion:** The applet storage pop/popout channels are NOT viable as a per-frame
capture mechanism for universal NROs. They are small-message IPC designed for control
data, not video streams. The v3.1 design correctly bypasses this entirely via vi capture.

### 8.3 The one viable use of storage channels in v3.1

The interactive channel IS appropriate for the v3.1 control plane:

- uMenu could push a small "resize viewport to X×Y" or "pause rendering" message to the
  running NRO **if** the NRO is a cooperating Q OS NRO that listens for this channel.
- For universal (non-cooperating) NROs, the control plane is irrelevant — capture happens
  externally regardless.

---

## 9. Capture Path Deep Dive — IDisplayController and caps:sc

### 9.1 IDisplayController (applet-side capture buffers)

From libnx `applet.h`, `IDisplayController` section:

```c
// Capture the current applet's own layer into a named shared buffer:
Result appletTakeScreenShotOfOwnLayer(bool flag, AppletCaptureSharedBuffer captureBuf);    // [2.0.0+]
Result appletTakeScreenShotOfOwnLayerEx(bool flag0, bool immediately,                      // [6.0.0+]
                                         AppletCaptureSharedBuffer captureBuf);

// Read named shared buffers (1280x720 RGBA8 = 0x384000 bytes):
Result appletGetLastForegroundCaptureImageEx(void* buffer, size_t size, bool *flag);
Result appletGetLastApplicationCaptureImageEx(void* buffer, size_t size, bool *flag);
Result appletGetCallerAppletCaptureImageEx(void* buffer, size_t size, bool *flag);

// Acquire/release shared buffer lock for direct access:
Result appletAcquireLastApplicationCaptureSharedBuffer(bool *flag, s32 *id);    // [4.0.0+]
Result appletReleaseLastApplicationCaptureSharedBuffer(void);
Result appletAcquireLastForegroundCaptureSharedBuffer(bool *flag, s32 *id);     // [4.0.0+]
Result appletReleaseLastForegroundCaptureSharedBuffer(void);
Result appletAcquireCallerAppletCaptureSharedBuffer(bool *flag, s32 *id);       // [4.0.0+]
Result appletReleaseCallerAppletCaptureSharedBuffer(void);

// Copy between capture buffers:
Result appletCopyBetweenCaptureBuffers(AppletCaptureSharedBuffer dst,           // [5.0.0+]
                                        AppletCaptureSharedBuffer src);
Result appletClearCaptureBuffer(bool flag,                                      // [3.0.0+]
                                 AppletCaptureSharedBuffer captureBuf, u32 color);
```

`AppletCaptureSharedBuffer` values:
- `0` = `LastApplication` — the last running application's final frame
- `1` = `LastForeground` — the last foreground applet's frame
- `2` = `CallerApplet` — the calling applet's frame

**Critical availability note:** `appletGetLastForegroundCaptureImageEx` returns a
previously captured image stored by am. It does NOT capture the current live frame.
The captured image is updated only when the applet calls `appletUpdateLastForegroundCaptureImage()`
or `appletTakeScreenShotOfOwnLayerEx`. This is suitable for **static screenshots** (launch
transitions) but not live frame streaming.

### 9.2 caps:sc — live screenshot service

From libnx `capssc.h` (full source retrieved):

```c
// [2.0.0–4.x] — STUBBED on [5.0.0+]:
Result capsscCaptureRawImageWithTimeout(
    void* buf, size_t size,         // output RGBA8 buffer, must be 0x384000 * buffer_count
    ViLayerStack layer_stack,       // which layer stack to capture
    u64 width, u64 height,          // must be 1280, 720
    s64 buffer_count, s64 buffer_index,
    s64 timeout);                   // nanoseconds; 100000000 (100ms) is typical default

// [3.0.0+] — requires debug mode:
Result capsscOpenRawScreenShotReadStream(u64 *out_size, u64 *out_width, u64 *out_height,
                                          ViLayerStack layer_stack, s64 timeout);
Result capsscCloseRawScreenShotReadStream(void);
Result capsscReadRawScreenShotReadStream(u64 *bytes_read, void* buf, size_t size, u64 offset);

// [9.0.0+] — requires debug mode before [10.0.0]:
Result capsscCaptureJpegScreenShot(u64* out_jpeg_size, void* jpeg_buf, size_t jpeg_buf_size,
                                    ViLayerStack layer_stack, s64 timeout);
```

**Key constraint:** `capsscCaptureRawImageWithTimeout` is **stubbed out on [5.0.0+]**.
Current firmware (18.x–21.x) always returns an error from this function. This is why
the 49_v3.1 hbloader research identified `vi:s CaptureScreenshot` as the target, not
caps:sc directly.

The stream API (`capsscOpenRawScreenShotReadStream`) requires debug mode, which is not
available on retail Switch without additional Atmosphère patches.

### 9.3 The confirmed v3.1 capture path: am IDisplayController

The correct path uses am's `IDisplayController` Acquire/Get functions, which are available
to system applets (AppletType_SystemApplet) without debug mode. uSystem runs as the
SystemApplet; uMenu runs as its library applet. The capture workflow is:

```
1. uSystem calls appletUpdateLastForegroundCaptureImage()
   (triggers am to snapshot the current foreground vi layer into LastForeground buffer)

2. uSystem calls appletGetLastForegroundCaptureImageEx(buf, 0x384000, &flag)
   (copies snapshot to CPU buffer)

3. uMenu reads the buffer as an SDL2 texture
```

The limitation: step 1 is not a continuous capture; it captures a single frame at the
moment it is called. For ~16ms polling (60fps) the caller must loop: call
`appletUpdateLastForegroundCaptureImage` → `appletGetLastForegroundCaptureImageEx`
once per frame. Each call is an IPC round-trip to am. The latency budget needs measurement
on hardware, but this is the path with no debug-mode requirement.

**Alternative: `appletTakeScreenShotOfOwnLayer`** — this is called FROM the applet itself,
not from the parent. The NRO would call this to push its own frame into the CallerApplet
capture buffer, which uMenu could then read. Again, not universal — requires NRO cooperation.

---

## 10. Open Questions

1. **IDisplayController update latency on hardware**: How long does a
   `appletUpdateLastForegroundCaptureImage` → `appletGetLastForegroundCaptureImageEx`
   round trip take on OG Erista at load? If >8ms, 60fps composite is not achievable.
   A live test is needed.

2. **Can uSystem call update while uLoader is the active foreground applet?** The
   applet model requires that uSystem (as the SystemApplet) is the caller. When uLoader
   is running, uSystem's main loop is spinning. Whether `appletUpdateLastForegroundCaptureImage`
   works with a library applet's layer as the "foreground" is not confirmed from source alone.

3. **uMenu must remain alive for v3.1**. The current architecture kills uMenu before
   uLoader launches. For v3.1, uMenu must stay alive to capture and composite. This requires
   changing `la::Start` to use `LibAppletMode_Background` for uLoader, and uMenu running
   concurrently as a different layer — a fundamental architecture change verified against
   `la_LibraryApplet.cpp::Create` which hardcodes `LibAppletMode_AllForeground`.

4. **LibAppletMode_BackgroundIndirect for uLoader**: If uLoader is launched with
   `LibAppletMode_BackgroundIndirect`, uMenu can obtain an `IndirectLayerConsumerHandle`
   via `appletHolderGetIndirectLayerConsumerHandle`, then call `viGetIndirectLayerImageMap`
   to read the NRO's composed layer directly — at GPU speed, without the IPC round-trip
   to am. This is the most promising live-capture primitive. The NRO would still call
   `nwindowGetDefault()` normally; the system compositor would route its output to the
   indirect layer. This requires verification that hbloader (running in the applet) can
   acquire vi:u in BackgroundIndirect mode — there is no source evidence this works or
   doesn't work.

5. **Memory budget of BackgroundIndirect vs AllForeground**: BackgroundIndirect applets
   may have a smaller memory allocation than AllForeground (the applet still renders,
   but to an offscreen buffer). If this reduces the NRO heap budget below what RetroArch
   or other memory-hungry NROs require, it would limit the set of NROs that can run
   windowed.

6. **`ViLayerStack_Default` vs `ViLayerStack_ApplicationForDebug` for capture**: Does
   `appletUpdateLastForegroundCaptureImage` capture only the foreground applet's layer
   or all compositor layers? If it captures the uMenu chrome as well as the NRO, the
   composite step must strip the border before displaying it windowed — or use a dedicated
   stack.

---

## Appendix A: libnx Changelog — Display-Related Entries

| Version | Change |
|---------|--------|
| 4.10.0 | `vi: swap close layer commands` (PR #688) — bug fix for viCloseLayer vs viDestroyManagedLayer ordering |
| 4.3.0 | `vi: Added [16.0.0+] Manager commands` |
| 3.2.0 | `vi: Added ViLayerStack enum` (added in capssc integration) |
| 3.2.0 | `capssc: Added capsscCaptureJpegScreenShot, capsscOpenRawScreenShotReadStream, Close, Read` |
| 3.2.0 | `capssc: Changed capsscCaptureRawImageWithTimeout to use ViLayerStack enum` |
| 3.1.0 | `Removed bqDetachBuffer calls from nwindowReleaseBuffers` (cleanup; no API change) |
| 3.1.0 | `Fixed nvFence/nvGpu/nvMap to use service guard` |
| 3.0.0 | Graphics display stack: NWindow API stable; major IPC system redesign unrelated to display |
| 4.0.0 | `Fixed crashes caused by arbitrary sizes in linear framebuffers` |

NWindow struct layout has **not changed** across any documented version. The core primitives
`nwindowCreate`, `nwindowCreateFromLayer`, `nwindowGetDefault`, `framebufferCreate`, and
`framebufferMakeLinear` have been API-stable since their introduction.

---

## Appendix B: nx-hbmenu Launch Path (builtin.c — legacy path)

The upstream `builtin.c` loader in hbmenu still uses `envSetNextLoad`:

```c
static void launchFile(const char* path, argData_s* args)
{
    Result rc = envSetNextLoad(path, argBuf);
    if (R_SUCCEEDED(rc)) {
        uiExitLoop();  // exits the main loop; main() returns; hbloader catches via NextLoadPath
    }
}
```

`envSetNextLoad` writes the NRO path into the `NextLoadPath` buffer that hbloader provided
in the ConfigEntry. When hbmenu's `main()` returns, hbloader's `loadNro()` loop finds the
new path and maps the requested NRO. This is the upstream chain-load mechanism.

uLoader replaces this with the `TargetInput`/`TargetOutput` IPC channel (applet storage),
which allows uSystem to control the choice from outside the applet process.

---

*Document sources: libnx `Changelog.md` fetched from github.com/switchbrew/libnx;
upstream nx-hbloader `source/main.c` fetched from github.com/switchbrew/nx-hbloader;
upstream nx-hbmenu `nx_main/main.c`, `nx_main/nx_graphics.c`, `nx_main/nx_launch.c`,
`nx_main/loaders/builtin.c` fetched from github.com/switchbrew/nx-hbmenu;
libnx `native_window.h`, `applet.h`, `capssc.h`, `vi.h` fetched via gh CLI;
Atmosphere `cfg_override.board.nintendo_nx.inc` fetched from github.com/Atmosphere-NX/Atmosphere;
SwitchBrew Homebrew_ABI wiki page; local source at
`src/projects/uSystem/source/ul/system/la/la_LibraryApplet.cpp`,
`src/projects/uMenu/source/ul/menu/qdesktop/qd_NintendoApps.cpp`,
`src/projects/uMenu/include/ul/menu/qdesktop/qd_NsIconCache.hpp`.
Fetch date: 2026-05-18.*
