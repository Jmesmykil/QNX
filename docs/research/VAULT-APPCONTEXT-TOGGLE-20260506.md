# Vault — "Launch as Application" context-menu toggle (2026-05-06)

hbmenu-parity for the QdVaultLayout ZL long-press menu.  When the focused
entry is an `.nro`, a new option **"[R] Launch as Application"** routes the
launch through `smi::LaunchHomebrewApplication` instead of the existing
`smi::LaunchHomebrewLibraryApplet` path used by A/Open.

## Touch points (single-file change)

- `src/projects/uMenu/include/ul/menu/qdesktop/qd_VaultLayout.hpp`
- `src/projects/uMenu/source/ul/menu/qdesktop/qd_VaultLayout.cpp`

No other files touched.  `smi_Commands.hpp`, `smi_Protocol.hpp`,
`qd_NxlinkServer.cpp`, `qd_DesktopIcons.cpp`, and uSystem are untouched.

## Menu hook (open site)

`qd_VaultLayout.cpp:1721-1727` — existing ZL handler, now also calls
`BuildVisibleContextMenuOptions()` so the visible row list is refreshed
before the first render of each open.

## New entry's predicate (`.nro` only)

`qd_VaultLayout.cpp:1336-1356` — `BuildVisibleContextMenuOptions()`:
`LaunchAsApp` is appended to `ctx_menu_visible_opts_` only when
`entries_[ctx_menu_entry_].kind == EntryKind::Nro`.  All other options are
unconditional.  Render (line 1197) and dispatch (line 1259) iterate the
visible list, so a folder/text/image entry can never reach `LaunchAsApp`.

## Call site

`qd_VaultLayout.cpp:1378-1398` — `DoLaunchAsApplication()`:
`g_MenuApplication->FadeOutToNonLibraryApplet()` → `Finalize()` →
`smi::LaunchHomebrewApplication(std::string(e.full_path), std::string(""))`.
Mirrors the fade/finalize sequence in `EnterFocused()` and the precedent in
`ui_MainMenuLayout.cpp:641-675`.

## app_id source

The actual SMI signature (`smi_Commands.hpp:73`) is path+argv only — no
`app_id` parameter.  uSystem reads
`HomebrewApplicationTakeoverApplicationId` from `cfg::Config` itself
(`uSystem/source/main.cpp:818-839`).  Nothing for uMenu to forward; the
task-prompt's "extra app_id arg" was based on a different signature than the
fork actually exposes.

## Build verification

`built ... uMenu.nso` (08:56). md5 = `4a63903f576938298358d45f9dd0eaa6`.
The trailing `Error 1` from `make umenu` is a pre-existing romfs cp step
(missing `SettingNonEditableIcon.png`/`DockControl.png`/`Selected.png`,
duplicate `sound/` dir) unrelated to this change.

## Unresolved TODOs

None for this scope.  Optional follow-up: surface
`LaunchHomebrewApplicationByDefault` config so the user can set the default
A-press behaviour on `.nro` files, matching hbmenu's persisted toggle.
