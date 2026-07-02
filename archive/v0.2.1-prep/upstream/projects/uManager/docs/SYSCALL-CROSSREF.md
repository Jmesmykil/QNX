# uManager Syscall / Service Cross-Reference

Version: Q OS Manager v0.6.1
Date: 2026-04-18
Title ID: 0x0500000051AFE003

---

## 1. uManager Service Surface

All calls enumerated from `projects/uManager/source/**/*.cpp`.

### Horizon services opened by uManager

| Call | Service | Source file | Purpose |
|------|---------|-------------|---------|
| `nsInitialize()` / `nsExit()` | `ns:am2` | main.cpp | Root gate — must succeed before any NS API call. AppScanner depends on this. |
| `nsListApplicationRecord(...)` | `ns:am2` cmd 0 | man_AppScanner.cpp | Enumerate all installed application records (up to 256 per scan). |
| `nsGetApplicationControlData(NsApplicationControlSource_Storage, ...)` | `ns:am2` cmd 400 | man_AppScanner.cpp | Fetch per-title NACP + embedded JPEG icon (~786 KiB struct on heap). |
| `smGetService(&g_PublicService, "ulsf:u")` | `ulsf:u` | sf_PublicService.cpp | Connect to uSystem's public IPC service (SMI public channel). |
| `tipcDispatchInOut(smGetServiceSessionTipc(), 65100, ...)` | `sm:` TIPC | sf_PublicService.cpp | Availability probe — cmd 65100 on the SM session checks if `ulsf:u` is registered. |
| `serviceDispatchOut(&g_PublicService, 0, ver)` | `ulsf:u` cmd 0 | sf_PublicService.cpp | GetVersion — reads the running uSystem version struct. |
| `setInitialize()` / `setExit()` | `set:` | ui_MainApplication.cpp | Language/locale lookup for UI string localisation. |
| `spsmInitialize()` / `spsmShutdown(true)` | `spsm:` | ui_MainMenuLayout.cpp | Trigger system reboot after activate/deactivate or update install. |
| `socketInitializeDefault()` / `socketExit()` | BSD socket / `nifm:` | man_Network.cpp | Network connectivity for the update downloader (curl over HTTPS). |
| `acc::ListAccounts(uids)` | `acc:u1` | ui_MainMenuLayout.cpp | Enumerate user accounts to locate per-user menu directories for reset. |
| `gethostid()` compared to `INADDR_LOOPBACK` | BSD socket | man_Network.cpp | `HasConnection()` — trivial connectivity check (implementation note: loopback check is wrong, see gap §3). |

### SMI protocol usage — uManager as a *client* of uSystem

uManager communicates with uSystem via the **Public Service** channel only (`ulsf:u`).
It never touches the Private Service (`ulsf:p`), which is reserved for uMenu.

| IPC command | Direction | Purpose |
|-------------|-----------|---------|
| SM probe cmd 65100 | Manager → SM | `IsAvailable()` — check `ulsf:u` registration before connecting |
| `ulsf:u` cmd 0 (`GetVersion`) | Manager → uSystem | Read the running uSystem `Version` struct for version-mismatch UI |

The SMI protocol magic (`0x21494D53` = "SMI!") lives in `libs/uCommon/include/ul/smi/smi_Protocol.hpp`.
uManager does **not** call any `SendCommand` / `ReceiveCommand` storage-exchange functions directly;
it only uses the lightweight `serviceDispatch` wrappers through `sf_PublicService`.

---

## 2. uMenu Service Surface (reference)

Calls enumerated from `projects/uMenu/source/**/*.cpp`.

| Call | Service | Shared with uManager? |
|------|---------|----------------------|
| `smGetService(&g_PrivateService, "ulsf:p")` | `ulsf:p` | No — uMenu holds the *private* channel |
| `tipcDispatch` / `serviceDispatch` on `ulsf:p` cmd 0,1 | `ulsf:p` | No |
| `appletPopInData` / `appletPushOutData` | Applet storage (SMI transport) | No — uMenu owns the menu→system storage pipe |
| `acc::ListAccounts`, `acc::GetAccountName`, `acc::LoadAccountImage` | `acc:u1` | Partially — uManager calls `ListAccounts` only |
| `setInitialize()` / `setExit()` | `set:` | Yes |
| `svcSleepThread(...)` | Kernel | Yes (implicit — uManager uses it via libnx internals) |
| `threadCreate` / `threadStart` / `threadWaitForExit` | Kernel | No — uMenu has the SMI receiver thread; uManager has no background thread |

### SMI commands uMenu sends to uSystem (SystemMessage enum)

These are what flow over the AppletStorage SMI pipe from uMenu to uSystem.
uManager sends **none** of these today — they represent the "gap" for v2.

```
SetSelectedUser, LaunchApplication, ResumeApplication, TerminateApplication,
LaunchHomebrewLibraryApplet, LaunchHomebrewApplication, ChooseHomebrew,
OpenWebPage, OpenAlbum, RestartMenu, ReloadConfig, UpdateMenuPaths,
UpdateMenuIndex, OpenUserPage, OpenMiiEdit, OpenAddUser, OpenNetConnect,
ListAddedApplications, ListDeletedApplications, OpenCabinet,
StartVerifyApplication, ListInVerifyApplications,
NotifyWarnedAboutOutdatedTheme, TerminateMenu, OpenControllerKeyRemapping
```

uMenu *receives* MenuMessages from uSystem:
```
HomeRequest, SdCardEjected, GameCardMountFailure, PreviousLaunchFailure,
ChosenHomebrew, FinishedSleep, ApplicationRecordsChanged,
ApplicationVerifyProgress, ApplicationVerifyResult
```

---

## 3. Gap Analysis — Missing Syscalls for a Fuller System Manager

The following syscalls and services are used by Nintendo's own system applets
(amiiboSettings, cabinet, playerSelect, swkbd, etc.) or by Atmosphère for
system-management duties, and are **absent** from uManager today.

| Missing capability | Horizon service / syscall | v2.0 relevance |
|--------------------|--------------------------|----------------|
| Application lifecycle control | `ns:am2` cmds 20 (LaunchApplication), 22 (TerminateProcess) | Allow uManager to launch/kill titles, not just list them |
| Title install / delete | `ns:ro` + `ns:am2` cmds 45,46 | Self-hosting update installs rather than extracting a zip |
| User management (create, delete, edit) | `acc:su` | Full user management panel |
| Account profile image upload | `acc:su` cmd SetImage | Avatar editor |
| Network interface status | `nifm:u` (via `nifmInitialize`) | Correct `HasConnection()` — current gethostid == INADDR_LOOPBACK is always false on Switch |
| Firmware version read | `set:sys` (`setsysGetFirmwareVersion`) | Display firmware version in the manager UI |
| Storage info | `ns:am2` `GetTotalSpaceSize` / `GetFreeSpaceSize` (StorageId) | Storage usage display |
| Notification / news | `ntc:` / `news:` | Push update notifications without polling GitHub |
| SMI pipe — sending SystemMessages | AppletStorage (`ulsf:p`) | uManager v2 as a true system applet that can reboot uMenu, reload config, etc. |
| Cabinet (Amiibo) | `nfp:user` | Amiibo management panel (matches Nintendo's amiiboSettings TID 0x0100...1008) |
| BT / wireless | `btm:` | Bluetooth device pairing panel |
| Sleep/wake control | `spsm:` (already partially present), `pm:` | Fine-grained power management |
| IR camera | `irsensor:` | Joy-Con IR configuration |

Priority for v2.0 scope: `nifm:u` (fixes the broken HasConnection), `set:sys` (firmware display), `ns:am2` lifecycle cmds (launch/terminate), and the SMI private pipe to send SystemMessages.

---

## 4. Nintendo System Applet Comparison

Reference: switchbrew.org/wiki/Applet_Manager_services, title-list

| Nintendo applet | TID | Services called that uManager lacks |
|-----------------|-----|-------------------------------------|
| amiiboSettings | 0x010000000000100B | `nfp:user`, `set:sys`, `btm:` |
| cabinet | 0x0100000000001002 | `nfp:sys`, `ns:am2` lifecycle |
| playerSelect | 0x0100000000001007 | `acc:su`, `friends:*` |
| swkbd | 0x010000000000100D | none relevant to uManager |
| myPage | 0x010000000000100F | `friends:*`, `acc:su`, `news:` |

uManager already covers the ns:am2 enumeration surface that all of the above share.
The primary delta is account mutation (`acc:su`), NFC (`nfp:`), and the private SMI pipe.
