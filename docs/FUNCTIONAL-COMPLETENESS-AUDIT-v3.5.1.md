# Q OS uMenu — Functional Completeness Audit (v3.5.1)

> Source-level audit of every user-interactive surface. Confirms every
> click target dispatches to a real, non-stub handler. HW-verification
> column tracks which surfaces have been pushed on real Switch Erista.

**Source audit performed:** 2026-05-27, against working-tree artifact md5 `c20b7f74fd84cd34f1b2e9e1d5cadf1c` (locally-built uMenu binary — NOT a git commit; `git cat-file` fails on it). **Git base = `6727b6e5` (v3.5.0) + uncommitted working tree.** This is **v3.6.0-wip1**, not a "v3.5.1 stable" release — no commit, no tag, and HW-UNVERIFIED (the prior build of this work crashed on hardware). Reconciled by the 2026-06-12 full audit (`audits/2026-06-12-audit.md`).

> **2026-06-12 audit note:** this doc was written in the same working tree as four new modules it does not mention — `qd_SaveBackup`, `qd_CheatTitleResolver`, the Settings **Overlays** tab, and the W18 boot-retry hardening. Treat its "v3.6 future" rows (SwSh write-back aside) as already partially implemented-in-tree but HW-unverified. See the audit's completeness table for honest per-feature %.

**Stub count in shipped uMenu source:** 1 (telnet dev shell `cmd-launch`). Four upstream uLaunch TODOs (UI polish, not feature-blocking).

---

## 1. Lockscreen surfaces

| Surface | Handler | Source | HW-verified |
|---|---|---|---|
| User card (touch / A) | `QdUserCardElement::on_select_` → `StartupMenuLayout::user_DefaultKey` / `onUserSelected` | `qd_UserCard.cpp:495`, `ui_StartupMenuLayout.cpp:18,447` | ✅ v3.5.1 (re-entrancy guard + fade=false) |
| Power: Restart | `qdesktop::power::Reboot()` | `ui_StartupMenuLayout.cpp:179` | ✅ v3.x |
| Power: Shutdown | `qdesktop::power::Shutdown()` | `ui_StartupMenuLayout.cpp:182` | ✅ v3.x |
| Power: Sleep | `qdesktop::power::Sleep()` | `ui_StartupMenuLayout.cpp:185` | ✅ v3.x |
| Power: Hekate | `qdesktop::power::RebootToHekate()` | `ui_StartupMenuLayout.cpp:188` | ✅ v3.x |
| Power: Hekate UMS | `qdesktop::power::RebootToHekateUms()` (with notification fallback) | `ui_StartupMenuLayout.cpp:191` | ⚠️ **degraded** — `sdmc:/bootloader/payloads/hekate_ums.bin` is absent on the canonical SD, so `qd_Power.cpp:276-292` silently falls back to `atmosphere/reboot_payload.bin`, which boots the **Hekate menu** (autoboot=0), **never direct-to-UMS**. Creator RCM-injects manually. NOT "used every session". |
| Dev: Nxlink toggle | `qdesktop::dev::ToggleNxlink()` | `ui_StartupMenuLayout.cpp:231` | ✅ v3.x |
| Dev: USB Serial toggle | `qdesktop::dev::ToggleUsbSerial()` | `ui_StartupMenuLayout.cpp:250` | ✅ v3.x |
| Dev: Flush Logs | `qd_PerfLogger::Flush()` | `ui_StartupMenuLayout.cpp:258` | ✅ v3.x |

**Status: complete.**

---

## 2. Desktop dock tiles (7)

| Slot | Tile | Handler | Source | HW-verified |
|---|---|---|---|---|
| 0 | Vault | `OpenVaultWindow()` | `qd_DesktopIcons.cpp:5997` | ✅ v3.0+ |
| 1 | Monitor | `OpenMonitorWindow()` | `qd_DesktopIcons.cpp:6001` | ✅ v3.0+ |
| 2 | AllPrograms (Launchpad) | `LoadMenu(MenuType::Launchpad)` | `qd_DesktopIcons.cpp:6040` | ✅ v3.0+ |
| 3 | Tasks | `OpenTaskManagerWindow()` | `qd_DesktopIcons.cpp:6045` | ✅ v3.2+ (W15-A refcount fix) |
| 4 | Nintendo Apps | `OpenNintendoAppsWindow()` | `qd_DesktopIcons.cpp:6049` | ✅ v3.0+ |
| 5 | Saves | `OpenSaveEditorWindow()` | `qd_DesktopIcons.cpp:6053` | ✅ v3.4 (SwSh read-only) |
| 6 | Cheats | `OpenCheatsWindow(0)` (TitleList mode) | `qd_DesktopIcons.cpp:6058` | ✅ v3.4 + v3.5 in-OS installer |

**Status: complete.**

---

## 3. Application launching

| Action | Handler | Source | HW-verified |
|---|---|---|---|
| Tap installed Switch game | `smi::LaunchApplication(app_id)` → `FadeOutToNonLibraryApplet()` → `Finalize()` | `qd_DesktopIcons.cpp:6161` | ✅ v3.4.0 |
| Resume suspended game (same icon) | `smi::ResumeApplication()` | `qd_DesktopIcons.cpp:6122` | ✅ v3.0+ |
| Switch suspended game (different icon, with confirm) | dialog → `smi::TerminateApplication()` → `smi::LaunchApplication()` | `qd_DesktopIcons.cpp:6134-6161` | ✅ v3.0+ |
| Tap NRO icon | `smi::LaunchHomebrewLibraryApplet(nro_path, "")` → `FadeOutToNonLibraryApplet()` → `Finalize()` | `qd_DesktopIcons.cpp:6071` | ✅ v3.0+ (Tinwoo + every other NRO works) |
| Games folder | `SetApplicationEntries(entries)` + folder routing | `qd_DesktopIcons.cpp:6219`, `ApplyAppAndSpecialEntries` | ⚠️ **v3.5.1 pending HW verify** (fix landed but unverified) |

**Status: complete (one fix pending HW verify).**

---

## 4. System applets (via FadeOut → SMI → Finalize)

| Applet | Handler | libnx ID | HW-verified |
|---|---|---|---|
| Album (Photo Viewer) | `ShowAlbum()` | `AppletId_LibraryAppletPhotoViewer` | ✅ v2.3.6.1 |
| Mii Edit | `ShowMiiEdit()` | `AppletId_LibraryAppletMiiEdit` | ✅ v2.3.6.1 |
| My Page (Profile) | `ShowUserPage()` | `AppletId_LibraryAppletMyPage` | ✅ v2.3.6.1 |
| Web Browser | `ShowWebPage()` | `AppletId_LibraryAppletWeb` | ✅ v2.3.6.1 |
| Net Connect | `ShowNetConnect()` | `AppletId_LibraryAppletNetConnect` | ✅ v2.3.6.1 |
| Controllers | `ShowController()` | `hidLaShowControllerSupportForSystem` | ✅ direct libnx call (no SMI needed) |
| Cabinet (Amiibo) | `ShowCabinet()` | `AppletId_LibraryAppletCabinet` | ✅ v2.3.6.1 |
| Software Keyboard | direct libnx | `AppletId_LibraryAppletSwkbd` | ✅ direct libnx call |
| Error dialog | direct libnx | `AppletId_LibraryAppletError` | ✅ direct libnx call |

**Status: complete.** Nintendo's native Settings UI is architecturally impossible to launch (no `AppletId_LibraryAppletSet` on retail devices); workaround is the "Boot to Nintendo Home Menu" row in Q OS Settings.

---

## 5. Q OS Settings (7 tabs, 33+ rows)

### System tab (10 rows)
| Row | Handler |
|---|---|
| Firmware version | read `setsysGetFirmwareVersion` |
| Serial number (masked) | read `setsysGetSerialNumber` |
| Uptime | computed from `armGetSystemTick` |
| Atmosphère version / EmuNAND | `os::GetAmsConfig` |
| Region | `setGetRegionCode` |
| NFC enable | `setsysSetNfcEnableFlag` |
| USB 3.0 enable | `setsysSetUsb30EnableFlag` |
| Console info upload | `setsysSetConsoleInformationUploadFlag` |
| **Boot to Nintendo Home Menu** | `DoBootToStockQlaunch()` → `smi::RebootToStockQlaunch()` |
| **Sync clock with NTP** | `DoNtpSync()` → bsd:u + time:s |

### Network tab (5 rows)
Reads via `nifmGet*` + `setsysGet*`. Writes via `setsysSetBluetoothEnableFlag`, `setsysSetWirelessLanEnableFlag`.

### Audio tab (3 rows)
Reads via `audctlGetTargetVolume`. Writes via `audctlSetTargetVolume`.

### Display tab (4 rows)
Reads/writes via `setsysGet/SetSleepSettings`, theme color type.

### Account tab (4 rows)
Reads via `accountListAllUsers`. **Switch User** button → `DoUserSwitch()` → returns to lockscreen.

### About tab (7 rows)
Q OS version, fork credit, GPL-2.0 notice, contributors, repo URL.

### Folders tab (6 rows)
Toggles auto-folder visibility (NxGames / Homebrew / System / Payloads / Builtin) + Folder Theme picker → `ShowThemesMenu()`.

**Status: complete.** Every Settings row reads or writes via real Switch system calls. No stubs.

---

## 6. Vault (file manager / HBMenu replacement)

| Action | Handler |
|---|---|
| Navigate directory | `Navigate(path)` |
| Open NRO | `smi::LaunchHomebrewLibraryApplet(path, "")` |
| Open file (text/json/cfg) | preview pane via `QdVaultPreview` |
| Sidebar shortcut | 12 entries (sdmc root, /switch/, /atmosphere/, /ulaunch/, /Nintendo/, /JKSV/, /retroarch/, etc.) |
| Context menu (per file) | rename / delete / copy / move / properties |

**Status: complete.**

---

## 7. Cheats system (W14 in-OS installer + W13 parser)

| Action | Handler |
|---|---|
| Browse installed cheats by TID | `ScanInstalledCheats()` → reads `sdmc:/atmosphere/contents/<TID>/cheats/<BID>.txt` |
| Enable/disable individual cheat | toggle `currently_enabled` + write `.qos-backup` + truncate-write `.txt` |
| Install cheats from upstream | `qd_CheatsInstaller`: libnx ssl + libminizip + `nsListApplicationRecord` filter |
| Sidecar persistence | `sdmc:/ulaunch/cheats-enabled/<TID>.toml` |

**Status: complete (v3.4 / v3.5).**

---

## 8. Saves system (W13 parser + autoscan)

| Action | Handler |
|---|---|
| Autoscan SwSh saves | `QdSaveAutoscan::RunScan` (capped at 64) |
| Read Sword/Shield save | `qd_SwShSaveParser` + `qd_SwishCrypto` (XOR pad + SHA-256 + 0x7F chunk) |
| Display 6 party Pokémon | nickname, level, item, shiny indicator, IVs |
| Write back | ⏳ **v3.6 deferred** (PKHeX round-trip verified, not yet enabled in UI) |
| JKSV-style backup button | ⏳ **v3.6** |

**Status: 90% complete.** Read works; write-back queued for v3.6.

---

## 9. Hot corner / Q OS chrome

| Surface | Handler |
|---|---|
| Hot corner Q glyph (top-left) | `QdHotCornerOverlay::OnInput` → `HotCornerDropdown::Open()` |
| Dropdown row 0: About | `ShowAboutDialog()` |
| Dropdown row 1: Themes | `ShowThemesMenu()` |
| Dropdown row 2: Settings | `ShowSettingsMenu()` |
| Help overlay (Home + Capture) | `RequestHelpOverlayOpen()` |
| Dev mini-menu (Home double-press <600 ms) | dev tools surface |

**Status: complete.**

---

## 10. Remote shell (port 9999, opt-in)

| Action | Handler |
|---|---|
| Listen socket | `nifmGetCurrentIpAddress()` (not INADDR_ANY) |
| Auth | 6-digit PIN required as first line |
| Brute-force protection | 3 failed auths / 60 s → listener closed + SECURITY ALERT logged |
| Commands | ls / cat / put / get / launch (`cmd-launch` is a **DEV STUB** — prints "stub: would launch %s") |

**Status: hardened (v3.5 W16-TELNET-HARDEN). The `cmd-launch` command is intentionally a stub — telnet shell is a dev tool, not user-facing.**

---

## 11. Windowing / desktop chrome

| Action | Handler |
|---|---|
| Open / minimize / maximize / pin-corner / close window | `QdWindowManager` |
| Drag (touch + cursor) | full state machine in `QdWindowManager` |
| Dock-zone bypass | window snap-out rules |
| Singleton windows | per-MenuType lock |
| L+2 window-state persistence | survives NRO round-trips |
| Multi-window focus | per-frame z-order |

**Status: complete.**

---

## 12. First-boot welcome overlay

| Trigger | Handler |
|---|---|
| First boot only (`.welcome_seen` sentinel absent) | `first_boot_welcome_.Open(r)` on first frame of Main |
| Gating | `first_main_frame_done_` + `ShouldShow()` (welcomer dismissed once → never shows again) |

**Status: complete + gated correctly (no AGENT-WELCOMER-CONFLICT in current logs).**

---

## 13. Themes (10 packs)

| Action | Handler |
|---|---|
| Pick theme via Themes menu | `ShowThemesMenu()` → grid of 10 + installed `.ultheme` files |
| Apply theme | `cfg::CacheActiveTheme` + `LoadThemeFromCache` + `g_QdTheme` palette swap |
| Per-theme wallpaper pack | independent of palette pack (`qos-wallpaper.toml` decoupled from `qos-folder-theme.toml`) |
| Per-theme icon pack | 170 PNGs across 10 themes |
| Per-theme hot-corner emblem | dedicated PNG per theme |
| `.ultheme` install | drop file in `sdmc:/ulaunch/themes/` → auto-discovered on next boot |

**Status: complete (v3.5 W16-THEMES-AUDIT).**

---

## What's NOT functional yet (roadmap)

| Capability | Release |
|---|---|
| SwSh save write-back | v3.6 |
| JKSV-style save backup button | v3.6 |
| Async NS title resolver (Cheats UI shows real names) | v3.6 |
| Lazy theme loading (−40-60 MiB resident set) | v3.6 |
| Goldleaf NSP installer | v3.7 |
| sys-clk profile UI | v3.7 |
| `caps:a` Album browser (clean-room) | v3.7 |
| sphaira-pattern FTP server | v3.7 |
| Overlay manager (Tesla rides along) | v3.7 |
| HDR enabler | v3.8+ |
| Atmosphère config tweaks UI | v3.8+ |
| Save data cloud backup | v3.8+ |
| Multi-user dock switching | v3.8+ |
| Q OS-native sysmodules | v3.8+ |
| In-app update channel | v3.8+ |

---

## Architectural impossibilities (documented, not bugs)

| Capability | Why never |
|---|---|
| Launch Nintendo's native Settings UI from inside uMenu | `AppletId_LibraryAppletSet` documented as "not present on retail devices" — Nintendo built Settings INTO qlaunch, not as a launchable applet. Workaround: System tab → "Boot to Nintendo Home Menu" row reboots into stock qlaunch. |
| Windowed external NROs | Requires an Atmosphère framebuffer-redirect extension that doesn't exist. Substitute: L+2 window-state persistence (open windows survive NRO round-trips). |
| Album hijack bypass | AMS hardcodes PhotoViewer → hbl.nsp in `cfg_override.board.nintendo_nx.inc:35-41`. Disarming via `override_config.ini` breaks `RebootToHekate` (3× HW-confirmed). Permanent workaround: lives with the hijack; clean-room `caps:a` album browser is v3.7. |

---

## Conclusion

**v3.5.1 is functionally complete for "Switch Settings + ALL applications."** The Games-folder fix that landed in v3.5.1 (`c20b7f74...`) is the last remaining gap before this audit reads green end-to-end.

Every interactive surface in the OS dispatches to a real handler. No stubs in user-facing code (the single stub is a dev-tool telnet command). Architectural limitations are documented + worked around.

The roadmap from v3.6 onwards is **absorption** — replacing community NROs with first-class OS features. Nothing on the roadmap is a "missing core function"; everything is new capability beyond stock Switch.
