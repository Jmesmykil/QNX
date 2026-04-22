# RESEARCH — Tier 2: SMI Binary Protocol + Ryujinx HLE Service Catalog

**Date:** 2026-04-18
**Scope:** uLaunch v0.2.1-prep upstream, archive path `QOS/tools/qos-ulaunch-fork/archive/v0.2.1-prep/upstream/`
**Short-form path prefix used in citations:** `[upstream]/` = `/Users/nsa/Astral/QOS/tools/qos-ulaunch-fork/archive/v0.2.1-prep/upstream/`

---

## 1. SMI Protocol (System-Menu Interaction)

The name "DMI" does not appear in the codebase. The correct name is **SMI** (System-Menu Interaction). All headers live under `smi/`, not `dmi/`.

### 1.1 Framing and Header Layout

**Source:** `[upstream]/libs/uCommon/include/ul/smi/smi_Protocol.hpp` lines 101–107

Every SMI transaction uses a fixed 8-byte header:

```cpp
struct CommandCommonHeader {
    u32 magic;   // Must equal 0x21494D53 ("SMI!" little-endian)
    u32 val;     // On send: command enum value. On response: Result code.
};
```

- Magic: `0x21494D53` = ASCII "SMI!" (bytes: 53 4D 49 21)
- Endianness: little-endian (ARM Cortex-A57, Nintendo Switch native)
- Alignment: natural (u32 fields, no padding needed)
- Storage size limit: `CommandStorageSize = 0x8000` (32 768 bytes) per transaction — the header + all payload data must fit within this limit

**Source:** `smi_Protocol.hpp` line 107: `constexpr size_t CommandStorageSize = 0x8000;`

The `val` field is dual-purpose: it carries the `SystemMessage` (or `MenuMessage`) enum value cast to `u32` in the outbound direction, and the `Result` code in the inbound (response) direction.

### 1.2 Size Fields, Overflow Behavior

There are no explicit size fields in the header. Size tracking is entirely positional (a running `cur_offset` inside the scoped writer/reader templates).

**Source:** `smi_Protocol.hpp` lines 136–145 (ScopedStorageWriterBase::PushData):

```cpp
if((cur_offset + size) <= CommandStorageSize) {
    UL_RC_TRY(appletStorageWrite(&this->st, this->cur_offset, data, size));
    this->cur_offset += size;
    return ResultSuccess;
}
else {
    return ResultOutOfPushSpace;  // rc 0x380-0x65 (module 380, desc 101)
}
```

Reader mirrors this with `ResultOutOfPopSpace` (module 380, desc 102).

### 1.3 Physical Transport

Two separate transport paths exist — one per direction:

**uMenu → uSystem (commands):**
- uMenu calls `appletPushOutData` (libnx) to push the storage.
- uSystem reads via `la::Pop` = `appletHolderPopOutData` on its `AppletHolder` for the menu applet.
- Source: `[upstream]/projects/uMenu/source/ul/menu/smi/smi_MenuProtocol.cpp` (PopStorage = `appletPopInData`; PushStorage = `appletPushOutData`)
- Source: `[upstream]/projects/uSystem/source/ul/system/smi/smi_SystemProtocol.cpp` (PopStorage = `la::Pop`; PushStorage = `la::Push` = `appletHolderPushInData`)

**uSystem → uMenu (events/notifications):**
- NOT AppletStorage. Uses the AMS SF service `ulsf:p` (see §1.5).
- uSystem enqueues a `MenuMessageContext` into `g_MenuMessageQueue`.
- uMenu polls via `TryPopMessageContext` on the private service.

### 1.4 Send / Receive Flow

**Send (uMenu side):**
Source: `smi_Protocol.hpp` lines 209–240 (`SendCommandImpl`):

1. Open a new `AppletStorage` of `CommandStorageSize` bytes.
2. Write `CommandCommonHeader{magic=0x21494D53, val=(u32)msg_type}`.
3. Execute caller's push lambda (payload data written sequentially at offsets 8+).
4. Storage destructor calls `PushStorage` (= `appletPushOutData`) — this is implicit via RAII.
5. Block waiting for response storage with `wait=true` (calls `LoopWaitStorageFunctionImpl`).
6. Pop response header; validate `out_header.magic == CommandMagic`.
7. Propagate `out_header.val` as a `Result` — if it failed, return early.
8. Execute caller's pop lambda (read response payload if any).

**Receive (uSystem side):**
Source: `smi_Protocol.hpp` lines 242–270 (`ReceiveCommandImpl`):

1. Pop incoming storage with `wait=false` (non-blocking). Returns `ResultNoMessagesAvailable` if nothing.
2. Read `CommandCommonHeader`; validate magic.
3. Call pop lambda with `msg_type`.
4. The pop lambda's return value is stored into `in_out_header.val` (becomes the response Result).
5. Open new storage, write response header + optional response payload.
6. System pushes response via `la::Push` = `appletHolderPushInData`.

### 1.5 Channel Establishment

**Library Applet lifecycle:**
Source: `[upstream]/projects/uSystem/source/ul/system/la/la_LibraryApplet.cpp`

- uSystem creates the uMenu library applet with `appletCreateLibraryApplet(LibAppletMode_AllForeground)` — uMenu takes full foreground.
- Args pushed via `libappletArgsPush` before start.
- `la::Pop` = `appletHolderPopOutData`; `la::Push` = `appletHolderPushInData`.
- Termination: `appletHolderRequestExitOrTerminate` with 15-second timeout.

**Private SF service (ulsf:p):**
Source: `[upstream]/libs/uCommon/include/ul/smi/sf/sf_Private.hpp` line 7:
```cpp
constexpr const char PrivateServiceName[] = "ulsf:p";
```

Source: `[upstream]/projects/uSystem/include/ul/system/smi/sf/sf_IPrivateService.hpp` lines 17–21:

- Interface hash: `0xCAFEBABE`
- Cmd 0: `Initialize(ClientProcessId)` — validates the caller is actually the running menu process
- Cmd 1: `TryPopMessageContext(Out<SfMenuMessageContext>)` — non-blocking queue pop

**Security guard on Initialize:**
Source: `[upstream]/projects/uSystem/source/ul/system/smi/sf/sf_IPrivateService.cpp` lines 12–29:

```cpp
pminfoGetProgramId(&program_id, client_pid.process_id.value);
const auto last_menu_program_id = la::GetMenuProgramId();
if((last_menu_program_id == 0) || (program_id == 0) || (program_id != last_menu_program_id)) {
    return ResultInvalidProcess;
}
```

Only the currently-running menu applet's program_id is accepted. Any other process calling `ulsf:p` Initialize gets `ResultInvalidProcess`.

**Menu polling thread:**
Source: `[upstream]/projects/uMenu/source/ul/menu/smi/sf/sf_PrivateService.cpp` lines 54–77, 89:

```cpp
threadCreate(&g_ReceiverThread, &MenuMessageReceiverThread, nullptr, nullptr, 0x1000, 49, -2)
// Inside thread: svcSleepThread(10'000'000ul);  // 10 ms poll interval
```

- Thread stack: 0x1000 (4 KB)
- Priority: 49
- Core: -2 (OS picks)
- Poll interval: 10 ms

**IPC server config:**
Source: `[upstream]/projects/uSystem/include/ul/system/sf/sf_IpcManager.hpp` lines 18–40:

- PointerBufferSize: 0x800
- MaxDomains: 0x40
- MaxDomainObjects: 0x100
- MaxPrivateSessions: 1 (ulsf:p — only the menu can connect)
- MaxPublicSessions: 32 (ulss — GetVersion only)
- MaxEcsExtraSessions: 5
- MaxSessions: MaxPrivateSessions + MaxEcsExtraSessions = 6

Public service name: `ulss` (hash 0xCAFEBEEF), cmd 0 = `GetVersion`.

### 1.6 Command Enum — SystemMessage (uMenu → uSystem)

Source: `smi_Protocol.hpp` lines 56–83

| Value | Name | Push args | Pop args |
|-------|------|-----------|----------|
| 0 | Invalid | — | — |
| 1 | SetSelectedUser | AccountUid (16 bytes) | — |
| 2 | LaunchApplication | u64 app_id | — |
| 3 | ResumeApplication | — | — |
| 4 | TerminateApplication | — | — |
| 5 | LaunchHomebrewLibraryApplet | loader::TargetInput (sizeof) | — |
| 6 | LaunchHomebrewApplication | loader::TargetInput (sizeof) | — |
| 7 | ChooseHomebrew | — | — |
| 8 | OpenWebPage | char[500] url (raw 500 bytes) | — |
| 9 | OpenAlbum | — | — |
| 10 | RestartMenu | bool reload_theme_cache | — |
| 11 | ReloadConfig | — | — |
| 12 | UpdateMenuPaths | char[FS_MAX_PATH] fs_path, char[FS_MAX_PATH] menu_path | — |
| 13 | UpdateMenuIndex | u32 menu_index | — |
| 14 | OpenUserPage | — | — |
| 15 | OpenMiiEdit | — | — |
| 16 | OpenAddUser | — | — |
| 17 | OpenNetConnect | — | — |
| 18 | ListAddedApplications | u32 count | u64[count] app_ids |
| 19 | ListDeletedApplications | u32 count | u64[count] app_ids |
| 20 | OpenCabinet | u8 NfpLaStartParamTypeForAmiiboSettings | — |
| 21 | StartVerifyApplication | u64 app_id | — |
| 22 | ListInVerifyApplications | u32 count | u64[count] app_ids |
| 23 | NotifyWarnedAboutOutdatedTheme | — | — |
| 24 | TerminateMenu | — | — |
| 25 | OpenControllerKeyRemapping | u32 npad_style_set, HidNpadJoyHoldType hold_type | — |

Source for all command signatures: `[upstream]/projects/uMenu/include/ul/menu/smi/smi_Commands.hpp` lines 8–324.

Notable payload constraints:
- `OpenWebPage`: fixed 500-byte buffer pushed raw (`PushData(url, sizeof(url))` — line 104). URL must be exactly 500 bytes (zero-padded).
- `UpdateMenuPaths`: two `FS_MAX_PATH` = 0x301 byte buffers pushed sequentially (lines 154–155). Total payload = header (8) + 0x301 + 0x301 = 0x60A bytes.
- `ListAddedApplications` / `ListDeletedApplications` / `ListInVerifyApplications`: push u32 count; pop `u64 * count` from response storage. Caller must pre-allocate the output buffer.
- `LaunchHomebrewLibraryApplet` / `LaunchHomebrewApplication`: push a `loader::TargetInput` struct (serialized NRO path + argv).

### 1.7 MenuMessage Enum (uSystem → uMenu)

Source: `smi_Protocol.hpp` lines 17–28

| Value | Name | Payload in MenuMessageContext union |
|-------|------|--------------------------------------|
| 0 | Invalid | — |
| 1 | HomeRequest | — |
| 2 | SdCardEjected | — |
| 3 | GameCardMountFailure | gc_mount_failure.mount_rc (Result) |
| 4 | PreviousLaunchFailure | — |
| 5 | ChosenHomebrew | chosen_hb.nro_path (char[FS_MAX_PATH]) |
| 6 | FinishedSleep | — |
| 7 | ApplicationRecordsChanged | app_records_changed.records_added_or_deleted (bool) |
| 8 | ApplicationVerifyProgress | app_verify_progress: u64 app_id, u64 done, u64 total |
| 9 | ApplicationVerifyResult | app_verify_rc: u64 app_id, Result rc, Result detail_rc |

Source: `smi_Protocol.hpp` lines 30–54 (MenuMessageContext union struct).

**Transport:** These are enqueued into `g_MenuMessageQueue` (a `std::queue<MenuMessageContext>` under `g_MenuMessageQueueLock` RecursiveMutex) by uSystem internal code, then delivered to uMenu by the `TryPopMessageContext` SF call on `ulsf:p`.

The `SfMenuMessageContext` type (used for the SF Out buffer) inherits `MenuMessageContext` plus `ams::sf::LargeData` and `ams::sf::PrefersMapAliasTransferMode` — meaning the payload is transferred via HipcMapAlias (not pointer buffer or copy), consistent with the client implementation:
Source: `[upstream]/projects/uMenu/source/ul/menu/smi/sf/sf_PrivateService.cpp` line 18:
```cpp
.buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
```

### 1.8 SystemStatus Struct

Source: `smi_Protocol.hpp` lines 85–97

```cpp
struct SystemStatus {
    AccountUid selected_user;                    // 16 bytes
    loader::TargetInput suspended_hb_target_ipt; // set when homebrew-as-app is suspended
    u64 suspended_app_id;                        // set when a normal app is suspended
    char last_menu_fs_path[FS_MAX_PATH];          // 0x301 bytes
    char last_menu_path[FS_MAX_PATH];             // 0x301 bytes
    u32 last_menu_index;
    bool reload_theme_cache;
    bool warned_about_outdated_theme;
    u32 last_added_app_count;
    u32 last_deleted_app_count;
    u32 in_verify_app_count;
};
```

This struct is not pushed over SMI storage directly. It is part of the SF interface (`GetSystemStatus` — found in public service or passed at menu startup via the system start mode argument).

### 1.9 Error Result Codes

Source: `[upstream]/libs/uCommon/include/ul/ul_Results.rc.hpp`

Result module: **380** (`R_DEFINE_NAMESPACE_RESULT_MODULE(ulaunch, 380)`)

| Range | Name | Description |
|-------|------|-------------|
| 1 | AssertionFailed | UL_ASSERT_TRUE / UL_ASSERT_FAIL |
| 2 | InvalidTransform | Result transformation failure |
| 101 | OutOfPushSpace | Payload exceeded CommandStorageSize on write |
| 102 | OutOfPopSpace | Read offset exceeded CommandStorageSize |
| 103 | InvalidInHeaderMagic | Incoming SMI header magic != 0x21494D53 |
| 104 | InvalidOutHeaderMagic | Response SMI header magic != 0x21494D53 |
| 105 | WaitTimeout | (Smi range, reserved; LoopWaitStorageFunctionImpl) |
| 201 | InvalidProcess | ulsf:p Initialize — caller program_id mismatch or 0 |
| 202 | NoMessagesAvailable | TryPopMessageContext — queue empty |
| 203 | ApplicationCacheBusy | Application cache mid-update |
| 204 | ApplicationNotCached | Control data not in cache |
| 205 | InsufficientIconSize | Icon buffer too small |
| 301 | InvalidProcessType | Loader — wrong process type |
| 302 | InvalidTargetInputMagic | Loader — TargetInput magic mismatch |
| 303 | InvalidTargetInputSize | Loader — TargetInput size mismatch |
| 401 | ApplicationActive | SystemSmi — cannot launch; app already running |
| 402 | InvalidSelectedUser | No valid user selected |
| 403 | AlreadyQueued | Command already in flight |
| 404 | ApplicationNotActive | Resume/terminate with no running app |
| 405 | NoHomebrewTakeoverApplication | HB launch requires a takeover target |
| 406 | InvalidApplicationListCount | Count param out of range |
| 501 | InvalidJson | Config JSON parse failure |
| 601 | RomfsNotFound | Menu RomFS missing |
| 701–710 | Theme errors | Various theme ZIP / manifest validation failures |

### 1.10 Rate and Throughput

No explicit rate limiting in the SMI layer. Practical bounds:

- **uMenu→uSystem commands:** Synchronous; uMenu blocks until uSystem responds. One in-flight command at a time. The `wait=true` pop on the response side means the calling thread stalls for the full round-trip. No timeout mechanism is present in the current code (LoopWaitStorageFunctionImpl loops indefinitely until storage is available).
- **uSystem→uMenu events:** Bounded by the 10 ms polling interval. In the worst case a MenuMessage sits in the queue up to 10 ms before uMenu processes it. There is no high-watermark or backpressure on `g_MenuMessageQueue`.
- **Max payload per transaction:** 32 760 bytes (32 KB − 8 byte header). The largest actual payload observed is `UpdateMenuPaths`: 8 + 0x301 + 0x301 = 1546 bytes, well under the limit.

---

## 2. Ryujinx HLE Service Catalog (qlaunch-relevant)

All Ryujinx data gathered from the `alula/Ryujinx` fork, qlaunch branch (the only publicly accessible post-takedown mirror with qlaunch-specific service work), supplemented by switchbrew.org wiki as ground truth for command IDs and signatures.

Ryujinx was archived October 2024. The canonical mirror at `github.com/ryujinx-mirror/ryujinx` returns HTTP 451. The `alula/Ryujinx` fork at `github.com/alula/Ryujinx` (qlaunch branch) is accessible and contains the most qlaunch-relevant HLE work.

### 2.1 ns:am2 — IApplicationManagerInterface

**Switchbrew:** https://switchbrew.org/wiki/NS_services#IApplicationManagerInterface

This is the primary NS service for application management. Real Horizon implements ~100+ commands. Ryujinx HLE (alula fork, `src/Ryujinx.HOS/Services/Ns/Ns.cs` and `IApplicationManagerInterface.cs`) implements only a small subset:

| Cmd | Name | Ryujinx status | Notes |
|-----|------|----------------|-------|
| 0 | ListApplicationRecord | Implemented | Returns fake/empty list or installed titles |
| 5 | GetApplicationRecordUpdateSystemEvent | Stub | Returns dummy event handle |
| 7 | GetApplicationViewDeprecated | Stub | Returns zeroed view data |
| 21 | DeleteApplicationRecord | Stub | No-op success |
| 23 | ResolveApplicationContentPath | Implemented (partial) | Used for content path resolution |
| 26 | GetApplicationView | Implemented (partial) | Returns installed title view |
| 30 | GetApplicationViewDownloadErrorContext | Stub | — |
| 94 | LaunchApplication (6.0.0+) | NOT IMPLEMENTED | Critical gap — see §2.6 |
| 996 | GetApplicationView2 (newer) | NOT IMPLEMENTED | — |

**Gap:** Command 94 (`LaunchApplication`) is the post-6.0.0 application launch path. uSystem calls this (or `appletCreateApplication` which calls it internally via AM). If targeting Ryujinx as a test harness, applications cannot be launched via the real `ns:am2` path — only `appletCreateApplication` HLE works.

### 2.2 pm:* — Process Manager services

**Switchbrew:** https://switchbrew.org/wiki/Process_Manager_services

qlaunch-relevant pm services: `pm:info`, `pm:bm`, `pm:dmnt`.

| Service | Cmd | Name | Ryujinx status |
|---------|-----|------|----------------|
| pm:info | 0 | GetProgramId | Implemented — used by ulsf:p Initialize |
| pm:bm | 0 | LaunchProgram | Implemented |
| pm:bm | 1 | TerminateProcess | Implemented |
| pm:bm | 4 | GetProcessId | Implemented |
| pm:bm | 65001 | AtmosphereGetProcessInfo | Stub (AMS extension) |

**Critical:** `pminfoGetProgramId` (pm:info cmd 0) is used in `sf_IPrivateService.cpp` line 16 to validate the connecting menu process. Ryujinx implements this. uSystem's security check will function in emulation.

### 2.3 pgl — Program Gamecard Lifecycle service

**Switchbrew:** https://switchbrew.org/wiki/PGL_services

| Cmd | Name | Ryujinx status |
|-----|------|----------------|
| 0 | LaunchProgram | Implemented |
| 1 | TerminateProcess | Implemented |
| 4 | GetProcessId | Implemented |
| 20 | GetShellEventObserver | Stub — returns dummy IEventObserver |
| 21 | Command21 | Not implemented |

**Impact:** `GetShellEventObserver` (cmd 20) provides the application lifecycle event stream. uSystem uses this to detect when a launched application exits and update menu state (notify uMenu via MenuMessage). In Ryujinx, this is stubbed — the event never fires on application exit, meaning uMenu will not receive `ApplicationRecordsChanged` or exit notifications in emulation.

### 2.4 am — Applet Manager services

The AM service group is the most critical for qlaunch. qlaunch IS the `qlaunch` system applet (AppletId 0x02), so it gets special AM treatment.

**ICommonStateGetter (IAppletCommonFunctions #0):**

| Cmd | Name | Ryujinx status |
|-----|------|----------------|
| 0 | GetEventHandle | Implemented |
| 1 | ReceiveMessage | Implemented |
| 5 | GetOperationMode | Implemented |
| 6 | GetPerformanceMode | Implemented |
| 9 | GetCurrentFocusState | Implemented |
| 10 | RequestToAcquireSleepLock | Stub — always returns success without actually blocking sleep |
| 11 | ReleaseSleepLock | Stub |
| 12 | ReleaseSleepLockTransiently | Stub |
| 50 | IsVrModeEnabled | Stub |
| 60 | GetDefaultDisplayResolution | Implemented |
| 62 | GetDefaultDisplayResolutionChangeEvent | Stub |
| 66 | SetCpuBoostMode | Stub |
| 91 | GetCurrentPlayRecordingState | Stub |

Sleep lock (cmds 10–12) being stubbed means Q OS NSP cannot prevent sleep mid-launch sequence in emulation, but this does not affect real hardware behavior.

**ISelfController:**

| Cmd | Name | Ryujinx status |
|-----|------|----------------|
| 0 | Exit | Stub — exit via OS mechanism instead |
| 1 | LockExit | Stub |
| 2 | UnlockExit | Stub |
| 11 | SetOperationModeChangedNotification | Implemented |
| 12 | SetPerformanceModeChangedNotification | Implemented |
| 13 | SetFocusHandlingMode | Implemented |
| 14 | SetRestartMessageEnabled | Stub |
| 40 | CreateManagedDisplayLayer | Implemented |
| 41 | IsSystemBufferSharingEnabled | Stub |
| 42 | GetSystemSharedLayerHandle | Stub |
| 43 | GetSystemSharedBufferHandle | Stub |
| 44 | CreateManagedDisplaySeparableLayer | Implemented (6.0.0+) |
| 50 | SetHandlesRequestToDisplay | Stub |
| 62 | SetIdleTimeDetectionExtension | Stub |
| 63 | GetIdleTimeDetectionExtension | Stub |

**IApplicationFunctions (AppletOE path, used by applications launched by qlaunch):**

| Cmd | Name | Ryujinx status |
|-----|------|----------------|
| 1 | PopLaunchParameter | Implemented — delivers pre-selected user arg |
| 10 | CreateApplicationAndRequestToStart | Stub |
| 11 | CreateApplicationAndRequestToStartForQuest | Stub |
| 20 | EnsureSaveData | Implemented (partial) |
| 21 | GetDesiredLanguage | Implemented |
| 22 | SetTerminateResult | Stub |
| 23 | GetDisplayVersion | Implemented |
| 26 | ReceiveApplicationInviteFromFriend | Stub |
| 27 | GetApplicationLaunchInfo | Stub |
| 30 | EnableApplicationCrashReport | Stub |
| 31 | IsApplicationCrashReportEnabled | Stub |
| 32 | EnableApplicationAllThreadDumpOnCrash | Stub |
| 34 | IsGamePlayRecordingSupported | Stub |
| 35 | InitializeGamePlayRecording | Stub |
| 36 | SetGamePlayRecordingState | Stub |
| 40 | RequestToShutdown | Stub |
| 50 | NotifyRunning | Implemented |
| 60 | GetPseudoDeviceId | Stub |
| 65 | InitializeIronMushroom | Stub |
| 66 | SetMediaPlaybackStateForApplication | Stub |
| 68 | IsMultiPlayerConnected | Stub |
| 100 | CreateGameMovieTrimmer | Not implemented |
| 101 | ReserveResourceForMovieOperation | Not implemented |
| 102 | UnreserveResourceForMovieOperation | Not implemented |
| 110 | GetGpuErrorDetectedSystemEvent | Stub |
| 120 | GetFriendInvitationStorageChannelEvent | Stub |
| 121 | TryPopFromFriendInvitationStorageChannel | Stub |
| 130 | GetNotificationStorageChannelEvent | Stub |
| 131 | TryPopFromNotificationStorageChannel | Stub |
| 140 | GetHealthWarningDisappearedSystemEvent | Stub |
| 150 | SetHdcpAuthenticationActivated | Stub |

**Home button blocking (cmds 30–33 of ICommonStateGetter or related):**
The Nintendo-specific home button blocking mechanism (DisableHomeButtonShortPressedEvent, etc.) is fully stubbed in Ryujinx. uLaunch's menu relies on being able to consume the home button press itself (as qlaunch). In emulation this path is bypassed — Ryujinx delivers AppletMessage_HomeButtonPressed via the normal ReceiveMessage queue instead, which IS implemented.

### 2.5 Other System Services

**acc:u0 (Account):**
| Cmd | Name | Ryujinx status |
|-----|------|----------------|
| 0 | GetUserCount | Implemented |
| 1 | GetUserExistence | Implemented |
| 2 | ListAllUsers | Implemented |
| 3 | ListOpenUsers | Implemented |
| 4 | GetLastOpenedUser | Implemented |
| 5 | GetProfile | Implemented |
| 50 | IsUserRegistrationRequestPermitted | Stub |
| 100 | TrySelectUserWithoutInteraction | Stub |

**hid (HID):**
Most input commands implemented. `GetNpadHandheldActivationMode`, `SetSupportedNpadIdType`, `SetSupportedNpadStyleSet`, `GetNpadStyleSet`, `GetNpadState`, all implemented or near-complete.

**set:sys (Settings System):**
Source: switchbrew.org/wiki/Settings_services
| Cmd | Name | Ryujinx status |
|-----|------|----------------|
| 3 | GetFirmwareVersion | Implemented |
| 4 | GetFirmwareVersion2 | Implemented |
| 23 | GetColorSetId | Implemented |
| 24 | SetColorSetId | Stub |
| 37 | GetSettingsItemValue | Implemented (returns defaults) |
| 60 | GetInitialSystemAppletProgramId | Implemented — returns 0x0100000000001000 |
| 61 | GetOverlayDispProgramId | Implemented — returns 0x010000000000100C |

**nifm:u (Network Interface):**
Core commands (GetClientId, CreateScanRequest, GetCurrentNetworkProfile) implemented. IsAnyInternetRequestAccepted and GetInternetConnectionStatus implemented. Network event handles implemented.

**friends:a / friends:u:**
Most viewer commands stubbed. OpenFriendList (library applet launch path) not applicable — uSystem handles this by launching the MyPage library applet directly.

**mii:u / mii:e:**
GetCount, Get, BuildImage all implemented. Used for profile display in menu.

**ncm (Content Meta):**
IContentMetaDatabase (List, Has, Get commands) partially implemented. ILocationResolver path resolution implemented. Critical for application enumeration.

**lr (Location Resolver):**
ResolveProgramPath, RedirectProgramPath implemented. Used by ncm pipeline for content path building.

### 2.6 Gap Analysis: No Open-Source Horizon Implementation

Services / commands where Ryujinx HLE is the **only** open-source documentation of the real Horizon API contract:

1. **ns:am2 cmd 94 (LaunchApplication, 6.0.0+)** — Not in any other open-source Horizon implementation. Ryujinx does NOT implement it either, making this a complete documentation gap. The pre-6.0.0 path (cmd 0 of pm:bm) is documented. uSystem on real hardware calls `appletCreateApplication` which goes through AM HLE; in emulation this works via Ryujinx's `IAllSystemAppletProxiesService`.

2. **pgl GetShellEventObserver (cmd 20) + IEventObserver event protocol** — The IEventObserver interface and its event encoding for application lifecycle events is undocumented outside Ryujinx stub code and switchbrew. Since Ryujinx stubs this, the exact event packet format is only reverse-engineered from Atmosphère's `pgl` mitm code.

3. **ILibraryAppletSelfAccessor (used by library applets)** — Commands for library applets to communicate back to their creator (e.g., `ExitProcessAndReturn`). Partially documented on switchbrew; only alula/Ryujinx implements stubs relevant to qlaunch's menu applet pattern.

4. **am IHomeMenuFunctions (AppletId 0x02 specific)** — Home menu exclusive AM commands. Switchbrew lists them but notes they are not called from outside qlaunch itself. Ryujinx stubs most. These include home button claim/release and the SuspendToMenu flow.

5. **ns:ec (E-Commerce) / ns:rid (Rights)** — Used for DLC / add-on listing. Ryujinx stubs ns:ec cmd 0 (ListAoc). No open-source Horizon implementation; switchbrew has partial documentation.

---

## 3. Implications for Q OS v1.0.0 NSP

### 3.1 SMI Protocol — Keep vs Replace

**Recommendation: Keep the binary AppletStorage transport; replace the polling SF service.**

The AppletStorage-based command channel (uMenu → uSystem direction) is correct and battle-tested. The header is 8 bytes, the framing is dead simple, and the 32 KB limit is never approached in practice. No reason to replace this.

The `ulsf:p` private SF service polling model (10 ms interval, 4 KB thread stack) has two problems for Q OS v1.0.0:

1. It requires Atmosphère SF service registration (`smRegisterService`), which means uSystem must be running as a privileged sysmodule under Atmosphère. This is correct for the real target but adds complexity to any testing harness that tries to run uSystem standalone.
2. The 10 ms poll creates up to 10 ms latency on every home button press, SdCard eject, and sleep/wake event. For a v1.0.0 "it works" milestone this is acceptable, but for snappy UX it should eventually become event-driven.

**Q OS v1.0.0 action:** Keep SMI as-is. Do not rewrite the transport. The only adaptation needed is ensuring the Q OS NSP program_id is registered with uSystem so `pminfoGetProgramId` returns the right value (the menu program_id check in `sf_IPrivateService.cpp` line 22 must pass).

### 3.2 Ryujinx HLE Gaps — Impact on v1.0.0 NSP

**Finding 1 — ns:am2 cmd 94 is a dead end for Ryujinx testing.**
`LaunchApplication` (cmd 94) is not implemented in any accessible Ryujinx build. However, uSystem uses `appletCreateApplication` (libnx), which routes through `IAllSystemAppletProxiesService` → `IApplicationCreator::CreateApplication` in Ryujinx HLE — not through ns:am2 cmd 94 directly. This path IS implemented. This is not a blocker; it is a false alarm. Do not implement a ns:am2 cmd 94 stub.

**Finding 2 — pgl IEventObserver is the real application-exit gap.**
When a launched application exits in Ryujinx, the `GetShellEventObserver` event never fires. uSystem detects application exit by waiting on `AppletApplication::StateChangedEvent` (line 73 in `app_Application.cpp`). Ryujinx DOES implement `AppletApplication::StateChangedEvent` signaling on application termination. This means uSystem's exit detection WORKS in emulation via the `appletApplicationCheckFinished` path, NOT via pgl. The pgl gap is real but bypassed in uSystem's architecture. Not a blocker for v1.0.0.

**Finding 3 — Home button handling works differently in Ryujinx; test on real hardware.**
The home button block/release flow (ISelfController cmds 1–2, ICommonStateGetter home button events) is stubbed in Ryujinx. On real hardware, qlaunch/uLaunch must consume the home button signal to prevent Horizon from sending the menu back to itself. In Ryujinx, AppletMessage_HomeButtonPressed is delivered via the normal ReceiveMessage queue, which bypasses the blocking mechanism entirely. This means any home button handling logic that works in Ryujinx is NOT verified to work on real hardware. The Q OS NSP MUST be tested on Switch hardware for any home button path — Ryujinx results are invalid for this feature area.

### 3.3 Proposed Simplified IPC Surface for v1.0.0 NSP

Given the above analysis, the minimal IPC surface Q OS v1.0.0 NSP (as a uMenu replacement) must handle:

**Services Q OS NSP will call directly (cannot stub):**
- `ulsf:p` — Initialize + TryPopMessageContext polling (mandatory; receives all uSystem→menu events)
- `appletPopInData` / `appletPushOutData` — SMI command transport (mandatory)
- `acc:u0` — GetUserCount, ListAllUsers, GetProfile (user picker display)
- `set:sys` — GetColorSetId, GetFirmwareVersion (theme + version display)
- `hid` — NpadGetState, touch input (all menu navigation)
- `nifm:u` — IsAnyInternetRequestAccepted (network indicator)

**Services Q OS NSP delegates to uSystem (does NOT call directly):**
- `pm:info` — Called by uSystem, not menu
- `pgl` — Called by uSystem
- `ns:am2` — Called by uSystem (LaunchApplication etc.)
- `am IApplicationFunctions` — Called by launched applications, not menu
- `ncm / lr` — Called by uSystem's cache layer

**Services Q OS NSP can stub to no-ops for v1.0.0:**
- `friends:*` — No friends integration in v1.0.0
- `mii:u` — Show placeholder avatar instead
- `nfc / nfp` — No amiibo in v1.0.0
- `ns:ec` — No DLC listing in v1.0.0

**SMI commands Q OS NSP must implement handlers for (from uSystem perspective — what uSystem expects the menu to invoke):**
The full 25-command `SystemMessage` table is the protocol contract. For v1.0.0 a minimum viable set is: SetSelectedUser, LaunchApplication, ResumeApplication, TerminateApplication, LaunchHomebrewLibraryApplet, RestartMenu, ReloadConfig, UpdateMenuPaths, UpdateMenuIndex. The UI-only commands (OpenWebPage, OpenAlbum, OpenMiiEdit, OpenAddUser, OpenNetConnect, OpenUserPage, OpenCabinet, OpenControllerKeyRemapping) can be stubs that return ResultSuccess without launching anything.

---

## Source File Index

All citations reference files under:
`/Users/nsa/Astral/QOS/tools/qos-ulaunch-fork/archive/v0.2.1-prep/upstream/`

| Short path | Role |
|------------|------|
| `libs/uCommon/include/ul/smi/smi_Protocol.hpp` | Master SMI protocol: header struct, enums, transport templates |
| `libs/uCommon/include/ul/ul_Results.rc.hpp` | All result codes, module 380 |
| `libs/uCommon/include/ul/smi/sf/sf_Private.hpp` | ulsf:p service name constant |
| `projects/uMenu/include/ul/menu/smi/smi_Commands.hpp` | All 24 SystemMessage command wrappers with push/pop signatures |
| `projects/uMenu/include/ul/menu/smi/smi_MenuProtocol.hpp` | Menu-side SendCommand wrapper |
| `projects/uMenu/source/ul/menu/smi/sf/sf_PrivateService.cpp` | Menu polling thread, 10 ms interval, smGetService connect |
| `projects/uSystem/include/ul/system/smi/smi_SystemProtocol.hpp` | System-side ReceiveCommand wrapper |
| `projects/uSystem/include/ul/system/smi/sf/sf_IPrivateService.hpp` | IPrivateService AMS SF interface, 0xCAFEBABE hash |
| `projects/uSystem/source/ul/system/smi/sf/sf_IPrivateService.cpp` | pminfoGetProgramId security check, queue pop |
| `projects/uSystem/include/ul/system/sf/sf_IpcManager.hpp` | Server options, session limits |
| `projects/uSystem/source/ul/system/la/la_LibraryApplet.cpp` | Library applet lifecycle, la::Pop / la::Push |
| `projects/uSystem/source/ul/system/app/app_Application.cpp` | Application launch/terminate flow, fixed available_size=0x4000 |

External sources: switchbrew.org wiki (NS services, PM services, Applet Manager services, Settings services, Process Manager services); alula/Ryujinx qlaunch branch (github.com/alula/Ryujinx, `src/Ryujinx.HOS/Services/`).
