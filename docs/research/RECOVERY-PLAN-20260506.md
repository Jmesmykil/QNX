# uMenu/uSystem Recovery Plan — 2026-05-06

**Constraint:** keep `exefs.nsp` md5 `5b22a1cd` (568,284 B, the Apr-26 boot-stable uSystem). Rebuild only uMenu. Apr-26 v1.6.x baseline `main` ≈ 6,927,489 B; latest crashing v2.3.7 `main` = 7,099,642 B (md5 `30d02092`). Headroom ≈ 170 KB. Every phase must boot or we revert.

## Decision tree

```
Phase N built → cp main → boot HW
  ├── boots to v2.3.x banner + reboot-to-Hekate works → advance to N+1
  └── crashes (svc::LimitReached / black screen / hbmenu hijack)
        → cp main.bak-pre-PhaseN → revert one feature → rebuild → retry
        → if 2 consecutive crashes at same phase → STOP, escalate
```

## Phase 0 — Banner + Album=error_applet (PROVEN-SAFE Album fix only)

**Adds (vs HEAD `dd0a3a31`):**
- `qd_NintendoApps.cpp`/.hpp dirty hunks for `LaunchAlbum` → `errorSystemShow` (no AMS override.ini, no Hekate impact). Drop the `icon_path` field from `kNintendoApps[]` in this phase only.
- `uSystem.json` dirty revert is **REJECTED**. Keep committed `0x0100000/0x400000`. We do NOT rebuild uSystem; we keep the on-SD `5b22a1cd` `exefs.nsp`.

**Defers:** tile icons, drag-scroll, vault drag, vault context-menu, DevTools worker thread, Nintendo dock romfs:/Logo.png.

**Estimated size:** HEAD-built `dd0a3a31` was ≈7,082,727 B (`main.bak-pre-NintendoAppsSMI`). Phase 0 swaps `LaunchAlbum` body (~−24 lines of SMI wrap → +14 lines of error_applet). Net ≈ **+1.5 KB → 7,084,000 B**. Already 30 KB above v1.6.x baseline; 15 KB under crashing build. Banner = v2.3.6.

**HW gate:** (a) cold boot to login screen, banner reads "v2.3.6"; (b) tap "Reboot to Hekate" — Hekate menu loads; (c) Nintendo dock → Album tile → error dialog appears, no hbmenu, no crash; (d) press HOME, login still responsive.

## Phase 1 — Tile icons (Nintendo Apps grid + Nintendo dock romfs)

**Adds:** dirty `qd_NintendoApps.hpp` `icon_path` field, `kNintendoApps[]` icon paths, `qd_DesktopIcons.cpp` Nintendo→Logo.png hunk. Five PNGs already in `src/default-theme/ui/Main/EntryIcon/` (no romfs change). `Logo.png` already shipped.

**Defers:** drag-scroll, all DevTools/Vault changes.

**Estimated size:** +0.6 KB code (icon_path string ptrs) + per-tile texture cache (~600 KB GPU but **runtime**, not binary). Binary ≈ **7,084,500 B**.

**HW gate:** Phase 0 gates PASS + each Album/Controllers/MiiEdit/Web/Settings tile shows its PNG, not solid color. Profile/Keyboard/ErrorInfo render solid color (nullptr — expected).

## Phase 2 — Login reactivity (DevTools worker thread)

**Adds:** entire `qd_DevTools.cpp` dirty hunk (R2 fix from `LOGIN-REACTIVITY-V237-AUDIT.md`) — moves `usbCommsInitialize` + SD `fread` loop to `g_usb_snapshot_thread`. Add `g_usb_serial_active=false` reset on worker error path (audit's adjacent risk).

**Defers:** drag-scroll, Vault.

**Estimated size:** +132 LOC, ≈ **7,087,000 B** (bracketed by `main.bak-pre-stabilization-final` 7,087,860 B).

**HW gate:** Phase 1 gates PASS + tap USB Serial button on login screen; ALL OTHER buttons remain responsive within 100 ms.

## Phase 3 — Nintendo Apps drag-scroll

**Adds:** `qd_NintendoAppsLayout.hpp/.cpp` 79-line drag state machine. Verify two-latch invariant from REGRESSION-TIMELINE R6.

**Estimated size:** +88 LOC, ≈ **7,090,000 B**.

**HW gate:** Phase 2 gates PASS + finger-flick scroll in Nintendo Apps grid; rapid taps still register single-tap launch.

## Phase 4 — Vault drag-scroll + context menu

**Adds:** `qd_VaultLayout.hpp/.cpp` 175-line block (drag + Launch-as-Application).

**Estimated size:** +201 LOC, ≈ **7,099,000 B** — at crashing-build size. Most likely failure point.

**HW gate:** Phase 3 PASS + Vault drag scroll + ZL long-press → context menu → Launch as Application on a .nro.

## Stop-loss

| Threshold | Action |
|-----------|--------|
| Any phase build > 7,095,000 B | HALT. Manual review of `nm -S main` BSS deltas. |
| Phase N crashes twice | Revert to N−1, log to `docs/research/RECOVERY-FAILURE-PHASE${N}-20260506.md`, escalate. |
| Hekate reboot regresses at any phase | Immediate `cp main.bak-pre-Phase${N}-20260506 main`. Do NOT touch `exefs.nsp`. |
| `system_resource_size` or `main_thread_stack_size` modified in any phase | HALT. Those values are fixed at committed `0xC00000`/`0x100000` for this recovery. |

## Phase 0 — single command (paste-ready)

```
cd /Users/nsa/QOS/tools/qos-ulaunch-fork && \
git checkout HEAD -- src/projects/uSystem/uSystem.json src/projects/uMenu/source/ul/menu/qdesktop/qd_DevTools.cpp src/projects/uMenu/source/ul/menu/qdesktop/qd_NintendoAppsLayout.cpp src/projects/uMenu/include/ul/menu/qdesktop/qd_NintendoAppsLayout.hpp src/projects/uMenu/source/ul/menu/qdesktop/qd_VaultLayout.cpp src/projects/uMenu/include/ul/menu/qdesktop/qd_VaultLayout.hpp src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp src/projects/uMenu/include/ul/menu/qdesktop/qd_NintendoApps.hpp && \
sed -i '' 's|, "ui/Main/EntryIcon/[A-Za-z]*"||g; s|, nullptr||g' src/projects/uMenu/source/ul/menu/qdesktop/qd_NintendoApps.cpp && \
cp "/Volumes/SWITCH SD/ulaunch/bin/uMenu/main" "/Volumes/SWITCH SD/ulaunch/bin/uMenu/main.bak-pre-Phase0-$(date +%Y%m%d-%H%M%S)" && \
make -C src/projects/uMenu -j$(sysctl -n hw.ncpu) && \
cp src/projects/uMenu/Output/main "/Volumes/SWITCH SD/ulaunch/bin/uMenu/main" && \
diskutil eject "/Volumes/SWITCH SD"
```

(Reverts dirty hunks except `qd_NintendoApps.cpp`'s Album→error_applet body; sed strips the icon_path arguments still in that file so the struct matches HEAD's 3-field layout. SD-eject per memory directive. exefs.nsp untouched.)
