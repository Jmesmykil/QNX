# ScanNros one-level recursion (2026-05-06)

## Goal

Absorb nx-hbmenu's scan surface: NROs nested one level under
`sdmc:/switch/<AppDir>/<app>.nro` (RetroArch, AppstoreNX, etc.) must show
up as launchable tiles. Pre-change, only the flat scan of
`sdmc:/switch/` and `sdmc:/` produced tiles.

## File / lines modified

- File: `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp`
- Inserted new third pass inside `ScanNros()` after the existing
  `sdmc:/` flat block (right before the closing `}`), original
  closing brace at line 2087 in the pre-edit file.
- Added range: a single contiguous block of ~125 lines appended
  inside `ScanNros()`, immediately after `closedir(d_root);`.
- Untouched: `LoadNroIconToCache`, `LaunchIcon`, dock layout,
  `OpenNintendoAppsWindow`, every other function in the file.

## Inner opendir pattern

1. `opendir("sdmc:/switch/")` (re-open: clean iterator, no fd reuse).
2. For each `dirent`, skip leading `.`, build `sdmc:/switch/<sub>` and
   `stat()` it (d_type unreliable on FAT).
3. `S_ISDIR` gate; non-dirs are skipped silently.
4. `opendir(sub_path)` — on failure, `UL_LOG_INFO` is emitted exactly
   once (via `logged_subdir_failure` flag) and the subdir is skipped.
5. Inner loop reads `*.nro`, builds full
   `sdmc:/switch/<sub>/<file>.nro` (snprintf into 768-byte buffer
   with truncation guard), reuses existing `Classify`,
   `ClassifyNroAutoFolder`, `RegisterClassification` paths so behaviour
   downstream is identical to the flat scan.
6. Caps: `MAX_SUBDIR_SCAN = 50` subdirs visited; existing
   `icon_count_ >= MAX_ICONS` continues to gate writes at every level.
7. `closedir` is called on both inner and outer DIR handles.

## Duplicate handling

No dedup. Per spec: `sdmc:/switch/foo.nro` and
`sdmc:/switch/Bar/foo.nro` are different files at different paths and
both appear as separate tiles. `g_entry_classification_` is keyed on
the full `nro_path`, so the side table stays consistent.

## Build

```
cd /Users/nsa/QOS/tools/qos-ulaunch-fork/src && make umenu
```

- Result: success (RomFS image rebuilt, no warnings/errors).
- `uMenu.nso` md5: `1de81a0376a70d40b227e728f9c36d2a`
- Size: 7,092,462 bytes
- Path: `src/projects/uMenu/uMenu.nso`
- Not deployed.
