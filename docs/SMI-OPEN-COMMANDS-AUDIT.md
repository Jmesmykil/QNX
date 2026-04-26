# SMI Open-Command Capability Audit

**Date:** 2026-04-25  
**Source tree:** `src/libs/uCommon/include/ul/smi/smi_Protocol.hpp`  
**Scope:** All `SystemMessage` enum values that open a native Switch applet or launch homebrew — the "open" surface the uMenu UI layer can reach through uSystem.

---

## Capability Matrix

| Command | `SystemMessage` enum ordinal | uMenu wrapper (`smi_Commands.hpp`) | uMenu call site(s) | uSystem dispatcher (`main.cpp` line) | uSystem action executor (`main.cpp` line) | Native dispatch (`la::`) | Status |
|---|---|---|---|---|---|---|---|
| `LaunchHomebrewApplication` | 6 | `smi::LaunchHomebrewApplication(nro_path, nro_argv)` — pushes `loader::TargetInput` | `ui_MainMenuLayout.cpp:1617,1634` | L818 | L1099 | `appletCreateLibraryApplet` (direct) | **ACTIVE** — requires `HomebrewApplicationTakeoverApplicationId` config entry to be non-`InvalidApplicationId`; returns `ResultNoHomebrewTakeoverApplication` otherwise |
| `UpdateMenuIndex` | 13 | `smi::UpdateMenuIndex(u32 menu_index)` — pushes `u32` | `ui_Common.cpp:316` (init restore), `ui_MainMenuLayout.cpp:535` (focused entry change) | L904 | stored to `g_CurrentMenuIndex` (no action queue) | N/A | **ACTIVE** — wired at two sites |
| `OpenWebPage` | 9 | `smi::OpenWebPage(const char(&url)[500])` — pushes 500-byte buffer | `ui_Common.cpp:426` | L857 | L1121 | `la::OpenWeb` | **ACTIVE** |
| `OpenAlbum` | 10 | `smi::OpenAlbum()` — no payload | `ui_Common.cpp:435` | L872 | L1131 | `la::OpenPhotoViewerAllAlbumFilesForHomeMenu` | **ACTIVE** |
| `OpenUserPage` | 15 | `smi::OpenUserPage()` — no payload | `ui_Common.cpp:363` | L911 | L1150 | `la::OpenMyPageMyProfile(g_SelectedUser)` | **ACTIVE** |
| `OpenMiiEdit` | 16 | `smi::OpenMiiEdit()` — no payload | `ui_Common.cpp:441` | L917 | L1160 | `la::OpenMiiEdit` | **ACTIVE** |
| `OpenAddUser` | 17 | `smi::OpenAddUser()` — no payload | `ui_Common.cpp` (not found in this audit pass) | L923 | L1170 | `la::OpenPlayerSelectUserCreator` | **ACTIVE** — wrapper present; no explicit UI call site found in this audit; may be triggered from a dialog path |
| `OpenNetConnect` | 18 | `smi::OpenNetConnect()` — no payload | `ui_Common.cpp:447` | L929 | L1181 | `la::OpenNetConnect` | **ACTIVE** |
| `OpenCabinet` | 21 | `smi::OpenCabinet(NfpLaStartParamTypeForAmiiboSettings)` — pushes `u8` | `ui_Common.cpp:458` | L943 | L1192 | `la::OpenCabinet(type)` | **ACTIVE** |
| `OpenControllerKeyRemapping` | 25 | `smi::OpenControllerKeyRemapping(u32 style_set, HidNpadJoyHoldType hold_type)` — pushes `u32` + `HidNpadJoyHoldType` | `ui_Common.cpp:404` | L978 | L1209 | `la::OpenControllerKeyRemappingForSystem(npad_style_set, hold_type)` | **ACTIVE** |

---

## Notes

### LaunchHomebrewApplication runtime gate

uSystem `main.cpp` L832–838 checks:

1. An application is not already active (`app::IsActive()` must be false).
2. A valid user is selected (`accountUidIsValid(&g_SelectedUser)`).
3. `HomebrewApplicationTakeoverApplicationId` config entry is set to a non-`InvalidApplicationId` value.

If condition 3 fails, uSystem returns `ResultNoHomebrewTakeoverApplication` immediately (no action queued). This is intentional — the takeover slot must be configured before the command is usable. The code path is fully implemented on both the uMenu and uSystem sides.

### UpdateMenuIndex flow

`UpdateMenuIndex` does NOT go through the `g_ActionQueue`. uSystem stores the received index directly into `g_CurrentMenuIndex` at L904–910 and returns success. The two uMenu call sites are:

- `ui_Common.cpp:316` — on init, restores the last saved index from `system_status.last_menu_index`
- `ui_MainMenuLayout.cpp:535` — on focused entry change, calls `g_GlobalSettings.UpdateMenuIndex(entry_menu->GetFocusedEntryIndex())`

### Dormant SMI commands (not in scope of this audit)

The following `SystemMessage` values are present in the enum but are NOT "open" applet commands and are excluded from this matrix:

`SetSelectedUser`, `LaunchApplication`, `ResumeApplication`, `TerminateApplication`, `LaunchHomebrewLibraryApplet`, `ChooseHomebrew`, `RestartMenu`, `ReloadConfig`, `UpdateMenuPaths`, `ListAddedApplications`, `ListDeletedApplications`, `StartVerifyApplication`, `ListInVerifyApplications`, `NotifyWarnedAboutOutdatedTheme`, `TerminateMenu`

These handle lifecycle, config, and data-sync — not native applet opens.

---

## Icon Grid Status

Icons NOT yet added to the grid. Per task spec (Task 5): "Document 8 native applet SMI commands — do NOT yet add icons to grid."
