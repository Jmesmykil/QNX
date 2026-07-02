# Regression Test Infrastructure Audit — 2026-05-06

Auditor: lead-auditor (read-only). Target: `qos-ulaunch-fork` (creator-owned).

---

## 1. What Exists Today

- **Host unit tests:** `src/projects/uMenu/tests/Makefile` + `tests/qdesktop/Makefile` build 20 binaries (Theme, PixelCanvas, IconCache, NroAsset, IconCategory, Anim, Curve, DockMagnify, Multitouch, Input, Launchpad_HotCorner, LoginChime, DesktopIcons_Hover, StructSize_Pinning, CoyoteTiming, FavoritesLayout, DpadZoneCycle, LaunchpadBgThread, WmConstants, Osk). Link real prod source via `PROD_SRC := ../../source/ul/menu/qdesktop`. `test_StructSize_Pinning` already pins NroEntry/LpItem with `static_assert`.
- **Library tests (vendored, not run):** `src/libs/{zip,json,stb}/test*/`.
- **`tools/test-rig/mac-bridge.py`** — manual SD/HW bridge.
- **CI: NONE.** No `.github/`, no `.gitlab-ci.yml`, no `.git/hooks/` (worktree), no `.clang-tidy`/`.clang-format`/`.cppcheck`. Existing tests are not pre-deploy gates.

---

## 2. Today's Five Regressions vs Detectability

| # | Regression | Catchable? | Check |
|---|---|---|---|
| R1 | Hekate reboot → HOS (`uSystem.json` `system_resource_size: 0xC00000→0x0`, `stack: 0x100000→0x080000`) | YES | `jq` numeric floor + `cmp` NPDM bytes vs LKG |
| R2 | Login reactivity loss (NPDM side-effect + per-frame `SetText` on `qd_lbl_nxlink`/`qd_lbl_usbserial`, `ui_StartupMenuLayout.cpp:445-461`) | YES | NPDM check + grep `SetText(` inside `OnMenuUpdate(` body |
| R3 | `nxlinkConnectToHost(true,true)` blocks main thread on login (`qd_DevTools.cpp:128` via `ui_StartupMenuLayout.cpp:221`) | YES | grep: `SetOnClick` in `ui_Startup*` calling fn that contains `nxlinkConnectToHost`/`fopen`/`svcSleepThread` outside `threadCreate` |
| R4 | NintendoApps `Finalize()` kills uMenu after applet failure (`qd_NintendoApps.cpp:67-113`) | PARTIAL | grep `MenuApplication->Finalize()` not preceded by `if(R_FAILED(...)) return;` |
| R5 | FilePicker scope-creep: 4 untracked files wired via `qd_DesktopIcons_WmBridge.cpp:259-280`; "pending integration" stub-as-log | YES | `git status --porcelain` tripwire + grep `pending\|TODO\|placeholder\|not yet` in `*.cpp` |

Each check 1-3 lines bash, sub-second.

---

## 3. Minimum Viable Suite — `tools/regression-gate.sh`

Single bash script, run before any `cp exefs.nsp` to SD. Exits non-zero on first failure.

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT/tools/qos-ulaunch-fork" 2>/dev/null || cd "$ROOT"

# G1 NPDM resource floor — catches R1 + R2-half
jq -e '(.system_resource_size|tonumber>=12582912) and (.main_thread_stack_size|tonumber>=1048576)' \
   src/projects/uSystem/uSystem.json >/dev/null \
   || { echo "FAIL G1: uSystem NPDM under floor"; exit 1; }

# G2 NPDM byte-diff against last-known-good (compiled snapshot)
test -f tools/regression-gate/uSystem.npdm.lkg \
   && cmp tools/regression-gate/uSystem.npdm.lkg \
          src/projects/uSystem/build/nintendo_nx_arm64_armv8a/release/exefs/main.npdm \
   || { echo "WARN G2: NPDM differs from LKG (review before deploy)"; }

# G3 SMI enum stability — every value must have explicit numeric assignment
! grep -EnA1 'enum class (MenuMessage|SystemMessage|MenuStartMode)' \
     src/libs/uCommon/include/ul/smi/smi_Protocol.hpp \
     src/projects/uSystem/include/ul/system/smi/smi_SystemProtocol.hpp \
   | grep -P '^\s*[A-Z][A-Za-z]+,?\s*(//.*)?$' \
   || { echo "FAIL G3: SMI enum value lacks explicit = N assignment"; exit 1; }

# G4 Login-screen invariants
grep -q 'POWER_CLICK_TOLERANCE_PX = 30' src/projects/uMenu/source/ul/menu/qdesktop/qd_PowerButton.cpp || { echo "FAIL G4a"; exit 1; }
grep -q 'first_main_frame_done_'         src/projects/uMenu/source/ul/menu/ui/ui_MainMenuLayout.cpp     || { echo "FAIL G4b"; exit 1; }
grep -q 'was_touch_active_'              src/projects/uMenu/source/ul/menu/qdesktop/qd_PowerButton.cpp || { echo "FAIL G4c"; exit 1; }

# G5 Per-frame work audit — no SetText/MakeText/IPC/file-IO inside OnMenu(Update|Input|Render)
python3 tools/regression-gate/per_frame_audit.py \
        src/projects/uMenu/source/ul/menu/ui src/projects/uMenu/source/ul/menu/qdesktop \
   || { echo "FAIL G5: per-frame banned call"; exit 1; }

# G6 Stub tripwire
! grep -REn 'pending integration|TODO\(stub\)|placeholder' \
     src/projects/uMenu/source src/projects/uSystem/source \
   || { echo "FAIL G6: stub disguised as log"; exit 1; }

# G7 Host unit tests
( cd src/projects/uMenu/tests          && make -j run ) >/dev/null
( cd src/projects/uMenu/tests/qdesktop && make -j run ) >/dev/null

# G8 Build-determinism (run only when --determinism flag passed)
[ "${1:-}" = "--determinism" ] && {
   make -C src clean >/dev/null && make -C src umenu  >/dev/null
   A=$(sha256sum src/projects/uMenu/uMenu.nso)
   make -C src clean >/dev/null && make -C src umenu  >/dev/null
   B=$(sha256sum src/projects/uMenu/uMenu.nso)
   [ "${A%% *}" = "${B%% *}" ] || { echo "FAIL G8: non-deterministic"; exit 1; }
}
echo "regression-gate PASS"
```

`per_frame_audit.py` ≈ 25 lines: walk `OnMenu(Update|Input|Render)` bodies, fail on non-comment `SetText(`/`MakeText(`/`fopen(`/`nxlinkConnectToHost(`/`bpcamsInitialize(`/`svcSleepThread(`/`appletStorage`.

---

## 4. CI

Two GitHub Actions workflows, each ≤30 lines:

**`.github/workflows/host-tests.yml`** — on every push: `apt-get install -y build-essential jq python3` then `bash tools/regression-gate.sh`.

**`.github/workflows/build-determinism.yml`** — nightly + manual dispatch: `bash tools/regression-gate.sh --determinism` inside the devkitA64 container.

A pre-push hook (`.git/hooks/pre-push`) is just `exec bash tools/regression-gate.sh`.

---

## 5. Single Biggest Leverage Point

**G1 + G2 (NPDM resource floor + NPDM byte-diff vs last-known-good).** Of today's five regressions, R1 + R2 (the worst — they ate Hekate reboot, login reactivity, `RebootToStockQlaunch`, and likely the bpc:ams trampoline failure) are caught by one `jq -e` and one `cmp`. ~10 minutes to land. Eliminates the entire "someone tweaked an NPDM number and bricked boot" class.
