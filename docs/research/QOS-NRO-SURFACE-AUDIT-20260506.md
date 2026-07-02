# Static Analysis Report: Q OS NRO/Homebrew Launch Surface

## Target
- **Path:** `/Users/nsa/QOS/tools/qos-ulaunch-fork/`
- **Type:** Creator-owned source tree (uLaunch fork)
- **Authorization:** Owned — full analysis
- **Analysis Date:** 2026-05-06

---

## 1. NRO Discovery

**Verdict: Implemented — two flat scans, no recursion**

`ScanNros()` at `qd_DesktopIcons.cpp:1932` runs two sequential `opendir` passes:

1. `sdmc:/switch/` — accepts `*.nro`, skips dotfiles (line 1941)
2. `sdmc:/` (SD root) — same filter, added in v1.6.11 for `hbmenu.nro` / `uManager.nro` (line 2019)

Both passes are flat (no `opendir` recursion into subdirectories). Filter: filename must end in `.nro`, first char must not be `.`. No depth limit needed because neither pass recurses. The `ScanPayloads()` pass at `sdmc:/bootloader/payloads/` accepts `*.bin` only and is unrelated to NRO launch.

`QdVaultLayout` (`qd_VaultLayout.cpp`) adds a third discovery path — it calls `ScanCurrentDirectory()` on any `Navigate(path)` call, accepting any `.nro` it encounters in the target directory. This is the only multi-directory scan but it is user-driven, not automatic.

**Gap for nx-hbmenu absorption:** hbmenu scans `sdmc:/switch/` recursively (one level of subdirectory). Q OS currently misses `sdmc:/switch/<subdir>/*.nro`. `ScanNros()` would need one additional `opendir` loop per top-level subdirectory.

---

## 2. Tile Launch Flow for an NRO

**Verdict: Implemented — full path traced**

```
Touch tap resolved in HandleInput()
  → qd_DesktopIcons.cpp:~3523 (touch-up detection, TAP_MAX_TICKS guard)
  → LaunchIcon(entry)             qd_DesktopIcons.cpp:4703
  → smi::LaunchHomebrewLibraryApplet(nro_path, "")   smi_Commands.hpp:58
    pushes SystemMessage::LaunchHomebrewLibraryApplet + TargetInput via AppletStorage
  → uSystem main.cpp:805  pops SMI storage, queues ActionType::LaunchHomebrewLibraryApplet
  → main.cpp:1118  dequeues action
  → ecs::RegisterLaunchAsApplet(hb_applet_takeover_program_id, 0,
        "/ulaunch/bin/uLoader/applet", &target_input, sizeof(target_input))
    ecs_ExternalContent.hpp:13
  → RegisterExternalContent()  calls ldrShellAtmosphereRegisterExternalCode (AMS IPC 65000)
  → la::Start(applet_id, 0, args, args_size)  triggers Horizon to launch the applet slot
  → uLoader (applet binary) reads TargetInput from storage, calls hbloader with nro_path
```

The action struct field is `launch_loader.target_input` (type `ul::loader::TargetInput`). The `SystemMessage` enum value is `LaunchHomebrewLibraryApplet` (smi_Protocol.hpp:71). The `TargetInput` is passed as raw bytes through AppletStorage (CommandStorageSize = 0x8000).

---

## 3. Applet vs Application Launch Context

**Verdict: Implemented — both exist, but the desktop tile always uses LibraryApplet mode**

Two SMI commands exist:

- `SystemMessage::LaunchHomebrewLibraryApplet` → `ecs::RegisterLaunchAsApplet` (main.cpp:1133)
- `SystemMessage::LaunchHomebrewApplication` → `ecs::RegisterLaunchAsApplication` (main.cpp:1156)

The Application path requires a configured `HomebrewApplicationTakeoverApplicationId` (a real installed game to donate its title slot). The desktop tile (`LaunchIcon`, line 4705) hardcodes `smi::LaunchHomebrewLibraryApplet` with no user-selectable toggle. VaultLayout (`qd_VaultLayout.cpp:1719`) also hardcodes LibraryApplet mode.

**What adding Application-mode selection requires:** A UI toggle per-NRO (context menu or long-press), a second call site invoking `smi::LaunchHomebrewApplication(nro_path, "")`, and a configured takeover app ID. The SMI plumbing already exists end-to-end; only the UI selection layer is absent.

---

## 4. Argv Passing

**Verdict: Partial — struct field exists and is wired, but callers always pass empty string**

`TargetInput` at `loader_TargetTypes.hpp:23` has `char nro_argv[NroArgvSize]` (2048 bytes). `TargetInput::Create(nro_path, nro_argv, target_once, menu_caption)` copies argv into the struct (line 35).

Both `smi::LaunchHomebrewLibraryApplet` and `smi::LaunchHomebrewApplication` in `smi_Commands.hpp:58,73` accept an `nro_argv` parameter and pass it into `TargetInput::Create`. However, every call site in `qd_DesktopIcons.cpp` and `qd_VaultLayout.cpp` passes `std::string("")` for `nro_argv`. The nxlink server (`qd_NxlinkServer.cpp:581-588`) receives cmdline args from the host but explicitly drains and discards them (comment: "We don't launch anything — just prevent broken pipe").

**What completing argv requires:** Pass the received cmdline string through `g_nxlink_scan_pending` notification (or a separate field) into the `ScanNros` / nxlink inject path so it lands in the `LaunchHomebrewLibraryApplet` call's `nro_argv` argument.

---

## 5. Nxlink Integration

**Verdict: Implemented for delivery; launch handoff is indirect via scan trigger**

`QdNxlinkServer::ReceiveOne()` at `qd_NxlinkServer.cpp:376`:

1. Receives HBmenu netloader wire protocol (namelen → filename → filelen → zlib chunks → cmdlen)
2. Decompresses with zlib; writes NRO to `sdmc:/switch/<basename>.nro` (line 432)
3. On success, sets `g_nxlink_scan_pending = true` (line 592)
4. The cmdline args block (step 7 of the protocol) is received and **discarded** (lines 581-588)

The main thread polls `g_nxlink_scan_pending` and calls `ScanNros()` + the icon-insert path at `qd_DesktopIcons.cpp:~1655`. There is no automatic launch after nxlink delivery — the NRO appears in the grid and the user must tap it. This diverges from hbmenu's behavior of immediately launching the received NRO.

---

## 6. File Browser

**Verdict: Implemented — `QdVaultLayout` is the file browser**

`qd_VaultLayout.hpp:49` documents a "Finder-style vault file browser." It supports: sidebar with six hardcoded roots, D-pad + touch navigation, `Navigate(path)` to any `sdmc:/` subdirectory, `EnterFocused()` which launches `.nro` entries via `smi::LaunchHomebrewLibraryApplet`. Context menu (ZL) exposes open/rename/delete/cut/copy/paste/new-folder. Category filter (`SetCategoryFilter`) allows pre-filtering by homebrew type.

The Vault is reached via the desktop dock slot 0 "Vault" entry (`qd_DesktopIcons.cpp:4668`). No dedicated NRO-only file picker separate from the Vault exists.

---

## 7. LaunchHomebrewLibraryApplet / LaunchHomebrewApplication Action Handlers

**Verdict: Implemented — parameterized except for takeover program ID (config-read)**

From `main.cpp:1118-1160`:

**LibraryApplet handler (1118):**
- Reads `action.launch_loader.target_input` (full `TargetInput` struct)
- Reads `HomebrewAppletTakeoverProgramId` from config under `g_ConfigLock`
- Calls `ecs::RegisterLaunchAsApplet(hb_applet_takeover_program_id, 0, "/ulaunch/bin/uLoader/applet", &target_input, sizeof(target_input))`
- libnx APIs reached: `ldrShellAtmosphereRegisterExternalCode` (IPC 65000) → `la::Start` → `appletPushInData`

**Application handler (1143):**
- Reads `action.launch_homebrew_application.app_target_input` and `.app_id`
- Queries NACP (`app::LoopQueryApplicationNacpMisc`) to set `is_auto_game_recording`
- Calls `ecs::RegisterLaunchAsApplication(app_id, "/ulaunch/bin/uLoader/application", &app_target_input, sizeof(...), g_SelectedUser)`
- libnx APIs reached: `ldrShellAtmosphereRegisterExternalCode` → `app::Start` → `nsLaunchApplication`

Hardcoded: the uLoader binary path strings (`/ulaunch/bin/uLoader/applet` and `/ulaunch/bin/uLoader/application`). Everything else — nro_path, nro_argv, target_once, is_auto_game_recording, app_id — is parameterized through `TargetInput`.

---

## 8. External Content / ecs_ExternalContent

**Verdict: Implemented — this is the AMS-bypass mechanism**

`ecs_ExternalContent.cpp:17`: `RegisterExternalContent(program_id, exefs_path)`:

1. Calls AMS private IPC command 65000 (`ldrShellAtmosphereRegisterExternalCode`) to obtain a handle
2. Opens `fsOpenSdCardFileSystem` and wraps it as an AMS `RemoteFileSystem`
3. Creates a `SubDirectoryFileSystem` rooted at `exefs_path` on SD
4. Registers that IFileSystem via `sf::RegisterSession` under the obtained handle

This causes Horizon's loader to map `exefs_path` (an SD directory containing `main` and `main.npdm`) instead of the title's installed ExeFS when `program_id` launches. The registered SD path is `/ulaunch/bin/uLoader/applet` or `…/application`. uLoader's `main` binary then reads `TargetInput` from the applet storage and calls into hbloader to actually map the NRO.

This entirely bypasses Atmosphère's HBL-launcher (`hbl.nsp`) hijack — no `/atmosphere/contents/` override is needed. The ExeFS swap happens at the loader level via AMS IPC, not via title-override layering.

---

## What Would Need to Change (Smallest-First)

| # | Change | Addresses |
|---|--------|-----------|
| 1 | Store received cmdline args in nxlink and thread them into the `nro_argv` field on the subsequent `LaunchHomebrewLibraryApplet` call | Q4 (argv — partial→full) |
| 2 | Add auto-launch after nxlink transfer instead of only setting `scan_pending` | Q5 (nxlink handoff gap) |
| 3 | Extend `ScanNros()` to iterate one level of subdirectories under `sdmc:/switch/` | Q1 (recursion depth gap) |
| 4 | Add a per-NRO UI toggle (context menu or long-press) that routes to `smi::LaunchHomebrewApplication` instead of `smi::LaunchHomebrewLibraryApplet` | Q3 (applet vs application — UI layer absent) |
| 5 | Disabling AMS HBL hijack: `RebootToStockQlaunch` already renames `exefs.nsp` ↔ `.nsp.disabled` (main.cpp:~1005). Targeting HBL specifically requires renaming `/atmosphere/contents/010000000000100D/` (hbmenu title override) or setting `override_key` in `hbl_config` to an unreachable combo. This is config-file work, not code changes to uSystem. | Q8 (AMS HBL bypass — already avoided by ECS path; stock HBL needs config disable) |
