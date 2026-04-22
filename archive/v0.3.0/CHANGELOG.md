# Q OS uLaunch Fork — v0.3.0 CHANGELOG

Released: 2026-04-18
Base: v0.2.1 (exefs.nsp SHA256 baseline: eae00c55...)
Build: BUILD-v0.2.1.sh (devkitA64/devkitPro, gnu++23, aarch64-none-elf)

## Build Output

- exefs.nsp SHA256: `0fcf62886e172655e47b0cf63fb3bdb9e0a48a498afe2c4c43744f2318ebe62b`
- uMenu main SHA256: `c7caad19556f7be1ea1dc40bbbdad798ec424ed8bc1c437dccd82fd17d1de9bc`
- Size: 587021 bytes
- Status: CLEAN BUILD — zero errors, zero warnings

## Changes

### QOS-PATCH-005: DockElement — Finder-style hover-scale magnification

**New files:**
- `include/ul/menu/ui/ui_DockElement.hpp`
- `source/ul/menu/ui/ui_DockElement.cpp`

**Modified:**
- `include/ul/menu/ui/ui_MainMenuLayout.hpp` — dock_element member + DockElement include
- `source/ul/menu/ui/ui_MainMenuLayout.cpp` — dock construction + focus_ring setup

Implements a bottom-dock strip with per-slot sigmoid magnification:
- Focused slot: 1.4x (100.8px from 72px base)
- Adjacent (±1): 1.2x
- Neighbor+2 (±2): 1.05x
- Far: 1.0x (base)

Scale interpolation runs in pixel-space via `SigmoidIncrementer<s32>` over 8 frame steps.
Icons bottom-align within the strip; total width re-centred each frame.
Translucent bg bar `rgba(0x10,0x10,0x10,0xCC)` drawn behind the strip.
Emits: `EVENT UX_DOCK_MAGNIFY focused_slot=N` via UL_LOG_INFO on each index change.

### QOS-PATCH-006: Icon grid D-pad boundary wrap

**Modified:**
- `source/ul/menu/ui/ui_EntryMenu.cpp`

Left at column 0 row R wraps to the last real column at row R (clamped to last entry).
Right at the last real column wraps back to column 0 via `SwipeRewind()` + `SwipeMode::Rewind`.
Emits: `EVENT UX_GRID_WRAP direction=left|right col=N -> col=M` via UL_LOG_INFO.

### QOS-PATCH-007: Desktop icon click — real chainload

**Modified:**
- `include/ul/menu/ui/ui_MainMenuLayout.hpp` — `HandleEntrySelection` declaration
- `source/ul/menu/ui/ui_MainMenuLayout.cpp` — `HandleEntrySelection` implementation

`HandleEntrySelection(const Entry &selected)` replaces the stub entry-launch path.
- Homebrew: delegates to existing `HandleHomebrewLaunch()` (unchanged)
- Application: full guard chain (IsSuspended, NeedsVerify, IsGamecard, HasContents,
  CanBeLaunched, IsNotUpdated), then calls `smi::LaunchApplication(app_id)`
- API gap resolved: design doc referenced `smi::sf::LaunchApplication` (does not exist);
  corrected to `smi::LaunchApplication` from `smi_Commands.hpp:21`

Emits: `EVENT UX_LAUNCH entry_type=N app_id=0xXXX|nro_path=...` via UL_LOG_INFO.

### QOS-PATCH-008: Finder-style D-pad focus state machine

**Modified:**
- `include/ul/menu/ui/ui_MainMenuLayout.hpp` — `FocusSurface` enum, `RouteDpadInput`,
  `focus_ring` Rectangle member, `current_surface`/`prev_surface`/`selected_dock_index`
- `source/ul/menu/ui/ui_MainMenuLayout.cpp` — `RouteDpadInput` + `UpdateDockHoverFromFocus`

`FocusSurface` enum (DesktopGrid, Dock, Window, OnScreenKeyboard, Overlay) controls D-pad routing.
`RouteDpadInput(u64 keys_down)` is called first in `OnMenuInput`; returns true to consume the event.
- ZR from DesktopGrid enters Dock at index 0
- ZL or Up from Dock returns to DesktopGrid
- Left/Right in Dock navigates slots with modular wrap at boundaries
- 1px cyan `focus_ring` Rectangle (rgba 0x7D,0xD3,0xFC,0xC0) positioned over focused dock slot

Emits: `EVENT UX_FOCUS_SWITCH surface=N` via UL_LOG_INFO on surface transitions.

## Deferred to v0.4.0+

- Fix 4 (QOS-PATCH-XXX): Spring-physics window drag
- Fix 6: OSK integration
- Fix 7: Notification toast system
- Fix 8: Multi-workspace switcher

## Files Not Touched

- `romfs/default/` — zero asset changes (sibling v0.2.2 agent owns this tree)
- All other source files — no collateral modifications
- Makefile — no changes needed (glob SOURCES expansion picks up ui_DockElement.cpp automatically)
