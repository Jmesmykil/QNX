# HBMenu Cutover Playbook — 2026-05-06

> **Authorization:** creator-owned (Q OS uLaunch fork). Analysis only.
> **Reversibility:** every step is reversible until Step 5. Rollback target: working hbmenu in <5 min.
> **Mount:** SD at `/Volumes/SWITCH SD` via UMS. After each change, `diskutil eject` and cold-boot.

---

## 1. Pre-cutover validation

All must pass on hardware before Step 1. If any fails, abort and fix Q OS first.

| # | Feature | Test | Pass |
|---|---------|------|------|
| 1 | Native NRO file browser | qdesktop → Vault → browse `sdmc:/switch/` | NROs enumerate; folders descend; `..` ascends |
| 2 | Dock NRO launch | Click pinned dock icon | NRO launches; on exit returns to qdesktop |
| 3 | Browser NRO launch | File browser → pick `.nro` → Open | Same as #2 |
| 4 | argv/argc | Launch NRO that prints argv | argv[0]=NRO path; passed args verbatim |
| 5 | nxlink push | `nxlink -s <nro>` from Mac | NRO auto-launches; stdout streams |
| 6 | Applet vs application | Launch NRO via both qdesktop paths | Both succeed; addr space matches launch metadata |
| 7 | Reboot to payload | qdesktop power menu → reboot to payload | Configured payload boots |
| 8 | Boot sanity | Cold boot | qdesktop within 30 s, input responsive |

If item 6 (application context) is not yet implemented in qdesktop, **stop** — that's the path we're disabling in AMS.

---

## 2. Cutover sequence (safety-first ordering)

Render hbmenu unreachable via **config first** (reversible), then delete binaries (irreversible).

### Step 1 — Snapshot
```bash
mkdir -p ~/QOS/backups/hbmenu-cutover-20260506
cp "/Volumes/SWITCH SD/atmosphere/config/override_config.ini" ~/QOS/backups/hbmenu-cutover-20260506/override_config.ini.orig 2>/dev/null || true
cp "/Volumes/SWITCH SD/atmosphere/hbl.nsp"                     ~/QOS/backups/hbmenu-cutover-20260506/hbl.nsp.orig
cp "/Volumes/SWITCH SD/hbmenu.nro"                             ~/QOS/backups/hbmenu-cutover-20260506/hbmenu.nro.orig
shasum -a 256 ~/QOS/backups/hbmenu-cutover-20260506/* > ~/QOS/backups/hbmenu-cutover-20260506/MANIFEST.sha256
```
**State:** SD unchanged, local backup created. **Switch:** unchanged.

### Step 2 — Disable hijack via config (REVERSIBLE)
Neutralize both AMS hijack paths (lines 407, 414 of `cfg_override.board.nintendo_nx.inc`):
```bash
cat > "/Volumes/SWITCH SD/atmosphere/config/override_config.ini" <<'EOF'
[hbl_config]
; QOS-CUTOVER 20260506 — hbmenu disabled, native qdesktop owns NRO launch.
program_id = 0x0100000000000000
override_key = !A+B+X+Y+L+R+ZL+ZR+PLUS+MINUS
override_any_app = false
override_any_app_key = !A+B+X+Y+L+R+ZL+ZR+PLUS+MINUS
EOF
```
Why two layers: `override_any_app=false` short-circuits line 407 globally. The unreachable input pattern (`!` = on-by-default + every button held) means even if a line-414 specific match ever fires, no human input can flip the XOR (`override_by_default ^ keys_triggered`, line 314). Reserved `program_id 0x0100000000000000` ensures the line-414 loop misses anyway.

**State:** hijack inert; binaries intact; PhotoViewer launches PhotoViewer; apps launch as themselves.
**Switch next cold-boot:** Album opens real PhotoViewer. R-held during launch does nothing special.

### Step 3 — Eject, cold-boot, re-run §1 matrix
```bash
diskutil eject "/Volumes/SWITCH SD"
```
Re-validate items 1–7. Any failure → **Rollback A**.

### Step 4 — Remove the config (still reversible — binaries intact)
After 24 h stable. Mount UMS:
```bash
rm "/Volumes/SWITCH SD/atmosphere/config/override_config.ini"
```
**Without paired Step 5 the compiled-in defaults re-arm the hijack.** Do not eject or boot between Step 4 and Step 5.

### Step 5 — Delete HBL payload + hbmenu binary (IRREVERSIBLE)
```bash
rm "/Volumes/SWITCH SD/atmosphere/hbl.nsp"
rm "/Volumes/SWITCH SD/hbmenu.nro"
diskutil eject "/Volumes/SWITCH SD"
```
**State:** AMS still tries to hijack PhotoViewer (compiled defaults). `OpenHblCodeFileSystemImpl` (`fs_code.cpp:166`) fails to open the missing package; Loader aborts the launch. Album button does nothing. qdesktop and Q OS launches unaffected. Cold-boot.

### Step 6 — Permanent disabled config (recommended, idempotent)
Spare AMS the failed package open every Album press:
```bash
cat > "/Volumes/SWITCH SD/atmosphere/config/override_config.ini" <<'EOF'
[hbl_config]
override_any_app = false
override_any_app_key = !A+B+X+Y+L+R+ZL+ZR+PLUS+MINUS
program_id = 0x0100000000000000
override_key  = !A+B+X+Y+L+R+ZL+ZR+PLUS+MINUS
EOF
diskutil eject "/Volumes/SWITCH SD"
```
**Final state:** Album opens real PhotoViewer. No HBL launch path. qdesktop is the only NRO launcher.

---

## 3. Regression test matrix

Run after Step 6. One cold-boot per row.

| Workflow | Test | Expected (post) | Pre |
|----------|------|-----------------|-----|
| Dock NRO launch | Click pinned dock icon | Launches via qdesktop loader; argv[0]=path | Same |
| File browser → NRO | Vault → file browser → Open | Launches; on exit returns qdesktop | Same |
| nxlink push | `nxlink -s build/x.nro` | Auto-launches; stdout to nxlink | Same (server lives in qdesktop) |
| R-held during app launch | Hold R, launch any game | Game launches normally — **no hbmenu** | hbmenu launched as application |
| Album button | Press Album from HOME | Real PhotoViewer | hbmenu opened as applet |
| NRO with arguments | qdesktop launch with argv | argv passed verbatim | Same |
| Applet ↔ application | Launch same NRO via both qdesktop paths | Both work; addr space per launch metadata | Pre: applet via Album, app via R+launch |
| Reboot to payload | qdesktop power menu → reboot | Configured payload boots | Same (AMS, not hbmenu) |

---

## 4. Rollback

### A — config-only regression (after Steps 2–3)
```bash
cp ~/QOS/backups/hbmenu-cutover-20260506/override_config.ini.orig "/Volumes/SWITCH SD/atmosphere/config/override_config.ini" 2>/dev/null \
  || rm "/Volumes/SWITCH SD/atmosphere/config/override_config.ini"
diskutil eject "/Volumes/SWITCH SD"
```
Cold-boot — hbmenu reachable again.

### B — full restore (after Step 5)
```bash
cp ~/QOS/backups/hbmenu-cutover-20260506/hbl.nsp.orig    "/Volumes/SWITCH SD/atmosphere/hbl.nsp"
cp ~/QOS/backups/hbmenu-cutover-20260506/hbmenu.nro.orig "/Volumes/SWITCH SD/hbmenu.nro"
cp ~/QOS/backups/hbmenu-cutover-20260506/override_config.ini.orig "/Volumes/SWITCH SD/atmosphere/config/override_config.ini" 2>/dev/null \
  || rm "/Volumes/SWITCH SD/atmosphere/config/override_config.ini"
shasum -a 256 -c ~/QOS/backups/hbmenu-cutover-20260506/MANIFEST.sha256
diskutil eject "/Volumes/SWITCH SD"
```
Cold-boot — pre-cutover state restored bit-for-bit.

---

## 5. Atmosphère-side considerations

Verified in `src/libs/Atmosphere-libs/libstratosphere/`:

- **`hbl.nsp` needed elsewhere?** No. Opened only by `OpenHblCodeFileSystemImpl` (`fs_code.cpp:166`), gated by `is_hbl` from `IsHbl()`, set only at lines 408/414 of `cfg_override.board.nintendo_nx.inc`. Disable both → dead binary.
- **Other `[hbl_config]` consumers?** None. `OverrideConfigIniHandler` (188–276) writes only `g_hbl_override_config` / `g_hbl_sd_path`; read only by `CaptureOverrideStatus` and `GetHblPath`. Cheats/locale/content_specific use different sections.
- **Config present, `hbl.nsp` missing?** AMS still flips `IsHbl()`, then package open fails and Loader aborts. Album press = silent no-op. Harmless — Step 6 fixes it.
- **`override_any_app=false`, binary exists?** Line 407 short-circuits. Line 414 still runs against defaults — PhotoViewer R-on-by-default, so Album-with-R still hijacks. Step 2 nullifies that path too via reserved `program_id` + unreachable combo.
- **uLaunch source dependency?** Zero references to `hbl.nsp` / `GetHblPath` / `override_any_app` outside Atmosphere-libs. Cutover is purely AMS-side.

---

## 6. SD layout snapshot

### BEFORE
```
/Volumes/SWITCH SD/
├── atmosphere/
│   ├── config/
│   │   └── override_config.ini       [absent OR upstream defaults]
│   ├── hbl.nsp                        ← target #1
│   ├── contents/                      [unchanged]
│   └── ...
├── hbmenu.nro                         ← target #2
├── switch/                            [unchanged]
└── ...
```
### AFTER (post Step 6)
```
/Volumes/SWITCH SD/
├── atmosphere/
│   ├── config/
│   │   └── override_config.ini       ← QOS-disabled stub
│   ├── contents/                      [unchanged]
│   └── ...
├── switch/                            [unchanged]
└── ...
```
Targets #1 and #2 gone; config present but neutered.

---

## 7. Future cleanup (after 30+ days stable)

1. **uMenu hbmenu classifier strings** — `qd_IconCategory.cpp:116,118,176`, `qd_DesktopIcons.cpp:296`, `qd_FolderClassifier.cpp:203`, `test_QdIconCategory.cpp:76–81`. No binary dependency; remove once no install ships `hbmenu.nro`.
2. **Fallback comments** — `ui_Common.cpp:452`, `qd_DesktopIcons.cpp:2016`. Re-read; simplify if moot.
3. **Test fixture** — `test_QdIconCache.cpp:174` uses `sdmc:/switch/hbmenu.nro`. Swap for Q OS-native path.
4. **Backups** — retain ≥ 90 days; archive after one firmware bump cycle.
5. **`docs/UPSTREAM-ANALYSIS.md`** — mark HBL hijack section as historical.

Do **not** strip `nx-hbloader.LICENSE.md` — attribution required for inherited code elsewhere.
