# nx-hbmenu Feature Inventory (clean-room, public-OSS)

**Target:** github.com/devkitPro/nx-hbmenu master, ISC.
**Goal:** Absorb every meaningful hbmenu feature into Q OS qdesktop so we can delete `hbl.nsp`, `hbmenu.nro`, `override_config.ini`.
**Method:** Read upstream source; map to Q OS files. No verbatim copy.

Legend: have / partial / gap.

## 1. NRO discovery & listing
`common/menu-list.c::menuScan` — single-level `readdir` on cwd, NO recursion. Skips dotfiles (`d_name[0]=='.'`). Subdirs become FOLDER tiles. Only `.nro` is launchable; other extensions become `FILE_OTHER`, filtered unless fileassoc matches. Sort: starred-first → folders-before-files → case-insensitive name. "Old-app folder" rule: a folder containing exactly one `.nro` (or matching `<name>/<name>.nro`) auto-collapses to that NRO. Root path `sdmc:/switch/`. **Q OS: have** — `qd_DesktopIcons.cpp:1941` flat-scans `sdmc:/switch/`. **Must-have. Covered.**

## 2. Launch options per NRO
hbmenu supports ONE action: `envSetNextLoad(path, argBuf)` then exit (`nx_main/loaders/builtin.c::launchFile`). Argv: argv[0]=path, each arg `"…"`-quoted, space-separated, max 1024 bytes (`ENTRY_ARGBUFSIZE`). cwd = NRO parent. No applet-vs-application toggle in upstream. **Q OS: have** — `LaunchHomebrewApplication` / `LaunchHomebrewLibraryApplet` SMI gives a toggle hbmenu LACKS. **Must-have. Covered + better.**

## 3. File browser
Tile grid of cwd; B = `menuScan("..")`; A = enter folder. No depth cap (chdir-driven), no extension filter beyond NRO+fileassoc, dotfiles hidden. **Q OS: have** — `qd_VaultLayout.cpp` is richer (cut/paste/delete). **Must-have. Covered.**

## 4. Network features (nxlink)
TCP listen on `NXLINK_SERVER_PORT=28280`. UDP discovery same port: client broadcasts `"nxboot"`, server replies `"bootnx"` to `NXLINK_CLIENT_PORT=28771` on the client IP. Wire (`common/netloader.c::loadnro`): `[u32 namelen][name][u32 filelen][zlib chunked: u32 chunksize + bytes]…[u32 cmdlen][argbuf]`. NRO written under `sdmc:/switch/`, auto-launched. Non-`.nro` accepted via fileassoc, written, NOT launched. nxlink-host arg auto-prepended as `XXXXXXXX_NXLINK_` token (libnx stdout-redirect signal). **Q OS: have** — `qd_NxlinkServer.cpp:129` matches ports + protocol. **Must-have. Covered.**

## 5. Theming
Reads `theme.cfg` (libconfig) from a dir, `.romfs`, or `.zip`. ~30 ThemeLayoutIds, light/dark base, RGBA colors, fonts, custom assets under `theme/<file>` (`common/theme.c::themeStartup`). **Q OS: partial divergent** — own theme system under `qd_Theme.hpp`/`SdOut/ulaunch/themes/`. **Skip hbmenu format parity.**

## 6. Configuration
`sdmc:/config/nx-hbmenu/settings.cfg`. Single key: `themePath` (`theme.c::SetThemePathToConfig`). That's all. **Q OS: have** — own settings via `qd_SettingsLayout.cpp`. **Covered.**

## 7. Argv input UX
**No interactive editor in upstream.** Argv = `[path]` plus auto-injected nxlink token. `launchAddArgsFromString` exists but is dead code (only called by commented-out shortcut XML). Max 1024 bytes. **Q OS: gap. Nice-to-have** — almost no homebrew uses interactive argv; nxlink covers dev case.

## 8. Reboot-to-payload UX
hbmenu has none. **Q OS: have** — `qd_HekateIni.cpp` scans `sdmc:/bootloader/ini` + `sdmc:/bootloader/payloads/`. **Skip parity.**

## 9. Edge cases hbmenu handles
Broken NROs (bad NRO0/ASET magic): invalid-icon fallback, entry still listed (`menu-entry.c::menuEntryLoadEmbeddedIcon`). Missing icon: gray fallback (we already do this — `qd_NroAsset.hpp::MakeFallbackIcon`). Old ABI rev (<1, NRO_ABI_MAGIC `0x32594E4C` at mod_offset+0x34): red recompile warning. Fileassoc maps non-NRO files to host NRO via `sdmc:/switch/.config/fileassoc/` entries. NACP localized name fallback chain. Stars persist as empty sentinel files (`<path>.star`). Path/dir sanitisation against `..` traversal in netloader.

## Prioritized gap list (must-haves Q OS lacks, ranked by effort)

1. **Star/favorites persistence** — small. `.star` sentinel files (or extend RecordsBin). ~1 day.
2. **ABI revision check + recompile warning badge** — small. Read NroStart.mod_offset+0x34, check magic+rev. ~half day.
3. **Old-app-folder collapse** — small. Single-NRO subdir → render as that NRO tile. ~half day.
4. **Argv swkbd editor** — small. Long-press tile → swkbd → argbuf. ~1 day.
5. **Fileassoc system** — medium. Map non-`.nro` extensions/filenames to a host NRO. ~2 days.

After these five land, removing `hbl.nsp` / `hbmenu.nro` / `override_config.ini` is safe.
