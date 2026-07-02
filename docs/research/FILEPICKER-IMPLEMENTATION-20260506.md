# qd_FilePicker — Implementation Report

**Date:** 2026-05-06
**Branch:** `qdesktop-filepicker-20260506`
**Build md5 (uMenu.nso):** `ea2c253c67eda639339a0f77e3632721`

## Files created

```
src/projects/uMenu/include/ul/menu/qdesktop/qd_FilePicker.hpp
src/projects/uMenu/include/ul/menu/qdesktop/qd_FilePickerLayout.hpp
src/projects/uMenu/source/ul/menu/qdesktop/qd_FilePicker.cpp
src/projects/uMenu/source/ul/menu/qdesktop/qd_FilePickerLayout.cpp
```

## Files edited (minimal — temporary 9th-tile hook only)

```
src/projects/uMenu/include/ul/menu/qdesktop/qd_NintendoAppsLayout.hpp
src/projects/uMenu/source/ul/menu/qdesktop/qd_NintendoAppsLayout.cpp
src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons_WmBridge.cpp
```

`qd_NintendoApps.{hpp,cpp}` were intentionally NOT touched — that file is in
the parallel agent's diff (Album/Mii/Profile/Web 3-step SMI rewrite). The
9th tile is wired through `QdNintendoAppsLayout::SetFilesTileCallback()`
plus a `nine_tile_mode` shrink (TILE_H 150 → 100, BODY_TOP 56 → 32) so the
tile stays inside the 480 px natural canvas. Native 4×2 layout is
unchanged when the callback is empty.

## Public API

```cpp
// qd_FilePickerLayout.hpp
using FilePickerSelectCallback = std::function<void(const std::string&)>;
using FilePickerCancelCallback = std::function<void()>;

QdFilePickerLayout::Ref
QdFilePickerLayout::New(const QdTheme &theme,
                        const char *start_path,           // "" → "sdmc:/switch/"
                        FilePickerSelectCallback on_select,
                        FilePickerCancelCallback on_cancel = {});

// qd_FilePicker.hpp
void OpenFilePickerWindow(QdWindowManager &wm,
                          const QdTheme &theme,
                          const char *start_path,
                          FilePickerSelectCallback on_select,
                          FilePickerCancelCallback on_cancel = {});
```

`on_select` fires once with the full `sdmc:/`-prefixed path of the file the
user tapped. The picker auto-closes itself after the callback returns
(via `QdWindow::on_close_requested`); the integration agent does NOT need
to call `Close()` from inside `on_select`.

## Behaviour summary

- 780×480 natural canvas; matches `DEFAULT_WIN_W/H`.
- Header bar: breadcrumb `cwd_` + `[Up]` button (greyed at root).
- Scrollable list: 13 visible rows × 28 px stride; touch flick scroll
  with 8-px drag-vs-tap threshold; right-edge thumb scrollbar.
- Toolbar: `Cancel` button + `Show: NRO`/`Show: All` toggle + entry-count
  hint.
- Filter: `.nro`-only by default; toggle exposes every file.
- Per-cwd LRU cache (8 entries). Re-opening recently-visited dirs is free.
- Errors (`opendir` failure) surface as a single non-clickable red row.
- 512-entry per-directory cap (logged, then truncated).
- `[..]` synthetic row when not at `sdmc:/` root.

## Manual test path

1. Boot uMenu.
2. Tap the **Nintendo Apps** dock icon.
3. The grid now shows 9 tiles; the 9th (teal) is **Files**.
4. Tap **Files** → file picker opens at `sdmc:/switch/`.
5. Tap a directory to descend, `[..]` to go up, `[Up]` button likewise,
   `Show: All` to toggle the filter.
6. Tap an `.nro` row → log line `filepicker: user selected '<path>'`
   appears in `log_uMenu.log`, picker closes.

## Open questions for the integration agent

1. **Launch wiring.** The picker only emits the path; the integration
   agent owns `smi::LaunchHomebrewLibraryApplet` invocation (and the
   `FadeOutToNonLibraryApplet` + `Finalize` 3-step the parallel agent is
   establishing for other Nintendo applets). Suggested location: replace
   the temporary log-only callback in
   `qd_DesktopIcons_WmBridge.cpp::OpenNintendoAppsWindow` (the lambda
   that currently logs `"user selected"`).
2. **argv/argc.** `smi::LaunchHomebrewLibraryApplet(nro_path, argv)` —
   the file picker passes only `nro_path`. argv is the integration
   agent's responsibility (probably `nro_path` itself for argv[0] per
   hbloader convention).
3. **Permanent entry point.** The 9th-tile hook is explicitly temporary.
   Once a permanent entry exists (dock icon, hot corner, Vault "Open
   external NRO…" context-menu option), remove the
   `SetFilesTileCallback()` call in `OpenNintendoAppsWindow` to revert
   the grid to native 4×2.
4. **Minimisable picker?** Currently the picker has no
   `on_minimize_begin_` wire-up — minimising would lose `cwd_`. If
   creator wants minimise support, store cwd snapshot in the closure
   passed to `wm_.SetPendingReopen`.
5. **D-pad / button input.** Touch is fully wired; D-pad scrolling and
   `A`-to-activate are not yet implemented. Integration agent may want
   to add `keys_down`-driven navigation if non-touch input is required
   for accessibility.
